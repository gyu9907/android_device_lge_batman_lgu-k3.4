# RIL patches

Apply these patches from the corresponding source project root.

For CM10, apply the extended SIM status patch from `hardware/ril`:

    patch -p1 < ../../device/lge/batman_lgu/patches/0001-libril-handle-lg-extended-sim-status.patch

`LGEQualcommUiccRIL` already consumes LG's four extra PIN/PUK retry counters,
so no framework patch is required. The temporary PBM unsolicited response 1050
handling is intentionally excluded because it is not required for radio or SIM
initialization.
