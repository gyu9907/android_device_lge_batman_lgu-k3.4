$(call inherit-product-if-exists, vendor/lge/batman_lgu/batman_lgu-vendor.mk)

$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_small.mk)

# The gps config appropriate for this device
#$(call inherit-product, device/common/gps/gps_us_supl.mk)
PRODUCT_COPY_FILES += $(LOCAL_PATH)/prebuilt/configs/gps.conf:system/etc/gps.conf

PRODUCT_TAGS += dalvik.gc.type-precise
$(call inherit-product, frameworks/native/build/phone-xhdpi-1024-dalvik-heap.mk)

DEVICE_PACKAGE_OVERLAYS += device/lge/batman_lgu/overlay

# This device is xhdpi.  However the platform doesn't
# currently contain all of the bitmaps at xhdpi density so
# we do this little trick to fall back to the hdpi version
# if the xhdpi doesn't exist.
PRODUCT_AAPT_CONFIG := normal hdpi xhdpi
PRODUCT_AAPT_PREF_CONFIG := xhdpi
PRODUCT_LOCALES += ko_KR

# override full.mk
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/configs/media_profiles.xml:system/etc/media_profiles.xml \
    $(LOCAL_PATH)/prebuilt/configs/media_codecs.xml:system/etc/media_codecs.xml

#$(call inherit-product, build/target/product/full.mk)
$(if $(wildcard device/lge/batman_lgu/stonecold/product/full.mk), $(call inherit-product-if-exists, device/lge/batman_lgu/stonecold/product/full.mk), $(call inherit-product, build/target/product/full.mk))

# root
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/root/init.batman_lgu_kr.rc:root/init.batman_lgu_kr.rc \
    $(LOCAL_PATH)/prebuilt/root/fstab.batman_lgu_kr:root/fstab.batman_lgu_kr \
    $(LOCAL_PATH)/prebuilt/root/init.batman_lgu.usb.rc:root/init.batman_lgu.usb.rc \
    $(LOCAL_PATH)/prebuilt/root/ueventd.batman_lgu_kr.rc:root/ueventd.batman_lgu_kr.rc \
    $(LOCAL_PATH)/prebuilt/root/init.qcom.class_core.sh:root/init.qcom.class_core.sh \
    $(LOCAL_PATH)/prebuilt/root/init.qcom.class_main.sh:root/init.qcom.class_main.sh \
    $(LOCAL_PATH)/prebuilt/root/init.qcom.sh:root/init.qcom.sh

# Boot Logo
PRODUCT_COPY_FILES += \
    $(call add-to-product-copy-files-if-exists, $(LOCAL_PATH)/prebuilt/root/boot_logo_00000.rle:root/bootimages/boot_logo_00000.rle)

# Scripts
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/scripts/init.qcom.bt.sh:system/etc/init.qcom.bt.sh \
    $(LOCAL_PATH)/prebuilt/scripts/init.qcom.post_boot.sh:system/etc/init.qcom.post_boot.sh

# Configs
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/prebuilt/configs/audio_policy.conf:system/vendor/etc/audio_policy.conf \
    $(LOCAL_PATH)/prebuilt/configs/atcmd_virtual_kbd.kl:system/usr/keylayout/atcmd_virtual_kbd.kl \
    $(LOCAL_PATH)/prebuilt/configs/ats_input.kl:system/usr/keylayout/ats_input.kl \
    $(LOCAL_PATH)/prebuilt/configs/batman_lgu-keypad.kl:system/usr/keylayout/batman_lgu-keypad.kl \
    $(LOCAL_PATH)/prebuilt/configs/pmic8xxx_pwrkey.kl:system/usr/keylayout/pmic8xxx_pwrkey.kl \
    $(LOCAL_PATH)/prebuilt/configs/synaptics_ts.kl:system/usr/keylayout/synaptics_ts.kl \
    $(LOCAL_PATH)/prebuilt/configs/synaptics_ts.idc:system/usr/idc/synaptics_ts.idc

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:system/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:system/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.telephony.cdma.xml:system/etc/permissions/android.hardware.telephony.cdma.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.nfc.xml:system/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/com.android.nfc_extras.xml:system/etc/permissions/com.android.nfc_extras.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:system/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:system/etc/permissions/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml

# Audio
PRODUCT_PACKAGES += \
    audio.a2dp.default \
    audio.r_submix.default \
    audio.usb.default \
    audio.primary.batman_lgu_kr \
    libaudioutils

# Graphics
PRODUCT_PACKAGES += \
	camera.msm8660 \
	libshim_camera \
	libshim_omx_venc \
	libshim_omx_vdec \
	libshim_thumbnail \
	copybit.msm8660 \
	gralloc.msm8660 \
	hwcomposer.msm8660 \
	libexternal \
	libgenlock \
	libmemalloc \
	liboverlay \
	libqdutils \
	libqservice \
	libvirtual \
    libQcomUI \
    libtilerenderer

# OMX
PRODUCT_PACKAGES += \
    libdivxdrmdecrypt \
    libI420colorconvert \
    libmm-omxcore \
    libOmxCore \
    libOmxVdec \
    libOmxVenc \
    libOmxAacEnc \
    libOmxAmrEnc \
    libstagefrighthw

# USB
PRODUCT_PACKAGES += \
    com.android.future.usb.accessory

# Keep ADB available from the first USB property trigger during bring-up.
# These settings are intentionally insecure and must not be used for release builds.
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
    ro.secure=0 \
    ro.adb.secure=0 \
    ro.debuggable=1 \
    security.perf_harden=0 \
    cm.service.adb.root=1 \
    service.adb.root=1 \
    persist.service.adb.enable=1 \
    persist.sys.usb.config=mtp,adb

# Filesystem Management Tools
PRODUCT_PACKAGES += \
    make_ext4fs \
    setup_fs

# Bluetooth
PRODUCT_PACKAGES += \
    brcm_patchram_plus \
    hcitool \
    hciconfig

# GPS
PRODUCT_PACKAGES += \
    gps.msm8660

# NFC
PRODUCT_COPY_FILES += \
    $(call add-to-product-copy-files-if-exists, packages/apps/Nfc/migrate_nfc.txt:system/etc/updatecmds/migrate_nfc.txt)

PRODUCT_PACKAGES += \
    nfc.msm8660 \
    libnfc \
    libnfc_jni \
    Nfc \
    Tag \
    com.android.nfc_extras

# Wi-Fi
PRODUCT_PACKAGES += \
    hostapd \
    wpa_supplicant

# Sensors
# Wrap the proprietary sensor HAL so its legacy flip gesture is not exposed as
# Android's lift-to-wake sensor.
PRODUCT_PACKAGES += \
    sensors.batman_lgu_kr

# Torch, WifiDirect
PRODUCT_PACKAGES += \
    Torch \
    WifiDirect

# Misc
PRODUCT_PACKAGES += \
    tcpdump \
    qrngd

# Qualcomm compatibility library
PRODUCT_PACKAGES += \
    libstlport

# src
PRODUCT_PACKAGES += \
    hwaddrs \
    firmware_init
#    ami304d \
#    audio.primary.batman_lgu \
#    lights.batman_lgu \
#    power.batman_lgu

# Device-local legacy Qualcomm RIL
$(call project-set-path,ril,device/lge/batman_lgu/ril)
