#include "cubpet_toy_state_machine.hpp"

#include <cassert>
#include <chrono>  // NOLINT(build/c++11)
#include <iostream>

namespace {

using ai_cubpet::ConversationMode;
using ai_cubpet::PowerState;
using ai_cubpet::ToyAction;
using ai_cubpet::ToyEvent;
using ai_cubpet::ToyState;
using ai_cubpet::ToyStateMachine;
using ai_cubpet::WifiState;

void TestBootWakeListeningTimeout()
{
    ToyStateMachine machine;
    assert(machine.state() == ToyState::kBooting);

    auto reaction = machine.HandleEvent(ToyEvent::BootComplete());
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kBootReady);

    reaction = machine.HandleEvent(ToyEvent::Wake("gpio"));
    assert(reaction.state == ToyState::kListening);
    assert(reaction.action == ToyAction::kWakeUp);

    reaction = machine.HandleEvent(ToyEvent::ListeningStarted());
    assert(reaction.state == ToyState::kListening);
    assert(reaction.action == ToyAction::kListen);

    reaction = machine.HandleEvent(ToyEvent::ListeningTimeout());
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kIdle);
}

void TestProvisioningPath()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());

    auto reaction = machine.HandleEvent(ToyEvent::StartProvisioning("voice"));
    assert(reaction.state == ToyState::kProvisioning);
    assert(reaction.wifi_state == WifiState::kProvisioning);
    assert(reaction.action == ToyAction::kStartProvisioning);

    reaction = machine.HandleEvent(ToyEvent::WifiConnected());
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.wifi_state == WifiState::kConnected);
    assert(reaction.action == ToyAction::kWifiConnected);

    reaction = machine.HandleEvent(ToyEvent::StartProvisioning("voice"));
    assert(reaction.state == ToyState::kProvisioning);
    reaction = machine.HandleEvent(ToyEvent::ListeningTimeout());
    assert(reaction.state == ToyState::kProvisioning);
    assert(reaction.wifi_state == WifiState::kProvisioning);
    assert(reaction.action == ToyAction::kNone);

    reaction = machine.HandleEvent(ToyEvent::ExitProvisioning());
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.wifi_state == WifiState::kDisconnected);
    assert(reaction.action == ToyAction::kIdle);
}

void TestContinuousConversation()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());

    auto reaction = machine.HandleEvent(ToyEvent::EnterContinuousConversation());
    assert(reaction.state == ToyState::kContinuousConversation);
    assert(reaction.conversation_mode == ConversationMode::kContinuous);
    assert(reaction.action == ToyAction::kConversationStart);

    reaction = machine.HandleEvent(ToyEvent::ListeningTimeout());
    assert(reaction.state == ToyState::kContinuousConversation);
    assert(reaction.action == ToyAction::kListen);

    reaction = machine.HandleEvent(ToyEvent::ExitContinuousConversation("voice"));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.conversation_mode == ConversationMode::kWakeTriggered);
    assert(reaction.action == ToyAction::kConversationStop);
}

void TestContinuousConversationIdleTimeout()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());
    machine.HandleEvent(ToyEvent::EnterContinuousConversation());

    auto reaction = machine.HandleEvent(ToyEvent::ConversationIdleTimeout());
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.conversation_mode == ConversationMode::kWakeTriggered);
    assert(reaction.action == ToyAction::kConversationStop);
}

void TestTouchAndCombo()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());

    auto reaction = machine.HandleEvent(ToyEvent::Touch("head", false));
    assert(reaction.state == ToyState::kActing);
    assert(reaction.action == ToyAction::kHappyTouchHead);

    reaction = machine.HandleEvent(ToyEvent::Touch("nose", true));
    assert(reaction.state == ToyState::kActing);
    assert(reaction.action == ToyAction::kNoseReject);

    reaction = machine.HandleEvent(ToyEvent::Touch("foot", false));
    assert(reaction.state == ToyState::kActing);
    assert(reaction.action == ToyAction::kFootPlay);

    reaction = machine.HandleEvent(ToyEvent::TouchCombo({"head", "nose", "foot"},
        std::chrono::seconds(9)));
    assert(reaction.state == ToyState::kActing);
    assert(reaction.action == ToyAction::kNone);

    reaction = machine.HandleEvent(ToyEvent::TouchCombo({"head", "nose", "foot"},
        std::chrono::seconds(10)));
    assert(reaction.state == ToyState::kProvisioning);
    assert(reaction.action == ToyAction::kStartProvisioning);

    reaction = machine.HandleEvent(ToyEvent::TouchCombo({"head", "nose", "foot"},
        std::chrono::seconds(3)));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.wifi_state == WifiState::kDisconnected);
    assert(reaction.action == ToyAction::kIdle);
}

