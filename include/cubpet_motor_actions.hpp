#ifndef CUBPET_MOTOR_ACTIONS_HPP
#define CUBPET_MOTOR_ACTIONS_HPP

#include "cubpet_keywords.hpp"

namespace ai_cubpet {

class CubpetMotorActions {
public:
    bool Initialize();
    void Shutdown();

    bool Execute(VoiceIntent intent, bool has_doa, float doa_angle_degrees);

private:
    bool initialized_ = false;
};

}  // namespace ai_cubpet

#endif  // CUBPET_MOTOR_ACTIONS_HPP
