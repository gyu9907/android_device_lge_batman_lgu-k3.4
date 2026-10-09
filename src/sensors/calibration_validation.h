/* Read-only validation for the fixed-size LGE iNemo calibration records. */
#ifndef BATMAN_CALIBRATION_VALIDATION_H
#define BATMAN_CALIBRATION_VALIDATION_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace batman {
enum class CalibrationKind { Gyro, Compass };

// Missing files use the original blob's cold-start initialization.
// Return a diagnostic only for an unsafe existing record or an access error.
// No defaults are written and no pre-existing record is repaired or replaced.
inline const char* validateCalibration(const char* path, CalibrationKind kind) {
    const size_t expected = kind == CalibrationKind::Gyro ? 12 : 24;
    // O_NONBLOCK keeps a substituted FIFO from hanging service startup.
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) return nullptr;
        return "cannot open regular calibration file";
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return "calibration is not an accessible regular file";
    }
    if (st.st_size != static_cast<off_t>(expected)) {
        close(fd);
        return "calibration has an unexpected size";
    }
    unsigned char data[24] = {};
    size_t offset = 0;
    while (offset < expected) {
        ssize_t n = read(fd, data + offset, expected - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            close(fd);
            return "calibration read was incomplete";
        }
        offset += static_cast<size_t>(n);
    }
    unsigned char extra;
    ssize_t tail;
    do {
        tail = read(fd, &extra, 1);
    } while (tail < 0 && errno == EINTR);
    bool stableSize = fstat(fd, &st) == 0 && st.st_size == static_cast<off_t>(expected);
    int closed = close(fd);
    if (tail != 0 || !stableSize || closed != 0) return "calibration changed or could not be read";

    // Gyro: three little-endian IEEE-754 floats (loadGBias, 12-byte fread).
    // Compass: only the final float at byte 20 is understood. The earlier
    // offsets/padding are preserved without imposing guessed semantic bounds.
    const size_t first = kind == CalibrationKind::Gyro ? 0 : 20;
    for (size_t i = first; i < expected; i += sizeof(float)) {
        uint32_t bits = static_cast<uint32_t>(data[i]) |
                (static_cast<uint32_t>(data[i + 1]) << 8) |
                (static_cast<uint32_t>(data[i + 2]) << 16) |
                (static_cast<uint32_t>(data[i + 3]) << 24);
        float value;
        static_assert(sizeof(value) == sizeof(bits), "32-bit float calibration ABI");
        memcpy(&value, &bits, sizeof(value));
        if (!std::isfinite(value)) return "calibration contains a non-finite float";
    }
    return nullptr;
}
}  // namespace batman
#endif
