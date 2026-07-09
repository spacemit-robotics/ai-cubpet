#include "cubpet_voice_interaction_gate.hpp"

namespace ai_cubpet {

VoiceInteractionGate::VoiceInteractionGate(std::chrono::milliseconds listening_window)
    : listening_window_(listening_window)
{
}

bool VoiceInteractionGate::AllowsAsr(const ToyStateMachine& machine,
    std::chrono::steady_clock::time_point now) const
{
    if (InContinuousMode(machine)) {
        return true;
    }
    return WakeSessionOpen(now);
}

bool VoiceInteractionGate::AllowsVad(const ToyStateMachine& machine,
    std::chrono::steady_clock::time_point now) const
{
    return AllowsAsr(machine, now);
}

bool VoiceInteractionGate::AllowsProductCommand(const ToyStateMachine& machine,
    ProductCommand command,
    std::chrono::steady_clock::time_point now) const
{
    if (command == ProductCommand::kUnknown) {
        return false;
    }
    if (InContinuousMode(machine)) {
        return true;
    }
    return WakeSessionOpen(now);
}

bool VoiceInteractionGate::AllowsVoiceIntent(const ToyStateMachine& machine,
    VoiceIntent intent,
    std::chrono::steady_clock::time_point now) const
{
    if (intent == VoiceIntent::kUnknown) {
        return false;
    }
    if (InContinuousMode(machine)) {
        return true;
    }
    return WakeSessionOpen(now);
}

void VoiceInteractionGate::NotifyWake(std::chrono::steady_clock::time_point now)
{
    RefreshWakeSession(now);
}

void VoiceInteractionGate::NotifyProductCommandHandled(ProductCommand command,
    std::chrono::steady_clock::time_point now)
{
    switch (command) {
    case ProductCommand::kEnterContinuousConversation:
    case ProductCommand::kExitContinuousConversation:
    case ProductCommand::kSleep:
        CloseWakeSession();
        break;
    case ProductCommand::kStartProvisioning:
    case ProductCommand::kExitProvisioning:
        RefreshWakeSession(now);
        break;
    case ProductCommand::kUnknown:
    default:
        break;
    }
}

void VoiceInteractionGate::NotifyVoiceIntentHandled(
    std::chrono::steady_clock::time_point now)
{
    RefreshWakeSession(now);
}

bool VoiceInteractionGate::InContinuousMode(const ToyStateMachine& machine) const
{
    return machine.conversation_mode() == ConversationMode::kContinuous ||
        machine.state() == ToyState::kContinuousConversation;
}

bool VoiceInteractionGate::WakeSessionOpen(
    std::chrono::steady_clock::time_point now) const
{
    return wake_session_open_ && now <= wake_session_deadline_;
}

void VoiceInteractionGate::RefreshWakeSession(
    std::chrono::steady_clock::time_point now)
{
    wake_session_open_ = true;
    wake_session_deadline_ = now + listening_window_;
}

void VoiceInteractionGate::CloseWakeSession()
{
    wake_session_open_ = false;
}

}  // namespace ai_cubpet
