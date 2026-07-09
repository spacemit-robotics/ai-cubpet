#ifndef CUBPET_TOUCH_GPIO_MONITOR_HPP
#define CUBPET_TOUCH_GPIO_MONITOR_HPP

#include "cubpet_peripheral_config.h"
#include "cubpet_peripheral_manager.hpp"

#include <chrono>  // NOLINT(build/c++11)
#include <functional>
#include <vector>

namespace ai_cubpet {

class TouchGpioMonitor {
public:
    using TouchCallback = std::function<void(const PeripheralEvent&)>;

    TouchGpioMonitor();
    ~TouchGpioMonitor();

    TouchGpioMonitor(const TouchGpioMonitor&) = delete;
    TouchGpioMonitor& operator=(const TouchGpioMonitor&) = delete;

    bool Start(const std::vector<cubpet_gpio_input_config>& inputs,
        std::chrono::milliseconds debounce,
        TouchCallback callback);
    void Stop();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace ai_cubpet

#endif  // CUBPET_TOUCH_GPIO_MONITOR_HPP
