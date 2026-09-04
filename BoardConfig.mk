BOARD_GLOBAL_CFLAGS += -DBATMAN_LGU -DNEED_UMS_ENABLE
TARGET_SPECIFIC_HEADER_PATH := device/lge/batman_lgu/include

BOARD_VENDOR := lge

#BOARD_HAVE_FM_RADIO := true
#BOARD_GLOBAL_CFLAGS += -DHAVE_FM_RADIO

USE_CAMERA_STUB := false

# inherit from the proprietary version
-include vendor/lge/batman_lgu/BoardConfigVendor.mk

TARGET_NO_BOOTLOADER := true

TARGET_BOARD_PLATFORM := msm8660
TARGET_BOARD_PLATFORM_GPU := qcom-adreno200

TARGET_ARCH := arm
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := scorpion
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_CPU_SMP := true

TARGET_BOOTLOADER_BOARD_NAME := BATMAN_LGU
TARGET_OTA_ASSERT_DEVICE := batman_lgu,batman
TARGET_GLOBAL_CFLAGS += -mfpu=neon -mfloat-abi=softfp
TARGET_GLOBAL_CPPFLAGS += -mfpu=neon -mfloat-abi=softfp
BOARD_GLOBAL_CFLAGS += -DQCOM_HARDWARE

# Preload bootanimation
TARGET_BOOTANIMATION_PRELOAD := true

# Camera
USE_DEVICE_SPECIFIC_CAMERA := true
BOARD_NEEDS_MEMORYHEAPPMEM := true
TARGET_NEEDS_PLATFORM_TEXT_RELOCATIONS := true
TARGET_HAS_LEGACY_CAMERA_HAL1 := true
BOARD_GLOBAL_CFLAGS += -DMR0_CAMERA_BLOB
BOARD_GLOBAL_CFLAGS += -DQCOM_BSP_CAMERA_ABI_HACK
BOARD_GLOBAL_CFLAGS += -DDISABLE_HW_ID_MATCH_CHECK
BOARD_GLOBAL_CFLAGS += -DNEEDS_VECTORIMPL_SYMBOLS
# The legacy P930 HAL emits a null preview data callback before its first
# display buffer.  CM12.1 otherwise dereferences data->handle and kills
# mediaserver in CameraHardwareInterface::__data_cb().
BOARD_GLOBAL_CFLAGS += -DLEGACY_CAMERA_NULL_DATA_CB

# Audio
BOARD_GLOBAL_CFLAGS += -DQCOM_ACDB_ENABLED -DLEGACY_QCOM_VOICE
TARGET_USES_ION_AUDIO := true
TARGET_QCOM_AUDIO_VARIANT := caf
BOARD_USES_LEGACY_ALSA_AUDIO := true

# QCOM hardware
BOARD_USES_QCOM_HARDWARE := true
BOARD_USES_QC_TIME_SERVICES := true
TARGET_USE_QCOM_BIONIC_OPTIMIZATION := true
TARGET_QCOM_MEDIA_VARIANT := caf

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
TARGET_NEEDS_BLUETOOTH_INIT_DELAY := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/lge/batman_lgu/bluetooth
BOARD_CUSTOM_BT_CONFIG := device/lge/batman_lgu/bluetooth/vnd_bt.txt

# Graphics
TARGET_QCOM_DISPLAY_VARIANT := caf
USE_OPENGL_RENDERER := true
TARGET_USES_C2D_COMPOSITION := true
TARGET_USES_ION := true
TARGET_DISPLAY_USE_RETIRE_FENCE := true
TARGET_DISPLAY_INSECURE_MM_HEAP := true
BOARD_USES_QCOM_LIBS := true
BOARD_EGL_CFG := device/lge/batman_lgu/prebuilt/configs/egl.cfg
TARGET_USES_OVERLAY := true
TARGET_USES_ASHMEM := true
TARGET_USES_SF_BYPASS := true
TARGET_HAVE_BYPASS := true
TARGET_MAX_BYPASS := 3
VSYNC_EVENT_PHASE_OFFSET_NS := 7500000
SF_VSYNC_EVENT_PHASE_OFFSET_NS := 5000000
PRESENT_TIME_OFFSET_FROM_VSYNC_NS := 7500000

TARGET_USE_SCORPION_BIONIC_OPTIMIZATION := true
TARGET_USE_SCORPION_PLD_SET := true
TARGET_SCORPION_BIONIC_PLDOFFS := 6
TARGET_SCORPION_BIONIC_PLDSIZE := 128

