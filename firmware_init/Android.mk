LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := firmware_init
LOCAL_MODULE_TAGS := optional

# Install links whose targets live on firmware partitions mounted at boot.
LOCAL_POST_INSTALL_CMD += \
    mkdir -p $(PRODUCT_OUT)/system/etc/firmware;

MDM_FIRMWARE_FILES := \
    amss.mbn dsp1.mbn dsp2.mbn dbl.mbn osbl.mbn \
    efs1.mbn efs2.mbn efs3.mbn
LOCAL_POST_INSTALL_CMD += \
    $(foreach f,$(MDM_FIRMWARE_FILES),ln -sf /modem/image/$f $(PRODUCT_OUT)/system/etc/firmware/$f;)

PIL_FIRMWARE_FILES := \
    modem.mdt modem.b00 modem.b01 modem.b02 modem.b03 modem.b04 \
    modem.b05 modem.b06 modem.b07 modem.b08 modem.b09 modem.b10 \
    q6.mdt q6.b00 q6.b01 q6.b02 q6.b03 q6.b04 q6.b05 q6.b06 q6.b07 \
    tzapps.mdt tzapps.b00 tzapps.b01 tzapps.b02 \
    playrdy.mdt playrdy.b00 playrdy.b01 playrdy.b02
LOCAL_POST_INSTALL_CMD += \
    $(foreach f,$(PIL_FIRMWARE_FILES),ln -sf /firmware/image/$f $(PRODUCT_OUT)/system/etc/firmware/$f;)

include $(BUILD_SHARED_LIBRARY)
