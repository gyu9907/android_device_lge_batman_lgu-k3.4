/*
 * Copyright (C) 2012, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file CameraWrapper.cpp
*
* This file wraps a vendor camera module.
*
*/

/* Keep parameter dumps disabled: logd's chatty suppression hides crash context. */

#define LOG_TAG "CameraWrapper"
#include <cutils/log.h>

#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <utils/threads.h>
#include <utils/String8.h>
#include <hardware/hardware.h>
#include <hardware/camera.h>
#include <camera/CameraParameters.h>
#include "CameraCallbacks.h"

static android::Mutex gCameraWrapperLock;
static android::Mutex gMemoryCallbackLock;
static camera_module_t *gVendorModule = 0;

/*
 * The vendor camera requests the IOMMU and ADSP heaps together.  Keep the
 * IOMMU request, which is required when the camera driver imports the preview
 * buffers into CAMERA_DOMAIN, but drop the incompatible ADSP heap bit.
 */
#define GRALLOC_USAGE_PRIVATE_IOMMU_HEAP 0x40000000
#define GRALLOC_USAGE_PRIVATE_ADSP_HEAP 0x01000000

typedef struct wrapper_preview_stream_ops {
    preview_stream_ops_t base;
    preview_stream_ops_t *vendor;
} wrapper_preview_stream_ops_t;

static int camera_device_open(const hw_module_t *module, const char *name, hw_device_t **device);
static int camera_device_close(hw_device_t* device);
static int camera_get_number_of_cameras(void);
static int camera_get_camera_info(int camera_id, struct camera_info *info);
static int camera_preview_enabled(struct camera_device *device);

static struct hw_module_methods_t camera_module_methods = {
    open: camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
         tag: HARDWARE_MODULE_TAG,
         version_major: 1,
         version_minor: 0,
         id: CAMERA_HARDWARE_MODULE_ID,
         name: "LG Optimus Vu msm8660 Camera Wrapper",
         author: "The CyanogenMod Project",
         methods: &camera_module_methods,
         dso: NULL, /* remove compilation warnings */
         reserved: {0}, /* remove compilation warnings */
    },
    get_number_of_cameras: camera_get_number_of_cameras,
    get_camera_info: camera_get_camera_info,
    set_callbacks: NULL,
    get_vendor_tag_ops: NULL,
    open_legacy: NULL,
    set_torch_mode: NULL,
    init: NULL,
    reserved: {0},
};

typedef struct wrapper_camera_device {
    camera_device_t base;
    int id;
    camera_device_t *vendor;
    wrapper_preview_stream_ops_t preview_window;
    CameraCallbacks *callbacks;
    camera_notify_callback notify_cb;
    camera_data_callback data_cb;
    camera_data_timestamp_callback data_cb_timestamp;
    camera_request_memory get_memory;
    void *callback_user;
} wrapper_camera_device_t;

static wrapper_camera_device_t *gMemoryCallbackDevice = 0;

#define VENDOR_CALL(device, func, ...) ({ \
    wrapper_camera_device_t *__wrapper_dev = (wrapper_camera_device_t*) device; \
    __wrapper_dev->vendor->ops->func(__wrapper_dev->vendor, ##__VA_ARGS__); \
})

#define CAMERA_ID(device) (((wrapper_camera_device_t *)(device))->id)

static int check_vendor_module()
{
    int rv = 0;
    ALOGV("%s", __FUNCTION__);

    if (gVendorModule)
        return 0;

    rv = hw_get_module_by_class(CAMERA_HARDWARE_MODULE_ID, "vendor", (const hw_module_t**)&gVendorModule);
    if (rv) {
        ALOGE("failed to open vendor camera module: %d", rv);
        return rv;
    }

    /*
     * The stock module predates camera_common.h.  Validate only the entries
     * that existed in that ABI before exposing it to Nougat's CameraModule.
     */
    if (!gVendorModule->common.methods ||
            !gVendorModule->common.methods->open ||
            !gVendorModule->get_number_of_cameras ||
            !gVendorModule->get_camera_info) {
        ALOGE("vendor camera module has an incomplete camera1 interface");
        gVendorModule = NULL;
        return -EINVAL;
    }
    return rv;
}

const static char * iso_values[] = {
    "auto,ISO100,ISO200,ISO400,ISO800",
    "auto,ISO100,ISO200,ISO400,ISO800"
};

