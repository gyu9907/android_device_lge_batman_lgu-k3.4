/*
 * Compatibility shim for the legacy msm8660 Qualcomm AVC encoder.
 */

#define LOG_TAG "OmxVencCompatShim"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cutils/log.h>

/* msm8660 msm_vidc_enc.h: _IO(VEN_IOCTLBASE_ENC, 52). */
#define VEN_IOCTLBASE_ENC 0x850
#define VEN_IOCTL_SET_VUI_BITSTREAM_RESTRICT_FLAG \
    _IO(VEN_IOCTLBASE_ENC, 52)

static pthread_mutex_t gVidcMutex = PTHREAD_MUTEX_INITIALIZER;
static int gEncoderFd = -1;
static int gEncoderLockFd = -1;

extern "C" int open(const char *path, int flags, ...)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    static open_fn real_open = reinterpret_cast<open_fn>(dlsym(RTLD_NEXT, "open"));
    static close_fn real_close = reinterpret_cast<close_fn>(dlsym(RTLD_NEXT, "close"));
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
    if (path && !strcmp(path, "/dev/msm_vidc_enc")) {
        int lockFd = real_open("/dev/msm_vidc_reg", O_RDONLY);
        if (lockFd < 0) return -1;
        if (flock(lockFd, LOCK_EX | LOCK_NB) < 0) {
            const int savedErrno = errno;
            real_close(lockFd);
            ALOGW("VIDC unavailable; refusing encoder without waiting");
            errno = savedErrno == EWOULDBLOCK ? EBUSY : savedErrno;
            return -1;
        }
        int fd = (flags & O_CREAT) ? real_open(path, flags, mode) : real_open(path, flags);
        if (fd < 0) {
            const int savedErrno = errno;
            real_close(lockFd);
            errno = savedErrno;
            return -1;
        }
        pthread_mutex_lock(&gVidcMutex);
        gEncoderFd = fd;
        gEncoderLockFd = lockFd;
        pthread_mutex_unlock(&gVidcMutex);
        ALOGI("encoder VIDC access acquired fd=%d", fd);
        return fd;
    }
    return (flags & O_CREAT) ? real_open(path, flags, mode) : real_open(path, flags);
}

extern "C" int close(int fd)
{
    typedef int (*close_fn)(int);
    static close_fn real_close = reinterpret_cast<close_fn>(dlsym(RTLD_NEXT, "close"));
    int lockFd = -1;
    pthread_mutex_lock(&gVidcMutex);
    if (fd == gEncoderFd) {
        lockFd = gEncoderLockFd;
        gEncoderFd = -1;
        gEncoderLockFd = -1;
    }
    pthread_mutex_unlock(&gVidcMutex);
    int result = real_close(fd);
    if (lockFd >= 0) {
        flock(lockFd, LOCK_UN);
        real_close(lockFd);
        ALOGI("encoder VIDC access released");
    }
    return result;
}

extern "C" int ioctl(int fd, int request, ...)
{
    typedef int (*ioctl_fn)(int, int, ...);
    static ioctl_fn real_ioctl =
            reinterpret_cast<ioctl_fn>(dlsym(RTLD_NEXT, "ioctl"));

    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);

    if (request == VEN_IOCTL_SET_VUI_BITSTREAM_RESTRICT_FLAG) {
        /*
         * The encoder blob treats this optional VUI property as mandatory,
         * while the Batman firmware rejects it.  CM13 did not require the
         * property for recording.  Pretend it was accepted so the otherwise
         * functional hardware encoder can continue configuration.
         */
        ALOGI("ignoring unsupported VUI bitstream restriction ioctl");
        return 0;
    }

    return real_ioctl(fd, request, arg);
}
