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
LOCAL_CFLAGS := -std=c++17 -fvisibility=hidden
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
LOCAL_CFLAGS := -std=c++17 -fvisibility=hidden
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
LOCAL_C_INCLUDES := $(TOP) frameworks/av/media/libstagefright frameworks/av/include/media frameworks/native/include/media/openmax
LOCAL_SHARED_LIBRARIES := libstagefright libstagefright_foundation libmedia libmediautils libutils libbinder liblog libgui libcutils libandroidicu libdl android.hardware.cas.native@1.0
LOCAL_CFLAGS := -std=c++17
LOCAL_C_INCLUDES += frameworks/av/media/libstagefright/mpeg2ts \
    frameworks/av/media/libstagefright/include \
    frameworks/av/media/libmediaplayerservice
LOCAL_STATIC_LIBRARIES := libplayerservice_datasource libstagefright_color_conversion libyuv_static
LOCAL_SHARED_LIBRARIES += libmediametrics libdatasource libdrmframework libmediadrm libui libmedia_codeclist libmedia_omx libstagefright_framecapture_utils libnativewindow
LOCAL_HEADER_LIBRARIES := media_plugin_headers libstagefright_headers libmediadrm_headers
include $(BUILD_SHARED_LIBRARY)

# Restore the ARM float/double-to-int64 helpers required by legacy blobs.
# Reuse compiler-rt's reference implementation; do not alter the vendor binary.
include $(CLEAR_VARS)
LOCAL_MODULE := libshim_arm_compat
LOCAL_SRC_FILES := ../../../../external/compiler-rt/lib/builtins/fixsfdi.c \
    ../../../../external/compiler-rt/lib/builtins/fixunssfdi.c \
    ../../../../external/compiler-rt/lib/builtins/fixdfdi.c \
    ../../../../external/compiler-rt/lib/builtins/fixunsdfdi.c
LOCAL_CXX_STL := none
include $(BUILD_SHARED_LIBRARY)
