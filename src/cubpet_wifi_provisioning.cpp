#include "cubpet_wifi_provisioning.hpp"

#include <algorithm>
#include <chrono>  // NOLINT(build/c++11)
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>  // NOLINT(build/c++11)
#include <utility>

#ifdef AI_CUBPET_USE_WIFI_SDK
#include <wifi.h>
#endif

namespace ai_cubpet {
namespace {

constexpr size_t kWifiSsidMaxLen = 32;

void LogLine(const std::function<void(const std::string&)>& log, const std::string& message)
{
    if (log) {
        log(message);
    }
}

std::string LastMacByteSuffix(const unsigned char* mac, size_t mac_len)
{
    if (!mac || mac_len == 0) {
        return "";
    }

    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
        << static_cast<unsigned int>(mac[mac_len - 1]);
    return oss.str();
}

#ifdef AI_CUBPET_USE_WIFI_SDK

struct LinkdContext {
    std::string ap_ssid;
    std::string target_ssid;
    bool credentials_received = false;
    enum wifi_status sta_connect_status = WIFI_STATUS_FAIL;
    std::function<void(const std::string&)> log;
    std::mutex mutex;
};

LinkdContext* g_linkd_context = nullptr;
std::mutex g_linkd_context_mutex;

std::string WifiStatusText(enum wifi_status status)
{
    std::ostringstream oss;
    oss << static_cast<int>(status);
    return oss.str();
}

std::string IpText(const uint8_t ip_addr[4])
{
    std::ostringstream oss;
    oss << static_cast<int>(ip_addr[0]) << "."
        << static_cast<int>(ip_addr[1]) << "."
        << static_cast<int>(ip_addr[2]) << "."
        << static_cast<int>(ip_addr[3]);
    return oss.str();
}

bool HasUsableIp(const uint8_t ip_addr[4])
{
    return ip_addr[0] != 0 || ip_addr[1] != 0 ||
        ip_addr[2] != 0 || ip_addr[3] != 0;
}

void LinkdConnectCallback(struct wifi_msg_data* msg)
{
    LinkdContext* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_linkd_context_mutex);
        context = g_linkd_context;
    }
    if (!context || !msg) {
        return;
    }

    auto* result = static_cast<struct wifi_linkd_result*>(msg->private_data);
    if (!result || !result->ssid) {
        return;
    }

    LogLine(context->log, "[wifi][linkd] credentials received");
    {
        std::lock_guard<std::mutex> lock(context->mutex);
        context->credentials_received = true;
        context->target_ssid = result->ssid;
    }

    struct wifi_sta_connect_param param;
    std::memset(&param, 0, sizeof(param));
    param.ssid = result->ssid;
    if (result->psk && *result->psk) {
        param.password = result->psk;
    }

    enum wifi_status ret = wifi_sta_remove_networks(context->ap_ssid.c_str());
    LogLine(context->log, "[wifi][linkd] remove temp AP profile " +
            context->ap_ssid + ": " + WifiStatusText(ret));

    ret = wifi_on(WIFI_MODE_STATION);
    if (ret != WIFI_STATUS_SUCCESS) {
        LogLine(context->log, "[wifi][linkd] wifi_on station failed: " +
                WifiStatusText(ret));
    }

    ret = wifi_sta_connect(&param);
    LogLine(context->log, "[wifi][linkd] wifi_sta_connect: " + WifiStatusText(ret));
    {
        std::lock_guard<std::mutex> lock(context->mutex);
        context->sta_connect_status = ret;
    }
}