// Qualcomm's CM11 camera HAL uses these vendor parameter keys.  They are not
// part of the CM12.1 CameraParameters API, so keep the ABI strings local to
// the wrapper instead of relying on removed CameraParameters extensions.
static const char KEY_SUPPORTED_ISO_MODES[] = "iso-values";
static const char KEY_ISO_MODE[] = "iso";
static const char KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO[] =
        "preferred-preview-size-for-video";
static const char KEY_SUPPORTED_PREVIEW_SIZES[] = "preview-size-values";
static const char BATMAN_PREVIEW_SIZES[] =
        "1360x768,1536x864,1280x720,960x720,800x480,768x432,720x480,640x480,"
        "576x432,480x320,384x288,352x288,320x240,240x160,176x144";

static char *camera_fixup_getparams(int id, const char *settings)
{
    if (!settings)
        return NULL;

    android::CameraParameters params;
    params.unflatten(android::String8(settings));

    ALOGV("%s: original parameters:", __FUNCTION__);

    if (id >= 0 && id < static_cast<int>(sizeof(iso_values) /
            sizeof(iso_values[0])))
        params.set(KEY_SUPPORTED_ISO_MODES, iso_values[id]);

    /* Select the p930 CM11 combination before Camera2 lays out its preview. */
    if (id == 0) {
        params.set(KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "1360x768");
        /*
         * 1360x768 is a selection-only alias: it matches the 1920x1088 ratio
         * within Camera2's tolerance and the Vu's 768-pixel short display
         * side. camera_fixup_setparams() maps it to the HAL's 1280x720 mode.
         */
        params.set(KEY_SUPPORTED_PREVIEW_SIZES, BATMAN_PREVIEW_SIZES);
    }

    android::String8 strParams = params.flatten();
    char *ret = strdup(strParams.string());

    ALOGD("%s: get parameters fixed up", __FUNCTION__);
    return ret;
}

static char *camera_fixup_setparams(int id, const char *settings)
{
    if (!settings)
        return NULL;

    android::CameraParameters params;
    params.unflatten(android::String8(settings));

    ALOGV("%s: original parameters:", __FUNCTION__);

    if(params.get(KEY_ISO_MODE)) {
        const char* isoMode = params.get(KEY_ISO_MODE);
        if(strcmp(isoMode, "ISO100") == 0)
            params.set(KEY_ISO_MODE, "100");
        else if(strcmp(isoMode, "ISO200") == 0)
            params.set(KEY_ISO_MODE, "200");
        else if(strcmp(isoMode, "ISO400") == 0)
            params.set(KEY_ISO_MODE, "400");
        else if(strcmp(isoMode, "ISO800") == 0)
            params.set(KEY_ISO_MODE, "800");
    }

    int previewWidth = 0;
    int previewHeight = 0;
    params.getPreviewSize(&previewWidth, &previewHeight);
    if (id == 0 && previewWidth == 1360 && previewHeight == 768) {
        ALOGV("preview size alias: 1360x768 -> 1280x720");
        params.setPreviewSize(1280, 720);
    }

    if (id == 0 && params.get("recording-hint") &&
            !strcmp(params.get("recording-hint"), "true")) {
        ALOGV("recording parameters: video=%s preview=%s power=%s",
                params.get("video-size"), params.get("preview-size"),
                params.get("power-mode"));
    }

    android::String8 strParams = params.flatten();
    char *ret = strdup(strParams.string());

    ALOGV("%s: fixed parameters:", __FUNCTION__);
    return ret;
}

/*******************************************************************
 * implementation of camera_device_ops functions
 *******************************************************************/

static wrapper_preview_stream_ops_t *get_wrapper_window(
        struct preview_stream_ops *window)
{
    return reinterpret_cast<wrapper_preview_stream_ops_t *>(window);
}

static int preview_dequeue_buffer(struct preview_stream_ops *window,
        buffer_handle_t **buffer, int *stride)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->dequeue_buffer(wrapper->vendor, buffer, stride);
}

static int preview_enqueue_buffer(struct preview_stream_ops *window,
        buffer_handle_t *buffer)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->enqueue_buffer(wrapper->vendor, buffer);
}

static int preview_cancel_buffer(struct preview_stream_ops *window,
        buffer_handle_t *buffer)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->cancel_buffer(wrapper->vendor, buffer);
}

