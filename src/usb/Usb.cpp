/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */
#define LOG_TAG "UsbHalBatman"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <android/hardware/usb/1.0/IUsb.h>
#include <cutils/uevent.h>
#include <hidl/HidlTransportSupport.h>
#include <poll.h>
#include <sys/eventfd.h>

#include <cstring>
#include <mutex>
#include <thread>

using namespace android::hardware::usb::V1_0;
using android::base::unique_fd;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::Void;
using android::sp;

namespace {
constexpr char kStatePath[] = "/sys/devices/virtual/android_usb/android0/state";
constexpr char kPortName[] = "micro-usb";

// The MSM8660 android gadget driver already publishes its state and uevents.
// This HAL reports that peripheral port; it does not control USB composition
// or claim Type-C/PD role switching support.
class Usb final : public IUsb {
public:
    bool start() {
        mUevent.reset(uevent_open_socket(64 * 1024, true));
        mStop.reset(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        if (mUevent.get() < 0 || mStop.get() < 0) {
            PLOG(ERROR) << "Cannot monitor USB events";
            return false;
        }
        mWorker = std::thread([this] { monitor(); });
        return true;
    }

    ~Usb() override {
        if (mWorker.joinable()) {
            const uint64_t stop = 1;
            const auto result = write(mStop.get(), &stop, sizeof(stop));
            if (result != sizeof(stop)) PLOG(ERROR) << "Cannot stop USB monitor";
            mWorker.join();
        }
    }

    Return<void> setCallback(const sp<IUsbCallback>& callback) override {
        {
            std::lock_guard<std::mutex> lock(mLock);
            mCallback = callback;
        }
        return queryPortStatus();
    }

    Return<void> queryPortStatus() override {
        // Serialize snapshots as well as callbacks so an older query cannot
        // overwrite a newer connection event in UsbPortManager.
        std::lock_guard<std::mutex> lock(mLock);
        if (mCallback == nullptr) return Void();
        hidl_vec<PortStatus> ports;
        std::string state;
        Status status = Status::ERROR;
        if (android::base::ReadFileToString(kStatePath, &state)) {
            state = android::base::Trim(state);
            const bool connected = state == "CONNECTED" || state == "CONFIGURED";
            if (connected || state == "DISCONNECTED") {
                ports.resize(1);
                auto& port = ports[0];
                port.portName = kPortName;
                port.currentDataRole = connected ? PortDataRole::DEVICE : PortDataRole::NONE;
                port.currentPowerRole = connected ? PortPowerRole::SINK : PortPowerRole::NONE;
                port.currentMode = connected ? PortMode::UFP : PortMode::NONE;
                port.canChangeMode = false;
                port.canChangeDataRole = false;
                port.canChangePowerRole = false;
                port.supportedModes = PortMode::UFP;
                status = Status::SUCCESS;
            }
        }
        if (status != Status::SUCCESS) LOG(ERROR) << "Cannot read USB gadget state: " << state;
        const auto result = mCallback->notifyPortStatusChange(ports, status);
        if (!result.isOk()) LOG(ERROR) << "USB status callback failed: " << result.description();
        return Void();
    }

    Return<void> switchRole(const hidl_string& name, const PortRole& role) override {
        std::lock_guard<std::mutex> lock(mLock);
        if (mCallback != nullptr) {
            const Status status = name == kPortName ? Status::ERROR : Status::INVALID_ARGUMENT;
            const auto result = mCallback->notifyRoleSwitchStatus(name, role, status);
            if (!result.isOk()) LOG(ERROR) << "USB role callback failed: " << result.description();
        }
        return Void();
    }

private:
    void monitor() {
        pollfd fds[] = {{mUevent.get(), POLLIN, 0}, {mStop.get(), POLLIN, 0}};
        for (;;) {
            const int result = TEMP_FAILURE_RETRY(poll(fds, 2, -1));
            if (result < 0 || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                LOG(FATAL) << "USB event monitor failed";
            }
            if (fds[1].revents) return;
            if (!(fds[0].revents & POLLIN)) continue;
            char msg[4096];
            const int length = uevent_kernel_multicast_recv(mUevent.get(), msg, sizeof(msg) - 1);
            if (length <= 0 || length >= static_cast<int>(sizeof(msg) - 1)) continue;
            msg[length] = '\0';
            bool androidUsb = false;
            bool android0 = false;
            for (const char* field = msg; field < msg + length; field += strlen(field) + 1) {
                androidUsb |= !strcmp(field, "SUBSYSTEM=android_usb");
                android0 |= !strcmp(field, "DEVPATH=/devices/virtual/android_usb/android0");
            }
            if (androidUsb && android0) queryPortStatus();
        }
    }

    std::mutex mLock;
    sp<IUsbCallback> mCallback;
    unique_fd mUevent;
    unique_fd mStop;
    std::thread mWorker;
};
}  // namespace

int main() {
    android::hardware::configureRpcThreadpool(1, true);
    sp<Usb> service = new Usb();
    if (!service->start() || service->registerAsService() != android::OK) {
        LOG(ERROR) << "Cannot register USB port HAL";
        return 1;
    }
    android::hardware::joinRpcThreadpool();
    return 1;
}