# Bring-up only: keep the serial console available and prevent init from
# switching SELinux into enforcing mode while early-boot failures are traced.
BOARD_KERNEL_CMDLINE := console=ttyDCC0,115200,n8 androidboot.hardware=batman_lgu_kr androidboot.selinux=permissive kgsl.mmutype=gpummu vmalloc=580M
TARGET_NO_KERNEL_BUILD_VARIANT := true
BOARD_KERNEL_BASE := 0x40200000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x01800000

TARGET_KERNEL_SOURCE := kernel/lge/msm8660
TARGET_KERNEL_CONFIG := batman_lgu_defconfig
KERNEL_TOOLCHAIN_PREFIX := arm-eabi-
KERNEL_TOOLCHAIN := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/arm/arm-eabi-4.4.3/bin

# Bring-up only. Prepend these values and remove inherited copies because
# default.prop uses the first occurrence of a duplicated property.
ADDITIONAL_DEFAULT_PROPERTIES := \
    ro.secure=0 \
    ro.adb.secure=0 \
    ro.debuggable=1 \
    security.perf_harden=0 \
    cm.service.adb.root=1 \
    service.adb.root=1 \
    persist.service.adb.enable=1 \
    persist.sys.usb.config=mtp,adb \
    $(filter-out \
        ro.secure=% \
        ro.adb.secure=% \
        ro.debuggable=% \
        security.perf_harden=% \
        cm.service.adb.root=% \
        service.adb.root=% \
        persist.service.adb.enable=% \
        persist.sys.usb.config=%, \
        $(ADDITIONAL_DEFAULT_PROPERTIES))

BOARD_BOOTIMAGE_PARTITION_SIZE := 0x00A00000
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 0x01000000
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1073741824
BOARD_USERDATAIMAGE_PARTITION_SIZE := 2147483648
BOARD_FLASH_BLOCK_SIZE := 131072

#BOARD_HAS_NO_SELECT_BUTTON := true
#BOARD_TOUCH_RECOVERY := true

# Board Characteristics
TARGET_AAPT_CHARACTERISTICS := nosdcard

# Wifi related defines
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
WPA_SUPPLICANT_VERSION := VER_0_8_X
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_WLAN_DEVICE := bcmdhd
WIFI_DRIVER_FW_PATH_PARAM := "/sys/module/bcmdhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA := "/system/etc/firmware/fw_bcmdhd_p2p.bin"
WIFI_DRIVER_FW_PATH_P2P := "/system/etc/firmware/fw_bcmdhd_p2p.bin"
WIFI_DRIVER_FW_PATH_AP := "/system/etc/firmware/fw_bcmdhd_apsta.bin"
BOARD_LEGACY_NL80211_STA_EVENTS := true

# GPS
TARGET_GPS_HAL_PATH := device/lge/batman_lgu/gps
BOARD_VENDOR_QCOM_GPS_LOC_API_AMSS_VERSION := 50000

# Vold
BOARD_VOLD_EMMC_SHARES_DEV_MAJOR := true
BOARD_VOLD_MAX_PARTITIONS := 66
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/platform/msm_hsusb/gadget/lun%d/file"

# Webkit
ENABLE_WEBGL := true
TARGET_FORCE_CPU_UPLOAD := false

# Recovery
BOARD_NO_SECURE_DISCARD := true
BOARD_CUSTOM_GRAPHICS := ../../../device/lge/batman_lgu/recovery/graphics.c
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
BOARD_CUSTOM_RECOVERY_KEYMAPPING := ../../device/lge/batman_lgu/recovery/recovery_keymapping.c
TARGET_RECOVERY_FSTAB := device/lge/batman_lgu/prebuilt/root/fstab.batman_lgu_kr
TARGET_USERIMAGES_USE_EXT4 := true

# CyanogenMod hardware abstraction
BOARD_HARDWARE_CLASS := device/lge/batman_lgu/cmhw/

# Legacy LGE Qualcomm RIL compatibility
BOARD_PROVIDES_LIBRIL := true
BOARD_PROVIDES_LIBREFERENCE_RIL := true
BOARD_PROVIDES_RILD := true
BOARD_RIL_CLASS := ../../../device/lge/batman_lgu/ril_class
TARGET_RIL_VARIANT_LEGACY := true
TARGET_RIL_SUPPORT_SEEK := true

# CM 13 uses the Qualcomm power HAL from hardware/qcom/power.
TARGET_POWERHAL_VARIANT := qcom