static int preview_set_buffer_count(struct preview_stream_ops *window,
        int count)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    ALOGV("preview buffer count: %d", count);
    return wrapper->vendor->set_buffer_count(wrapper->vendor, count);
}

static int preview_set_buffers_geometry(struct preview_stream_ops *window,
        int width, int height, int format)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->set_buffers_geometry(wrapper->vendor, width,
            height, format);
}

static int preview_set_crop(struct preview_stream_ops *window, int left,
        int top, int right, int bottom)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->set_crop(wrapper->vendor, left, top, right,
            bottom);
}

static int preview_set_usage(struct preview_stream_ops *window, int usage)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    int fixed_usage = usage;

    if ((usage & GRALLOC_USAGE_PRIVATE_IOMMU_HEAP) &&
            (usage & GRALLOC_USAGE_PRIVATE_ADSP_HEAP)) {
        fixed_usage &= ~GRALLOC_USAGE_PRIVATE_ADSP_HEAP;
        ALOGV("preview heap usage fixed: 0x%08x -> 0x%08x", usage,
                fixed_usage);
    }

    return wrapper->vendor->set_usage(wrapper->vendor, fixed_usage);
}

static int preview_set_swap_interval(struct preview_stream_ops *window,
        int interval)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->set_swap_interval(wrapper->vendor, interval);
}

static int preview_get_min_undequeued_buffer_count(
        const struct preview_stream_ops *window, int *count)
{
    const wrapper_preview_stream_ops_t *wrapper =
            reinterpret_cast<const wrapper_preview_stream_ops_t *>(window);
    return wrapper->vendor->get_min_undequeued_buffer_count(wrapper->vendor,
            count);
}

static int preview_lock_buffer(struct preview_stream_ops *window,
        buffer_handle_t *buffer)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->lock_buffer(wrapper->vendor, buffer);
}

static int preview_set_timestamp(struct preview_stream_ops *window,
        int64_t timestamp)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    return wrapper->vendor->set_timestamp(wrapper->vendor, timestamp);
}

static int camera_set_preview_window(struct camera_device *device,
        struct preview_stream_ops *window)
{
    ALOGV("%s device=%08X vendor=%08X window=%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor),
            (uintptr_t)window);

    if (!device)
        return -EINVAL;

    if (!window) {
        wrapper_camera_device_t *wrapper_dev =
                reinterpret_cast<wrapper_camera_device_t *>(device);
        wrapper_dev->preview_window.vendor = NULL;
        return VENDOR_CALL(device, set_preview_window, window);
    }

    wrapper_camera_device_t *wrapper_dev =
            reinterpret_cast<wrapper_camera_device_t *>(device);
    wrapper_preview_stream_ops_t *wrapper = &wrapper_dev->preview_window;

    memset(wrapper, 0, sizeof(*wrapper));
    wrapper->vendor = window;
    wrapper->base.dequeue_buffer = preview_dequeue_buffer;
    wrapper->base.enqueue_buffer = preview_enqueue_buffer;
    wrapper->base.cancel_buffer = preview_cancel_buffer;
    wrapper->base.set_buffer_count = preview_set_buffer_count;
    wrapper->base.set_buffers_geometry = preview_set_buffers_geometry;
    wrapper->base.set_crop = preview_set_crop;
    wrapper->base.set_usage = preview_set_usage;
    wrapper->base.set_swap_interval = preview_set_swap_interval;
    wrapper->base.get_min_undequeued_buffer_count =
            preview_get_min_undequeued_buffer_count;
    wrapper->base.lock_buffer = preview_lock_buffer;
    wrapper->base.set_timestamp = preview_set_timestamp;

    int rc = VENDOR_CALL(device, set_preview_window, &wrapper->base);
    ALOGV("%s vendor returned %d", __FUNCTION__, rc);
    return rc;
}

static void wrapper_notify_callback(int32_t msg_type, int32_t ext1,
        int32_t ext2, void *user)
{
    wrapper_camera_device_t *wrapper =
            reinterpret_cast<wrapper_camera_device_t *>(user);
    if (wrapper && wrapper->notify_cb)
        wrapper->notify_cb(msg_type, ext1, ext2, wrapper->callback_user);
}

