#ifndef CUBPET_PERIPHERAL_MANAGER_HPP
#define CUBPET_PERIPHERAL_MANAGER_HPP

#include "cubpet_peripheral_config.h"

#include <chrono>  // NOLINT(build/c++11)
#include <string>
#include <vector>

namespace ai_cubpet {

enum class PeripheralEventType {
    kUnknown,
    kTouch,
    kTouchCombo,
    kWake,
    kWifi,
    kNfc,
    kLight,
    kMotion,
    kPower,
};

struct PeripheralEvent {
    PeripheralEventType type = PeripheralEventType::kUnknown;
    std::string role;
    std::vector<std::string> roles;
    std::string source;
    std::string value;
    bool long_press = false;
    std::chrono::milliseconds hold_duration{0};
};

class PeripheralManager {
public:
    PeripheralManager();

    int LoadAuto();
    int LoadForModel(const char* model, const char* board_config_dir);

    const cubpet_peripheral_config& config() const { return config_; }
    const cubpet_gpio_input_config* FindGpioByRole(const std::string& role) const;

    PeripheralEvent MakeTouchEvent(const std::string& role, bool long_press) const;
    PeripheralEvent MakeTouchComboEvent(std::vector<std::string> roles,
        std::chrono::milliseconds hold_duration) const;
    PeripheralEvent MakeWakeEvent(const std::string& source) const;
    PeripheralEvent MakeNfcEvent(const std::string& uid) const;
    PeripheralEvent MakeLightEvent(const std::string& level) const;
    PeripheralEvent MakePowerEvent(const std::string& state) const;
    PeripheralEvent MakeMotionEvent(const std::string& motion) const;

private:
    cubpet_peripheral_config config_{};
};

const char* PeripheralEventTypeName(PeripheralEventType type);

}  // namespace ai_cubpet

#endif  // CUBPET_PERIPHERAL_MANAGER_HPP
