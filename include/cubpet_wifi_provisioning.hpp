#ifndef CUBPET_WIFI_PROVISIONING_HPP
#define CUBPET_WIFI_PROVISIONING_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT(build/c++11)
#include <string>
#include <thread>  // NOLINT(build/c++11)

namespace ai_cubpet {

struct WifiProvisioningConfig {
    std::string ap_ssid_prefix = "AI_CUBPET_AP_LINK";
    std::string ap_password = "12345678";
    int timeout_seconds = 300;
    int connection_confirm_seconds = 30;
};

struct WifiProvisioningStatus {
    bool started = false;
    bool already_running = false;
    std::string message;
};

struct WifiProvisioningResult {
    bool success = false;
    bool credentials_received = false;
    bool connected = false;
    bool has_ip = false;
    std::string ssid;
    std::string ip_addr;
    std::string message;
};

class IWifiProvisioningBackend {
public:
    virtual ~IWifiProvisioningBackend() = default;

    virtual WifiProvisioningResult Run(const WifiProvisioningConfig& config,
        std::atomic<bool>* stop_requested,
        std::function<void(const std::string&)> log) = 0;
    virtual void Stop(std::function<void(const std::string&)> log) = 0;
};

class CubpetWifiProvisioningRuntime {
public:
    explicit CubpetWifiProvisioningRuntime(
        std::unique_ptr<IWifiProvisioningBackend> backend = nullptr);
    ~CubpetWifiProvisioningRuntime();

    CubpetWifiProvisioningRuntime(const CubpetWifiProvisioningRuntime&) = delete;
    CubpetWifiProvisioningRuntime& operator=(const CubpetWifiProvisioningRuntime&) = delete;

    WifiProvisioningStatus Start(const WifiProvisioningConfig& config,
        std::function<void(const std::string&)> log,
        std::function<void(const WifiProvisioningResult& result)> completion = nullptr);
    void Stop(std::function<void(const std::string&)> log);
    bool IsRunning() const;

private:
    void JoinFinishedLocked();

    std::unique_ptr<IWifiProvisioningBackend> backend_;
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
};

std::unique_ptr<IWifiProvisioningBackend> CreateWifiProvisioningBackend();
std::string BuildWifiProvisioningApSsid(const std::string& prefix,
    const unsigned char* mac,
    size_t mac_len);

}  // namespace ai_cubpet

#endif  // CUBPET_WIFI_PROVISIONING_HPP
