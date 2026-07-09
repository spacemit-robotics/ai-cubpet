#include "cubpet_action_rate_limiter.hpp"

#include <cassert>
#include <chrono>  // NOLINT(build/c++11)
#include <iostream>

namespace {

using ai_cubpet::ActionRateLimiter;
using ai_cubpet::VoiceIntent;

void TestMotorIntentRateLimited()
{
    ActionRateLimiter limiter(std::chrono::seconds(2));
    const auto now = ActionRateLimiter::Clock::time_point(std::chrono::seconds(100));

    assert(limiter.ShouldRunMotor(VoiceIntent::kNodHead, now));
    assert(!limiter.ShouldRunMotor(VoiceIntent::kNodHead,
        now + std::chrono::milliseconds(500)));
    assert(limiter.ShouldRunMotor(VoiceIntent::kNodHead,
        now + std::chrono::seconds(2)));
}

void TestUnknownIntentNeverRunsMotor()
{
    ActionRateLimiter limiter(std::chrono::seconds(2));
    const auto now = ActionRateLimiter::Clock::time_point(std::chrono::seconds(100));

    assert(!limiter.ShouldRunMotor(VoiceIntent::kUnknown, now));
}

void TestResetAllowsNextAction()
{
    ActionRateLimiter limiter(std::chrono::seconds(2));
    const auto now = ActionRateLimiter::Clock::time_point(std::chrono::seconds(100));

    assert(limiter.ShouldRunMotor(VoiceIntent::kShakeHead, now));
    limiter.Reset();
    assert(limiter.ShouldRunMotor(VoiceIntent::kShakeHead,
        now + std::chrono::milliseconds(100)));
}

}  // namespace

int main()
{
    TestMotorIntentRateLimited();
    TestUnknownIntentNeverRunsMotor();
    TestResetAllowsNextAction();
    std::cout << "action rate limiter tests passed\n";
    return 0;
}
