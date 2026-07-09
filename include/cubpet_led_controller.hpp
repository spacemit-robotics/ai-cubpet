#ifndef CUBPET_LED_CONTROLLER_HPP
#define CUBPET_LED_CONTROLLER_HPP

#include "cubpet_action_controller.hpp"
#include "cubpet_peripheral_config.h"

#include <memory>

namespace ai_cubpet {

class CubpetLedController {
public:
    CubpetLedController();
    ~CubpetLedController();

    CubpetLedController(const CubpetLedController&) = delete;
    CubpetLedController& operator=(const CubpetLedController&) = delete;

    bool Initialize(const cubpet_led_config& config);
    bool PlayCue(const LedCue& cue);
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ai_cubpet

#endif  // CUBPET_LED_CONTROLLER_HPP