static void wrapper_data_callback(int32_t msg_type,
        const camera_memory_t *data, unsigned int index,
        camera_frame_metadata_t *metadata, void *user)
{
    wrapper_camera_device_t *wrapper =
            reinterpret_cast<wrapper_camera_device_t *>(user);
    camera_memory_t *metadata_memory = NULL;

    /*
     * The JB Qualcomm HAL sends CAMERA_MSG_PREVIEW_METADATA with a NULL data
     * buffer.  Nougat's CameraHardwareInterface unconditionally dereferences
     * data->handle before forwarding every camera1 data callback.  Supply a
     * one-byte framework-owned buffer for this metadata-only notification.
     */
    if (!data && wrapper && wrapper->get_memory &&
            (msg_type & CAMERA_MSG_PREVIEW_METADATA)) {
        metadata_memory = wrapper->get_memory(-1, 1, 1,
                wrapper->callback_user);
        if (!metadata_memory || !metadata_memory->handle) {
            if (metadata_memory && metadata_memory->release)
                metadata_memory->release(metadata_memory);
            ALOGE("cannot allocate preview metadata callback buffer");
            return;
        }
        data = metadata_memory;
        index = 0;
    }
    if (wrapper && wrapper->data_cb)
        wrapper->data_cb(msg_type, data, index, metadata,
                wrapper->callback_user);
    if (metadata_memory && metadata_memory->release)
        metadata_memory->release(metadata_memory);
}

static void wrapper_data_timestamp_callback(int64_t timestamp,
        int32_t msg_type, const camera_memory_t *data, unsigned int index,
        void *user)
{
    wrapper_camera_device_t *wrapper =
            reinterpret_cast<wrapper_camera_device_t *>(user);
    if (wrapper && wrapper->data_cb_timestamp)
        wrapper->data_cb_timestamp(timestamp, msg_type, data, index,
                wrapper->callback_user);
}

static camera_memory_t *wrapper_request_memory(int fd, size_t buf_size,
        unsigned int num_bufs, void *user)
{
    /*
     * This legacy Qualcomm HAL passes QCameraStream* here instead of the
     * callback cookie registered through set_callbacks().  Nougat normally
     * ignores this argument, but the wrapper needs its own saved state.
     */
    (void)user;
    android::Mutex::Autolock lock(gMemoryCallbackLock);
    wrapper_camera_device_t *wrapper = gMemoryCallbackDevice;
    if (!wrapper || !wrapper->get_memory) {
        ALOGE("memory request without framework callback: fd=%d size=%u count=%u",
                fd, static_cast<unsigned int>(buf_size), num_bufs);
        return NULL;
    }

    camera_memory_t *memory = wrapper->get_memory(fd, buf_size, num_bufs,
            wrapper->callback_user);
    return CameraCallbacks::wrapMemory(memory, buf_size, num_bufs);
}

static void camera_set_callbacks(struct camera_device *device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    wrapper_camera_device_t *wrapper =
            reinterpret_cast<wrapper_camera_device_t *>(device);
    wrapper->notify_cb = notify_cb;
    wrapper->data_cb = data_cb;
    wrapper->data_cb_timestamp = data_cb_timestamp;
    wrapper->get_memory = get_memory;
    wrapper->callback_user = user;
    {
        android::Mutex::Autolock lock(gMemoryCallbackLock);
        gMemoryCallbackDevice = wrapper;
    }

    wrapper->callbacks->set(wrapper_notify_callback, wrapper_data_callback,
            wrapper_data_timestamp_callback, wrapper, get_memory, user);
    // Keep Nougat's memory bridge: the vendor supplies a stream pointer
    // instead of the callback cookie to request_memory.
    VENDOR_CALL(device, set_callbacks, CameraCallbacks::notify,
            CameraCallbacks::data, CameraCallbacks::timestamp,
            wrapper_request_memory, wrapper->callbacks);
}

static void camera_enable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    ((wrapper_camera_device_t *)device)->callbacks->enable(msg_type);
    VENDOR_CALL(device, enable_msg_type, msg_type);
}

static void camera_disable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    ((wrapper_camera_device_t *)device)->callbacks->disable(msg_type);
    VENDOR_CALL(device, disable_msg_type, msg_type);
}

static int camera_msg_type_enabled(struct camera_device *device,
        int32_t msg_type)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return 0;

    return VENDOR_CALL(device, msg_type_enabled, msg_type);
}

