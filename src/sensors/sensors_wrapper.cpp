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
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <set>

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
            if (vendorSensors[i].type == SENSOR_TYPE_WAKE_GESTURE ||
                    vendorSensors[i].type == SENSOR_TYPE_GLANCE_GESTURE) {
                ALOGI("Hiding unsupported legacy gesture sensor '%s' (handle %#x)",
                        vendorSensors[i].name, vendorSensors[i].handle);
                continue;
            }
            sensor_t& sensor = gFilteredSensors[output++];
            // The ICS blob has the same 32-bit stride, but everything after
            // minDelay was reserved. Do not expose those fields as 1.3 metadata.
            memcpy(&sensor, &vendorSensors[i], offsetof(sensor_t, fifoReservedEventCount));
            sensor.stringType = "";
            sensor.requiredPermission = "";
            sensor.maxDelay = 1000000;  // microseconds, like mako's 1.3 adapter
            sensor.flags = SENSOR_FLAG_CONTINUOUS_MODE;
            if (sensor.type == SENSOR_TYPE_LIGHT || sensor.type == SENSOR_TYPE_PROXIMITY) {
                sensor.minDelay = 0;
                sensor.flags = SENSOR_FLAG_ON_CHANGE_MODE;
                if (sensor.type == SENSOR_TYPE_PROXIMITY) sensor.flags |= SENSOR_FLAG_WAKE_UP;
            }
        }
        gFilteredSensorCount = output;
    }

    *list = gFilteredSensors;
    return gFilteredSensorCount;
}

// Adapt the pre-1.0 device ABI, following mako commit 8e9aa7b. The blob
// implements activate/setDelay/poll only; changing its version in place would
// make Oreo call function pointers beyond that structure.
struct SensorState {
    sensors_poll_device_t* vendor = nullptr;
    std::mutex lock;
    std::condition_variable changed;
    std::deque<sensors_event_t> events;
    std::set<int> active;
    bool stopping = false;
    bool started = false;
    int error = 0;
    ~SensorState() {
        if (vendor != nullptr) sensors_close(vendor);
    }
};

struct SensorDevice {
    sensors_poll_device_1_t device;
    std::shared_ptr<SensorState> state;
};

const sensor_t* findSensor(int handle) {
    for (int i = 0; i < gFilteredSensorCount; ++i) {
        if (gFilteredSensors[i].handle == handle) return &gFilteredSensors[i];
    }
    return nullptr;
}

SensorDevice* wrapper(void* device) {
    return static_cast<SensorDevice*>(device);
}

void* readEvents(void* argument) {
    std::unique_ptr<std::shared_ptr<SensorState>> owner(
            static_cast<std::shared_ptr<SensorState>*>(argument));
    const auto state = *owner;
    sensors_event_t data[32];
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(state->lock);
            state->changed.wait(lock, [&] {
                return state->stopping || state->events.size() < 256;
            });
            if (state->stopping) return nullptr;
        }
        const int count = state->vendor->poll(state->vendor, data, 32);
        std::lock_guard<std::mutex> lock(state->lock);
        if (state->stopping) return nullptr;
        if (count < 0 && count != -EINTR) {
            state->error = count;
            state->changed.notify_all();
            return nullptr;
        }
        for (int i = 0; i < count; ++i) {
            if (findSensor(data[i].sensor) && state->active.count(data[i].sensor)) {
                state->events.push_back(data[i]);
            }
        }
        state->changed.notify_all();
    }
}

int closeDevice(hw_device_t* device) {
    auto* self = wrapper(device);
    {
        std::lock_guard<std::mutex> lock(self->state->lock);
        self->state->stopping = true;
        for (int handle : self->state->active) {
            self->state->vendor->activate(self->state->vendor, handle, 0);
        }
        self->state->changed.notify_all();
    }
    // Legacy poll has no cancellation API. Its worker retains the state until
    // poll returns, so close must not free a device still used by the blob.
    delete self;
    return 0;
}

