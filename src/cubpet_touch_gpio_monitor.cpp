#include "cubpet_touch_gpio_monitor.hpp"

#include "cubpet_touch_event_aggregator.hpp"
#include "misc_io.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>  // NOLINT(build/c++11)
#include <string>
#include <utility>

namespace ai_cubpet {
namespace {

constexpr auto kTouchLongPressThreshold = std::chrono::milliseconds(800);
constexpr auto kTouchComboEmitThreshold = std::chrono::seconds(3);

}  // namespace

struct TouchGpioMonitor::Impl {
    struct LineContext {
        Impl* owner = nullptr;
        std::string name;
        std::string role;
        struct misc_dev* dev = nullptr;
    };

    std::vector<std::unique_ptr<LineContext>> lines;
    TouchEventAggregator aggregator{{"head", "nose", "foot"},
        kTouchLongPressThreshold, kTouchComboEmitThreshold};
    TouchCallback callback;
    std::mutex mutex;
    std::atomic<bool> running{false};

    static void OnEvent(struct misc_dev* dev,
        enum misc_event event,
        uint64_t timestamp_us,
        void* args)
    {
        (void)dev;
        auto* line = static_cast<LineContext*>(args);
        if (!line || !line->owner || !line->owner->running.load()) {
            return;
        }

        std::vector<PeripheralEvent> events;
        TouchCallback callback;
        {
            std::lock_guard<std::mutex> lock(line->owner->mutex);
            const auto now = TouchEventAggregator::Clock::now();
            if (event == MISC_EV_ACTIVE) {
                std::cout << "[touch] " << line->name
                        << " role=" << line->role
                        << " ACTIVE timestamp_us=" << timestamp_us << std::endl;
                events = line->owner->aggregator.OnActive(line->role, now);
            } else {
                std::cout << "[touch] " << line->name
                        << " role=" << line->role
                        << " INACTIVE timestamp_us=" << timestamp_us << std::endl;
                events = line->owner->aggregator.OnInactive(line->role, now);
            }
            callback = line->owner->callback;
        }

        if (!callback) {
            return;
        }
        for (const auto& generated : events) {
            callback(generated);
        }
    }
};

TouchGpioMonitor::TouchGpioMonitor()
    : impl_(new Impl())
{
}

TouchGpioMonitor::~TouchGpioMonitor()
{
    Stop();
    delete impl_;
}

bool TouchGpioMonitor::Start(const std::vector<cubpet_gpio_input_config>& inputs,
    std::chrono::milliseconds debounce,
    TouchCallback callback)
{
    Stop();

    if (inputs.empty()) {
        std::cerr << "[touch] no touch GPIO inputs configured" << std::endl;
        return false;
    }

    impl_->aggregator = TouchEventAggregator({"head", "nose", "foot"},
        kTouchLongPressThreshold, kTouchComboEmitThreshold);
    impl_->callback = std::move(callback);
    impl_->running.store(true);
    impl_->lines.reserve(inputs.size());

    const auto debounce_ms = static_cast<uint16_t>(debounce.count());
    bool any_started = false;
    for (const auto& input : inputs) {
        auto line = std::make_unique<Impl::LineContext>();
        line->owner = impl_;
        line->name = input.name;
        line->role = input.role;

        struct misc_gpiod_ctx ctx = {
            input.chip_name,
            input.line_offset,
            "ai-cubpet-touch",
        };

        line->dev = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
        if (!line->dev) {
            std::cerr << "[touch] alloc failed: " << input.name
                    << " chip=" << input.chip_name
                    << " line=" << input.line_offset << std::endl;
            continue;
        }

        const auto logic = input.active_high ? MISC_ACTIVE_HIGH : MISC_ACTIVE_LOW;
        misc_io_config(line->dev, logic, debounce_ms);
        misc_io_trigger(line->dev, &Impl::OnEvent, line.get());
        std::cout << "[touch] monitor ready: " << input.name
                << " role=" << input.role
                << " chip=" << input.chip_name
                << " line=" << input.line_offset
                << " active=" << (input.active_high ? "high" : "low")
                << " debounce_ms=" << debounce.count() << std::endl;
        any_started = true;
        impl_->lines.push_back(std::move(line));
    }

    if (!any_started) {
        Stop();
    }
    return any_started;
}

void TouchGpioMonitor::Stop()
{
    impl_->running.store(false);
    for (auto& line : impl_->lines) {
        if (line && line->dev) {
            misc_io_free(line->dev);
            line->dev = nullptr;
        }
    }
    impl_->lines.clear();
    impl_->callback = nullptr;
    impl_->aggregator = TouchEventAggregator({"head", "nose", "foot"},
        kTouchLongPressThreshold, kTouchComboEmitThreshold);
}

}  // namespace ai_cubpet
