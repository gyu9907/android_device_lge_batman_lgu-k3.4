// Device-local Oreo thumbnail configuration. The Nougat FFMPEGSoftCodec
// entry point no longer exists. The device linker mapping loads this shim
// only for mediaserver, including when its domain transition sets AT_SECURE.
#define LOG_TAG "BatmanThumbnailOMX"
#include <cstring>
#include <OMX_Video.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/AString.h>
#include <gui/Surface.h>
#include <media/ICrypto.h>
#include <android/hardware/cas/native/1.0/IDescrambler.h>

namespace android {
status_t MediaCodec::configure(const sp<AMessage>& format,
        const sp<Surface>& surface, const sp<ICrypto>& crypto, uint32_t flags)
{
    sp<AMessage> config = format;
    AString component;
    if (format != NULL && surface == NULL && crypto == NULL && flags == 0 &&
            getName(&component) == OK &&
            (component == "OMX.qcom.video.decoder.avc" ||
             component == "OMX.qcom.video.decoder.mpeg4")) {
        // Omit the retriever's one-buffer hints: the legacy VCD requires its
        // complete registered input/output pools. changesFrom preserves every
        // other format entry without relying on AMessage's private layout.
        sp<AMessage> hints = new AMessage;
        int32_t count;
        if (format->findInt32("android._num-input-buffers", &count))
            hints->setInt32("android._num-input-buffers", count);
        if (format->findInt32("android._num-output-buffers", &count))
            hints->setInt32("android._num-output-buffers", count);
        config = format->changesFrom(hints);
        // A sync-frame thumbnail can use the driver's existing IDR-only mode.
        // Retain the complete pools reported for that mode; forcing one input
        // buffer independently of the driver can stall legacy VCD.
        if (component == "OMX.qcom.video.decoder.avc" &&
                format->findInt32("android._num-output-buffers", &count) && count == 1)
            config->setInt32("vendor.qcom.sync-frame", 1);
        // This Oreo changesFrom implementation reads float/double entries via
        // the integer union member. Preserve their actual values explicitly.
        for (size_t i = 0; i < format->countEntries(); ++i) {
            AMessage::Type type;
            const char *name = format->getEntryNameAt(i, &type);
            float floatValue;
            double doubleValue;
            if (type == AMessage::kTypeFloat && format->findFloat(name, &floatValue))
                config->setFloat(name, floatValue);
            else if (type == AMessage::kTypeDouble && format->findDouble(name, &doubleValue))
                config->setDouble(name, doubleValue);
        }
        // The device retriever converts NV12 itself; the legacy C2D planar
        // conversion produces incorrect chroma on this decoder.
        config->setInt32("color-format", OMX_COLOR_FormatYUV420SemiPlanar);
    }
    // Oreo's four-argument overload delegates to this five-argument overload.
    // Call it directly so no private mangled symbol lookup is needed.
    return configure(config, surface, crypto, sp<IDescrambler>(), flags);
}
}
