#include "cubpet_toy_state_machine.hpp"

#include <algorithm>
#include <utility>

namespace ai_cubpet {
namespace {

constexpr auto kProvisioningHoldThreshold = std::chrono::seconds(10);
constexpr auto kProvisioningExitHoldThreshold = std::chrono::seconds(3);

bool HasRole(const std::vector<std::string>& roles, const std::string& role)
{
    return std::find(roles.begin(), roles.end(), role) != roles.end();
}

bool HasProvisioningComboRoles(const ToyEvent& event)
{
    return event.roles.size() >= 3 &&
        HasRole(event.roles, "head") &&
        HasRole(event.roles, "nose") &&
        HasRole(event.roles, "foot");
}

bool IsProvisioningStartCombo(const ToyEvent& event)
{
    return HasProvisioningComboRoles(event) &&
        event.hold_duration >= kProvisioningHoldThreshold;
}

bool IsProvisioningExitCombo(const ToyEvent& event)
{
    return HasProvisioningComboRoles(event) &&
        event.hold_duration >= kProvisioningExitHoldThreshold;
}

ToyAction TouchAction(const std::string& role, bool long_press)
{
    if (role == "head") {
        return long_press ? ToyAction::kAffectionateHeadLongTouch
            : ToyAction::kHappyTouchHead;
    }
    if (role == "nose") {
        return long_press ? ToyAction::kNoseReject : ToyAction::kNoseSurprise;
    }
    if (role == "foot") {
        return long_press ? ToyAction::kFootExcited : ToyAction::kFootPlay;
    }
    return ToyAction::kNone;
}

}  // namespace

ToyEvent ToyEvent::BootComplete()
{
    ToyEvent event;
    event.type = ToyEventType::kBootComplete;
    return event;
}

ToyEvent ToyEvent::Wake(std::string source)
{
    ToyEvent event;
    event.type = ToyEventType::kWake;
    event.source = std::move(source);
    return event;
}

ToyEvent ToyEvent::ListeningStarted()
{
    ToyEvent event;
    event.type = ToyEventType::kListeningStarted;
    return event;
}

ToyEvent ToyEvent::ListeningTimeout()
{
    ToyEvent event;
    event.type = ToyEventType::kListeningTimeout;
    return event;
}

ToyEvent ToyEvent::StartProvisioning(std::string source)
{
    ToyEvent event;
    event.type = ToyEventType::kStartProvisioning;
    event.source = std::move(source);
    return event;
}

ToyEvent ToyEvent::ExitProvisioning()
{
    ToyEvent event;
    event.type = ToyEventType::kExitProvisioning;
    return event;
}

ToyEvent ToyEvent::WifiConnected()
{
    ToyEvent event;
    event.type = ToyEventType::kWifiConnected;
    return event;
}

ToyEvent ToyEvent::WifiDisconnected()
{
    ToyEvent event;
    event.type = ToyEventType::kWifiDisconnected;
    return event;
}

ToyEvent ToyEvent::WifiError()
{
    ToyEvent event;
    event.type = ToyEventType::kWifiError;
    return event;
}

ToyEvent ToyEvent::EnterContinuousConversation()
{
    ToyEvent event;
    event.type = ToyEventType::kEnterContinuousConversation;
    return event;
}

ToyEvent ToyEvent::ExitContinuousConversation(std::string source)
{
    ToyEvent event;
    event.type = ToyEventType::kExitContinuousConversation;
    event.source = std::move(source);
    return event;
}

ToyEvent ToyEvent::ConversationIdleTimeout()
{
    ToyEvent event;
    event.type = ToyEventType::kConversationIdleTimeout;
    return event;
}

ToyEvent ToyEvent::Sleep()
{
    ToyEvent event;
    event.type = ToyEventType::kSleep;
    return event;
}

ToyEvent ToyEvent::Touch(std::string role, bool long_press)
{
    ToyEvent event;
    event.type = ToyEventType::kTouch;
    event.role = std::move(role);
    event.long_press = long_press;
    return event;
}

ToyEvent ToyEvent::TouchCombo(std::vector<std::string> roles,
    std::chrono::milliseconds hold_duration)
{
    ToyEvent event;
    event.type = ToyEventType::kTouchCombo;
    event.roles = std::move(roles);
    event.hold_duration = hold_duration;
    return event;
}

ToyEvent ToyEvent::NfcDetected(std::string uid)
{
    ToyEvent event;
    event.type = ToyEventType::kNfcDetected;
    event.source = std::move(uid);
    return event;
}

ToyEvent ToyEvent::LightChanged(std::string level)
{
    ToyEvent event;
    event.type = ToyEventType::kLightChanged;
    event.source = std::move(level);
    return event;
}

ToyEvent ToyEvent::Power(PowerState state)
{
    ToyEvent event;
    event.type = ToyEventType::kPower;
    event.power_state = state;
    return event;
}

ToyEvent ToyEvent::MotionFall()
{
    ToyEvent event;
    event.type = ToyEventType::kMotionFall;
    return event;
}

ToyEvent ToyEvent::MotionShake()
{
    ToyEvent event;
    event.type = ToyEventType::kMotionShake;
    return event;
}

ToyReaction ToyStateMachine::React(ToyAction action, bool handled) const
{
    ToyReaction reaction;
    reaction.state = state_;
    reaction.wifi_state = wifi_state_;
    reaction.conversation_mode = conversation_mode_;
    reaction.action = action;
    reaction.handled = handled;
    return reaction;
}

ToyReaction ToyStateMachine::EnterState(ToyState state, ToyAction action, bool handled)
{
    state_ = state;
    return React(action, handled);
}

ToyReaction ToyStateMachine::HandleEvent(const ToyEvent& event)
{
    if (InSafeError()) {
        if (event.type == ToyEventType::kPower && event.power_state == PowerState::kNormal) {
            return EnterState(ToyState::kIdle, ToyAction::kIdle);
        }
        if (event.type == ToyEventType::kPower &&
                event.power_state == PowerState::kCharging) {
            return EnterState(ToyState::kIdle, ToyAction::kCharging);
        }
        if (event.type == ToyEventType::kPower && event.power_state == PowerState::kFull) {
            return EnterState(ToyState::kIdle, ToyAction::kPowerFull);
        }
        return React(ToyAction::kNone, false);
    }

    switch (event.type) {
    case ToyEventType::kBootComplete:
        return EnterState(ToyState::kIdle, ToyAction::kBootReady);
    case ToyEventType::kWake:
        return EnterState(ToyState::kListening, ToyAction::kWakeUp);
    case ToyEventType::kListeningStarted:
        return EnterState(ToyState::kListening, ToyAction::kListen);
    case ToyEventType::kListeningTimeout:
        if (conversation_mode_ == ConversationMode::kContinuous) {
            state_ = ToyState::kContinuousConversation;
            return React(ToyAction::kListen);
        }
        if (state_ != ToyState::kListening && state_ != ToyState::kAwake) {
            return React(ToyAction::kNone, false);
        }
        return EnterState(ToyState::kIdle, ToyAction::kIdle);
    case ToyEventType::kStartProvisioning:
        wifi_state_ = WifiState::kProvisioning;
        return EnterState(ToyState::kProvisioning, ToyAction::kStartProvisioning);
    case ToyEventType::kExitProvisioning:
        wifi_state_ = WifiState::kDisconnected;
        return EnterState(ToyState::kIdle, ToyAction::kIdle);
    case ToyEventType::kWifiConnected:
        wifi_state_ = WifiState::kConnected;
        return EnterState(ToyState::kIdle, ToyAction::kWifiConnected);
    case ToyEventType::kWifiDisconnected:
        wifi_state_ = WifiState::kDisconnected;
        return React(ToyAction::kNone);
    case ToyEventType::kWifiError:
        wifi_state_ = WifiState::kError;
        return EnterState(ToyState::kIdle, ToyAction::kWifiError);
    case ToyEventType::kEnterContinuousConversation:
        conversation_mode_ = ConversationMode::kContinuous;
        return EnterState(ToyState::kContinuousConversation,
            ToyAction::kConversationStart);
    case ToyEventType::kExitContinuousConversation:
        conversation_mode_ = ConversationMode::kWakeTriggered;
        return EnterState(ToyState::kIdle, ToyAction::kConversationStop);
    case ToyEventType::kConversationIdleTimeout:
        if (conversation_mode_ != ConversationMode::kContinuous &&
                state_ != ToyState::kContinuousConversation) {
            return React(ToyAction::kNone, false);
        }
        conversation_mode_ = ConversationMode::kWakeTriggered;
        return EnterState(ToyState::kIdle, ToyAction::kConversationStop);
    case ToyEventType::kSleep:
        conversation_mode_ = ConversationMode::kWakeTriggered;
        return EnterState(ToyState::kSleep, ToyAction::kSleep);
    case ToyEventType::kTouch: {
        const ToyAction action = TouchAction(event.role, event.long_press);
        if (action == ToyAction::kNone) {
            return React(ToyAction::kNone, false);
        }
        return EnterState(ToyState::kActing, action);
    }
    case ToyEventType::kTouchCombo:
        if (state_ == ToyState::kProvisioning && IsProvisioningExitCombo(event)) {
            wifi_state_ = WifiState::kDisconnected;
            return EnterState(ToyState::kIdle, ToyAction::kIdle);
        }
        if (IsProvisioningStartCombo(event)) {
            wifi_state_ = WifiState::kProvisioning;
            return EnterState(ToyState::kProvisioning, ToyAction::kStartProvisioning);
        }
        return React(ToyAction::kNone, false);
    case ToyEventType::kNfcDetected:
        return EnterState(ToyState::kActing, ToyAction::kNfcDetected);
    case ToyEventType::kLightChanged:
        if (state_ == ToyState::kProvisioning ||
                conversation_mode_ == ConversationMode::kContinuous) {
            return React(ToyAction::kNone);
        }
        if (event.source == "dark") {
            conversation_mode_ = ConversationMode::kWakeTriggered;
            return EnterState(ToyState::kSleep, ToyAction::kNightMode);
        }
        if (event.source == "bright") {
            return EnterState(ToyState::kIdle, ToyAction::kDayMode);
        }
        return React(ToyAction::kNone, false);
    case ToyEventType::kPower:
        if (event.power_state == PowerState::kLowBattery ||
                event.power_state == PowerState::kFault) {
            return EnterState(ToyState::kSafeError, ToyAction::kLowBattery);
        }
        if (event.power_state == PowerState::kCharging) {
            return EnterState(ToyState::kIdle, ToyAction::kCharging);
        }
        if (event.power_state == PowerState::kFull) {
            return EnterState(ToyState::kIdle, ToyAction::kPowerFull);
        }
        return React(ToyAction::kNone, false);
    case ToyEventType::kMotionFall:
        return EnterState(ToyState::kSafeError, ToyAction::kFallDetected);
    case ToyEventType::kMotionShake:
        return EnterState(ToyState::kActing, ToyAction::kShakeDetected);
    case ToyEventType::kUnknown:
    default:
        return React(ToyAction::kNone, false);
    }
}

const char* ToyStateName(ToyState state)
{
    switch (state) {
    case ToyState::kBooting: return "booting";
    case ToyState::kIdle: return "idle";
    case ToyState::kAwake: return "awake";
    case ToyState::kListening: return "listening";
    case ToyState::kThinking: return "thinking";
    case ToyState::kSpeaking: return "speaking";
    case ToyState::kActing: return "acting";
    case ToyState::kContinuousConversation: return "continuous_conversation";
    case ToyState::kProvisioning: return "provisioning";
    case ToyState::kSleep: return "sleep";
    case ToyState::kSafeError: return "safe_error";
    }
    return "unknown";
}

const char* WifiStateName(WifiState state)
{
    switch (state) {
    case WifiState::kUnconfigured: return "unconfigured";
    case WifiState::kConnecting: return "connecting";
    case WifiState::kConnected: return "connected";
    case WifiState::kDisconnected: return "disconnected";
    case WifiState::kProvisioning: return "provisioning";
    case WifiState::kError: return "error";
    }
    return "unknown";
}

const char* ConversationModeName(ConversationMode mode)
{
    switch (mode) {
    case ConversationMode::kWakeTriggered: return "wake_triggered";
    case ConversationMode::kContinuous: return "continuous";
    }
    return "unknown";
}

const char* ToyActionName(ToyAction action)
{
    switch (action) {
    case ToyAction::kNone: return "none";
    case ToyAction::kBootReady: return "boot_ready";
    case ToyAction::kIdle: return "idle";
    case ToyAction::kWakeUp: return "wake_up";
    case ToyAction::kListen: return "listen";
    case ToyAction::kStartProvisioning: return "start_provisioning";
    case ToyAction::kWifiConnected: return "wifi_connected";
    case ToyAction::kWifiError: return "wifi_error";
    case ToyAction::kConversationStart: return "conversation_start";
    case ToyAction::kConversationStop: return "conversation_stop";
    case ToyAction::kSleep: return "sleep";
    case ToyAction::kHappyTouchHead: return "happy_touch_head";
    case ToyAction::kAffectionateHeadLongTouch: return "affectionate_head_long_touch";
    case ToyAction::kNoseSurprise: return "nose_surprise";
    case ToyAction::kNoseReject: return "nose_reject";
    case ToyAction::kFootPlay: return "foot_play";
    case ToyAction::kFootExcited: return "foot_excited";
    case ToyAction::kNfcDetected: return "nfc_detected";
    case ToyAction::kNightMode: return "night_mode";
    case ToyAction::kDayMode: return "day_mode";
    case ToyAction::kCharging: return "charging";
    case ToyAction::kPowerFull: return "power_full";
    case ToyAction::kLowBattery: return "low_battery";
    case ToyAction::kFallDetected: return "fall_detected";
    case ToyAction::kShakeDetected: return "shake_detected";
    }
    return "unknown";
}

}  // namespace ai_cubpet
