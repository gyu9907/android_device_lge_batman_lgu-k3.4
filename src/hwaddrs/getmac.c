/*
 * Copyright (C) 2011-2012 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cutils/klog.h>
#include <cutils/properties.h>
#include <private/android_filesystem_config.h>
#include <selinux/android.h>

#define BT_ADDR_FILE "/data/misc/bd_addr"
#define WIFI_ADDR_FILE "/data/misc/wifi/config"
#define BT_READY_SYSFS "/sys/kernel/batman_hwaddrs/bt_ready"
#define WIFI_READY_SYSFS "/sys/kernel/batman_hwaddrs/wifi_ready"
#define NV_RPC_DIRECTORY "/dev/oncrpc"
#define RPC_WAIT_SECONDS 60
#define READ_WAIT_SECONDS 20

extern void oncrpc_init(void);
extern void oncrpc_task_start(void);
extern int nv_cmd_remote(int, int, void *);

static int valid_address(const unsigned char *mac, int wifi)
{
    static const unsigned char zero[6] = {0};
    static const unsigned char broadcast[6] = {255, 255, 255, 255, 255, 255};
    return (!wifi || !(mac[0] & 1)) &&
           memcmp(mac, zero, sizeof(zero)) != 0 &&
           memcmp(mac, broadcast, sizeof(broadcast)) != 0;
}

/* Do not open the generic RPC router here: that would load the modem early. */
static int nv_rpc_ready(void)
{
    DIR *directory;
    struct dirent *entry;
    struct stat st;
    char path[128];
    int ready = 0;
    directory = opendir(NV_RPC_DIRECTORY);
    if (!directory)
        return 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "3000000e:", 9) != 0 || strlen(entry->d_name) != 17)
            continue;
        snprintf(path, sizeof(path), "%s/%s", NV_RPC_DIRECTORY, entry->d_name);
        if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) {
            ready = 1;
            break;
        }
    }
    closedir(directory);
    return ready;
}

/* Persist the factory address at the original LineageOS path before releasing
 * its consumer. Rename prevents readers from seeing a partially written file.
 */
static int save_address(int wifi, const unsigned char *mac)
{
    const char *path = wifi ? WIFI_ADDR_FILE : BT_ADDR_FILE;
    const char *directory = wifi ? "/data/misc/wifi" : "/data/misc";
    char temporary[128], text[64];
    int fd, dirfd, length, result = -1;
    ssize_t written;

    if (snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary))
        return -1;
    length = snprintf(text, sizeof(text), "%s%02x:%02x:%02x:%02x:%02x:%02x\n",
                      wifi ? "cur_etheraddr=" : "",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0)
        return -1;
    fd = mkstemp(temporary);
    if (fd < 0) {
        close(dirfd);
        return -1;
    }
    do {
        written = write(fd, text, length);
    } while (written < 0 && errno == EINTR);
    if (written == length &&
        fchown(fd, wifi ? AID_WIFI : AID_BLUETOOTH,
               wifi ? AID_WIFI : AID_NET_BT_STACK) == 0 &&
        fchmod(fd, 0660) == 0 && fsync(fd) == 0)
        result = 0;
    if (close(fd) != 0)
        result = -1;
    if (result == 0 && rename(temporary, path) == 0 &&
        selinux_android_restorecon(path, 0) == 0 && fsync(dirfd) == 0) {
        close(dirfd);
        return 0;
    }
    unlink(temporary);
    close(dirfd);
    return -1;
}

/* Notify readiness only. Each consumer reads the actual /data file itself. */
static int notify_ready(const char *path)
{
    int fd, result;
    ssize_t written;
    fd = open(path, O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    do {
        written = write(fd, "1\n", 2);
    } while (written < 0 && errno == EINTR);
    result = written == 2 ? 0 : -1;
    if (close(fd) != 0)
        result = -1;
    return result;
}

static int initialize_address(int wifi)
{
    unsigned char nv[8] = {0}, addr[6];
    const char *name = wifi ? "Wi-Fi" : "Bluetooth";
    int i, status;
    status = nv_cmd_remote(0, wifi ? 0x1246 : 447, nv);
    if (status != 0) {
        KLOG_ERROR("hwaddrs", "%s NV read failed: %d\n", name, status);
        return 1;
    }
    for (i = 0; i < 6; ++i)
        addr[i] = nv[wifi ? i : 5 - i];
    if (!valid_address(addr, wifi)) {
        KLOG_ERROR("hwaddrs", "invalid %s NV address\n", name);
        return 1;
    }
    if (save_address(wifi, addr) != 0) {
        KLOG_ERROR("hwaddrs", "cannot save %s address: %s\n", name, strerror(errno));
        return 1;
    }
    if (notify_ready(wifi ? WIFI_READY_SYSFS : BT_READY_SYSFS) != 0) {
        KLOG_ERROR("hwaddrs", "cannot signal %s file readiness: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

static int initialize_addresses(void)
{
    int bt_result, wifi_result;
    oncrpc_init();
    oncrpc_task_start();
    bt_result = initialize_address(0);
    wifi_result = initialize_address(1);
    return bt_result || wifi_result;
}

static int fail(const char *reason)
{
    property_set("sys.hwaddrs.status", reason);
    KLOG_ERROR("hwaddrs", "%s; unpublished radios remain unavailable\n", reason);
    return 1;
}

int main(void)
{
    pid_t child, result;
    int elapsed, status = 0;
    klog_init();
    klog_set_level(KLOG_INFO_LEVEL);
    if (property_set("sys.hwaddrs.ready", "0") != 0)
        return fail("property_failed");
    property_set("sys.hwaddrs.status", "waiting_rpc");
    KLOG_INFO("hwaddrs", "waiting for NV RPC endpoint\n");
    for (elapsed = 0; elapsed < RPC_WAIT_SECONDS; ++elapsed) {
        if (nv_rpc_ready())
            break;
        sleep(1);
    }
    if (!nv_rpc_ready())
        return fail("rpc_timeout");

    property_set("sys.hwaddrs.status", "reading_nv");
    KLOG_INFO("hwaddrs", "NV RPC ready; reading addresses\n");
    /* Isolate vendor RPC failures and bound a stalled RPC without blocking init. */
    child = fork();
    if (child < 0)
        return fail("fork_failed");
    if (child == 0)
        _exit(initialize_addresses());
    for (elapsed = 0; elapsed < READ_WAIT_SECONDS; ++elapsed) {
        do {
            result = waitpid(child, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == child) {
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                return fail("read_failed");
            KLOG_INFO("hwaddrs", "factory address files saved; readers released\n");
            property_set("sys.hwaddrs.status", "ready");
            if (property_set("sys.hwaddrs.ready", "1") != 0)
                return fail("property_failed");
            return 0;
        }
        if (result < 0)
            break;
        sleep(1);
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return fail("read_timeout");
}
