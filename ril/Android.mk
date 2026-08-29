RIL_PATH := $(call my-dir)

ifeq ($(BOARD_VENDOR),lge)
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
include $(call first-makefiles-under,$(RIL_PATH))
endif
endif
