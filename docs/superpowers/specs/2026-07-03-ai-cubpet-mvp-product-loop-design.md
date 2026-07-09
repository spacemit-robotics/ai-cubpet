# AI Cubpet MVP Product Loop Design

## Goal

Implement the V0.1 product loop as a maintainable runtime skeleton: unified product states, peripheral events, action templates, and voice-mode commands, while keeping `examples/peripherals.c` as the standalone peripheral validation tool.

## Scope

This pass builds the product-layer foundation on top of the current voice demo. It does not remove or rewrite the existing peripheral CLI sample. Hardware-specific calls remain in existing modules or behind narrow abstractions so the state machine can be unit tested without MUSE-Pi-Pro hardware.

## Architecture

The implementation adds three product-layer units:

- `ToyStateMachine`: pure C++ state and mode transition logic for boot, idle, awake, listening, thinking, speaking, acting, provisioning, continuous conversation, sleep, and safe/error.
- `ActionController`: maps state-machine action templates to existing UI, audio, and motor behavior. It reuses `CubpetMotorActions` for motor execution.
- `PeripheralManager`: owns board configuration and converts product-facing peripheral facts into normalized events. The first pass exposes testable event construction and role lookup, with room to add hardware polling threads later.

Voice transcripts continue to flow through `cubpet_keywords`, but product commands such as provisioning and conversation-mode entry produce product events instead of simple motor-only actions.

## Data Flow

```text
ASR transcript / touch / wake / NFC / light / motion / power
    -> PeripheralManager or keyword mapper
    -> ToyStateMachine::HandleEvent()
    -> ToyReaction(action template, next state, mode, wifi state)
    -> ActionController::Execute()
    -> UI DDS / local audio / CubpetMotorActions / log-only fallback
```

## MVP Behavior

- Boot moves to `Idle`.
- Wake events move `Idle` or `Sleep` to `Awake`, then listening can be entered explicitly.
- Listening timeout returns to `Idle` unless continuous conversation is active.
- Voice provisioning commands enter `Provisioning` and `WiFiProvisioning`; exit commands return to offline `Idle`.
- Wi-Fi connected returns to `Idle` with `WiFiConnected`.
- Continuous conversation commands enter and exit `ContinuousConversation`.
- Touch events map `head`, `nose`, and `foot` to product action templates.
- Three-touch long hold for 10 seconds enters provisioning and suppresses individual touch feedback.
- Low battery and fall events enter `SafeError` and suppress normal entertainment actions.

## Configuration

`config/boards/MUSE-Pi-Pro.json` will use product roles for the three touch inputs:

```json
"touch1": "head",
"touch2": "nose",
"touch3": "foot"
```

The existing config parser already preserves `role`, so the change is in data and tests.

## Error Handling

The state machine is pure and cannot fail. `PeripheralManager` reports config load failure with an error code and leaves hardware-dependent execution disabled. `ActionController` treats UI/audio/motor failures as non-fatal and returns a result containing which channels ran.

## Testing

Add focused unit tests for:

- board role mapping to `head`, `nose`, `foot`;
- state-machine boot, wake/listening timeout, provisioning, continuous conversation, safe/error priority;
- touch action and three-touch provisioning behavior;
- keyword mapping for provisioning and conversation-mode commands.

The tests build without external hardware by linking only product-layer sources and config parsing.
