LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := nfc.msm8660
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := nfc_batman_lgu.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
