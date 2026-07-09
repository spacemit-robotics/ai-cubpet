#ifndef CUBPET_ACTION_CONTROLLER_HPP
#define CUBPET_ACTION_CONTROLLER_HPP

#include "cubpet_keywords.hpp"
#include "cubpet_toy_state_machine.hpp"

#include <cstdint>
#include <string>

namespace ai_cubpet {

enum class LedEffect {
    kNone,
    kSoftBlink,
};

struct LedColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct LedCue {
    bool enabled = false;
    LedEffect effect = LedEffect::kNone;
    LedColor color;
    uint8_t brightness = 0;
    uint8_t count = 0;
    uint16_t period_ms = 0;
    uint16_t on_ms = 0;
};

struct ActionMedia {
    VoiceIntent intent = VoiceIntent::kUnknown;
    VoiceIntent motor_intent = VoiceIntent::kUnknown;
    std::string audio_path;
    std::string gif_path;
    LedCue led;
};

class ActionController {
public:
    static ActionMedia ResolveActionMedia(ToyAction action);
};

}  // namespace ai_cubpet

#endif  // CUBPET_ACTION_CONTROLLER_HPP