void TestSafePriority()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());

    auto reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kLowBattery));
    assert(reaction.state == ToyState::kSafeError);
    assert(reaction.action == ToyAction::kLowBattery);

    reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kCharging));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kCharging);

    reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kLowBattery));
    assert(reaction.state == ToyState::kSafeError);
    assert(reaction.action == ToyAction::kLowBattery);

    reaction = machine.HandleEvent(ToyEvent::Touch("head", false));
    assert(reaction.state == ToyState::kSafeError);
    assert(reaction.action == ToyAction::kNone);

    reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kNormal));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kIdle);

    reaction = machine.HandleEvent(ToyEvent::MotionFall());
    assert(reaction.state == ToyState::kSafeError);
    assert(reaction.action == ToyAction::kFallDetected);

    reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kNormal));
    assert(reaction.state == ToyState::kIdle);
    reaction = machine.HandleEvent(ToyEvent::MotionShake());
    assert(reaction.state == ToyState::kActing);
    assert(reaction.action == ToyAction::kShakeDetected);
}

void TestNfcLightAndPowerEvents()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());

    auto reaction = machine.HandleEvent(ToyEvent::NfcDetected("04AABBCC"));
    assert(reaction.state == ToyState::kActing);
    assert(reaction.action == ToyAction::kNfcDetected);

    reaction = machine.HandleEvent(ToyEvent::LightChanged("dark"));
    assert(reaction.state == ToyState::kSleep);
    assert(reaction.action == ToyAction::kNightMode);

    reaction = machine.HandleEvent(ToyEvent::LightChanged("bright"));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kDayMode);

    reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kCharging));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kCharging);

    reaction = machine.HandleEvent(ToyEvent::Power(PowerState::kFull));
    assert(reaction.state == ToyState::kIdle);
    assert(reaction.action == ToyAction::kPowerFull);
}

void TestLightChangesDoNotInterruptProtectedModes()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());

    machine.HandleEvent(ToyEvent::StartProvisioning("voice"));
    auto reaction = machine.HandleEvent(ToyEvent::LightChanged("dark"));
    assert(reaction.state == ToyState::kProvisioning);
    assert(reaction.wifi_state == WifiState::kProvisioning);
    assert(reaction.action == ToyAction::kNone);
    assert(reaction.handled);

    reaction = machine.HandleEvent(ToyEvent::LightChanged("bright"));
    assert(reaction.state == ToyState::kProvisioning);
    assert(reaction.wifi_state == WifiState::kProvisioning);
    assert(reaction.action == ToyAction::kNone);
    assert(reaction.handled);

    machine.HandleEvent(ToyEvent::ExitProvisioning());
    machine.HandleEvent(ToyEvent::EnterContinuousConversation());
    reaction = machine.HandleEvent(ToyEvent::LightChanged("dark"));
    assert(reaction.state == ToyState::kContinuousConversation);
    assert(reaction.conversation_mode == ConversationMode::kContinuous);
    assert(reaction.action == ToyAction::kNone);
    assert(reaction.handled);

    reaction = machine.HandleEvent(ToyEvent::LightChanged("bright"));
    assert(reaction.state == ToyState::kContinuousConversation);
    assert(reaction.conversation_mode == ConversationMode::kContinuous);
    assert(reaction.action == ToyAction::kNone);
    assert(reaction.handled);
}

}  // namespace

int main()
{
    TestBootWakeListeningTimeout();
    TestProvisioningPath();
    TestContinuousConversation();
    TestContinuousConversationIdleTimeout();
    TestTouchAndCombo();
    TestSafePriority();
    TestNfcLightAndPowerEvents();
    TestLightChangesDoNotInterruptProtectedModes();
    std::cout << "toy state machine tests passed\n";
    return 0;
}
