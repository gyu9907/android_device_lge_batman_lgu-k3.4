// Adapt the LGE MSM8660/MDM9600 RIL v7 ABI to the platform Oreo RIL.
#define LOG_TAG "LgeRilCompat"
#include "LgeRilCompat.h"
#include <log/log.h>
#include <limits.h>
#include <string.h>
#include <map>
#include <new>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct LgeAppStatus {
    RIL_AppStatus app;
    int pin1Retries, puk1Retries, pin2Retries, puk2Retries;
};
struct LgeCardStatus {
    RIL_CardState cardState;
    RIL_PinState pinState;
    int gsmIndex, cdmaIndex, imsIndex, count;
    LgeAppStatus apps[RIL_CARD_MAX_APPS];
};
struct LgeSimIo { int cla; RIL_SIM_IO_v6 io; };
static_assert(sizeof(LgeSimIo) == sizeof(int) + sizeof(RIL_SIM_IO_v6),
        "LGE SIM_IO uses the 32-bit SEEK layout");

RIL_Env frameworkEnv, vendorEnv;
RIL_RadioFunctions functions;
const RIL_RadioFunctions *vendor;
std::mutex lock;
std::map<RIL_Token, int> requests;
std::string usimAid;

bool supported(int request) {
    // This v7 blob predates the cell-info, NV and subscription-control APIs.
    return request > 0 && request <= RIL_REQUEST_VOICE_RADIO_TECH;
}

int appIndex(int index, int count) {
    return index >= 0 && index < count ? index : -1;
}

bool cardStatus(void *data, size_t size, RIL_CardStatus_v6 &out) {
    if (!data) return false;
    if (size == sizeof(LgeCardStatus)) {
        const auto &in = *static_cast<const LgeCardStatus *>(data);
        if (in.count < 0 || in.count > RIL_CARD_MAX_APPS) return false;
        out.card_state = in.cardState;
        out.universal_pin_state = in.pinState;
        out.gsm_umts_subscription_app_index = in.gsmIndex;
        out.cdma_subscription_app_index = in.cdmaIndex;
        out.ims_subscription_app_index = in.imsIndex;
        out.num_applications = in.count;
        for (int i = 0; i < in.count; ++i) out.applications[i] = in.apps[i].app;
    } else if (size == sizeof(out)) {
        out = *static_cast<const RIL_CardStatus_v6 *>(data);
    } else {
        return false;
    }
    if (out.num_applications < 0 || out.num_applications > RIL_CARD_MAX_APPS)
        return false;
    int state = out.card_state;
    if (state >= 3 && state <= 5) state -= 3;
    if (state < 0 || state > 2) return false;
    out.card_state = static_cast<RIL_CardState>(state);
    out.gsm_umts_subscription_app_index = appIndex(out.gsm_umts_subscription_app_index,
            out.num_applications);
    out.cdma_subscription_app_index = appIndex(out.cdma_subscription_app_index,
            out.num_applications);
    out.ims_subscription_app_index = appIndex(out.ims_subscription_app_index,
            out.num_applications);
    std::lock_guard<std::mutex> guard(lock);
    usimAid.clear();
    for (int i = 0; i < out.num_applications; ++i) {
        if (out.applications[i].app_type == RIL_APPTYPE_USIM &&
                out.applications[i].aid_ptr) usimAid = out.applications[i].aid_ptr;
    }
    return true;
}

RIL_SignalStrength_v8 signalStrength(const void *data) {
    RIL_SignalStrength_v8 result = {};
    memcpy(&result, data, sizeof(RIL_SignalStrength_v6));
    result.LTE_SignalStrength.timingAdvance = INT_MAX;
    if (result.LTE_SignalStrength.signalStrength == 99) {
        result.LTE_SignalStrength.rsrp = -1;
        result.LTE_SignalStrength.rsrq = -1;
        result.LTE_SignalStrength.rssnr = INT_MAX;
        result.LTE_SignalStrength.cqi = -1;
    }
    return result;
}

void complete(RIL_Token token, RIL_Errno error, void *data, size_t size) {
    int request;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto entry = requests.find(token);
        if (entry == requests.end()) {
            ALOGE("Completion for an unknown request token");
            return;
        }
        request = entry->second;
        requests.erase(entry);
    }
    if (error == RIL_E_SUCCESS && request == RIL_REQUEST_GET_SIM_STATUS) {
        RIL_CardStatus_v6 status = {};
        if (cardStatus(data, size, status)) {
            frameworkEnv.OnRequestComplete(token, error, &status, sizeof(status));
        } else {
            ALOGE("Invalid LGE card status size %zu", size);
            frameworkEnv.OnRequestComplete(token, RIL_E_GENERIC_FAILURE, nullptr, 0);
        }
        return;
    }
    if (error == RIL_E_SUCCESS && request == RIL_REQUEST_QUERY_AVAILABLE_NETWORKS) {
        if ((!data && size) || size % (5 * sizeof(char *))) {
            frameworkEnv.OnRequestComplete(token, RIL_E_GENERIC_FAILURE, nullptr, 0);
            return;
        }
        // LGE appends a radio technology string to each four-string operator.
        std::vector<char *> networks;
        auto strings = static_cast<char **>(data);
        for (size_t i = 0; i < size / sizeof(char *); i += 5)
            networks.insert(networks.end(), strings + i, strings + i + 4);
        frameworkEnv.OnRequestComplete(token, error, networks.data(),
                networks.size() * sizeof(char *));
        return;
    }
    if (error == RIL_E_SUCCESS && request == RIL_REQUEST_SIGNAL_STRENGTH &&
            data && size == sizeof(RIL_SignalStrength_v6)) {
        auto strength = signalStrength(data);
        frameworkEnv.OnRequestComplete(token, error, &strength, sizeof(strength));
        return;
    }
    // Oreo already handles v6 data-call responses and v7 registration strings.
    frameworkEnv.OnRequestComplete(token, error, data, size);
}

