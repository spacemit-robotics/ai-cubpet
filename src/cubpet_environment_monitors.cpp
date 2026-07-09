#include "cubpet_environment_monitors.hpp"

#include "cubpet_environment_levels.hpp"
#include "light_sensor.h"
#include "misc_io.h"
#include "motor.h"
#include "nfc.h"
#include "pm.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>  // NOLINT(build/c++11)
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <mutex>  // NOLINT(build/c++11)
#include <sstream>
#include <thread>  // NOLINT(build/c++11)
#include <utility>

namespace ai_cubpet {
namespace {

struct pwm_generic_info {
    uint32_t period;
    uint32_t duty_cycle;
};

constexpr auto kNfcPollTimeout = std::chrono::milliseconds(100);
constexpr auto kNfcPollInterval = std::chrono::milliseconds(200);
constexpr auto kNfcDuplicateSuppress = std::chrono::seconds(2);
constexpr auto kLightPollInterval = std::chrono::seconds(1);
constexpr auto kPowerPollInterval = std::chrono::seconds(10);
constexpr auto kMotionDebounce = std::chrono::milliseconds(50);

bool HasText(const char* value)
{
    return value && value[0] != '\0';
}

void LogLine(const EnvironmentMonitorSet::LogCallback& log, const std::string& line)
{
    if (log) {
        log(line);
    }
}

std::string NfcUidToString(const nfc_tag_info& info)
{
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    const uint8_t uid_len = std::min<uint8_t>(info.uid_len, sizeof(info.uid));
    for (uint8_t i = 0; i < uid_len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(info.uid[i]);
    }
    return oss.str();
}

PeripheralEvent MakeValueEvent(PeripheralEventType type, std::string value)
{
    PeripheralEvent event;
    event.type = type;
    event.value = std::move(value);
    return event;
}

std::string PowerValueFromState(const pm_state& state, float low_battery_percent)
{
    if (state.status == PM_STATUS_FAULT || state.error_code != 0) {
        return "fault";
    }
    if (state.status == PM_STATUS_FULL) {
        return "full";
    }
    if (state.status == PM_STATUS_CHARGING) {
        return "charging";
    }
    if (state.percentage > 0.0f && state.percentage <= low_battery_percent) {
        return "low";
    }
    return "normal";
}

struct PwmFanController {
    std::mutex mutex;
    cubpet_fan_config config{};
    motor_dev* fan = nullptr;
    bool configured = false;
    bool initialized = false;
    int current_percent = 0;

    void Configure(const cubpet_fan_config& fan_config)
    {
        std::lock_guard<std::mutex> lock(mutex);
        config = fan_config;
        configured = fan_config.gpio != 0 && fan_config.period != 0;
    }

    bool SetSpeed(int percent)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!configured) {
            return false;
        }

        percent = std::max(0, std::min(100, percent));
        if (percent == 0 && !initialized) {
            current_percent = 0;
            return true;
        }
        if (!EnsureInitializedLocked()) {
            return false;
        }

        motor_cmd cmd {};
        cmd.mode = percent == 0 ? MOTOR_MODE_IDLE : MOTOR_MODE_VEL;
        cmd.vel_des = static_cast<float>(percent);
        const bool ok = motor_set_cmd_one(fan, &cmd) == 0;
        if (ok) {
            current_percent = percent;
        }
        return ok;
    }

    void Stop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (fan) {
            motor_cmd cmd {};
            cmd.mode = MOTOR_MODE_IDLE;
            motor_set_cmd_one(fan, &cmd);
            motor_free(&fan, 1);
            fan = nullptr;
        }
        initialized = false;
        current_percent = 0;
    }

private:
    bool EnsureInitializedLocked()
    {
        if (initialized) {
            return true;
        }

        pwm_generic_info info {};
        info.period = config.period;
        info.duty_cycle = config.duty_cycle;
        fan = motor_alloc_pwm("pwm_gpio", config.gpio, &info);
        if (!fan) {
            return false;
        }
        if (motor_init_one(fan) < 0) {
            motor_free(&fan, 1);
            fan = nullptr;
            return false;
        }
        initialized = true;
        return true;
    }
};

}  // namespace

struct EnvironmentMonitorSet::Impl {
    cubpet_peripheral_config config {};
    EnvironmentMonitorOptions options;
    EventCallback event_callback;
    LogCallback log_callback;
    std::atomic<bool> running{false};
    std::thread nfc_thread;
    std::thread light_thread;
    std::thread power_thread;
    misc_dev* motion_dev = nullptr;
    PwmFanController fan;

