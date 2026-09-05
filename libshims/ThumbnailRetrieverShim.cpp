// Device-local retriever implementation: preserve platform behavior except
// admit hardware frame extraction only after the previous VIDC client closes.
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <new>
#include <cutils/log.h>
#include <media/stagefright/MediaCodecList.h>
#include <media/stagefright/FFMPEGSoftCodec.h>

namespace android {
// The platform retriever can replace a selected component with FFmpeg after
// codec-list filtering. Keep that second selection path hardware-only too.
struct BatmanThumbnailNoSoftwareOverride {
    static const char *overrideComponentName(uint32_t, const sp<MetaData>&,
            const char *, bool) { return NULL; }
};
// Wait before creating MediaCodec, never inside the OMX open callback where
// waiting can deadlock Nougat's synchronous resource-reclaim path. This is an
// availability check, not a transferred reservation: the decoder shim must
// still acquire its exclusive lock atomically. If another client wins that
// race, extraction fails rather than overlapping clients or using software.
static bool waitForThumbnailVidc()
{
    int fd = open("/dev/msm_vidc_reg", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("Batman thumbnail: cannot open VIDC lock: %d", errno);
        return false;
    }
    const unsigned int maxWaits = 100;
    for (unsigned int wait = 0; wait <= maxWaits; ++wait) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            flock(fd, LOCK_UN);
            close(fd);
            ALOGI("Batman thumbnail: VIDC available after %u ms; hardware only",
                    wait * 50);
            return true;
        }
        const int error = errno;
        if ((error != EWOULDBLOCK && error != EAGAIN && error != EINTR) ||
                wait == maxWaits) {
            close(fd);
            ALOGW("Batman thumbnail: VIDC admission failed: errno=%d; no software fallback",
                    error);
            return false;
        }
        if (wait == 0)
            ALOGI("Batman thumbnail: waiting for VIDC client release (max 5s)");
        usleep(50000);
    }
    close(fd);
    return false;
}

struct BatmanThumbnailCodecList {
    static void findMatchingCodecs(const char *mime, bool encoder,
            uint32_t flags, Vector<AString> *matches) {
        matches->clear();
        if (!waitForThumbnailVidc())
            return;
        MediaCodecList::findMatchingCodecs(mime, encoder,
                (flags & ~MediaCodecList::kPreferSoftwareCodecs) |
                MediaCodecList::kHardwareCodecsOnly, matches);
    }
};
}

#define StagefrightMetadataRetriever BatmanThumbnailRetriever
#define MediaCodecList BatmanThumbnailCodecList
#define FFMPEGSoftCodec BatmanThumbnailNoSoftwareOverride
#undef LOG_TAG
#include "frameworks/av/media/libstagefright/StagefrightMetadataRetriever.cpp"
#undef MediaCodecList
#undef FFMPEGSoftCodec
#undef StagefrightMetadataRetriever

// MetadataRetrieverClient allocates the original class before calling C1.
// Verify that the renamed implementation has exactly the same object size.
#undef STAGEFRIGHT_METADATA_RETRIEVER_H_
#include "frameworks/av/media/libstagefright/include/StagefrightMetadataRetriever.h"
static_assert(sizeof(android::BatmanThumbnailRetriever) ==
        sizeof(android::StagefrightMetadataRetriever), "retriever ABI changed");

extern "C" void batman_retriever_ctor(void *self)
    asm("_ZN7android28StagefrightMetadataRetrieverC1Ev");
extern "C" void batman_retriever_ctor(void *self) {
    ALOGI("Batman thumbnail retriever: sequential hardware decoding enabled");
    new (self) android::BatmanThumbnailRetriever;
}
