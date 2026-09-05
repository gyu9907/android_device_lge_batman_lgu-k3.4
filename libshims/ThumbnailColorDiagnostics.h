// Only compiled into the standalone diagnostic library, never the media service.
#include "ThumbnailColorConverter.h"
#include <cstdio>
#include <cstdlib>
namespace android {
struct BatmanDiagnosticColorConverter : BatmanThumbnailColorConverter {
    OMX_COLOR_FORMATTYPE format;
    BatmanDiagnosticColorConverter(OMX_COLOR_FORMATTYPE from, OMX_COLOR_FORMATTYPE to)
        : BatmanThumbnailColorConverter(from, to), format(from) {}
    status_t convert(const void* src, size_t w, size_t h,
            size_t l, size_t t, size_t r, size_t b, void* dst,
            size_t dw, size_t dh, size_t dl, size_t dt, size_t dr, size_t db) {
        printf("YUV format=%#x stride=%zu slice=%zu crop=%zu,%zu,%zu,%zu\n",
                format, w, h, l, t, r, b);
        const char* path = getenv("BATMAN_YUV_DUMP");
        if (path && (format == OMX_COLOR_FormatYUV420Planar ||
                format == OMX_COLOR_FormatYUV420SemiPlanar) && w && h &&
                w <= 4096 && h <= 4096) {
            FILE* file = fopen(path, "wb");
            if (file) {
                size_t size = w * h * 3 / 2;
                printf("YUV dump=%s bytes=%zu/%zu\n", path,
                        fwrite(src, 1, size, file), size);
                fclose(file);
            }
        }
        return BatmanThumbnailColorConverter::convert(src,w,h,l,t,r,b,dst,dw,dh,dl,dt,dr,db);
    }
};
}
