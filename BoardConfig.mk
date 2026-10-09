BOARD_GLOBAL_CFLAGS += -DBATMAN_LGU -DNEED_UMS_ENABLE
TARGET_SPECIFIC_HEADER_PATH := device/lge/batman_lgu/include

BOARD_VENDOR := lge

# Preserve executable modes for vendor payloads outside the standard bin paths.
TARGET_FS_CONFIG_GEN += device/lge/batman_lgu/config.fs

#BOARD_HAVE_FM_RADIO := true
#BOARD_GLOBAL_CFLAGS += -DHAVE_FM_RADIO

USE_CAMERA_STUB := false

# inherit from the proprietary version
-include vendor/lge/batman_lgu/BoardConfigVendor.mk

TARGET_NO_BOOTLOADER := true
DEVICE_MANIFEST_FILE := device/lge/batman_lgu/manifest.xml

TARGET_BOARD_PLATFORM := msm8660
TARGET_BOARD_PLATFORM_GPU := qcom-adreno200

# Preserve the current Qualcomm policy baseline and exclude Pie test domains.
# The broad legacy include requires a separate neverallow compatibility audit.
include device/qcom/sepolicy/sepolicy.mk
BOARD_SEPOLICY_DIRS := $(filter-out device/qcom/sepolicy/vendor/test,$(BOARD_SEPOLICY_DIRS))
SELINUX_IGNORE_NEVERALLOWS := false
BOARD_SEPOLICY_DIRS += device/lge/batman_lgu/sepolicy

TARGET_ARCH := arm
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_ARCH_VARIANT := armv7-a-neon
# Oreo Soong has no Scorpion variant. Use ARMv7/NEON without Krait's
# VFPv4/LPAE assumptions; the kernel still selects the MSM8660 CPU.
TARGET_CPU_VARIANT := generic
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_CPU_SMP := true
# Match CONFIG_ANDROID_BINDER_IPC_32BIT and existing 32-bit vendor clients.
TARGET_USES_64_BIT_BINDER := false

TARGET_BOOTLOADER_BOARD_NAME := BATMAN_LGU
TARGET_OTA_ASSERT_DEVICE := batman_lgu,batman
BOARD_GLOBAL_CFLAGS += -DQCOM_HARDWARE

# Preload bootanimation
TARGET_BOOTANIMATION_PRELOAD := true

# Camera
USE_DEVICE_SPECIFIC_CAMERA := true
BOARD_NEEDS_MEMORYHEAPPMEM := true
# Retain the SELinux text-relocation compatibility policy.
TARGET_NEEDS_PLATFORM_TEXT_RELOCATIONS := true
# Lineage Pie linker compatibility for the verified legacy blob hosts only.
# HAL1 camera runs in mediaserver; AMI has its own exec payload. Match /proc/self/exe,
# including the actual payload path after the native wrapper calls execv().
# Never apply this to app_process/system_server or arbitrary applications.
TARGET_PROCESS_SDK_VERSION_OVERRIDE := \
    /system/bin/audioserver=22 \
    /system/bin/mediaserver=22 \
    /system/bin/rild=22 \
    /system/bin/hwaddrs=22 \
    /system/vendor/libexec/batman/cnd=22 \
    /system/vendor/libexec/batman/ks=22 \
    /system/vendor/libexec/batman/mpdecision=22 \
    /system/vendor/libexec/batman/netmgrd=22 \
    /system/vendor/libexec/batman/qcks=22 \
    /system/vendor/libexec/batman/qmiproxy=22 \
    /system/vendor/libexec/batman/qmuxd=22 \
    /system/vendor/libexec/batman/qseecomd=22 \
    /system/vendor/libexec/batman/rmt_storage=22 \
    /system/vendor/libexec/batman/thermald=22 \
    /system/vendor/libexec/batman/time_daemon=22 \
    /system/vendor/libexec/batman/bridgemgrd=22 \
    /system/vendor/libexec/batman/port-bridge=22 \
    /system/vendor/libexec/batman/mm-qcamera-daemon=22 \
    /system/vendor/libexec/batman/ami304d=22 \
    /system/vendor/bin/hw/android.hardware.sensors@1.0-service.batman=22
