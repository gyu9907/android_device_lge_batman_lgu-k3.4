ifneq ($(BUILD_TINY_ANDROID),true)
ifneq ($(BOARD_PROVIDES_LIBGPS_UTILS),true)
#Compile this library only for builds with the latest modem image

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

## Libs
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libutils \
    libcutils

LOCAL_SRC_FILES += \
    loc_log.cpp \
    loc_cfg.cpp \
    msg_q.c \
    linked_list.c

LOCAL_CFLAGS += \
     -fno-short-enums \
     -D_ANDROID_

## Includes
LOCAL_C_INCLUDES:=


LOCAL_MODULE := libgps.utils

LOCAL_MODULE_TAGS := optional

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
include $(BUILD_SHARED_LIBRARY)
endif # BOARD_PROVIDES_LIBGPS_UTILS
endif # not BUILD_TINY_ANDROID
