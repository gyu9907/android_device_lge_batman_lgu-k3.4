#define LOG_TAG "OmxVdecCompatShim"
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <unistd.h>
#include <cutils/log.h>

static pthread_mutex_t gVidcMutex = PTHREAD_MUTEX_INITIALIZER;
static int gDecoderFd = -1;
static int gDecoderLockFd = -1;

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
    if (path && (!strcmp(path, "/dev/msm_vidc_dec") ||
            !strcmp(path, "/dev/msm_vidc_dec_sec"))) {
        int lockFd = real_open("/dev/msm_vidc_reg", O_RDONLY);
        if (lockFd < 0) return -1;
        if (lockFd >= 0) {
            if (flock(lockFd, LOCK_EX | LOCK_NB) < 0) {
                const int savedErrno = errno;
                real_close(lockFd);
                errno = savedErrno == EWOULDBLOCK ? EBUSY : savedErrno;
                ALOGW("VIDC busy; rejecting hardware decoder without waiting");
                return -1;
            }
        }
        int fd = (flags & O_CREAT) ? real_open(path, flags, mode) : real_open(path, flags);
        if (fd < 0) {
            const int savedErrno = errno;
            real_close(lockFd);
            errno = savedErrno;
            return -1;
        }
        pthread_mutex_lock(&gVidcMutex);
        gDecoderFd = fd;
        gDecoderLockFd = lockFd;
        pthread_mutex_unlock(&gVidcMutex);
        ALOGI("decoder VIDC access acquired fd=%d", fd);
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
    if (fd == gDecoderFd) {
        lockFd = gDecoderLockFd;
        gDecoderFd = -1;
        gDecoderLockFd = -1;
    }
    pthread_mutex_unlock(&gVidcMutex);
    int result = real_close(fd);
    if (lockFd >= 0) {
        flock(lockFd, LOCK_UN);
        real_close(lockFd);
        ALOGI("decoder VIDC access released");
    }
    return result;
}
