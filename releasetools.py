# Copyright (C) 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0


def FullOTA_Assertions(info):
    info.script.AppendExtra(
        'assert(getprop("ro.batman.storage_layout") == '
        '"p28-system-p32-data-v1" || '
        'abort("Install requires batman p28-system/p32-data recovery."));')


def IncrementalOTA_Assertions(info):
    raise ValueError("Storage-role transition requires a full OTA")