static int camera_start_preview(struct camera_device *device)
{
    ALOGV("%s device=%08X vendor=%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    /*
     * Camera2 selects the 1080p video size only after it has already started
     * a 720p preview.  Reallocating the legacy msm8660 recording stream at
     * that point requires preview STREAMOFF, which this vendor HAL can
     * deadlock in while its frame-processing thread owns a buffer.  Seed the
     * rear camera's recording size before the first preview STREAMON so the
     * vendor allocates 1280x720 preview and 1920x1088 video buffers together.
     */
    if (CAMERA_ID(device) == 0) {
        char *settings = VENDOR_CALL(device, get_parameters);
        if (settings) {
            android::CameraParameters prepared;
            prepared.unflatten(android::String8(settings));
            const char *videoSize = prepared.get("video-size");
            if (!videoSize || strcmp(videoSize, "1920x1088")) {
                prepared.set("video-size", "1920x1088");
                android::String8 flattened = prepared.flatten();
                int setResult = VENDOR_CALL(device, set_parameters,
                        flattened.string());
                ALOGV("prepared 1920x1088 video buffers before preview: %d",
                        setResult);
            }
            VENDOR_CALL(device, put_parameters, settings);
        }
    }

    CameraCallbacks *callbacks = ((wrapper_camera_device_t *)device)->callbacks;
    callbacks->preview(true);
    int rc = VENDOR_CALL(device, start_preview);
    if (rc) callbacks->preview(false);
    ALOGV("%s vendor returned %d", __FUNCTION__, rc);
    return rc;
}

static void camera_stop_preview(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    ((wrapper_camera_device_t *)device)->callbacks->preview(false);
    VENDOR_CALL(device, stop_preview);
}

static int camera_preview_enabled(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, preview_enabled);
}

static int camera_store_meta_data_in_buffers(struct camera_device *device,
        int enable)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, store_meta_data_in_buffers, enable);
}

static int camera_start_recording(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return EINVAL;

    return VENDOR_CALL(device, start_recording);
}

static void camera_stop_recording(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    VENDOR_CALL(device, stop_recording);

}

static int camera_recording_enabled(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, recording_enabled);
}

static void camera_release_recording_frame(struct camera_device *device,
                const void *opaque)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    VENDOR_CALL(device, release_recording_frame, opaque);
}

static int camera_auto_focus(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;


    return VENDOR_CALL(device, auto_focus);
}

static int camera_cancel_auto_focus(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, cancel_auto_focus);
}

static int camera_take_picture(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    /*
     * Camera2's legacy adapter may leave face detection enabled while taking
     * a still image.  This JB HAL then waits forever in preview STREAMOFF
     * while its frame-processing thread is still consuming preview frames.
     */
    ((wrapper_camera_device_t *)device)->callbacks->preview(false);
    ALOGV("stopping legacy face detection before still capture");
    VENDOR_CALL(device, send_command, CAMERA_CMD_STOP_FACE_DETECTION, 0, 0);
    return VENDOR_CALL(device, take_picture);
}

static int camera_cancel_picture(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X",__FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, cancel_picture);
}

static int camera_set_parameters(struct camera_device *device,
        const char *params)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    char *tmp = NULL;
    android::CameraParameters incoming;
    incoming.unflatten(android::String8(params));
    if (!incoming.get("video-size")) {
        char *current = VENDOR_CALL(device, get_parameters);
        if (current) {
            android::CameraParameters previous;
            previous.unflatten(android::String8(current));
            const char *videoSize = previous.get("video-size");
            if (videoSize && videoSize[0])
                incoming.set("video-size", videoSize);
            VENDOR_CALL(device, put_parameters, current);
        }
    }
    tmp = camera_fixup_setparams(CAMERA_ID(device), incoming.flatten().string());

    if (!tmp)
        return -EINVAL;

#ifdef LOG_PARAMETERS
    __android_log_write(ANDROID_LOG_VERBOSE, LOG_TAG, tmp);
