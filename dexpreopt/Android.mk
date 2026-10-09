LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),batman_lgu)
batman_boot_profile := $(PRODUCT_OUT)/obj/ETC/batman_boot_image_profile_intermediates/boot-image-profile.txt
$(batman_boot_profile): PRIVATE_PLATFORM_PROFILE := frameworks/base/config/boot-image-profile.txt
$(batman_boot_profile): PRIVATE_DEVICE_PROFILE := $(LOCAL_PATH)/boot-image-profile-extra.txt
$(batman_boot_profile): frameworks/base/config/boot-image-profile.txt $(LOCAL_PATH)/boot-image-profile-extra.txt
	@mkdir -p $(dir $@)
	$(hide) cat $(PRIVATE_PLATFORM_PROFILE) $(PRIVATE_DEVICE_PROFILE) > $@.tmp
	$(hide) mv $@.tmp $@
batman_boot_profile :=
endif
