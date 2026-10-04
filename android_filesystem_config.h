/* Preserve executable modes for the legacy daemon payloads in system images. */
#define NO_ANDROID_FILESYSTEM_CONFIG_DEVICE_DIRS 1

static const struct fs_path_config android_device_files[] = {
    { 00755, AID_ROOT, AID_ROOT, 0, "system/vendor/libexec/batman/*" },
};
