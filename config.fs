# Legacy service wrappers exec these payloads from a private vendor directory.
[vendor/libexec/batman/*]
mode: 0755
user: AID_ROOT
group: AID_ROOT
caps: 0

# Restore Oreo's network capabilities on kernels without ambient capabilities.
# The Wi-Fi driver is built in, so loading kernel modules is not required.
[vendor/bin/hw/android.hardware.wifi@1.0-service]
mode: 0755
user: AID_ROOT
group: AID_SHELL
caps: NET_ADMIN NET_RAW

[persist/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0

[mpt/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0

[drm/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0

[mm/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0

[tombstones/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0

[firmware/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0

[modem/]
mode: 0771
user: AID_SYSTEM
group: AID_SYSTEM
caps: 0