int activate(sensors_poll_device_t* device, int handle, int enabled) {
    if (!findSensor(handle)) return -EINVAL;
    auto state = wrapper(device)->state;
    std::lock_guard<std::mutex> lock(state->lock);
    int error = state->vendor->activate(state->vendor, handle, enabled);
    if (error == 0) {
        if (enabled) {
            state->active.insert(handle);
        } else {
            state->active.erase(handle);
            auto& events = state->events;
            events.erase(std::remove_if(events.begin(), events.end(), [=](const sensors_event_t& e) {
                return e.type != SENSOR_TYPE_META_DATA && e.sensor == handle;
            }), events.end());
            state->changed.notify_all();
        }
    }
    return error;
}

int setDelay(sensors_poll_device_t* device, int handle, int64_t ns) {
    const sensor_t* sensor = findSensor(handle);
    if (!sensor || ns < 0) return -EINVAL;
    ns = std::max(ns, static_cast<int64_t>(sensor->minDelay) * 1000);
    ns = std::min(ns, static_cast<int64_t>(sensor->maxDelay) * 1000);
    auto state = wrapper(device)->state;
    std::lock_guard<std::mutex> lock(state->lock);
    return state->vendor->setDelay(state->vendor, handle, ns);
}

int batch(sensors_poll_device_1_t* device, int handle, int flags,
          int64_t period, int64_t latency) {
    if (flags != 0 || latency < 0) return -EINVAL;
    // No hardware FIFO: stream immediately even when the client allows delay.
    return setDelay(&device->v0, handle, period);
}

int flush(sensors_poll_device_1_t* device, int handle) {
    const sensor_t* sensor = findSensor(handle);
    if (!sensor || (sensor->flags & SENSOR_FLAG_MASK_REPORTING_MODE) == SENSOR_FLAG_ONE_SHOT_MODE)
        return -EINVAL;
    auto state = wrapper(device)->state;
    std::lock_guard<std::mutex> lock(state->lock);
    if (!state->active.count(handle)) return -EINVAL;
    if (state->events.size() >= 1024) return -ENOMEM;
    sensors_event_t event = {};
    event.version = META_DATA_VERSION;
    event.type = SENSOR_TYPE_META_DATA;
    event.meta_data.what = META_DATA_FLUSH_COMPLETE;
    event.meta_data.sensor = handle;
    // Serialize with events already delivered by the non-batching blob. A
    // separate reader lets flush wake poll even when an on-change sensor is idle.
    state->events.push_back(event);
    state->changed.notify_all();
    return 0;
}

int pollEvents(sensors_poll_device_t* device, sensors_event_t* data, int count) {
    if (!data || count <= 0) return -EINVAL;
    auto state = wrapper(device)->state;
    std::unique_lock<std::mutex> lock(state->lock);
    if (!state->started) {
        pthread_t thread;
        auto* argument = new std::shared_ptr<SensorState>(state);
        int error = pthread_create(&thread, nullptr, readEvents, argument);
        if (error != 0) {
            delete argument;
            return -error;
        }
        pthread_detach(thread);
        state->started = true;
    }
    state->changed.wait(lock, [&] {
        return !state->events.empty() || state->error || state->stopping;
    });
    int output = 0;
    while (output < count && !state->events.empty()) {
        data[output++] = state->events.front();
        state->events.pop_front();
    }
    state->changed.notify_all();
    return output ? output : (state->error ? state->error : -ENODEV);
}

int openSensors(const hw_module_t* module, const char* id, hw_device_t** device) {
    if (!device || !id || strcmp(id, SENSORS_HARDWARE_POLL) != 0) return -EINVAL;
    *device = nullptr;
    const sensor_t* list;
    int error = getSensorsList(nullptr, &list);
    if (error < 0) return error;
    auto state = std::make_shared<SensorState>();
    error = gVendorModule->common.methods->open(&gVendorModule->common, id,
            reinterpret_cast<hw_device_t**>(&state->vendor));
    if (error != 0) return error;
    if (!state->vendor || !state->vendor->activate || !state->vendor->setDelay ||
            !state->vendor->poll) return -EINVAL;
    auto* self = new SensorDevice();
    self->state = state;
    self->device.common.tag = HARDWARE_DEVICE_TAG;
    self->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
    self->device.common.module = const_cast<hw_module_t*>(module);
    self->device.common.close = closeDevice;
    self->device.activate = activate;
    self->device.setDelay = setDelay;
    self->device.poll = pollEvents;
    self->device.batch = batch;
    self->device.flush = flush;
    *device = &self->device.common;
    return 0;
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
