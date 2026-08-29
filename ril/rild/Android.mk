# Copyright 2006 The Android Open Source Project

ifeq ($(BOARD_PROVIDES_RILD),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    rild.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libril \
    libdl

# Disable usage of Clang Toolchain here
LOCAL_CLANG := false

# temporary hack for broken vendor rils
LOCAL_WHOLE_STATIC_LIBRARIES := \
	librilutils_static

LOCAL_CFLAGS := -DRIL_SHLIB

LOCAL_MODULE := rild
LOCAL_MODULE_TAGS := optional

LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/libril

include $(BUILD_EXECUTABLE)

# For radiooptions binary
# =======================
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    radiooptions.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

# Disable usage of Clang Toolchain here
LOCAL_CLANG := false

LOCAL_CFLAGS :=

LOCAL_MODULE := radiooptions
LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)
endif # BOARD_PROVIDES_RILD
