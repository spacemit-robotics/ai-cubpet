#include "cubpet_touch_event_aggregator.hpp"

#include <algorithm>
#include <utility>

namespace ai_cubpet {

TouchEventAggregator::TouchEventAggregator(std::vector<std::string> combo_roles,
    std::chrono::milliseconds long_press_threshold,
    std::chrono::milliseconds combo_emit_threshold)
    : combo_roles_(std::move(combo_roles)),
        long_press_threshold_(long_press_threshold),
        combo_emit_threshold_(combo_emit_threshold)
{
}

std::vector<PeripheralEvent> TouchEventAggregator::OnActive(
    const std::string& role,
    Clock::time_point now)
{
    auto& state = roles_[role];
    state.active = true;
    state.active_since = now;

    if (!combo_suppressed_until_release_ && AllComboRolesActive() && !combo_active_) {
        combo_active_ = true;
        combo_since_ = ComboActiveSince();
    }
    return {};
}

std::vector<PeripheralEvent> TouchEventAggregator::OnInactive(
    const std::string& role,
    Clock::time_point now)
{
    std::vector<PeripheralEvent> events;
    auto it = roles_.find(role);
    if (it == roles_.end() || !it->second.active) {
        return events;
    }

    const bool was_full_combo = combo_active_ && AllComboRolesActive() &&
        IsComboRole(role);
    const auto active_since = it->second.active_since;
    it->second.active = false;

    if (was_full_combo) {
        const auto hold_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - combo_since_);
        combo_suppressed_until_release_ = true;
        if (hold_duration >= combo_emit_threshold_) {
            PeripheralEvent event;
            event.type = PeripheralEventType::kTouchCombo;
            event.roles = combo_roles_;
            event.hold_duration = hold_duration;
            events.push_back(std::move(event));
        }
    } else if (!combo_suppressed_until_release_) {
        const auto hold_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - active_since);
        PeripheralEvent event;
        event.type = PeripheralEventType::kTouch;
        event.role = role;
        event.long_press = hold_duration >= long_press_threshold_;
        event.hold_duration = hold_duration;
        events.push_back(std::move(event));
    }

    RefreshComboState();
    return events;
}

bool TouchEventAggregator::IsComboRole(const std::string& role) const
{
    return std::find(combo_roles_.begin(), combo_roles_.end(), role) !=
        combo_roles_.end();
}

bool TouchEventAggregator::AllComboRolesActive() const
{
    if (combo_roles_.empty()) {
        return false;
    }
    for (const auto& role : combo_roles_) {
        const auto it = roles_.find(role);
        if (it == roles_.end() || !it->second.active) {
            return false;
        }
    }
    return true;
}

bool TouchEventAggregator::AnyComboRoleActive() const
{
    for (const auto& role : combo_roles_) {
        const auto it = roles_.find(role);
        if (it != roles_.end() && it->second.active) {
            return true;
        }
    }
    return false;
}

TouchEventAggregator::Clock::time_point TouchEventAggregator::ComboActiveSince() const
{
    auto since = Clock::time_point::min();
    for (const auto& role : combo_roles_) {
        const auto it = roles_.find(role);
        if (it != roles_.end() && it->second.active &&
                it->second.active_since > since) {
            since = it->second.active_since;
        }
    }
    return since;
}

void TouchEventAggregator::RefreshComboState()
{
    combo_active_ = AllComboRolesActive();
    if (combo_active_) {
        combo_since_ = ComboActiveSince();
        return;
    }
    if (!AnyComboRoleActive()) {
        combo_suppressed_until_release_ = false;
        combo_since_ = Clock::time_point{};
    }
}

}  // namespace ai_cubpet