    static void OnMotionEvent(misc_dev* dev,
        misc_event event,
        uint64_t timestamp_us,
        void* args)
    {
        (void)dev;
        Impl* self = static_cast<Impl*>(args);
        if (!self || !self->running.load() || event != MISC_EV_ACTIVE) {
            return;
        }

        std::ostringstream oss;
        oss << "[motion] g_sensor shake ACTIVE timestamp_us=" << timestamp_us;
        LogLine(self->log_callback, oss.str());
        self->Emit(MakeValueEvent(PeripheralEventType::kMotion, "shake"));
    }

    void Emit(const PeripheralEvent& event)
    {
        if (event_callback) {
            event_callback(event);
        }
    }

    void SleepInterruptible(std::chrono::milliseconds duration)
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (running.load() && std::chrono::steady_clock::now() < deadline) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::min<std::chrono::milliseconds>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                std::chrono::milliseconds(100)));
        }
    }

    void StartNfc()
    {
        nfc_thread = std::thread([this]() {
            nfc_dev* dev = nfc_alloc_i2c(config.nfc.name,
                config.nfc.i2c_dev,
                config.nfc.i2c_addr);
            if (!dev) {
                LogLine(log_callback, "[nfc] monitor disabled: nfc_alloc_i2c failed");
                return;
            }
            if (nfc_init(dev) < 0) {
                LogLine(log_callback, "[nfc] monitor disabled: nfc_init failed");
                nfc_free(dev);
                return;
            }

            LogLine(log_callback, "[nfc] monitor ready: " +
                std::string(config.nfc.i2c_dev));

            std::string last_uid;
            auto last_emit = std::chrono::steady_clock::time_point::min();
            while (running.load()) {
                nfc_tag_info info {};
                const int ret = nfc_poll(dev, &info,
                    static_cast<uint32_t>(kNfcPollTimeout.count()));
                if (ret == 0) {
                    const std::string uid = NfcUidToString(info);
                    const auto now = std::chrono::steady_clock::now();
                    if (!uid.empty() &&
                            (uid != last_uid || now - last_emit >= kNfcDuplicateSuppress)) {
                        last_uid = uid;
                        last_emit = now;
                        LogLine(log_callback, "[nfc] tag uid=" + uid);
                        Emit(MakeValueEvent(PeripheralEventType::kNfc, uid));
                    }
                } else if (ret != 1) {
                    LogLine(log_callback, "[nfc] poll error=" + std::to_string(ret));
                    SleepInterruptible(std::chrono::seconds(1));
                }
                SleepInterruptible(kNfcPollInterval);
            }
            nfc_free(dev);
        });
    }

    void StartLight()
    {
        light_thread = std::thread([this]() {
            light_sensor_dev* dev = light_sensor_alloc_i2c(config.light_sensor.name,
                config.light_sensor.i2c_dev,
                config.light_sensor.i2c_addr);
            if (!dev) {
                LogLine(log_callback,
                    "[light] monitor disabled: light_sensor_alloc_i2c failed");
                return;
            }
            if (light_sensor_init(dev) < 0) {
                LogLine(log_callback, "[light] monitor disabled: light_sensor_init failed");
                light_sensor_free(dev);
                return;
            }

            LogLine(log_callback, "[light] monitor ready: " +
                std::string(config.light_sensor.i2c_dev));

            std::string current_level;
            while (running.load()) {
                uint32_t lux = 0;
                const int ret = light_sensor_poll(dev, &lux);
                if (ret == 0) {
                    const std::string next_level =
                        LightLevelName(ClassifyLightLevel(lux));

                    if (!next_level.empty() && next_level != current_level) {
                        current_level = next_level;
                        LogLine(log_callback, "[light] level=" + current_level +
                            " lux=" + std::to_string(lux));
                        Emit(MakeValueEvent(PeripheralEventType::kLight,
                            current_level));
                    }
                } else if (ret != -EAGAIN) {
                    LogLine(log_callback, "[light] poll error=" + std::to_string(ret));
                }
                SleepInterruptible(kLightPollInterval);
            }
            light_sensor_free(dev);
        });
    }

    void StartPower()
    {
        power_thread = std::thread([this]() {
            pm_dev* dev = pm_alloc_generic("main_batt",
                config.pm.charger_node,
                config.pm.capacity_node,
                nullptr);
            if (!dev) {
                LogLine(log_callback, "[pm] monitor disabled: pm_alloc_generic failed");
                return;
            }
            if (pm_init(dev, nullptr) < 0) {
                LogLine(log_callback, "[pm] monitor disabled: pm_init failed");
                pm_free(dev);
                return;
            }

            LogLine(log_callback, "[pm] monitor ready");

            std::string current_value;
            int consecutive_errors = 0;
            while (running.load()) {
                pm_state state {};
                const int ret = pm_get_state(dev, &state);
                if (ret == 0) {
                    consecutive_errors = 0;
                    const std::string next_value = PowerValueFromState(state,
                        options.low_battery_percent);
                    if (next_value != current_value) {
                        current_value = next_value;
                        std::ostringstream oss;
                        oss << "[pm] state=" << current_value
                            << " percent=" << state.percentage
                            << " status=" << static_cast<int>(state.status)
                            << " error=" << state.error_code;
                        LogLine(log_callback, oss.str());
                        Emit(MakeValueEvent(PeripheralEventType::kPower,
                            current_value));
                    }
                } else {
                    ++consecutive_errors;
                    if (consecutive_errors == 1 || consecutive_errors % 6 == 0) {
                        LogLine(log_callback, "[pm] get_state failed: " +
                            std::to_string(ret));
                    }
                }
                SleepInterruptible(kPowerPollInterval);
            }
            pm_free(dev);
        });
    }

    bool StartMotion()
    {
        misc_gpiod_ctx ctx {
            "gpiochip0",
            config.g_sensor.int1_gpio,
            "ai-cubpet-gsensor",
        };
        motion_dev = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
        if (!motion_dev) {
            LogLine(log_callback, "[motion] monitor disabled: misc_io_alloc failed");
            return false;
        }

        misc_io_config(motion_dev, MISC_ACTIVE_HIGH,
            static_cast<uint16_t>(kMotionDebounce.count()));
        misc_io_trigger(motion_dev, &Impl::OnMotionEvent, this);
        LogLine(log_callback, "[motion] monitor ready: gpiochip0 line=" +
            std::to_string(config.g_sensor.int1_gpio));
        return true;
    }

    void Stop()
    {
        running.store(false);

        if (nfc_thread.joinable()) {
            nfc_thread.join();
        }
        if (light_thread.joinable()) {
            light_thread.join();
        }
        if (power_thread.joinable()) {
            power_thread.join();
        }
        if (motion_dev) {
            misc_io_free(motion_dev);
            motion_dev = nullptr;
        }
        fan.Stop();
    }
};

