#ifndef CUBPET_ACTION_RATE_LIMITER_HPP
#define CUBPET_ACTION_RATE_LIMITER_HPP

#include "cubpet_keywords.hpp"

#include <chrono>  // NOLINT(build/c++11)
#include <unordered_map>

namespace ai_cubpet {

class ActionRateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    explicit ActionRateLimiter(std::chrono::milliseconds motor_interval);

    bool ShouldRunMotor(VoiceIntent intent, Clock::time_point now);
    void Reset();

private:
    std::chrono::milliseconds motor_interval_;
    std::unordered_map<int, Clock::time_point> last_motor_time_;
};

}  // namespace ai_cubpet

#endif  // CUBPET_ACTION_RATE_LIMITER_HPP
