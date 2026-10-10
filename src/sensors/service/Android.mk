LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.sensors@1.0-service.batman
# Use the module name as its executable name to avoid a duplicate platform output.
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
# Reuse the platform implementation, including its 32-bit hwbinder setup.
LOCAL_SRC_FILES := ../../../../../../hardware/interfaces/sensors/1.0/default/service.cpp
LOCAL_INIT_RC := sensors-service.batman.rc
LOCAL_CFLAGS := -DARCH_ARM_32 -Wall -Werror
LOCAL_SHARED_LIBRARIES := liblog libcutils libdl libbase libutils \
    libhidlbase android.hardware.sensors@1.0
include $(BUILD_EXECUTABLE)
