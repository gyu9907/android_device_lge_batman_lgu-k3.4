# Copyright 2006 The Android Open Source Project

ifeq ($(BOARD_PROVIDES_LIBREFERENCE_RIL),true)

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    reference-ril.c \
    atchannel.c \
    misc.c \
    at_tok.c

LOCAL_SHARED_LIBRARIES := \
    liblog libcutils libutils libril librilutils

# Disable usage of Clang Toolchain here
LOCAL_CLANG := false

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE

ifeq ($(TARGET_RIL_VARIANT_LEGACY),true)
    LOCAL_CFLAGS += -DRIL_VARIANT_LEGACY
endif

LOCAL_C_INCLUDES :=

ifeq ($(TARGET_DEVICE),sooner)
    LOCAL_CFLAGS += -DUSE_TI_COMMANDS
endif

ifeq ($(TARGET_DEVICE),surf)
    LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

ifeq ($(TARGET_DEVICE),dream)
    LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

ifeq (foo,foo)
    # build shared library
    LOCAL_CFLAGS += -DRIL_SHLIB

    LOCAL_MODULE := libreference-ril

    include $(BUILD_SHARED_LIBRARY)
else
    # build executable
    LOCAL_MODULE := reference-ril

    include $(BUILD_EXECUTABLE)
endif
endif # BOARD_PROVIDES_LIBREFERENCE_RIL
