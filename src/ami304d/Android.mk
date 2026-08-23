#ifeq ($(BOARD_SENSOR_CHIP),ami304)
ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT)/bin
LOCAL_SRC_FILES:= Middletester.c
LOCAL_LDFLAGS := $(LOCAL_PATH)/libami304middleware.a
LOCAL_SHARED_LIBRARIES := libcutils libutils liblog
LOCAL_MODULE := ami304d
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

endif	# TARGET_SIMULATOR != true
#endif	# build for only thunder*
