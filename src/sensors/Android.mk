LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := sensors.batman_lgu_kr
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := sensors_wrapper.cpp
LOCAL_SHARED_LIBRARIES := libdl liblog
LOCAL_CFLAGS := -Wall -Werror

include $(BUILD_SHARED_LIBRARY)

# The recursive device scan stops at this Android.mk.
include $(LOCAL_PATH)/service/Android.mk