class SdkWifiProvisioningBackend : public IWifiProvisioningBackend {
public:
    WifiProvisioningResult Run(const WifiProvisioningConfig& config,
        std::atomic<bool>* stop_requested,
        std::function<void(const std::string&)> log) override
    {
        WifiProvisioningResult result;
        if (!stop_requested) {
            result.message = "missing stop flag";
            return result;
        }

        enum wifi_status ret = wifi_init();
        if (ret != WIFI_STATUS_SUCCESS) {
            LogLine(log, "[wifi][linkd] wifi_init failed: " + WifiStatusText(ret));
            result.message = "wifi_init failed: " + WifiStatusText(ret);
            return result;
        }

        struct wifi_ap_config ap_config;
        std::memset(&ap_config, 0, sizeof(ap_config));

        std::string ap_ssid = BuildApSsid(config.ap_ssid_prefix, log);
        std::string ap_password = config.ap_password.empty() ? "12345678" : config.ap_password;

        ap_config.ssid = const_cast<char*>(ap_ssid.c_str());
        ap_config.psk = const_cast<char*>(ap_password.c_str());
        ap_config.sec = WIFI_SEC_WPA2_PSK;
        ap_config.ip_addr[0] = 192;
        ap_config.ip_addr[1] = 168;
        ap_config.ip_addr[2] = 1;
        ap_config.ip_addr[3] = 1;
        ap_config.gw_addr[0] = 192;
        ap_config.gw_addr[1] = 168;
        ap_config.gw_addr[2] = 1;
        ap_config.gw_addr[3] = 1;

        ret = wifi_on(WIFI_MODE_AP);
        if (ret != WIFI_STATUS_SUCCESS) {
            LogLine(log, "[wifi][linkd] wifi_on ap failed: " + WifiStatusText(ret));
        }

        ret = wifi_ap_enable(&ap_config);
        LogLine(log, "[wifi][linkd] wifi_ap_enable: " + WifiStatusText(ret));
        if (ret != WIFI_STATUS_SUCCESS) {
            wifi_deinit();
            result.message = "wifi_ap_enable failed: " + WifiStatusText(ret);
            return result;
        }

        LogLine(log, "[wifi][linkd] connect to AP SSID=" + ap_ssid +
                " password=<hidden>");
        LogLine(log, "[wifi][linkd] open http://192.168.1.1:8000 and submit target Wi-Fi");

        LinkdContext context;
        context.ap_ssid = ap_ssid;
        context.log = std::move(log);
        {
            std::lock_guard<std::mutex> lock(g_linkd_context_mutex);
            g_linkd_context = &context;
        }

        const int timeout_seconds = std::max(1, config.timeout_seconds);
        ret = wifi_linkd_protocol(WIFI_LINKD_MODE_SOFTAP,
            LinkdConnectCallback, nullptr, timeout_seconds);

        {
            std::lock_guard<std::mutex> lock(g_linkd_context_mutex);
            if (g_linkd_context == &context) {
                g_linkd_context = nullptr;
            }
        }

        LogLine(context.log, "[wifi][linkd] wifi_linkd_protocol: " +
                WifiStatusText(ret));

        {
            std::lock_guard<std::mutex> lock(context.mutex);
            result.credentials_received = context.credentials_received;
            result.ssid = context.target_ssid;
            if (context.credentials_received &&
                    context.sta_connect_status != WIFI_STATUS_SUCCESS) {
                result.message = "wifi_sta_connect failed: " +
                    WifiStatusText(context.sta_connect_status);
            }
        }

        if (stop_requested->load()) {
            const enum wifi_status disable_ret = wifi_ap_disable();
            LogLine(context.log, "[wifi][linkd] wifi_ap_disable: " +
                    WifiStatusText(disable_ret));
            result.message = "stopped";
        } else if (ret != WIFI_STATUS_SUCCESS) {
            const enum wifi_status disable_ret = wifi_ap_disable();
            LogLine(context.log, "[wifi][linkd] wifi_ap_disable: " +
                    WifiStatusText(disable_ret));
            result.message = "wifi_linkd_protocol failed: " + WifiStatusText(ret);
        } else {
            LogLine(context.log, "[wifi][linkd] provisioning callback handled AP handoff");
            result = ConfirmStationConnected(config, stop_requested, context.log, result);
        }

        wifi_deinit();
        return result;
    }

