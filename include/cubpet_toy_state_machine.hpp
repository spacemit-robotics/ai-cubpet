#ifndef CUBPET_TOY_STATE_MACHINE_HPP
#define CUBPET_TOY_STATE_MACHINE_HPP

#include <chrono>  // NOLINT(build/c++11)
#include <string>
#include <vector>

namespace ai_cubpet {

enum class ToyState {
    kBooting,
    kIdle,
    kAwake,
    kListening,
    kThinking,
    kSpeaking,
    kActing,
    kContinuousConversation,
    kProvisioning,
    kSleep,
    kSafeError,
};

enum class WifiState {
    kUnconfigured,
    kConnecting,
    kConnected,
    kDisconnected,
    kProvisioning,
    kError,
};

enum class ConversationMode {
    kWakeTriggered,
    kContinuous,
};

enum class PowerState {
    kNormal,
    kLowBattery,
    kCharging,
    kFull,
    kFault,
};

enum class ToyEventType {
    kUnknown,
    kBootComplete,
    kWake,
    kListeningStarted,
    kListeningTimeout,
    kStartProvisioning,
    kExitProvisioning,
    kWifiConnected,
    kWifiDisconnected,
    kWifiError,
    kEnterContinuousConversation,
    kExitContinuousConversation,
    kConversationIdleTimeout,
    kSleep,
    kTouch,
    kTouchCombo,
    kNfcDetected,
    kLightChanged,
    kPower,
    kMotionFall,
    kMotionShake,
};

enum class ToyAction {
    kNone,
    kBootReady,
    kIdle,
    kWakeUp,
    kListen,
    kStartProvisioning,
    kWifiConnected,
    kWifiError,
    kConversationStart,
    kConversationStop,
    kSleep,
    kHappyTouchHead,
    kAffectionateHeadLongTouch,
    kNoseSurprise,
    kNoseReject,
    kFootPlay,
    kFootExcited,
    kNfcDetected,
    kNightMode,
    kDayMode,
    kCharging,
    kPowerFull,
    kLowBattery,
    kFallDetected,
    kShakeDetected,
};

struct ToyEvent {
    ToyEventType type = ToyEventType::kUnknown;
    std::string source;
    std::string role;
    std::vector<std::string> roles;
    bool long_press = false;
    std::chrono::milliseconds hold_duration{0};
    PowerState power_state = PowerState::kNormal;

    static ToyEvent BootComplete();
    static ToyEvent Wake(std::string source);
    static ToyEvent ListeningStarted();
    static ToyEvent ListeningTimeout();
    static ToyEvent StartProvisioning(std::string source);
    static ToyEvent ExitProvisioning();
    static ToyEvent WifiConnected();
    static ToyEvent WifiDisconnected();
    static ToyEvent WifiError();
    static ToyEvent EnterContinuousConversation();
    static ToyEvent ExitContinuousConversation(std::string source);
    static ToyEvent ConversationIdleTimeout();
    static ToyEvent Sleep();
    static ToyEvent Touch(std::string role, bool long_press);
    static ToyEvent TouchCombo(std::vector<std::string> roles,
        std::chrono::milliseconds hold_duration);
    static ToyEvent NfcDetected(std::string uid);
    static ToyEvent LightChanged(std::string level);
    static ToyEvent Power(PowerState state);
    static ToyEvent MotionFall();
    static ToyEvent MotionShake();
};

struct ToyReaction {
    ToyState state = ToyState::kBooting;
    WifiState wifi_state = WifiState::kUnconfigured;
    ConversationMode conversation_mode = ConversationMode::kWakeTriggered;
    ToyAction action = ToyAction::kNone;
    bool handled = false;
};

class ToyStateMachine {
public:
    ToyStateMachine() = default;

    ToyReaction HandleEvent(const ToyEvent& event);

    ToyState state() const { return state_; }
    WifiState wifi_state() const { return wifi_state_; }
    ConversationMode conversation_mode() const { return conversation_mode_; }

private:
    ToyReaction React(ToyAction action, bool handled = true) const;
    ToyReaction EnterState(ToyState state, ToyAction action, bool handled = true);
    bool InSafeError() const { return state_ == ToyState::kSafeError; }

    ToyState state_ = ToyState::kBooting;
    WifiState wifi_state_ = WifiState::kUnconfigured;
    ConversationMode conversation_mode_ = ConversationMode::kWakeTriggered;
};

const char* ToyStateName(ToyState state);
const char* WifiStateName(WifiState state);
const char* ConversationModeName(ConversationMode mode);
const char* ToyActionName(ToyAction action);

}  // namespace ai_cubpet

#endif  // CUBPET_TOY_STATE_MACHINE_HPP
