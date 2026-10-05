LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libEGL_adreno200_compat
LOCAL_MODULE_RELATIVE_PATH := egl
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := EglImageCompat.cpp
LOCAL_SHARED_LIBRARIES := libEGL_adreno200 libdl liblog libnativewindow
# Retain the vendor dependency: dlsym on this driver must find unmodified EGL
# entry points in it even though our overrides resolve their originals.
LOCAL_LDFLAGS := -Wl,--no-as-needed
LOCAL_CFLAGS := -std=c++11 -fvisibility=hidden
# q3dtools opens this absolute legacy path. Both discovery names must resolve
# to the wrapper; the original blob remains outside the driver directory.
LOCAL_POST_INSTALL_CMD := ln -sf libEGL_adreno200_compat.so $(TARGET_OUT_SHARED_LIBRARIES)/egl/libEGL_adreno200.so
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libGLESv2_adreno200_compat
LOCAL_MODULE_RELATIVE_PATH := egl
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := GlesTextureRgCompat.cpp
LOCAL_SHARED_LIBRARIES := libGLESv2_adreno200 libdl liblog libEGL_adreno200_compat
LOCAL_LDFLAGS := -Wl,--no-as-needed
LOCAL_CFLAGS := -std=c++11 -fvisibility=hidden
LOCAL_POST_INSTALL_CMD := ln -sf libGLESv2_adreno200_compat.so $(TARGET_OUT_SHARED_LIBRARIES)/egl/libGLESv2_adreno200.so
include $(BUILD_SHARED_LIBRARY)

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
LOCAL_SRC_FILES := ThumbnailRetrieverShim.cpp ThumbnailOmxCompat.cpp
LOCAL_C_INCLUDES := $(TOP) frameworks/av/media/libstagefright frameworks/av/include/media frameworks/av/media/libavextensions frameworks/native/include/media/openmax
LOCAL_SHARED_LIBRARIES := libstagefright libstagefright_foundation libmedia libmediautils libutils libbinder liblog libgui libcutils libicuuc libicui18n libdl android.hardware.cas.native@1.0
LOCAL_CFLAGS := -std=c++11
LOCAL_C_INCLUDES += frameworks/av/media/libstagefright/mpeg2ts
include $(BUILD_SHARED_LIBRARY)