    void Stop(std::function<void(const std::string&)> log) override
    {
        const enum wifi_status disable_ret = wifi_ap_disable();
        LogLine(log, "[wifi][linkd] stop wifi_ap_disable: " +
                WifiStatusText(disable_ret));
    }

private:
    WifiProvisioningResult ConfirmStationConnected(const WifiProvisioningConfig& config,
        std::atomic<bool>* stop_requested,
        const std::function<void(const std::string&)>& log,
        WifiProvisioningResult result)
    {
        const int timeout_seconds = std::max(1, config.connection_confirm_seconds);
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(timeout_seconds);

        while (!stop_requested->load() && std::chrono::steady_clock::now() < deadline) {
            struct wifi_state state;
            std::memset(&state, 0, sizeof(state));
            const enum wifi_status state_ret = wifi_get_state(&state);
            if (state_ret == WIFI_STATUS_SUCCESS) {
                LogLine(log, "[wifi][linkd] station state=" +
                        std::to_string(static_cast<int>(state.sta_state)));
                if (state.sta_state == WIFI_STA_DHCP_TIMEOUT ||
                        state.sta_state == WIFI_STA_BUSY_TIMEOUT ||
                        state.sta_state == WIFI_STA_DISCONNECTED) {
                    result.message = "station failed, state=" +
                        std::to_string(static_cast<int>(state.sta_state));
                    break;
                }
            }

            struct wifi_sta_info info;
            std::memset(&info, 0, sizeof(info));
            const enum wifi_status info_ret = wifi_sta_get_info(&info);
            if (info_ret == WIFI_STATUS_SUCCESS && HasUsableIp(info.ip_addr)) {
                result.connected = true;
                result.has_ip = true;
                result.success = true;
                if (result.ssid.empty()) {
                    result.ssid = info.ssid;
                }
                result.ip_addr = IpText(info.ip_addr);
                result.message = "station connected";
                LogLine(log, "[wifi][linkd] station connected ssid=" +
                        result.ssid + " ip=" + result.ip_addr);
                return result;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (result.message.empty()) {
            result.message = stop_requested->load()
                ? "stopped"
                : "station connection not confirmed";
        }
        return result;
    }

    std::string BuildApSsid(const std::string& prefix,
        const std::function<void(const std::string&)>& log)
    {
        unsigned char mac[6] = {0};
        if (wifi_get_mac(nullptr, mac) == WIFI_STATUS_SUCCESS) {
            return BuildWifiProvisioningApSsid(prefix, mac, sizeof(mac));
        }

        const std::string ssid = BuildWifiProvisioningApSsid(prefix, nullptr, 0);
        LogLine(log, "[wifi][linkd] failed to get MAC, using SSID=" + ssid);
        return ssid;
    }
};

#else

class UnavailableWifiProvisioningBackend : public IWifiProvisioningBackend {
public:
    WifiProvisioningResult Run(const WifiProvisioningConfig&,
        std::atomic<bool>*,
        std::function<void(const std::string&)> log) override
    {
        LogLine(log, "[wifi][linkd] wifi SDK not enabled");
        WifiProvisioningResult result;
        result.message = "wifi SDK not enabled";
        return result;
    }

    void Stop(std::function<void(const std::string&)> log) override
    {
        LogLine(log, "[wifi][linkd] wifi SDK not enabled");
    }
};

#endif

}  // namespace

std::string BuildWifiProvisioningApSsid(const std::string& prefix,
    const unsigned char* mac,
    size_t mac_len)
{
    std::string ssid = prefix.empty() ? "AI_CUBPET_AP_LINK" : prefix;
    const std::string suffix = LastMacByteSuffix(mac, mac_len);
    if (!suffix.empty()) {
        ssid += "_" + suffix;
    }
    return ssid.substr(0, kWifiSsidMaxLen);
}

CubpetWifiProvisioningRuntime::CubpetWifiProvisioningRuntime(
    std::unique_ptr<IWifiProvisioningBackend> backend)
    : backend_(backend ? std::move(backend) : CreateWifiProvisioningBackend())
{
}

CubpetWifiProvisioningRuntime::~CubpetWifiProvisioningRuntime()
{
    Stop(nullptr);
}

WifiProvisioningStatus CubpetWifiProvisioningRuntime::Start(
    const WifiProvisioningConfig& config,
    std::function<void(const std::string&)> log,
    std::function<void(const WifiProvisioningResult& result)> completion)
{
    std::lock_guard<std::mutex> lock(mutex_);
    JoinFinishedLocked();
    if (running_.load()) {
        return {false, true, "already running"};
    }
    if (!backend_) {
        return {false, false, "backend unavailable"};
    }

    stop_requested_.store(false);
    running_.store(true);
    worker_ = std::thread(
        [this, config, log = std::move(log),
            completion = std::move(completion)]() mutable {
        WifiProvisioningResult result = backend_->Run(config, &stop_requested_, log);
        const bool stopped = stop_requested_.load();
        if (stopped) {
            result.success = false;
            result.message = "stopped";
        }
        result.success = result.success && result.connected && result.has_ip;
        running_.store(false);
        LogLine(log, result.success ? "[wifi][linkd] provisioning finished"
                                    : "[wifi][linkd] provisioning failed: " +
                                        result.message);
        if (completion) {
            completion(result);
        }
    });

    return {true, false, "started"};
}

void CubpetWifiProvisioningRuntime::Stop(std::function<void(const std::string&)> log)
{
    std::unique_lock<std::mutex> lock(mutex_);
    stop_requested_.store(true);
    if (backend_ && running_.load()) {
        backend_->Stop(log);
    }
    if (worker_.joinable()) {
        if (worker_.get_id() == std::this_thread::get_id()) {
            running_.store(false);
            return;
        }
        lock.unlock();
        worker_.join();
    }
    running_.store(false);
}

bool CubpetWifiProvisioningRuntime::IsRunning() const
{
    return running_.load();
}

void CubpetWifiProvisioningRuntime::JoinFinishedLocked()
{
    if (!running_.load() && worker_.joinable()) {
        worker_.join();
    }
}

std::unique_ptr<IWifiProvisioningBackend> CreateWifiProvisioningBackend()
{
#ifdef AI_CUBPET_USE_WIFI_SDK
    return std::make_unique<SdkWifiProvisioningBackend>();
#else
    return std::make_unique<UnavailableWifiProvisioningBackend>();
#endif
}

}  // namespace ai_cubpet
