#include "cubpet_motor_actions.hpp"

#include "motor.h"

#include <algorithm>
#include <chrono>  // NOLINT(build/c++11)
#include <cstdint>
#include <iostream>
#include <thread>  // NOLINT(build/c++11)
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

constexpr float kCenterAngle = 90.0f;
constexpr float kHeadUpAngle = 75.0f;
constexpr float kHeadDownAngle = 105.0f;
constexpr float kMotorSpeed = 1.0f;
constexpr auto kStepDelay = std::chrono::milliseconds(420);

float SafeDoaTarget(float doa_angle_degrees)
{
    return std::clamp(doa_angle_degrees, 60.0f, 120.0f);
}

RohsMotorInfo ToRohsMotorInfo(const cubpet_motor_config& config)
{
    return {
        config.motor_index,
        config.step_gpio,
        config.dir_gpio,
        config.enable_gpio,
        config.stop_gpio,
        config.current_position,
        config.constant_range,
        config.gpio_max_steps,
        config.enable_gpio_level != 0,
        config.dir_gpio_left_level != 0,
        config.stop_gpio_active_level != 0,
        config.range_steps,
    };
}

bool RunRohsSequence(const cubpet_motor_config* fixed,
                    const std::vector<float>& positions,
                    float speed)
{
    if (!fixed) {
        std::cerr << "[motor] missing motor config" << std::endl;
        return false;
    }

    RohsMotorInfo info = ToRohsMotorInfo(*fixed);
    motor_dev* motor = motor_alloc_pwm("pwm_RoHS", 0, &info);
    if (!motor) {
        std::cerr << "[motor] " << fixed->name << " alloc failed" << std::endl;
        return false;
    }

    if (motor_init_one(motor) < 0) {
        std::cerr << "[motor] " << fixed->name << " init failed" << std::endl;
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
            std::cerr << "[motor] " << fixed->name << " target " << position
                    << " failed" << std::endl;
            ok = false;
            break;
        }

        motor_state state{};
        motor_get_state_one(motor, &state);
        std::cout << "[motor] " << fixed->name << " target=" << position
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
    cubpet_peripheral_config_init_defaults(&config_);
    const int rc = cubpet_peripheral_config_load_auto(&config_);
    if (rc != 0) {
        std::cerr << "[motor] failed to load peripheral config, rc=" << rc
                << std::endl;
        initialized_ = false;
        return false;
    }
    std::cout << "[motor] peripheral config=" << config_.board_name << std::endl;
    initialized_ = true;
    std::cout << "[motor] actions ready" << std::endl;
    return true;
}

bool CubpetMotorActions::Supports(VoiceIntent intent) const
{
    if (!initialized_) {
        return false;
    }

    switch (intent) {
    case VoiceIntent::kHeadUp:
    case VoiceIntent::kNodHead:
        return cubpet_peripheral_find_motor(&config_, "head_ud") != nullptr;
    case VoiceIntent::kShakeHead:
        return cubpet_peripheral_find_motor(&config_, "head_lr") != nullptr;
    case VoiceIntent::kWagTail:
        return cubpet_peripheral_find_motor(&config_, "tail_lr") != nullptr;
    case VoiceIntent::kUnknown:
    default:
        return false;
    }
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
        if (!Supports(intent)) {
            std::cout << "[motor] action=" << VoiceIntentName(intent)
                    << " skipped: motor not configured" << std::endl;
            return true;
        }
        bool ok = RunRohsSequence(
            cubpet_peripheral_find_motor(&config_, "head_ud"),
            {kHeadUpAngle, kCenterAngle}, kMotorSpeed);
        if (has_doa) {
            const float target = SafeDoaTarget(doa_angle_degrees);
            ok = RunRohsSequence(
                cubpet_peripheral_find_motor(&config_, "head_lr"),
                {target, kCenterAngle}, kMotorSpeed) && ok;
        }
        return ok;
    }
    case VoiceIntent::kNodHead:
        if (!Supports(intent)) {
            std::cout << "[motor] action=" << VoiceIntentName(intent)
                    << " skipped: motor not configured" << std::endl;
            return true;
        }
        return RunRohsSequence(cubpet_peripheral_find_motor(&config_, "head_ud"),
            {kHeadDownAngle, kHeadUpAngle, kHeadDownAngle, kCenterAngle}, kMotorSpeed);
    case VoiceIntent::kShakeHead:
        if (!Supports(intent)) {
            std::cout << "[motor] action=" << VoiceIntentName(intent)
                    << " skipped: motor not configured" << std::endl;
            return true;
        }
        return RunRohsSequence(cubpet_peripheral_find_motor(&config_, "head_lr"),
            {75.0f, 105.0f, 75.0f, kCenterAngle}, kMotorSpeed);
    case VoiceIntent::kWagTail:
        if (!Supports(intent)) {
            std::cout << "[motor] action=" << VoiceIntentName(intent)
                    << " skipped: motor not configured" << std::endl;
            return true;
        }
        return RunRohsSequence(cubpet_peripheral_find_motor(&config_, "tail_lr"),
            {75.0f, 105.0f, kCenterAngle}, kMotorSpeed);
    case VoiceIntent::kUnknown:
    default:
        return false;
    }
}

}  // namespace ai_cubpet
