#include "cubpet_voice_interaction_gate.hpp"

#include <cassert>
#include <chrono>  // NOLINT(build/c++11)
#include <iostream>

namespace {

using ai_cubpet::ProductCommand;
using ai_cubpet::ToyEvent;
using ai_cubpet::ToyStateMachine;
using ai_cubpet::VoiceInteractionGate;
using ai_cubpet::VoiceIntent;

void TestIdleBlocksVadAsrAndCommands()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());
    VoiceInteractionGate gate(std::chrono::seconds(10));
    const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

    assert(!gate.AllowsVad(machine, now));
    assert(!gate.AllowsAsr(machine, now));
    assert(!gate.AllowsProductCommand(machine, ProductCommand::kEnterContinuousConversation, now));
    assert(!gate.AllowsVoiceIntent(machine, VoiceIntent::kNodHead, now));
    assert(!gate.AllowsProductCommand(machine, ProductCommand::kWake, now));
}

void TestHardwareWakeOpensListeningWindow()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());
    VoiceInteractionGate gate(std::chrono::seconds(10));
    const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

    machine.HandleEvent(ToyEvent::Wake("gpio"));
    gate.NotifyWake(now);

    assert(gate.AllowsVad(machine, now + std::chrono::seconds(9)));
    assert(gate.AllowsAsr(machine, now + std::chrono::seconds(9)));
    assert(gate.AllowsProductCommand(
        machine, ProductCommand::kEnterContinuousConversation, now + std::chrono::seconds(9)));
    assert(gate.AllowsVoiceIntent(
        machine, VoiceIntent::kNodHead, now + std::chrono::seconds(9)));
    assert(!gate.AllowsVad(machine, now + std::chrono::seconds(11)));
    assert(!gate.AllowsAsr(machine, now + std::chrono::seconds(11)));
    assert(!gate.AllowsVoiceIntent(
        machine, VoiceIntent::kNodHead, now + std::chrono::seconds(11)));
}

void TestContinuousModeStaysOpen()
{
    ToyStateMachine machine;
    machine.HandleEvent(ToyEvent::BootComplete());
    VoiceInteractionGate gate(std::chrono::seconds(10));
    const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

    machine.HandleEvent(ToyEvent::Wake("gpio"));
    gate.NotifyWake(now);
    machine.HandleEvent(ToyEvent::EnterContinuousConversation());
    gate.NotifyProductCommandHandled(ProductCommand::kEnterContinuousConversation, now);

    assert(gate.AllowsVad(machine, now + std::chrono::minutes(10)));
    assert(gate.AllowsAsr(machine, now + std::chrono::minutes(10)));
    assert(gate.AllowsVoiceIntent(
        machine, VoiceIntent::kShakeHead, now + std::chrono::minutes(10)));
    assert(gate.AllowsProductCommand(
        machine, ProductCommand::kExitContinuousConversation, now + std::chrono::minutes(10)));
}

}  // namespace

int main()
{
    TestIdleBlocksVadAsrAndCommands();
    TestHardwareWakeOpensListeningWindow();
    TestContinuousModeStaysOpen();
    std::cout << "voice interaction gate tests passed" << std::endl;
    return 0;
}
