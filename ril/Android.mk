LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_PROVIDES_LIBRIL),true)
include $(call all-makefiles-under,$(LOCAL_PATH))
endif
