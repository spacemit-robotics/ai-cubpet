#ifndef CUBPET_WAKE_GPIO_MONITOR_HPP
#define CUBPET_WAKE_GPIO_MONITOR_HPP

#include "cubpet_peripheral_config.h"

#include <chrono>  // NOLINT(build/c++11)
#include <functional>

namespace ai_cubpet {

class WakeGpioMonitor {
public:
    using WakeCallback = std::function<void()>;

    WakeGpioMonitor();
    ~WakeGpioMonitor();

    WakeGpioMonitor(const WakeGpioMonitor&) = delete;
    WakeGpioMonitor& operator=(const WakeGpioMonitor&) = delete;

    bool Start(const cubpet_gpio_input_config& input,
        std::chrono::milliseconds debounce,
        WakeCallback callback);
    void Stop();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace ai_cubpet

#endif  // CUBPET_WAKE_GPIO_MONITOR_HPP
