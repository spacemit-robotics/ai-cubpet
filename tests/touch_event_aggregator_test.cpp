#include "cubpet_touch_event_aggregator.hpp"

#include <cassert>
#include <chrono>  // NOLINT(build/c++11)
#include <iostream>
#include <string>

namespace {

using ai_cubpet::PeripheralEventType;
using ai_cubpet::TouchEventAggregator;

TouchEventAggregator MakeAggregator()
{
    return TouchEventAggregator({"head", "nose", "foot"},
        std::chrono::milliseconds(800), std::chrono::seconds(3));
}

void TestShortAndLongTouch()
{
    auto aggregator = MakeAggregator();
    const auto now = TouchEventAggregator::Clock::time_point(std::chrono::seconds(100));

    auto events = aggregator.OnActive("head", now);
    assert(events.empty());
    events = aggregator.OnInactive("head", now + std::chrono::milliseconds(200));
    assert(events.size() == 1);
    assert(events[0].type == PeripheralEventType::kTouch);
    assert(events[0].role == "head");
    assert(!events[0].long_press);

    events = aggregator.OnActive("nose", now + std::chrono::seconds(1));
    assert(events.empty());
    events = aggregator.OnInactive("nose", now + std::chrono::seconds(2));
    assert(events.size() == 1);
    assert(events[0].type == PeripheralEventType::kTouch);
    assert(events[0].role == "nose");
    assert(events[0].long_press);
}

void TestComboEventAndSuppressesTouches()
{
    auto aggregator = MakeAggregator();
    const auto now = TouchEventAggregator::Clock::time_point(std::chrono::seconds(100));

    aggregator.OnActive("head", now);
    aggregator.OnActive("nose", now + std::chrono::milliseconds(20));
    aggregator.OnActive("foot", now + std::chrono::milliseconds(40));

    auto events = aggregator.OnInactive("head", now + std::chrono::seconds(11));
    assert(events.size() == 1);
    assert(events[0].type == PeripheralEventType::kTouchCombo);
    assert(events[0].roles.size() == 3);
    assert(events[0].hold_duration >= std::chrono::seconds(10));

    events = aggregator.OnInactive("nose", now + std::chrono::seconds(11));
    assert(events.empty());
    events = aggregator.OnInactive("foot", now + std::chrono::seconds(11));
    assert(events.empty());
}

void TestShortComboSuppressesTouches()
{
    auto aggregator = MakeAggregator();
    const auto now = TouchEventAggregator::Clock::time_point(std::chrono::seconds(100));

    aggregator.OnActive("head", now);
    aggregator.OnActive("nose", now);
    aggregator.OnActive("foot", now);

    auto events = aggregator.OnInactive("foot", now + std::chrono::milliseconds(500));
    assert(events.empty());
    events = aggregator.OnInactive("head", now + std::chrono::milliseconds(500));
    assert(events.empty());
    events = aggregator.OnInactive("nose", now + std::chrono::milliseconds(500));
    assert(events.empty());
}

}  // namespace

int main()
{
    TestShortAndLongTouch();
    TestComboEventAndSuppressesTouches();
    TestShortComboSuppressesTouches();
    std::cout << "touch event aggregator tests passed\n";
    return 0;
}
