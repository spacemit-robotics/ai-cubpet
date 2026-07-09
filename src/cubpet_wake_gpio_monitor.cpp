#include "cubpet_wake_gpio_monitor.hpp"

#include "misc_io.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <utility>

namespace ai_cubpet {

struct WakeGpioMonitor::Impl {
    struct misc_dev* dev = nullptr;
    WakeCallback callback;
    std::atomic<bool> running{false};

    static void OnEvent(struct misc_dev* dev,
        enum misc_event event,
        uint64_t timestamp_us,
        void* args)
    {
        (void)dev;
        Impl* self = static_cast<Impl*>(args);
        if (!self || !self->running.load() || event != MISC_EV_ACTIVE) {
            return;
        }
        std::cout << "[wake] gpio ACTIVE timestamp_us=" << timestamp_us << std::endl;
        if (self->callback) {
            self->callback();
        }
    }
};

WakeGpioMonitor::WakeGpioMonitor()
    : impl_(new Impl())
{
}

WakeGpioMonitor::~WakeGpioMonitor()
{
    Stop();
    delete impl_;
}

bool WakeGpioMonitor::Start(const cubpet_gpio_input_config& input,
    std::chrono::milliseconds debounce,
    WakeCallback callback)
{
    Stop();

    struct misc_gpiod_ctx ctx = {
        input.chip_name,
        input.line_offset,
        "ai-cubpet-wake",
    };

    impl_->dev = misc_io_alloc(MISC_TYPE_GENERIC, MISC_DIR_INPUT, &ctx);
    if (!impl_->dev) {
        std::cerr << "[wake] alloc failed: " << input.name
                << " chip=" << input.chip_name
                << " line=" << input.line_offset << std::endl;
        return false;
    }

    const auto logic = input.active_high ? MISC_ACTIVE_HIGH : MISC_ACTIVE_LOW;
    const auto debounce_ms = static_cast<uint16_t>(debounce.count());
    misc_io_config(impl_->dev, logic, debounce_ms);
    impl_->callback = std::move(callback);
    impl_->running.store(true);
    misc_io_trigger(impl_->dev, &Impl::OnEvent, impl_);
    std::cout << "[wake] monitor ready: " << input.name
            << " role=" << input.role
            << " chip=" << input.chip_name
            << " line=" << input.line_offset
            << " active=" << (input.active_high ? "high" : "low")
            << " debounce_ms=" << debounce.count() << std::endl;
    return true;
}

void WakeGpioMonitor::Stop()
{
    impl_->running.store(false);
    if (impl_->dev) {
        misc_io_free(impl_->dev);
        impl_->dev = nullptr;
    }
    impl_->callback = nullptr;
}

}  // namespace ai_cubpet
