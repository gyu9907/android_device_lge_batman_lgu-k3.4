// Device-local bridge for msm8660's payload-free thumbnail extension.
#define LOG_TAG "BatmanThumbnailOMX"
#include <cstring>
#include <dlfcn.h>
#include <OMX_Component.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <media/stagefright/FFMPEGSoftCodec.h>

namespace android {
status_t FFMPEGSoftCodec::setVideoFormat(status_t status,
        const sp<AMessage>& msg, const char* mime, sp<IOMX> omx,
        IOMX::node_id node, bool encoder,
        OMX_VIDEO_CODINGTYPE* compression, const char* component)
{
    typedef status_t (*Original)(status_t, const sp<AMessage>&, const char*,
            sp<IOMX>, IOMX::node_id, bool, OMX_VIDEO_CODINGTYPE*, const char*);
    static Original original = reinterpret_cast<Original>(dlsym(RTLD_NEXT,
            "_ZN7android15FFMPEGSoftCodec14setVideoFormatEiRKNS_2spINS_8AMessageEEEPKcNS1_INS_4IOMXEEEjbP20OMX_VIDEO_CODINGTYPES7_"));
    if (!original) {
        ALOGE("platform setVideoFormat ABI unavailable");
        return INVALID_OPERATION;
    }
    char board[PROPERTY_VALUE_MAX];
    property_get("ro.board.platform", board, "");
    int32_t thumbnail = 0;
    const bool bridge = !encoder && component &&
            !strcmp(component, "OMX.qcom.video.decoder.avc") &&
            !strcmp(board, "msm8660") &&
            msg->findInt32("thumbnail-mode", &thumbnail) && thumbnail > 0;
    if (!bridge)
        return original(status, msg, mime, omx, node, encoder, compression, component);

    // Suppress only the legacy four-byte request; retain all other platform
    // video setup. Pool-count hints are reconciled below after enabling IDR.
    sp<AMessage> config = msg->dup();
    config->setInt32("thumbnail-mode", 0);
    status_t err = original(status, config, mime, omx, node, encoder,
            compression, component);
    if (err != OK) return err;

    OMX_INDEXTYPE index;
    err = omx->getExtensionIndex(node,
            "OMX.QCOM.index.param.video.SyncFrameDecodingMode", &index);
    if (err != OK) return err;

    // This msm8660 extension ignores paramData: the command itself enables
    // IDR-only decoding, then refreshes the output-buffer requirements.
    // Send a valid OMX header instead of QOMX_ENABLETYPE (only four bytes).
    // Do not relax IOMX's minimum-size or declared-size validation.
    struct Header { OMX_U32 nSize; OMX_VERSIONTYPE nVersion; } header = {};
    static_assert(sizeof(Header) == 8, "unexpected OMX header ABI");
    header.nSize = sizeof(header);
    header.nVersion.s.nVersionMajor = 1;
    header.nVersion.s.nVersionMinor = 0;
    err = omx->setParameter(node, index, &header, sizeof(header));
    ALOGI("hardware thumbnail mode: node=%u result=%d", node, err);
    if (err == OK) {
        // Keep the HAL's native pool counts instead of forcing one input
        // buffer. The legacy VCD validates the complete registered pool.
        for (OMX_U32 port = 0; port < 2; ++port) {
            OMX_PARAM_PORTDEFINITIONTYPE def = {};
            def.nSize = sizeof(def);
            def.nVersion.s.nVersionMajor = 1;
            def.nPortIndex = port;
            status_t result = omx->getParameter(node, OMX_IndexParamPortDefinition,
                    &def, sizeof(def));
            if (result != OK) return result;
            ALOGI("thumbnail port=%u min=%u actual=%u size=%u", port,
                    def.nBufferCountMin, def.nBufferCountActual, def.nBufferSize);
            msg->setInt32(port ? "android._num-output-buffers" :
                    "android._num-input-buffers", def.nBufferCountActual);
        }
    }
    return err;
}
}
