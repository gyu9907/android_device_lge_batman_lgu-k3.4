/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *      http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "batman PowerHAL"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <log/log.h>
#include <hardware/hardware.h>
#include <hardware/power.h>

#define ONDEMAND "/sys/devices/system/cpu/cpufreq/ondemand/"

/* CPU frequency floors in kHz and pulse durations in microseconds. */
struct boost_profile { unsigned int freq; unsigned int duration; };
static const struct boost_profile normal = { 1512000, 40000 };
static const struct boost_profile saver = { 810000, 80000 };
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool interactive = true;
static bool low_power;
static bool configured;
static bool error_reported;

static bool write_value(const char *path, unsigned int value)
{
    char buf[24];
    int len = snprintf(buf, sizeof(buf), "%u", value);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    int error = errno;
    bool ok = false;
    if (fd >= 0) {
        ssize_t result;
        do { result = write(fd, buf, len); } while (result < 0 && errno == EINTR);
        error = result < 0 ? errno : EIO;
        ok = result == len;
        close(fd);
    }
    if (!ok && !error_reported) {
        ALOGE("Cannot write %s: %s", path, strerror(error));
        error_reported = true;
    }
    return ok;
}

/* Called with lock held. A mode change cancels the previous pulse first. */
static bool configure(void)
{
    const struct boost_profile *profile = low_power ? &saver : &normal;
    configured = false;
    if (!write_value(ONDEMAND "boostpulse", 0) ||
        !write_value(ONDEMAND "boostpulse_duration", profile->duration) ||
        !write_value(ONDEMAND "boost_freq", interactive ? profile->freq : 0))
        return false;
    configured = true;
    error_reported = false;
    return true;
}

static void power_init(struct power_module *module __attribute__((unused)))
{
    pthread_mutex_lock(&lock);
    configure();
    pthread_mutex_unlock(&lock);
}

static void power_set_interactive(struct power_module *module __attribute__((unused)), int on)
{
    pthread_mutex_lock(&lock);
    if (interactive != !!on || !configured) {
        interactive = !!on;
        configure();
    }
    pthread_mutex_unlock(&lock);
}

static void power_hint(struct power_module *module __attribute__((unused)),
                       power_hint_t hint, void *data)
{
    pthread_mutex_lock(&lock);
    if (hint == POWER_HINT_LOW_POWER) {
        bool enabled = data && *(int *)data;
        if (enabled != low_power || !configured) {
            low_power = enabled;
            configure();
        }
    } else if (hint == POWER_HINT_INTERACTION && interactive) {
        /* Duration hints do not extend the device's bounded pulse policy. */
        if (configured || configure()) {
            if (!write_value(ONDEMAND "boostpulse", 1))
                configured = false;
        }
    }
    pthread_mutex_unlock(&lock);
}

static struct hw_module_methods_t methods = { .open = NULL };
struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_2,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "Batman interaction and battery saver Power HAL",
        .author = "The LineageOS Project",
        .methods = &methods,
    },
    .init = power_init,
    .setInteractive = power_set_interactive,
    .powerHint = power_hint,
};
