#ifndef CUBPET_VOICE_INTERACTION_GATE_HPP
#define CUBPET_VOICE_INTERACTION_GATE_HPP

#include "cubpet_keywords.hpp"
#include "cubpet_toy_state_machine.hpp"

#include <chrono>  // NOLINT(build/c++11)

namespace ai_cubpet {

class VoiceInteractionGate {
public:
    explicit VoiceInteractionGate(std::chrono::milliseconds listening_window);

    bool AllowsVad(const ToyStateMachine& machine,
        std::chrono::steady_clock::time_point now) const;
    bool AllowsAsr(const ToyStateMachine& machine,
        std::chrono::steady_clock::time_point now) const;
    bool AllowsProductCommand(const ToyStateMachine& machine,
        ProductCommand command,
        std::chrono::steady_clock::time_point now) const;
    bool AllowsVoiceIntent(const ToyStateMachine& machine,
        VoiceIntent intent,
        std::chrono::steady_clock::time_point now) const;

    void NotifyWake(std::chrono::steady_clock::time_point now);
    void NotifyProductCommandHandled(ProductCommand command,
        std::chrono::steady_clock::time_point now);
    void NotifyVoiceIntentHandled(std::chrono::steady_clock::time_point now);

private:
    bool InContinuousMode(const ToyStateMachine& machine) const;
    bool WakeSessionOpen(std::chrono::steady_clock::time_point now) const;
    void RefreshWakeSession(std::chrono::steady_clock::time_point now);
    void CloseWakeSession();

    std::chrono::milliseconds listening_window_;
    bool wake_session_open_ = false;
    std::chrono::steady_clock::time_point wake_session_deadline_{};
};

}  // namespace ai_cubpet

#endif  // CUBPET_VOICE_INTERACTION_GATE_HPP
