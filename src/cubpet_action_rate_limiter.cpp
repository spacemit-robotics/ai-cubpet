#include "cubpet_action_rate_limiter.hpp"

namespace ai_cubpet {

ActionRateLimiter::ActionRateLimiter(std::chrono::milliseconds motor_interval)
    : motor_interval_(motor_interval)
{
}

bool ActionRateLimiter::ShouldRunMotor(VoiceIntent intent, Clock::time_point now)
{
    if (intent == VoiceIntent::kUnknown) {
        return false;
    }

    const int key = static_cast<int>(intent);
    const auto it = last_motor_time_.find(key);
    if (it != last_motor_time_.end() && now - it->second < motor_interval_) {
        return false;
    }

    last_motor_time_[key] = now;
    return true;
}

void ActionRateLimiter::Reset()
{
    last_motor_time_.clear();
}

}  // namespace ai_cubpet
