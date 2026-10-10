#ifndef BATMAN_THUMBNAIL_COLOR_CONVERTER_H
#define BATMAN_THUMBNAIL_COLOR_CONVERTER_H
#include <media/stagefright/ColorConverter.h>
#include <stdlib.h>
#include "ThumbnailYuv.h"

namespace android {
// Local to the retriever, not an interposition of the global ColorConverter.
// C2D's planar output duplicates V into U on this device; its NV12 is correct.
struct BatmanThumbnailColorConverter : ColorConverter {
    const bool nv12;
    BatmanThumbnailColorConverter(OMX_COLOR_FORMATTYPE from, OMX_COLOR_FORMATTYPE to)
        : ColorConverter(from == OMX_COLOR_FormatYUV420SemiPlanar ?
                OMX_COLOR_FormatYUV420Planar : from, to),
          nv12(from == OMX_COLOR_FormatYUV420SemiPlanar) {}

    status_t convert(const void* src, size_t w, size_t h, size_t stride,
            size_t l, size_t t, size_t r, size_t b, void* dst,
            size_t dw, size_t dh, size_t dstStride, size_t dl, size_t dt, size_t dr, size_t db) {
        if (!nv12)
            return ColorConverter::convert(src,w,h,stride,l,t,r,b,dst,dw,dh,dstStride,dl,dt,dr,db);
        if (!src || !dst || !w || !h || stride < w || ((stride | h | l | t) & 1) ||
                l > r || t > b || r >= w || b >= h || stride > SIZE_MAX / h ||
                dl > dr || dt > db || dr >= dw || db >= dh ||
                r - l != dr - dl || b - t != db - dt)
            return BAD_VALUE;
        const size_t inputY = stride * h;
        if (inputY > SIZE_MAX - inputY / 2) return BAD_VALUE;
        const size_t cw = r - l + 1, ch = b - t + 1;
        const size_t pw = (cw + 1) & ~size_t(1), ph = (ch + 1) & ~size_t(1);
        if (!pw || !ph || pw > SIZE_MAX / ph) return BAD_VALUE;
        const size_t ySize = pw * ph;
        if (ySize > SIZE_MAX - ySize / 2) return BAD_VALUE;
        const size_t size = ySize + ySize / 2;
        uint8_t* planar = static_cast<uint8_t*>(malloc(size));
        if (!planar) return NO_MEMORY;
        status_t result = BAD_VALUE;
        if (thumbnailNv12ToI420(static_cast<const uint8_t*>(src),
                inputY + inputY / 2, stride, h, l, t, cw, ch, planar, size)) {
            result = ColorConverter::convert(planar, pw, ph, pw, 0, 0, cw - 1, ch - 1,
                    dst, dw, dh, dstStride, dl, dt, dr, db);
        }
        free(planar);
        return result;
    }
};
}
#endif
