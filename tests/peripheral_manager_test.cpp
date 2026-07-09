#include "cubpet_peripheral_manager.hpp"

#include <cassert>
#include <chrono>  // NOLINT(build/c++11)
#include <iostream>
#include <string>

namespace {

using ai_cubpet::PeripheralEventType;
using ai_cubpet::PeripheralManager;

void TestLoadAndRoleLookup()
{
    PeripheralManager manager;
    const int rc = manager.LoadForModel("spacemit k1-x MUSE-Pi-Pro board",
        "tests/fixtures/boards");
    assert(rc == 0);
    assert(std::string(manager.config().board_name) == "MUSE-Pi-Pro");

    const auto* head = manager.FindGpioByRole("head");
    const auto* nose = manager.FindGpioByRole("nose");
    const auto* foot = manager.FindGpioByRole("foot");
    const auto* wake = manager.FindGpioByRole("wake");

    assert(head != nullptr);
    assert(nose != nullptr);
    assert(foot != nullptr);
    assert(wake != nullptr);
    assert(std::string(head->name) == "touch1");
    assert(std::string(nose->name) == "touch2");
    assert(std::string(foot->name) == "touch3");
}

void TestTouchEvents()
{
    PeripheralManager manager;
    assert(manager.LoadForModel("spacemit k1-x MUSE-Pi-Pro board",
        "tests/fixtures/boards") == 0);

    auto event = manager.MakeTouchEvent("head", false);
    assert(event.type == PeripheralEventType::kTouch);
    assert(event.role == "head");
    assert(!event.long_press);

    event = manager.MakeTouchEvent("nose", true);
    assert(event.type == PeripheralEventType::kTouch);
    assert(event.role == "nose");
    assert(event.long_press);

    event = manager.MakeWakeEvent("gpio");
    assert(event.type == PeripheralEventType::kWake);
    assert(event.source == "gpio");
}

void TestComboEvent()
{
    PeripheralManager manager;
    assert(manager.LoadForModel("spacemit k1-x MUSE-Pi-Pro board",
        "tests/fixtures/boards") == 0);

    auto event = manager.MakeTouchComboEvent({"head", "nose", "foot"},
        std::chrono::seconds(10));
    assert(event.type == PeripheralEventType::kTouchCombo);
    assert(event.roles.size() == 3);
    assert(event.hold_duration == std::chrono::seconds(10));
}

void TestEnvironmentEvents()
{
    PeripheralManager manager;
    assert(manager.LoadForModel("spacemit k1-x MUSE-Pi-Pro board",
        "tests/fixtures/boards") == 0);

    auto event = manager.MakeNfcEvent("04AABBCC");
    assert(event.type == PeripheralEventType::kNfc);
    assert(event.value == "04AABBCC");

    event = manager.MakeLightEvent("dark");
    assert(event.type == PeripheralEventType::kLight);
    assert(event.value == "dark");

    event = manager.MakePowerEvent("charging");
    assert(event.type == PeripheralEventType::kPower);
    assert(event.value == "charging");

    event = manager.MakeMotionEvent("fall");
    assert(event.type == PeripheralEventType::kMotion);
    assert(event.value == "fall");
}

}  // namespace

int main()
{
    TestLoadAndRoleLookup();
    TestTouchEvents();
    TestComboEvent();
    TestEnvironmentEvents();
    std::cout << "peripheral manager tests passed\n";
    return 0;
}
