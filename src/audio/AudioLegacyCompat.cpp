// Oreo's legacy audio interface declares this optional method without a base
// implementation. Keep the fallback local to this HAL; a future platform
// implementation takes precedence over this weak definition.
#include <hardware_legacy/AudioHardwareInterface.h>

namespace android_audio_legacy {
__attribute__((weak)) android::status_t AudioStreamOut::getPresentationPosition(
        uint64_t *, struct timespec *)
{
    return android::INVALID_OPERATION;
}
}
