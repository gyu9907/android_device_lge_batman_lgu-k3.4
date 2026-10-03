LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
# The shipped AMI306 payload requires text relocations; enter sensors through
# a clean executable before running the preserved vendor binary.
LOCAL_MODULE := ami304d
LOCAL_SRC_FILES := ../legacy_daemon/legacy_daemon.c
LOCAL_CFLAGS := -Wall -Werror -DLEGACY_DAEMON=\"ami304d\"
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