#endif

    android::CameraParameters next;
    next.unflatten(android::String8(tmp));

    bool restartPreview = false;
    char *oldSettings = VENDOR_CALL(device, get_parameters);
    if (oldSettings) {
        android::CameraParameters old;
        old.unflatten(android::String8(oldSettings));
        const char *newVideo = next.get("video-size");
        const char *oldVideo = old.get("video-size");
        restartPreview = newVideo && newVideo[0] &&
                (!oldVideo || strcmp(newVideo, oldVideo)) &&
                VENDOR_CALL(device, preview_enabled) &&
                !VENDOR_CALL(device, recording_enabled);
        VENDOR_CALL(device, put_parameters, oldSettings);
    }
    if (restartPreview) {
        ALOGV("reconfiguring preview for video size %s", next.get("video-size"));
        /*
         * The legacy frame-processing thread may still own a preview buffer
         * while CAF/face detection is active.  STREAMOFF then waits forever,
         * especially when recording is started shortly after opening the
         * camera.  Quiesce those users before switching to the larger video
         * buffer layout, as is already required for still capture.
         */
        VENDOR_CALL(device, send_command,
                CAMERA_CMD_STOP_FACE_DETECTION, 0, 0);
        VENDOR_CALL(device, cancel_auto_focus);
        ((wrapper_camera_device_t *)device)->callbacks->preview(false);
        VENDOR_CALL(device, stop_preview);
    }
    int ret = VENDOR_CALL(device, set_parameters, tmp);
    if (restartPreview) {
        CameraCallbacks *callbacks = ((wrapper_camera_device_t *)device)->callbacks;
        callbacks->preview(true);
        int previewResult = VENDOR_CALL(device, start_preview);
        if (previewResult) callbacks->preview(false);
        if (!ret)
            ret = previewResult;
    }

    free(tmp);

    return ret;
}

static char *camera_get_parameters(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return NULL;

    char *params = VENDOR_CALL(device, get_parameters);

    if (!params) {
        ALOGE("vendor camera returned null parameters");
        return NULL;
    }

#ifdef LOG_PARAMETERS
    __android_log_write(ANDROID_LOG_VERBOSE, LOG_TAG, params);
#endif

    char *tmp = camera_fixup_getparams(CAMERA_ID(device), params);
    VENDOR_CALL(device, put_parameters, params);
    params = tmp;

    if (!params)
        return NULL;

#ifdef LOG_PARAMETERS
    __android_log_write(ANDROID_LOG_VERBOSE, LOG_TAG, params);
#endif

    return params;
}

static void camera_put_parameters(struct camera_device *device, char *params)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (params)
        free(params);
}

static int camera_send_command(struct camera_device *device,
            int32_t cmd, int32_t arg1, int32_t arg2)
{
    ALOGV("%s cmd=%d arg1=%d arg2=%d", __FUNCTION__, cmd, arg1, arg2);

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, send_command, cmd, arg1, arg2);
}

static void camera_release(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    ((wrapper_camera_device_t *)device)->callbacks->preview(false);
    VENDOR_CALL(device, release);
}

static int camera_dump(struct camera_device *device, int fd)
{
    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, dump, fd);
}

extern "C" void heaptracker_free_leaked_memory(void);

static int camera_device_close(hw_device_t *device)
{
    int ret = 0;
    wrapper_camera_device_t *wrapper_dev = NULL;

    ALOGV("%s", __FUNCTION__);

    android::Mutex::Autolock lock(gCameraWrapperLock);

    if (!device) {
        ret = -EINVAL;
        goto done;
    }

    wrapper_dev = (wrapper_camera_device_t*) device;
    wrapper_dev->callbacks->preview(false);

    {
        android::Mutex::Autolock callbackLock(gMemoryCallbackLock);
        if (gMemoryCallbackDevice == wrapper_dev)
            gMemoryCallbackDevice = NULL;
    }

    if (wrapper_dev->vendor)
        wrapper_dev->vendor->common.close((hw_device_t*)wrapper_dev->vendor);
    delete wrapper_dev->callbacks;
    if (wrapper_dev->base.ops)
        free(wrapper_dev->base.ops);
    free(wrapper_dev);
done:
#ifdef HEAPTRACKER
    heaptracker_free_leaked_memory();
#endif
    return ret;
}

/*******************************************************************
 * implementation of camera_module functions
 *******************************************************************/

/* open device handle to one of the cameras
 *
 * assume camera service will keep singleton of each camera
 * so this function will always only be called once per camera instance
 */

