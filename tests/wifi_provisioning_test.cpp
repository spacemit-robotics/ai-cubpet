#include "cubpet_wifi_provisioning.hpp"

#include <cassert>
#include <atomic>
#include <chrono>  // NOLINT(build/c++11)
#include <memory>
#include <system_error>  // NOLINT(build/c++11)
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

namespace {

using ai_cubpet::CubpetWifiProvisioningRuntime;
using ai_cubpet::IWifiProvisioningBackend;
using ai_cubpet::BuildWifiProvisioningApSsid;
using ai_cubpet::WifiProvisioningConfig;
using ai_cubpet::WifiProvisioningResult;

class FakeWifiBackend : public IWifiProvisioningBackend {
public:
    explicit FakeWifiBackend(bool result = true) : result_(result) {}

    WifiProvisioningResult Run(const WifiProvisioningConfig&,
        std::atomic<bool>* stop_requested,
        std::function<void(const std::string&)> log) override
    {
        ++run_count;
        if (log) {
            log("fake run");
        }
        while (!stop_requested->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        WifiProvisioningResult result;
        result.success = result_;
        result.credentials_received = result_;
        result.connected = result_;
        result.has_ip = result_;
        result.message = result_ ? "connected" : "failed";
        return result;
    }

    void Stop(std::function<void(const std::string&)> log) override
    {
        ++stop_count;
        if (log) {
            log("fake stop");
        }
    }

    std::atomic<int> run_count{0};
    std::atomic<int> stop_count{0};

private:
    bool result_ = true;
};

class ImmediateWifiBackend : public IWifiProvisioningBackend {
public:
    explicit ImmediateWifiBackend(WifiProvisioningResult result) : result_(std::move(result)) {}

    WifiProvisioningResult Run(const WifiProvisioningConfig&,
        std::atomic<bool>*,
        std::function<void(const std::string&)> log) override
    {
        ++run_count;
        if (log) {
            log("fake immediate");
        }
        return result_;
    }

    void Stop(std::function<void(const std::string&)>) override {}

    std::atomic<int> run_count{0};

private:
    WifiProvisioningResult result_;
};

WifiProvisioningResult ConnectedResult()
{
    WifiProvisioningResult result;
    result.success = true;
    result.credentials_received = true;
    result.connected = true;
    result.has_ip = true;
    result.ssid = "test-ap";
    result.ip_addr = "192.168.1.23";
    result.message = "connected";
    return result;
}

WifiProvisioningResult CredentialsOnlyResult()
{
    WifiProvisioningResult result;
    result.credentials_received = true;
    result.connected = false;
    result.has_ip = false;
    result.ssid = "test-ap";
    result.message = "credentials received but station connection not confirmed";
    return result;
}

void TestStartIsSingleFlightAndStopRequestsBackend()
{
    auto backend = std::make_unique<FakeWifiBackend>();
    FakeWifiBackend* fake = backend.get();
    CubpetWifiProvisioningRuntime runtime(std::move(backend));
    std::atomic<int> log_count{0};

    auto status = runtime.Start(WifiProvisioningConfig{},
        [&](const std::string&) { ++log_count; });
    assert(status.started);
    assert(!status.already_running);

    while (fake->run_count.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(runtime.IsRunning());

    status = runtime.Start(WifiProvisioningConfig{}, nullptr);
    assert(!status.started);
    assert(status.already_running);
    assert(fake->run_count.load() == 1);

    runtime.Stop([&](const std::string&) { ++log_count; });
    assert(!runtime.IsRunning());
    assert(fake->stop_count.load() == 1);
    assert(log_count.load() > 0);
}

void TestCompletionReceivesBackendResult()
{
    auto backend = std::make_unique<ImmediateWifiBackend>(ConnectedResult());
    ImmediateWifiBackend* fake = backend.get();
    CubpetWifiProvisioningRuntime runtime(std::move(backend));
    std::atomic<int> success_count{0};

    auto status = runtime.Start(WifiProvisioningConfig{}, nullptr,
        [&](const WifiProvisioningResult& result) {
            if (result.success && result.connected && result.has_ip) {
                ++success_count;
            }
        });
    assert(status.started);
    while (runtime.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runtime.Stop(nullptr);

    assert(fake->run_count.load() == 1);
    assert(success_count.load() == 1);
}

void TestCompletionRequiresConfirmedConnection()
{
    auto backend = std::make_unique<ImmediateWifiBackend>(CredentialsOnlyResult());
    CubpetWifiProvisioningRuntime runtime(std::move(backend));
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    auto status = runtime.Start(WifiProvisioningConfig{}, nullptr,
        [&](const WifiProvisioningResult& result) {
            if (result.success) {
                ++success_count;
            } else {
                ++failure_count;
            }
        });
    assert(status.started);
    while (runtime.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runtime.Stop(nullptr);

    assert(success_count.load() == 0);
    assert(failure_count.load() == 1);
}

void TestCompletionCanStopRuntime()
{
    auto backend = std::make_unique<ImmediateWifiBackend>(ConnectedResult());
    CubpetWifiProvisioningRuntime runtime(std::move(backend));
    std::atomic<bool> callback_done{false};
    std::atomic<bool> stop_threw{false};

    auto status = runtime.Start(WifiProvisioningConfig{}, nullptr,
        [&](const WifiProvisioningResult&) {
            try {
                runtime.Stop(nullptr);
            } catch (const std::system_error&) {
                stop_threw.store(true);
            }
            callback_done.store(true);
        });
    assert(status.started);
    while (!callback_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runtime.Stop(nullptr);

    assert(!stop_threw.load());
}

void TestApSsidUsesLastMacByteAsTwoLowercaseHexDigits()
{
    const unsigned char mac[] = {0xfc, 0x23, 0xcd, 0x1e, 0x37, 0x6c};
    assert(BuildWifiProvisioningApSsid("AI_CUBPET_AP_LINK", mac, sizeof(mac)) ==
        "AI_CUBPET_AP_LINK_6c");

    const unsigned char short_suffix_mac[] = {0xfc, 0x23, 0xcd, 0x1e, 0x37, 0x06};
    assert(BuildWifiProvisioningApSsid("AI_CUBPET_AP_LINK", short_suffix_mac,
        sizeof(short_suffix_mac)) == "AI_CUBPET_AP_LINK_06");
}

}  // namespace

int main()
{
    TestStartIsSingleFlightAndStopRequestsBackend();
    TestCompletionReceivesBackendResult();
    TestCompletionRequiresConfirmedConnection();
    TestCompletionCanStopRuntime();
    TestApSsidUsesLastMacByteAsTwoLowercaseHexDigits();
    return 0;
}
