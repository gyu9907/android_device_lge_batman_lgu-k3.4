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

#define LOG_PARAMETERS

#define LOG_TAG "CameraWrapper"
#include <cutils/log.h>

#include <utils/threads.h>
#include <utils/String8.h>
#include <hardware/hardware.h>
#include <hardware/camera.h>
#include <camera/Camera.h>
#include <camera/CameraParameters.h>

static android::Mutex gCameraWrapperLock;
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
    int width;
    int height;
    int format;
    int usage;
    unsigned int dequeue_count;
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
} wrapper_camera_device_t;

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
    if (rv)
        ALOGE("failed to open vendor camera module");
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

static char *camera_fixup_setparams(int id, const char *settings, struct camera_device *device)
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
        ALOGI("preview size alias: 1360x768 -> 1280x720");
        params.setPreviewSize(1280, 720);
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
    int rc = wrapper->vendor->dequeue_buffer(wrapper->vendor, buffer, stride);
    if (rc || wrapper->dequeue_count < 8) {
        ALOGI("preview dequeue[%u]: rc=%d buffer=%p stride=%d geometry=%dx%d format=0x%x usage=0x%08x",
                wrapper->dequeue_count, rc, buffer ? *buffer : 0,
                stride ? *stride : -1, wrapper->width, wrapper->height,
                wrapper->format, wrapper->usage);
    }
    wrapper->dequeue_count++;
    return rc;
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
    ALOGI("preview buffer count: %d", count);
    return wrapper->vendor->set_buffer_count(wrapper->vendor, count);
}

static int preview_set_buffers_geometry(struct preview_stream_ops *window,
        int width, int height, int format)
{
    wrapper_preview_stream_ops_t *wrapper = get_wrapper_window(window);
    wrapper->width = width;
    wrapper->height = height;
    wrapper->format = format;
    wrapper->dequeue_count = 0;
    ALOGI("preview geometry: %dx%d format=0x%x", width, height, format);
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
        ALOGI("preview heap usage fixed: 0x%08x -> 0x%08x", usage,
                fixed_usage);
    }

    wrapper->usage = fixed_usage;
    ALOGI("preview usage: requested=0x%08x applied=0x%08x", usage,
            fixed_usage);

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
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    if (!window)
        return VENDOR_CALL(device, set_preview_window, window);

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

    return VENDOR_CALL(device, set_preview_window, &wrapper->base);
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

    VENDOR_CALL(device, set_callbacks, notify_cb, data_cb, data_cb_timestamp,
            get_memory, user);
}

static void camera_enable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

    VENDOR_CALL(device, enable_msg_type, msg_type);
}

static void camera_disable_msg_type(struct camera_device *device,
        int32_t msg_type)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

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
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return -EINVAL;

    return VENDOR_CALL(device, start_preview);
}

static void camera_stop_preview(struct camera_device *device)
{
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

    if (!device)
        return;

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
    tmp = camera_fixup_setparams(CAMERA_ID(device), params, device);

    if (!tmp)
        return -EINVAL;

#ifdef LOG_PARAMETERS
    __android_log_write(ANDROID_LOG_VERBOSE, LOG_TAG, tmp);
#endif

    int ret = VENDOR_CALL(device, set_parameters, tmp);

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
    ALOGV("%s->%08X->%08X", __FUNCTION__, (uintptr_t)device,
            (uintptr_t)(((wrapper_camera_device_t*)device)->vendor));

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

    wrapper_dev->vendor->common.close((hw_device_t*)wrapper_dev->vendor);
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

    if (name != NULL) {
        if (check_vendor_module())
            return -EINVAL;

        cameraid = atoi(name);
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

        rv = gVendorModule->common.methods->open(
                    (const hw_module_t*)gVendorModule, name,
                    (hw_device_t**)&(camera_device->vendor));
        if (rv) {
            ALOGE("vendor camera open fail");
            goto fail;
        }
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
        camera_device->base.common.version = 0;
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
    if (check_vendor_module())
        return 0;
    return gVendorModule->get_camera_info(camera_id, info);
}
