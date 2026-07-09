#ifndef CUBPET_MOTOR_ACTIONS_HPP
#define CUBPET_MOTOR_ACTIONS_HPP

#include "cubpet_keywords.hpp"
#include "cubpet_peripheral_config.h"

namespace ai_cubpet {

class CubpetMotorActions {
public:
    bool Initialize();
    void Shutdown();

    bool Supports(VoiceIntent intent) const;
    bool Execute(VoiceIntent intent, bool has_doa, float doa_angle_degrees);

private:
    bool initialized_ = false;
    cubpet_peripheral_config config_{};
};

}  // namespace ai_cubpet

#endif  // CUBPET_MOTOR_ACTIONS_HPP
