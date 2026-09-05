#ifndef BATMAN_THUMBNAIL_YUV_H
#define BATMAN_THUMBNAIL_YUV_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace android {
// NV12 has interleaved U,V; I420 has separate U then V planes. Copy the crop
// into an even-sized layout, avoiding the platform converter's crop offsets.
inline bool thumbnailNv12ToI420(const uint8_t* src, size_t srcSize,
        size_t stride, size_t slice, size_t left, size_t top,
        size_t width, size_t height, uint8_t* dst, size_t dstSize) {
    if (!src || !dst || !stride || !slice || !width || !height ||
            ((stride | slice | left | top) & 1) ||
            left >= stride || top >= slice || width > stride - left ||
            height > slice - top || stride > SIZE_MAX / slice)
        return false;
    const size_t srcYSize = stride * slice;
    if (srcYSize > SIZE_MAX - srcYSize / 2 ||
            srcSize < srcYSize + srcYSize / 2)
        return false;
    const size_t outStride = (width + 1) & ~size_t(1);
    const size_t outSlice = (height + 1) & ~size_t(1);
    if (!outStride || !outSlice || outStride > SIZE_MAX / outSlice)
        return false;
    const size_t ySize = outStride * outSlice;
    if (ySize > SIZE_MAX - ySize / 2 || dstSize < ySize + ySize / 2)
        return false;
    memset(dst, 0, ySize);
    memset(dst + ySize, 128, ySize / 2);
    for (size_t y = 0; y < height; ++y)
        memcpy(dst + y * outStride, src + (top + y) * stride + left, width);
    uint8_t* u = dst + ySize;
    uint8_t* v = u + ySize / 4;
    for (size_t y = 0; y < outSlice / 2; ++y) {
        const uint8_t* uv = src + srcYSize + (top / 2 + y) * stride + left;
        for (size_t x = 0; x < outStride / 2; ++x) {
            u[y * (outStride / 2) + x] = uv[2 * x];
            v[y * (outStride / 2) + x] = uv[2 * x + 1];
        }
    }
    return true;
}
}
#endif
