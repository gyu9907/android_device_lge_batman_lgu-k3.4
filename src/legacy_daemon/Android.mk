LOCAL_PATH := $(call my-dir)

# The entrypoint must have an exec_type label, whereas Android 7 permits
# legacy text relocations only on system_file. Keep the vendor payloads
# separate and enter their service domains through these PIE wrappers.
define batman-legacy-daemon
include $$(CLEAR_VARS)
LOCAL_MODULE := batman_legacy_$(1)
LOCAL_MODULE_STEM := $(1)
LOCAL_SRC_FILES := legacy_daemon.c
LOCAL_CFLAGS := -Wall -Werror -DLEGACY_DAEMON=\"$(1)\"
LOCAL_MODULE_TAGS := optional
include $$(BUILD_EXECUTABLE)
endef

$(foreach daemon,cnd ks mpdecision netmgrd qcks qmiproxy qmuxd qseecomd rmt_storage thermald time_daemon bridgemgrd port-bridge mm-qcamera-daemon,$(eval $(call batman-legacy-daemon,$(daemon))))
