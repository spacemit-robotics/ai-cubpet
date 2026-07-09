#ifndef CUBPET_ENVIRONMENT_MONITORS_HPP
#define CUBPET_ENVIRONMENT_MONITORS_HPP

#include "cubpet_peripheral_config.h"
#include "cubpet_peripheral_manager.hpp"
#include "cubpet_toy_state_machine.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ai_cubpet {

struct EnvironmentMonitorOptions {
    bool enable_nfc = true;
    bool enable_light = true;
    bool enable_power = true;
    bool enable_motion = true;
    bool enable_fan = true;
    unsigned int dark_lux_threshold = 30;
    unsigned int bright_lux_threshold = 80;
    float low_battery_percent = 15.0f;
};

class EnvironmentMonitorSet {
public:
    using EventCallback = std::function<void(const PeripheralEvent& event)>;
    using LogCallback = std::function<void(const std::string& line)>;

    EnvironmentMonitorSet();
    ~EnvironmentMonitorSet();

    EnvironmentMonitorSet(const EnvironmentMonitorSet&) = delete;
    EnvironmentMonitorSet& operator=(const EnvironmentMonitorSet&) = delete;

    bool Start(const cubpet_peripheral_config& config,
        const EnvironmentMonitorOptions& options,
        EventCallback event_callback,
        LogCallback log_callback);
    void Stop();

    bool SetFanSpeed(int percent);
    void StopFan();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

PowerState PowerStateFromPeripheralValue(const std::string& value);

}  // namespace ai_cubpet

#endif  // CUBPET_ENVIRONMENT_MONITORS_HPP
