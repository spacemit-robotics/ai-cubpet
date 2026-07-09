#ifndef CUBPET_TOUCH_EVENT_AGGREGATOR_HPP
#define CUBPET_TOUCH_EVENT_AGGREGATOR_HPP

#include "cubpet_peripheral_manager.hpp"

#include <chrono>  // NOLINT(build/c++11)
#include <string>
#include <unordered_map>
#include <vector>

namespace ai_cubpet {

class TouchEventAggregator {
public:
    using Clock = std::chrono::steady_clock;

    TouchEventAggregator(std::vector<std::string> combo_roles,
        std::chrono::milliseconds long_press_threshold,
        std::chrono::milliseconds combo_emit_threshold);

    std::vector<PeripheralEvent> OnActive(const std::string& role,
        Clock::time_point now);
    std::vector<PeripheralEvent> OnInactive(const std::string& role,
        Clock::time_point now);

private:
    struct RoleState {
        bool active = false;
        Clock::time_point active_since{};
    };

    bool IsComboRole(const std::string& role) const;
    bool AllComboRolesActive() const;
    bool AnyComboRoleActive() const;
    Clock::time_point ComboActiveSince() const;
    void RefreshComboState();

    std::vector<std::string> combo_roles_;
    std::chrono::milliseconds long_press_threshold_;
    std::chrono::milliseconds combo_emit_threshold_;
    std::unordered_map<std::string, RoleState> roles_;
    bool combo_active_ = false;
    bool combo_suppressed_until_release_ = false;
    Clock::time_point combo_since_{};
};

}  // namespace ai_cubpet

#endif  // CUBPET_TOUCH_EVENT_AGGREGATOR_HPP
