LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    CameraCompatShim.cpp

LOCAL_SHARED_LIBRARIES := \
    libbinder

LOCAL_MODULE := libshim_camera
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    OmxVencCompatShim.cpp

LOCAL_SHARED_LIBRARIES := \
    libdl \
    liblog

LOCAL_MODULE := libshim_omx_venc
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := OmxVdecCompatShim.cpp
LOCAL_SHARED_LIBRARIES := libdl liblog
LOCAL_MODULE := libshim_omx_vdec
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
