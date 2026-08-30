/*
 * Copyright (C) 2026 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#define LOG_TAG "SensorsWrapper"

#include <dlfcn.h>
#include <errno.h>
#include <hardware/hardware.h>
#include <hardware/sensors.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>

namespace {

const char kVendorHalPath[] = "/system/lib/hw/sensors.vendor.msm8660.so";

void* gVendorHandle;
sensors_module_t* gVendorModule;
sensor_t* gFilteredSensors;
int gFilteredSensorCount = -1;

int loadVendorModule() {
    if (gVendorModule != NULL) {
        return 0;
    }

    gVendorHandle = dlopen(kVendorHalPath, RTLD_NOW | RTLD_LOCAL);
    if (gVendorHandle == NULL) {
        ALOGE("Failed to load %s: %s", kVendorHalPath, dlerror());
        return -ENOENT;
    }

    gVendorModule = reinterpret_cast<sensors_module_t*>(
            dlsym(gVendorHandle, HAL_MODULE_INFO_SYM_AS_STR));
    if (gVendorModule == NULL) {
        ALOGE("Failed to find %s in %s: %s", HAL_MODULE_INFO_SYM_AS_STR,
                kVendorHalPath, dlerror());
        return -EINVAL;
    }

    return 0;
}

int getSensorsList(sensors_module_t*, const sensor_t** list) {
    int error = loadVendorModule();
    if (error != 0) {
        *list = NULL;
        return error;
    }

    if (gFilteredSensorCount < 0) {
        const sensor_t* vendorSensors = NULL;
        const int vendorCount = gVendorModule->get_sensors_list(
                gVendorModule, &vendorSensors);
        if (vendorCount < 0) {
            *list = NULL;
            return vendorCount;
        }

        gFilteredSensors = static_cast<sensor_t*>(
                calloc(static_cast<size_t>(vendorCount), sizeof(sensor_t)));
        if (gFilteredSensors == NULL) {
            *list = NULL;
            return -ENOMEM;
        }

        int output = 0;
        for (int i = 0; i < vendorCount; ++i) {
            if (vendorSensors[i].type == SENSOR_TYPE_WAKE_GESTURE) {
                ALOGI("Hiding unsupported wake gesture sensor '%s' (handle %#x)",
                        vendorSensors[i].name, vendorSensors[i].handle);
                continue;
            }
            memcpy(&gFilteredSensors[output++], &vendorSensors[i], sizeof(sensor_t));
        }
        gFilteredSensorCount = output;
    }

    *list = gFilteredSensors;
    return gFilteredSensorCount;
}

int openSensors(const hw_module_t*, const char* id, hw_device_t** device) {
    int error = loadVendorModule();
    if (error != 0) {
        return error;
    }
    return gVendorModule->common.methods->open(&gVendorModule->common, id, device);
}

hw_module_methods_t gModuleMethods = {
    .open = openSensors,
};

}  // namespace

extern "C" sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SENSORS_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "batman_lgu sensor HAL wrapper",
        .author = "The CyanogenMod Project",
        .methods = &gModuleMethods,
        .dso = NULL,
        .reserved = {0},
    },
    .get_sensors_list = getSensorsList,
    .set_operation_mode = NULL,
};
