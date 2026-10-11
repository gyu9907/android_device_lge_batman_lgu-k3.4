LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.power@1.0-impl.batman
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := Power.cpp
LOCAL_CPPFLAGS := -std=c++17 -Wall -Werror
LOCAL_SHARED_LIBRARIES := liblog libhardware libhidlbase libutils android.hardware.power@1.0
include $(BUILD_SHARED_LIBRARY)
