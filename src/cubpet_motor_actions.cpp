#include "cubpet_motor_actions.hpp"

#include "motor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace ai_cubpet {
namespace {

struct RohsMotorInfo {
    uint8_t motor_index;
    uint8_t step_gpio;
    uint8_t dir_gpio;
    uint8_t enable_gpio;
    uint8_t stop_gpio;

    int current_position;
    int constant_range;
    int gpio_max_steps;

    bool enable_gpio_level;
    bool dir_gpio_left_level;
    bool stop_gpio_active_level;

    int range_steps;
};

struct FixedRohsMotor {
    const char* name;
    RohsMotorInfo info;
};

constexpr float kCenterAngle = 90.0f;
constexpr float kHeadUpAngle = 75.0f;
constexpr float kHeadDownAngle = 105.0f;
constexpr float kMotorSpeed = 1.0f;
constexpr auto kStepDelay = std::chrono::milliseconds(420);

const FixedRohsMotor kHeadLr = {
    "head_lr",
    {
        1, 46, 43, 42, 83,
        -1, 60, -1,
        false, false, false,
        0,
    },
};

const FixedRohsMotor kTailLr = {
    "tail_lr",
    {
        2, 37, 38, 39, 61,
        -1, 60, -1,
        false, false, false,
        0,
    },
};

const FixedRohsMotor kHeadUd = {
    "head_ud",
    {
        3, 34, 35, 36, 82,
        -1, 60, -1,
        false, false, false,
        0,
    },
};

float SafeDoaTarget(float doa_angle_degrees)
{
    return std::clamp(doa_angle_degrees, 60.0f, 120.0f);
}

bool RunRohsSequence(const FixedRohsMotor& fixed,
                    const std::vector<float>& positions,
                    float speed)
{
    RohsMotorInfo info = fixed.info;
    motor_dev* motor = motor_alloc_pwm("pwm_RoHS", 0, &info);
    if (!motor) {
        std::cerr << "[motor] " << fixed.name << " alloc failed" << std::endl;
        return false;
    }

    if (motor_init_one(motor) < 0) {
        std::cerr << "[motor] " << fixed.name << " init failed" << std::endl;
        motor_free(&motor, 1);
        return false;
    }

    bool ok = true;
    motor_cmd cmd{};
    cmd.mode = MOTOR_MODE_POS;
    cmd.vel_des = speed;

    for (float position : positions) {
        cmd.pos_des = position;
        if (motor_set_cmd_one(motor, &cmd) < 0) {
            std::cerr << "[motor] " << fixed.name << " target " << position
                    << " failed" << std::endl;
            ok = false;
            break;
        }

        motor_state state{};
        motor_get_state_one(motor, &state);
        std::cout << "[motor] " << fixed.name << " target=" << position
                << " pos=" << state.pos << " vel=" << state.vel << std::endl;
        std::this_thread::sleep_for(kStepDelay);
    }

    cmd.mode = MOTOR_MODE_IDLE;
    cmd.vel_des = 0.0f;
    motor_set_cmd_one(motor, &cmd);
    motor_free(&motor, 1);
    return ok;
}

}  // namespace

bool CubpetMotorActions::Initialize()
{
    initialized_ = true;
    std::cout << "[motor] actions ready" << std::endl;
    return true;
}

void CubpetMotorActions::Shutdown()
{
    if (initialized_) {
        std::cout << "[motor] actions shutdown" << std::endl;
    }
    initialized_ = false;
}

bool CubpetMotorActions::Execute(VoiceIntent intent,
                                bool has_doa,
                                float doa_angle_degrees)
{
    if (!initialized_) {
        return false;
    }

    std::cout << "[motor] action=" << VoiceIntentName(intent);
    if (has_doa) {
        std::cout << " doa=" << doa_angle_degrees;
    } else {
        std::cout << " doa=--";
    }
    std::cout << std::endl;

    switch (intent) {
    case VoiceIntent::kHeadUp: {
        bool ok = RunRohsSequence(kHeadUd, {kHeadUpAngle, kCenterAngle}, kMotorSpeed);
        if (has_doa) {
            const float target = SafeDoaTarget(doa_angle_degrees);
            ok = RunRohsSequence(kHeadLr, {target, kCenterAngle}, kMotorSpeed) && ok;
        }
        return ok;
    }
    case VoiceIntent::kNodHead:
        return RunRohsSequence(kHeadUd,
            {kHeadDownAngle, kHeadUpAngle, kHeadDownAngle, kCenterAngle}, kMotorSpeed);
    case VoiceIntent::kShakeHead:
        return RunRohsSequence(kHeadLr, {75.0f, 105.0f, 75.0f, kCenterAngle}, kMotorSpeed);
    case VoiceIntent::kWagTail:
        return RunRohsSequence(kTailLr, {75.0f, 105.0f, kCenterAngle}, kMotorSpeed);
    case VoiceIntent::kUnknown:
    default:
        return false;
    }
}

}  // namespace ai_cubpet
