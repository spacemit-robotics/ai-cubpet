#include "cubpet_peripheral_manager.hpp"

#include <utility>

namespace ai_cubpet {

PeripheralManager::PeripheralManager()
{
    cubpet_peripheral_config_init_defaults(&config_);
}

int PeripheralManager::LoadAuto()
{
    cubpet_peripheral_config_init_defaults(&config_);
    return cubpet_peripheral_config_load_auto(&config_);
}

int PeripheralManager::LoadForModel(const char* model, const char* board_config_dir)
{
    cubpet_peripheral_config_init_defaults(&config_);
    return cubpet_peripheral_config_load_for_model(model, board_config_dir, &config_);
}

const cubpet_gpio_input_config* PeripheralManager::FindGpioByRole(
    const std::string& role) const
{
    for (size_t i = 0; i < config_.gpio_input_count; ++i) {
        if (role == config_.gpio_inputs[i].role) {
            return &config_.gpio_inputs[i];
        }
    }
    return nullptr;
}

PeripheralEvent PeripheralManager::MakeTouchEvent(
    const std::string& role,
    bool long_press) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kTouch;
    event.role = role;
    event.long_press = long_press;
    return event;
}

PeripheralEvent PeripheralManager::MakeTouchComboEvent(
    std::vector<std::string> roles,
    std::chrono::milliseconds hold_duration) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kTouchCombo;
    event.roles = std::move(roles);
    event.hold_duration = hold_duration;
    return event;
}

PeripheralEvent PeripheralManager::MakeWakeEvent(const std::string& source) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kWake;
    event.source = source;
    return event;
}

PeripheralEvent PeripheralManager::MakeNfcEvent(const std::string& uid) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kNfc;
    event.value = uid;
    return event;
}

PeripheralEvent PeripheralManager::MakeLightEvent(const std::string& level) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kLight;
    event.value = level;
    return event;
}

PeripheralEvent PeripheralManager::MakePowerEvent(const std::string& state) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kPower;
    event.value = state;
    return event;
}

PeripheralEvent PeripheralManager::MakeMotionEvent(const std::string& motion) const
{
    PeripheralEvent event;
    event.type = PeripheralEventType::kMotion;
    event.value = motion;
    return event;
}

const char* PeripheralEventTypeName(PeripheralEventType type)
{
    switch (type) {
    case PeripheralEventType::kTouch: return "touch";
    case PeripheralEventType::kTouchCombo: return "touch_combo";
    case PeripheralEventType::kWake: return "wake";
    case PeripheralEventType::kWifi: return "wifi";
    case PeripheralEventType::kNfc: return "nfc";
    case PeripheralEventType::kLight: return "light";
    case PeripheralEventType::kMotion: return "motion";
    case PeripheralEventType::kPower: return "power";
    case PeripheralEventType::kUnknown:
    default:
        return "unknown";
    }
}

}  // namespace ai_cubpet