TARGET_HAS_LEGACY_CAMERA_HAL1 := true
# Oreo's linker reads shim mappings from the product configuration at build
# time; init's LD_SHIM_LIBS environment variable is no longer consulted.
TARGET_LD_SHIM_LIBS := \
    /system/lib/hw/camera.vendor.msm8660.so|libshim_camera.so \
    /system/lib/libOmxVenc.so|libshim_omx_venc.so \
    /system/lib/libOmxVdec.so|libshim_omx_vdec.so \
    /system/lib/libmediaplayerservice.so|libshim_thumbnail.so
# A mediaserver domain transition sets AT_SECURE in enforcing mode, so
# LD_PRELOAD is ignored. The service library mapping loads the thumbnail shim
# before its dependencies while keeping normal linker security checks.
BOARD_GLOBAL_CFLAGS += -DMR0_CAMERA_BLOB
# Keep the gralloc handle layout used by the stock camera. Oreo no longer
# propagates BOARD_GLOBAL_CFLAGS into display and video modules.
TARGET_QCOM_BSP_CAMERA_ABI_HACK := true
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
# Use the msm8660 policy implementation instead of the shared QCOM policy HAL.
USE_LEGACY_AUDIO_POLICY := 1
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := false
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := false

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
# Preserve the legacy OMX decoder's uncached-buffer request through Gralloc2.
TARGET_ADDITIONAL_GRALLOC_10_USAGE_BITS := 0x02000000
# Keep the working device-specific lights blob instead of the generic CAF HAL.
TARGET_PROVIDES_LIBLIGHT := true
TARGET_QCOM_DISPLAY_VARIANT := caf
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 3
USE_OPENGL_RENDERER := true
TARGET_USES_C2D_COMPOSITION := true
TARGET_USES_ION := true
TARGET_DISPLAY_USE_RETIRE_FENCE := true
TARGET_DISPLAY_INSECURE_MM_HEAP := true
# Keep display-only rotation buffers out of the small VIDC SMI pool.
TARGET_DISPLAY_ROTATOR_USE_SF_HEAP := true
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

BOARD_KERNEL_CMDLINE := console=ttyDCC0,115200,n8 androidboot.hardware=batman_lgu_kr kgsl.mmutype=gpummu vmalloc=580M
TARGET_NO_KERNEL_BUILD_VARIANT := true
BOARD_KERNEL_BASE := 0x40200000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_MKBOOTIMG_ARGS := --ramdisk_offset 0x01800000

TARGET_KERNEL_SOURCE := kernel/lge/msm8660
TARGET_KERNEL_CONFIG := batman_lgu_defconfig
TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-eabi-
TARGET_KERNEL_ADDITIONAL_FLAGS := -j16
KERNEL_TOOLCHAIN := $(abspath prebuilts/gcc/$(HOST_PREBUILT_TAG)/arm/arm-eabi-4.8/bin)

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
WIFI_DRIVER_FW_PATH_STA := "/vendor/firmware/fw_bcmdhd_p2p.bin"
WIFI_DRIVER_FW_PATH_P2P := "/vendor/firmware/fw_bcmdhd_p2p.bin"
WIFI_DRIVER_FW_PATH_AP := "/vendor/firmware/fw_bcmdhd_apsta.bin"
BOARD_LEGACY_NL80211_STA_EVENTS := true

# GPS
# The legacy location blobs require loc_read_conf from the vendor implementation.
BOARD_PROVIDES_LIBGPS_UTILS := true
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
JAVA_SOURCE_OVERLAYS := org.lineageos.hardware|device/lge/batman_lgu/lineagehw|**/*.java

# Legacy LGE Qualcomm RIL compatibility
BOARD_PROVIDES_LIBRIL := true
BOARD_PROVIDES_LIBREFERENCE_RIL := true
BOARD_PROVIDES_RILD := true
BOARD_RIL_CLASS := ../../../device/lge/batman_lgu/ril_class

# Device Power HAL requests bounded ondemand pulses through the Oreo adapter.
