# RIL patches

Apply these patches from the corresponding source project root.

For CM10, apply the extended SIM status patch from `hardware/ril`:

    patch -p1 < ../../device/lge/batman_lgu/patches/0001-libril-handle-lg-extended-sim-status.patch

`LGEQualcommUiccRIL` already consumes LG's four extra PIN/PUK retry counters,
so no additional framework patch is required for the extended SIM status. The
temporary PBM unsolicited response 1050 handling is intentionally excluded
because it is not required for radio or SIM initialization.

Apply the batman airplane-mode workaround from `frameworks/base`:

    patch -p1 < ../../device/lge/batman_lgu/patches/0002-frameworks-base-batman-airplane-mode-ril.patch

This patch is local to `LGEQualcommUiccRIL`; it does not modify the Phone app.
