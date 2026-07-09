#include "cubpet_action_controller.hpp"

#include <cassert>
#include <iostream>
#include <set>
#include <string>

namespace {

using ai_cubpet::ActionController;
using ai_cubpet::LedEffect;
using ai_cubpet::ToyAction;
using ai_cubpet::VoiceIntent;

const std::set<std::string>& RequiredAudioAssets()
{
    static const std::set<std::string> assets = {
        "001_zh_daily_weather.wav",
        "002_en_daily_weather.wav",
        "003_zh_en_search.wav",
        "004_zh_selling_sausages.wav",
        "005_what_is_the_day_today.wav",
        "006_im_here.wav",
        "007_daily_response.wav",
        "008_happy.wav",
        "009_greet_move.wav",
        "010_funny.wav",
        "011_diz.wav",
        "012_heart.wav",
        "013_shake_head.wav",
        "014_vp_register_start.wav",
        "015_vp_register_fail.wav",
        "016_vp_register_done.wav",
        "017_vp_welcome.wav",
        "018_vp_like_you.wav",
        "019_vp_come_here.wav",
        "020_vp_good_night.wav",
        "021_vp_great.wav",
    };
    return assets;
}

const std::set<std::string>& RequiredGifAssets()
{
    static const std::set<std::string> assets = {
        "01_daily.gif",
        "02_expect.gif",
        "03_diz.gif",
        "04_moist.gif",
        "05_heart.gif",
        "06_blink.gif",
        "07_angry.gif",
        "08_explore.gif",
        "09_funny.gif",
        "10_close_eye.gif",
        "11_squint.gif",
    };
    return assets;
}

void TestMediaMapping()
{
    auto media = ActionController::ResolveActionMedia(ToyAction::kHappyTouchHead);
    assert(media.intent == VoiceIntent::kNodHead);
    assert(media.audio_path == "008_happy.wav");
    assert(media.gif_path == "02_expect.gif");

    media = ActionController::ResolveActionMedia(ToyAction::kNoseReject);
    assert(media.intent == VoiceIntent::kShakeHead);
    assert(media.audio_path == "013_shake_head.wav");
    assert(media.gif_path == "03_diz.gif");

    media = ActionController::ResolveActionMedia(ToyAction::kWakeUp);
    assert(media.intent == VoiceIntent::kHeadUp);
    assert(media.audio_path == "009_greet_move.wav");
    assert(media.gif_path == "08_explore.gif");

    media = ActionController::ResolveActionMedia(ToyAction::kStartProvisioning);
    assert(media.intent == VoiceIntent::kUnknown);
    assert(media.audio_path.empty());
    assert(media.gif_path == "06_blink.gif");

    media = ActionController::ResolveActionMedia(ToyAction::kNfcDetected);
    assert(media.intent == VoiceIntent::kUnknown);
    assert(media.motor_intent == VoiceIntent::kShakeHead);
    assert(media.audio_path == "009_greet_move.wav");
    assert(media.gif_path == "08_explore.gif");
    assert(media.led.enabled);
    assert(media.led.effect == LedEffect::kSoftBlink);
    assert(media.led.color.r == 160);
    assert(media.led.color.g == 255);
    assert(media.led.color.b == 64);
    assert(media.led.brightness == 128);
    assert(media.led.count == 3);
    assert(media.led.period_ms == 700);
    assert(media.led.on_ms == 250);

    media = ActionController::ResolveActionMedia(ToyAction::kNightMode);
    assert(media.intent == VoiceIntent::kUnknown);
    assert(media.audio_path.empty());
    assert(media.gif_path == "10_close_eye.gif");

    media = ActionController::ResolveActionMedia(ToyAction::kDayMode);
    assert(media.intent == VoiceIntent::kUnknown);
    assert(media.audio_path.empty());
    assert(media.gif_path == "01_daily.gif");
}

void TestActionMediaUsesPackagedAssets()
{
    const ToyAction actions[] = {
        ToyAction::kBootReady,
        ToyAction::kIdle,
        ToyAction::kWakeUp,
        ToyAction::kListen,
        ToyAction::kStartProvisioning,
        ToyAction::kWifiConnected,
        ToyAction::kWifiError,
        ToyAction::kConversationStart,
        ToyAction::kConversationStop,
        ToyAction::kSleep,
        ToyAction::kHappyTouchHead,
        ToyAction::kAffectionateHeadLongTouch,
        ToyAction::kNoseSurprise,
        ToyAction::kNoseReject,
        ToyAction::kFootPlay,
        ToyAction::kFootExcited,
        ToyAction::kNfcDetected,
        ToyAction::kNightMode,
        ToyAction::kDayMode,
        ToyAction::kCharging,
        ToyAction::kPowerFull,
        ToyAction::kLowBattery,
        ToyAction::kFallDetected,
        ToyAction::kShakeDetected,
    };

    for (const ToyAction action : actions) {
        const auto media = ActionController::ResolveActionMedia(action);
        if (!media.audio_path.empty()) {
            assert(RequiredAudioAssets().count(media.audio_path) == 1);
        }
        if (!media.gif_path.empty()) {
            assert(RequiredGifAssets().count(media.gif_path) == 1);
        }
    }
}

void TestNoopMapping()
{
    const auto media = ActionController::ResolveActionMedia(ToyAction::kNone);
    assert(media.intent == VoiceIntent::kUnknown);
    assert(media.audio_path.empty());
    assert(media.gif_path.empty());
}

}  // namespace

int main()
{
    TestMediaMapping();
    TestActionMediaUsesPackagedAssets();
    TestNoopMapping();
    std::cout << "action controller tests passed\n";
    return 0;
}