EnvironmentMonitorSet::EnvironmentMonitorSet()
    : impl_(std::make_unique<Impl>())
{
}

EnvironmentMonitorSet::~EnvironmentMonitorSet()
{
    Stop();
}

bool EnvironmentMonitorSet::Start(const cubpet_peripheral_config& config,
    const EnvironmentMonitorOptions& options,
    EventCallback event_callback,
    LogCallback log_callback)
{
    Stop();
    impl_->config = config;
    impl_->options = options;
    impl_->event_callback = std::move(event_callback);
    impl_->log_callback = std::move(log_callback);
    impl_->running.store(true);

    bool any_started = false;
    if (options.enable_nfc && HasText(config.nfc.i2c_dev) && config.nfc.i2c_addr != 0) {
        impl_->StartNfc();
        any_started = true;
    }
    if (options.enable_light && HasText(config.light_sensor.i2c_dev) &&
            config.light_sensor.i2c_addr != 0) {
        impl_->StartLight();
        any_started = true;
    }
    if (options.enable_power && HasText(config.pm.charger_node) &&
            HasText(config.pm.capacity_node)) {
        impl_->StartPower();
        any_started = true;
    }
    if (options.enable_motion && config.g_sensor.int1_gpio != 0) {
        any_started = impl_->StartMotion() || any_started;
    }
    if (options.enable_fan && config.fan.gpio != 0 && config.fan.period != 0) {
        impl_->fan.Configure(config.fan);
        LogLine(impl_->log_callback, "[fan] policy ready: gpio=" +
            std::to_string(config.fan.gpio));
        any_started = true;
    }

    if (!any_started) {
        impl_->running.store(false);
        LogLine(impl_->log_callback, "[environment] no monitors started");
    }
    return any_started;
}

void EnvironmentMonitorSet::Stop()
{
    if (impl_) {
        impl_->Stop();
    }
}

bool EnvironmentMonitorSet::SetFanSpeed(int percent)
{
    if (!impl_) {
        return false;
    }
    const bool ok = impl_->fan.SetSpeed(percent);
    LogLine(impl_->log_callback, std::string("[fan] speed=") +
        std::to_string(std::max(0, std::min(100, percent))) +
        (ok ? "" : " failed"));
    return ok;
}

void EnvironmentMonitorSet::StopFan()
{
    if (impl_) {
        impl_->fan.SetSpeed(0);
    }
}

PowerState PowerStateFromPeripheralValue(const std::string& value)
{
    if (value == "low") {
        return PowerState::kLowBattery;
    }
    if (value == "charging") {
        return PowerState::kCharging;
    }
    if (value == "full") {
        return PowerState::kFull;
    }
    if (value == "fault") {
        return PowerState::kFault;
    }
    return PowerState::kNormal;
}

}  // namespace ai_cubpet