void unsolicited(int response, const void *data, size_t size) {
    // LGE v7 uses 1050 for PBM_INIT_DONE, not Oreo's KEEPALIVE_STATUS.
    // There is no equivalent framework indication for this phonebook event.
    constexpr int lgePbmInitDone = 1050;
    if (response == lgePbmInitDone) return;
    if (response == RIL_UNSOL_UICC_SUBSCRIPTION_STATUS_CHANGED &&
            (!data || size != sizeof(int) ||
             (*static_cast<const int *>(data) != 0 && *static_cast<const int *>(data) != 1))) {
        ALOGW("Ignoring malformed UICC subscription indication");
        return;
    }
    if (response == RIL_UNSOL_SIGNAL_STRENGTH && data &&
            size == sizeof(RIL_SignalStrength_v6)) {
        auto strength = signalStrength(data);
        frameworkEnv.OnUnsolicitedResponse(response, &strength, sizeof(strength));
        return;
    }
    frameworkEnv.OnUnsolicitedResponse(response, data, size);
}

// LGE v7 power, subscription and screen-state handlers dereference their
// DMS/NAS/PBM clients before checking readiness. RIL_CONNECTED can arrive
// while those clients are still being initialized.
// Keep the original request and payload on the existing RIL event loop until
// the vendor reports a usable radio state; never acknowledge an unsent request.
struct PendingRadioRequest {
    RIL_Token token;
    int code;
    int value;
    unsigned int attempts;
};

void request(int code, void *data, size_t size, RIL_Token token);

void retryRadioRequest(void *opaque) {
    auto pending = static_cast<PendingRadioRequest *>(opaque);
    if (vendor->onStateRequest() != RADIO_STATE_UNAVAILABLE) {
        request(pending->code, &pending->value, sizeof(pending->value), pending->token);
        delete pending;
        return;
    }
    if (++pending->attempts >= 120) {
        frameworkEnv.OnRequestComplete(pending->token,
                RIL_E_RADIO_NOT_AVAILABLE, nullptr, 0);
        delete pending;
        return;
    }
    const timeval retry = {0, 250000};
    frameworkEnv.RequestTimedCallback(retryRadioRequest, pending, &retry);
}

void request(int code, void *data, size_t size, RIL_Token token) {
    if (!supported(code)) {
        frameworkEnv.OnRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, nullptr, 0);
        return;
    }
    if ((code == RIL_REQUEST_CDMA_SET_SUBSCRIPTION_SOURCE ||
            code == RIL_REQUEST_RADIO_POWER || code == RIL_REQUEST_SCREEN_STATE) &&
            vendor->onStateRequest() == RADIO_STATE_UNAVAILABLE) {
        if (!data || size != sizeof(int)) {
            frameworkEnv.OnRequestComplete(token, RIL_E_GENERIC_FAILURE, nullptr, 0);
            return;
        }
        auto pending = new (std::nothrow) PendingRadioRequest{
                token, code, *static_cast<const int *>(data), 0};
        if (!pending) {
            frameworkEnv.OnRequestComplete(token, RIL_E_NO_MEMORY, nullptr, 0);
            return;
        }
        const timeval retry = {0, 250000};
        frameworkEnv.RequestTimedCallback(retryRadioRequest, pending, &retry);
        return;
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        requests.emplace(token, code);
    }
    if (code == RIL_REQUEST_SIM_IO) {
        if (!data || size != sizeof(RIL_SIM_IO_v6)) {
            complete(token, RIL_E_GENERIC_FAILURE, nullptr, 0);
            return;
        }
        LgeSimIo io = {0, *static_cast<RIL_SIM_IO_v6 *>(data)};
        std::string path;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (io.io.path && io.io.aidPtr && !usimAid.empty() && usimAid == io.io.aidPtr) {
                path = io.io.path;
                if (path.size() >= 4 && path.compare(path.size() - 4, 4, "7F20") == 0) {
                    path.replace(path.size() - 4, 4, "7FFF");
                    io.io.path = const_cast<char *>(path.c_str());
                }
            }
        }
        vendor->onRequest(code, &io, sizeof(io), token);
    } else {
        vendor->onRequest(code, data, size, token);
    }
}

int supports(int code) { return supported(code) && vendor->supports(code); }
} // namespace

extern "C" const RIL_RadioFunctions *LGE_RIL_Init(
        const RIL_RadioFunctions *(*init)(const RIL_Env *, int, char **),
        const RIL_Env *env, int argc, char **argv) {
    frameworkEnv = *env;
    vendorEnv = *env;
    vendorEnv.OnRequestComplete = complete;
    vendorEnv.OnUnsolicitedResponse = unsolicited;
    vendor = init(&vendorEnv, argc, argv);
    if (!vendor || vendor->version != 7 || !vendor->onRequest || !vendor->supports ||
            !vendor->onStateRequest) {
        ALOGE("Expected the LGE v7 vendor RIL");
        return nullptr;
    }
    functions = *vendor;
    functions.onRequest = request;
    functions.supports = supports;
    return &functions;
}
