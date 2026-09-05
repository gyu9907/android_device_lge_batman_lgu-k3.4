#include <binder/ProcessState.h>
#include <media/MediaMetadataRetrieverInterface.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include "frameworks/av/media/libstagefright/include/StagefrightMetadataRetriever.h"
int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) return 1;
    android::ProcessState::self()->startThreadPool();
    void *lib = dlopen(argc >= 3 ? argv[2] : "libshim_thumbnail.so", RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    typedef void (*ctor)(void *);
    ctor create = reinterpret_cast<ctor>(dlsym(lib,
        "_ZN7android28StagefrightMetadataRetrieverC1Ev"));
    if (!create) return 3;
    void *mem = ::operator new(sizeof(android::StagefrightMetadataRetriever));
    create(mem);
    android::MediaMetadataRetrieverInterface *r =
        static_cast<android::StagefrightMetadataRetriever *>(mem);
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) return 4;
    int result = r->setDataSource(fd, 0, st.st_size);
    close(fd);
    printf("setDataSource=%d\n", result); fflush(stdout);
    if (!result) {
        android::VideoFrame *frame = r->getFrameAtTime(-1, 2);
        printf("frame=%s\n", frame ? "OK" : "NULL");
        result = frame ? 0 : 5;
        if (frame && argc == 4) {
            FILE *out = fopen(argv[3], "wb");
            if (!out) return 6;
            fprintf(out, "P6\n%u %u\n255\n", frame->mWidth, frame->mHeight);
            const uint16_t *pixels = reinterpret_cast<const uint16_t *>(frame->mData);
            for (unsigned int i = 0; i < frame->mWidth * frame->mHeight; ++i) {
                const uint16_t p = pixels[i];
                const unsigned char rgb[] = {
                    static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                    static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
                    static_cast<unsigned char>((p & 31) * 255 / 31)};
                if (fwrite(rgb, 1, sizeof(rgb), out) != sizeof(rgb)) return 7;
            }
            if (fclose(out)) return 8;
            printf("saved=%s width=%u height=%u rotation=%u\n", argv[3],
                    frame->mWidth, frame->mHeight, frame->mRotationAngle);
        }
        delete frame;
    }
    delete r;
    return result;
}
