# AI Cubpet MVP Product Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the V0.1 product-loop foundation with state-machine, action-template, peripheral-event, and voice-command integration while leaving `examples/peripherals.c` unchanged.

**Architecture:** Add pure product logic first, then wire it into the current voice demo. Hardware integration stays behind `PeripheralManager` and existing action modules so tests can run on the build host without MUSE-Pi-Pro peripherals.

**Tech Stack:** C++17, CMake, existing `cubpet_peripheral_config`, existing DDS UI publisher, existing `CubpetMotorActions`, assert-based unit tests.

---

### Task 1: Product State Machine

**Files:**
- Create: `include/cubpet_toy_state_machine.hpp`
- Create: `src/cubpet_toy_state_machine.cpp`
- Create: `tests/toy_state_machine_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Cover boot to idle, wake/listening timeout, provisioning, continuous conversation, touch actions, combo provisioning, and safe priority.

- [ ] **Step 2: Verify tests fail**

Run: `cmake --build /tmp/ai-cubpet-test --target toy_state_machine_test`

Expected: fail because the target and headers do not exist.

- [ ] **Step 3: Implement minimal state machine**

Define `ToyState`, `WifiState`, `ConversationMode`, `ToyEvent`, `ToyAction`, and `ToyReaction`. Implement `ToyStateMachine::HandleEvent()` with deterministic transitions and no hardware dependencies.

- [ ] **Step 4: Verify tests pass**

Run: `/tmp/ai-cubpet-test/bin/toy_state_machine_test`

Expected: `toy state machine tests passed`.

### Task 2: Product Intent Mapping

**Files:**
- Modify: `include/cubpet_keywords.hpp`
- Modify: `src/cubpet_keywords.cpp`
- Create: `tests/keywords_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing keyword tests**

Cover existing motor commands plus `开始配网`, `退出配网`, `进入连续对话`, `陪我聊天`, `退出聊天`, `休息一下`, `去睡觉`, and wake words.

- [ ] **Step 2: Verify tests fail**

Run: `/tmp/ai-cubpet-test/bin/keywords_test`

Expected: fail because product command APIs are missing.

- [ ] **Step 3: Add product command matching**

Keep `VoiceIntent` intact. Add `ProductCommand`, `MatchProductCommand()`, and `ProductCommandName()`.

- [ ] **Step 4: Verify tests pass**

Run: `/tmp/ai-cubpet-test/bin/keywords_test`

Expected: `keyword tests passed`.

### Task 3: Peripheral Event Abstraction

**Files:**
- Create: `include/cubpet_peripheral_manager.hpp`
- Create: `src/cubpet_peripheral_manager.cpp`
- Create: `tests/peripheral_manager_test.cpp`
- Modify: `config/boards/MUSE-Pi-Pro.json`
- Modify: `tests/fixtures/boards/MUSE-Pi-Pro.json`
- Modify: `tests/peripheral_config_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Cover role lookup for `head`, `nose`, `foot`, `wake`, touch event construction, and three-touch combo conversion.

- [ ] **Step 2: Verify tests fail**

Run: `/tmp/ai-cubpet-test/bin/peripheral_manager_test`

Expected: fail because `PeripheralManager` is missing and config roles are still old.

- [ ] **Step 3: Implement abstraction and role config update**

Add `PeripheralManager` with config loading, `FindGpioByRole()`, touch event helpers, and combo helper. Update board JSON roles.

- [ ] **Step 4: Verify tests pass**

Run: `/tmp/ai-cubpet-test/bin/peripheral_manager_test`

Expected: `peripheral manager tests passed`.

### Task 4: Action Controller

**Files:**
- Create: `include/cubpet_action_controller.hpp`
- Create: `src/cubpet_action_controller.cpp`
- Create: `tests/action_controller_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Cover action template to media/intent mapping without requiring DDS or motor hardware.

- [ ] **Step 2: Verify tests fail**

Run: `/tmp/ai-cubpet-test/bin/action_controller_test`

Expected: fail because `ActionController` is missing.

- [ ] **Step 3: Implement action mapping**

Add static `ResolveActionMedia()` and execution result types. Runtime execution may call optional UI publisher and motor actions when compiled into the voice binary.

- [ ] **Step 4: Verify tests pass**

Run: `/tmp/ai-cubpet-test/bin/action_controller_test`

Expected: `action controller tests passed`.

### Task 5: Voice Demo Integration

**Files:**
- Modify: `src/cubpet_voice_demo.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing integration-oriented test or compile check**

Ensure product sources are part of the `ai-cubpet` target and test product commands map to state-machine events.

- [ ] **Step 2: Wire command flow**

In ASR result handling, check `MatchProductCommand()` before motor-only `MatchVoiceIntent()`. Product commands feed `ToyStateMachine`, then `ActionController` resolves UI/audio/motor actions.

- [ ] **Step 3: Preserve existing motor commands**

Keep existing `VoiceIntent` command behavior for `抬头`, `点头`, `摇头`, and `摇尾巴`.

- [ ] **Step 4: Verify build/tests**

Run unit tests and a host build with voice/UI/peripheral demos disabled where dependencies are unavailable.

Expected: product-layer tests pass and no source changes touch `examples/peripherals.c`.
