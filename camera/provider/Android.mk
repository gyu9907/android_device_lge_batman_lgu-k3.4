LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.camera.provider@2.4-impl.batman
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := CameraProvider.cpp CameraDevice.cpp
LOCAL_CPPFLAGS := -std=c++17
LOCAL_C_INCLUDES := frameworks/native/include/media/openmax
LOCAL_SHARED_LIBRARIES := \
    libhidlbase \
    libhidlmemory \
    libutils \
    libcutils \
    liblog \
    libhardware \
    libcamera_metadata \
    android.hardware.camera.device@1.0 \
    android.hardware.camera.device@3.2 \
    android.hardware.camera.device@3.3 \
    camera.device@3.2-impl \
    camera.device@3.3-impl \
    android.hardware.camera.provider@2.4 \
    android.hardware.camera.common@1.0 \
    android.hardware.graphics.allocator@2.0 \
    android.hardware.graphics.mapper@2.0 \
    android.hardware.graphics.mapper@3.0 \
    android.hardware.graphics.mapper@4.0 \
    libgralloctypes \
    libexif \
    android.hardware.graphics.common@1.0 \
    android.hidl.allocator@1.0 \
    android.hidl.memory@1.0
LOCAL_STATIC_LIBRARIES := android.hardware.camera.common@1.0-helper
LOCAL_HEADER_LIBRARIES := media_plugin_headers
include $(BUILD_SHARED_LIBRARY)