static int camera_device_open(const hw_module_t *module, const char *name,
                hw_device_t **device)
{
    int rv = 0;
    int num_cameras = 0;
    int cameraid;
    wrapper_camera_device_t *camera_device = NULL;
    camera_device_ops_t *camera_ops = NULL;

    android::Mutex::Autolock lock(gCameraWrapperLock);

    ALOGV("camera_device open");

    if (!name || !device)
        return -EINVAL;

    *device = NULL;

    if (name != NULL) {
        if (check_vendor_module())
            return -EINVAL;

        char *end = NULL;
        errno = 0;
        long parsed_id = strtol(name, &end, 10);
        if (errno || end == name || *end != '\0' || parsed_id < 0 ||
                parsed_id > INT_MAX) {
            ALOGE("invalid camera id: %s", name);
            return -EINVAL;
        }
        cameraid = static_cast<int>(parsed_id);
        num_cameras = gVendorModule->get_number_of_cameras();

        if (cameraid < 0 || cameraid >= num_cameras) {
            ALOGE("camera service provided cameraid out of bounds, "
                    "cameraid = %d, num supported = %d",
                    cameraid, num_cameras);
            rv = -EINVAL;
            goto fail;
        }

        camera_device = (wrapper_camera_device_t*)malloc(sizeof(*camera_device));
        if (!camera_device) {
            ALOGE("camera_device allocation fail");
            rv = -ENOMEM;
            goto fail;
        }
        memset(camera_device, 0, sizeof(*camera_device));
        camera_device->id = cameraid;
        camera_device->callbacks = new CameraCallbacks();
        if (!camera_device->callbacks) {
            rv = -ENOMEM;
            goto fail;
        }
        rv = camera_device->callbacks->start();
        if (rv) goto fail;

        rv = gVendorModule->common.methods->open(
                    (const hw_module_t*)gVendorModule, name,
                    (hw_device_t**)&(camera_device->vendor));
        if (rv) {
            ALOGE("vendor camera open fail");
            goto fail;
        }

        // Reserve VIDC only for actual codec instances so
        // Camera2 can keep this camera open while playing a recorded clip.
        ALOGV("%s: got vendor camera device 0x%08X",
                __FUNCTION__, (uintptr_t)(camera_device->vendor));

        camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
        if (!camera_ops) {
            ALOGE("camera_ops allocation fail");
            rv = -ENOMEM;
            goto fail;
        }

        memset(camera_ops, 0, sizeof(*camera_ops));

        camera_device->base.common.tag = HARDWARE_DEVICE_TAG;
        camera_device->base.common.version = CAMERA_DEVICE_API_VERSION_1_0;
        camera_device->base.common.module = (hw_module_t *)(module);
        camera_device->base.common.close = camera_device_close;
        camera_device->base.ops = camera_ops;

        camera_ops->set_preview_window = camera_set_preview_window;
        camera_ops->set_callbacks = camera_set_callbacks;
        camera_ops->enable_msg_type = camera_enable_msg_type;
        camera_ops->disable_msg_type = camera_disable_msg_type;
        camera_ops->msg_type_enabled = camera_msg_type_enabled;
        camera_ops->start_preview = camera_start_preview;
        camera_ops->stop_preview = camera_stop_preview;
        camera_ops->preview_enabled = camera_preview_enabled;
        camera_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        camera_ops->start_recording = camera_start_recording;
        camera_ops->stop_recording = camera_stop_recording;
        camera_ops->recording_enabled = camera_recording_enabled;
        camera_ops->release_recording_frame = camera_release_recording_frame;
        camera_ops->auto_focus = camera_auto_focus;
        camera_ops->cancel_auto_focus = camera_cancel_auto_focus;
        camera_ops->take_picture = camera_take_picture;
        camera_ops->cancel_picture = camera_cancel_picture;
        camera_ops->set_parameters = camera_set_parameters;
        camera_ops->get_parameters = camera_get_parameters;
        camera_ops->put_parameters = camera_put_parameters;
        camera_ops->send_command = camera_send_command;
        camera_ops->release = camera_release;
        camera_ops->dump = camera_dump;

        *device = &camera_device->base.common;
    }

    return rv;

fail:
    if (camera_device) {
        if (camera_device->vendor)
            camera_device->vendor->common.close(
                    reinterpret_cast<hw_device_t *>(camera_device->vendor));
        delete camera_device->callbacks;
        free(camera_device);
        camera_device = NULL;
    }
    if (camera_ops) {
        free(camera_ops);
        camera_ops = NULL;
    }
    *device = NULL;
    return rv;
}

static int camera_get_number_of_cameras(void)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;
    return gVendorModule->get_number_of_cameras();
}

static int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    ALOGV("%s", __FUNCTION__);
    if (!info)
        return -EINVAL;
    if (check_vendor_module())
        return -ENODEV;
    return gVendorModule->get_camera_info(camera_id, info);
}
