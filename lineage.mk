## Specify phone tech before including full_phone
# LG U+ APN
PRODUCT_COPY_FILES += $(LOCAL_PATH)/prebuilt/configs/apns-conf.xml:system/etc/apns-conf.xml
PRODUCT_PACKAGES += Stk

# Release name
PRODUCT_RELEASE_NAME := OptimusVu

TARGET_SCREEN_WIDTH := 768
TARGET_SCREEN_HEIGHT := 1024

# Inherit the LineageOS phone configuration.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Inherit device configuration
$(call inherit-product, device/lge/batman_lgu/batman_lgu.mk)

# Device naming
PRODUCT_DEVICE := batman_lgu
PRODUCT_NAME := lineage_batman_lgu
PRODUCT_BRAND := LGE
PRODUCT_MODEL := LG-F100L
PRODUCT_MANUFACTURER := LGE
PRODUCT_DEFAULT_LANGUAGE := ko
PRODUCT_DEFAULT_REGION := KR

PRODUCT_BUILD_PROP_OVERRIDES += \
    BUILD_UTC_DATE=1366341132 \
    PRODUCT_NAME=batman_lgu_kr \
    PRODUCT_BOARD=batman_lgu_kr \
    PRIVATE_BUILD_DESC="batman_lgu_kr-user 4.1.2 JZO54K 2b7eaa3e36 release-keys" \
    TARGET_AAPT_CHARACTERISTICS=nosdcard \
    PRODUCT_DEFAULT_LANGUAGE=ko \
    PRODUCT_DEFAULT_REGION=KR

BUILD_FINGERPRINT := LGE/batman_lgu_kr/batman:4.1.2/JZO54K/LG-F100L-V40b.1e5d95b998:user/release-keys
