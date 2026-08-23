# CM11 porting patches

Apply these patches from the corresponding source project root.

Apply the extended SIM status patch from the CM11 `hardware/ril` project:

    patch -p1 < ../../device/lge/batman_lgu/patches/0001-libril-handle-lg-extended-sim-status.patch

`LGEQualcommUiccRIL` already consumes LG's four extra PIN/PUK retry counters,
so no additional framework patch is required for the extended SIM status. The
temporary PBM unsolicited response 1050 handling is intentionally excluded
because it is not required for radio or SIM initialization.

Apply the airplane-mode workaround from the CM11 `frameworks/opt/telephony`
project:

    patch -p1 < ../../../device/lge/batman_lgu/patches/0002-telephony-batman-airplane-mode-ril.patch

This version uses CM11's `Settings.Global` API. It is local to
`LGEQualcommUiccRIL`; it does not modify the Phone app.

The `rmt_storage` trigger is a device-tree change and is therefore applied
directly in `prebuilt/root/init.batman_lgu_kr.rc`, not carried as a patch.
CM10 normalized `androidboot.emmc=true` to `ro.boot.emmc=1`, while CM11 keeps
the value as `true`. Without this adaptation the userspace daemon never starts,
so QMI cannot load modem EFS/NV data.
