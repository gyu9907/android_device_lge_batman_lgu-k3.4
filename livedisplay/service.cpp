/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "vendor.lineage.livedisplay@2.0-service.batman"
#include <array>
#include <sstream>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <hidl/HidlTransportSupport.h>
#include <vendor/lineage/livedisplay/2.0/IDisplayColorCalibration.h>

using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_vec;
using vendor::lineage::livedisplay::V2_0::IDisplayColorCalibration;

namespace {
// Preserve the path and range of the Pie DisplayColorCalibration extension.
constexpr const char* kKcalPath = "/sys/devices/platform/mdp.524801/kcal";
constexpr int32_t kMin = 0;
constexpr int32_t kMax = 255;

bool readCalibration(std::array<int32_t, 3>* rgb) {
    std::string value;
    if (!android::base::ReadFileToString(kKcalPath, &value)) return false;
    std::istringstream input(value);
    std::string extra;
    if (!(input >> (*rgb)[0] >> (*rgb)[1] >> (*rgb)[2]) || (input >> extra)) return false;
    for (int32_t color : *rgb) {
        if (color < kMin || color > kMax) return false;
    }
    return true;
}

class DisplayColorCalibration : public IDisplayColorCalibration {
    Return<int32_t> getMaxValue() override { return kMax; }
    Return<int32_t> getMinValue() override { return kMin; }
    Return<void> getCalibration(getCalibration_cb callback) override {
        std::array<int32_t, 3> value;
        hidl_vec<int32_t> rgb;
        if (readCalibration(&value)) {
            rgb.resize(value.size());
            for (size_t i = 0; i < value.size(); ++i) rgb[i] = value[i];
        } else {
            LOG(ERROR) << "Could not read MDP color calibration";
        }
        callback(rgb);
        return Void();
    }
    Return<bool> setCalibration(const hidl_vec<int32_t>& rgb) override {
        if (rgb.size() != 3) return false;
        for (int32_t color : rgb) {
            if (color < kMin || color > kMax) return false;
        }
        return android::base::WriteStringToFile(
                android::base::StringPrintf("%d %d %d\n", rgb[0], rgb[1], rgb[2]),
                kKcalPath, true);
    }
};
}  // namespace

int main() {
    android::hardware::configureRpcThreadpool(1, true);
    android::sp<IDisplayColorCalibration> service = new DisplayColorCalibration();
    if (service->registerAsService() != android::OK) {
        LOG(ERROR) << "Could not register MDP color calibration service";
        return 1;
    }
    android::hardware::joinRpcThreadpool();
    return 1;
}
