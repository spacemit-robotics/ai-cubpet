#include "cubpet_led_controller.hpp"

#include "led.h"

#include <algorithm>
#include <chrono>  // NOLINT(build/c++11)
#include <cstring>
#include <iostream>
#include <thread>  // NOLINT(build/c++11)

namespace ai_cubpet {
namespace {

struct led_ws2812_spi_args {
    const char* dev_path;
    uint32_t num_leds;
    uint32_t spi_speed_hz;
    uint32_t reset_bytes;
};

bool HasText(const char* value)
{
    return value && value[0] != '\0';
}

led_color ToLedColor(const LedColor& color)
{
    return led_color{color.r, color.g, color.b};
}

}  // namespace

struct CubpetLedController::Impl {
    led_dev* dev = nullptr;

    bool Initialize(const cubpet_led_config& config)
    {
        Stop();
        if (!HasText(config.type) || !HasText(config.name)) {
            std::cout << "[led] skipped: no led configured" << std::endl;
            return false;
        }

        if (std::strcmp(config.type, "spi-ws2812") == 0) {
            led_ws2812_spi_args args {
                config.dev_path,
                config.num_leds,
                config.spi_speed_hz,
                config.reset_bytes,
            };
            dev = led_alloc_spi(config.name, &args);
        } else {
            std::cout << "[led] skipped unsupported type=" << config.type << std::endl;
            return false;
        }

        if (!dev) {
            std::cout << "[led] init failed: type=" << config.type
                << " name=" << config.name
                << " dev=" << config.dev_path << std::endl;
            return false;
        }

        std::cout << "[led] ready: type=" << config.type
            << " name=" << config.name
            << " dev=" << config.dev_path
            << " count=" << config.num_leds << std::endl;
        led_set_state(dev, false);
        return true;
    }

    bool PlayCue(const LedCue& cue)
    {
        if (!cue.enabled) {
            return true;
        }
        if (!dev) {
            std::cout << "[led] cue skipped: led not ready" << std::endl;
            return false;
        }

        const led_color color = ToLedColor(cue.color);
        led_set_color(dev, &color);
        led_set_brightness(dev, cue.brightness);

        if (cue.effect == LedEffect::kSoftBlink) {
            const auto on_ms = std::chrono::milliseconds(
                std::max<uint16_t>(1, cue.on_ms));
            const auto off_ms = std::chrono::milliseconds(
                std::max<int>(1, static_cast<int>(cue.period_ms) -
                    static_cast<int>(cue.on_ms)));
            const uint8_t count = std::max<uint8_t>(1, cue.count);
            for (uint8_t i = 0; i < count; ++i) {
                led_set_color(dev, &color);
                led_set_brightness(dev, cue.brightness);
                std::this_thread::sleep_for(on_ms);
                led_set_state(dev, false);
                if (i + 1 < count) {
                    std::this_thread::sleep_for(off_ms);
                }
            }
            led_set_state(dev, false);
        }

        return true;
    }

    void Stop()
    {
        if (dev) {
            led_set_state(dev, false);
            led_free(dev);
            dev = nullptr;
        }
    }
};

CubpetLedController::CubpetLedController()
    : impl_(std::make_unique<Impl>())
{
}

CubpetLedController::~CubpetLedController()
{
    Stop();
}

bool CubpetLedController::Initialize(const cubpet_led_config& config)
{
    return impl_ && impl_->Initialize(config);
}

bool CubpetLedController::PlayCue(const LedCue& cue)
{
    return impl_ && impl_->PlayCue(cue);
}

void CubpetLedController::Stop()
{
    if (impl_) {
        impl_->Stop();
    }
}

}  // namespace ai_cubpet
