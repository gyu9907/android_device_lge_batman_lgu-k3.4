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

include $(CLEAR_VARS)
LOCAL_MODULE := libshim_thumbnail
LOCAL_INIT_RC := ../prebuilt/configs/batman-thumbnail.rc
LOCAL_SRC_FILES := ThumbnailRetrieverShim.cpp ThumbnailOmxCompat.cpp
LOCAL_C_INCLUDES := $(TOP) frameworks/av/media/libstagefright frameworks/av/include/media frameworks/av/media/libavextensions frameworks/native/include/media/openmax
LOCAL_SHARED_LIBRARIES := libstagefright libstagefright_foundation libmedia libmediautils libutils libbinder liblog libgui libcutils libicuuc libicui18n libdl
LOCAL_CFLAGS := -std=c++11
LOCAL_C_INCLUDES += frameworks/av/media/libstagefright/mpeg2ts
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := batman_thumbnail_probe
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := ThumbnailProbe.cpp
LOCAL_C_INCLUDES := $(TOP)
LOCAL_SHARED_LIBRARIES := libbinder libmedia libutils libdl
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := libshim_thumbnail_diagnostic
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := ThumbnailRetrieverShim.cpp ThumbnailOmxCompat.cpp
LOCAL_C_INCLUDES := $(TOP) frameworks/av/media/libstagefright frameworks/av/include/media frameworks/av/media/libavextensions frameworks/native/include/media/openmax frameworks/av/media/libstagefright/mpeg2ts
LOCAL_SHARED_LIBRARIES := libstagefright libstagefright_foundation libmedia libmediautils libutils libbinder liblog libgui libcutils libicuuc libicui18n libdl
LOCAL_CFLAGS := -std=c++11 -DBATMAN_THUMBNAIL_COLOR_DIAGNOSTICS
include $(BUILD_SHARED_LIBRARY)
