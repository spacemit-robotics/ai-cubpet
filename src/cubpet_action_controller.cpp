#include "cubpet_action_controller.hpp"

namespace ai_cubpet {
namespace {

ActionMedia FromIntent(VoiceIntent intent)
{
    ActionMedia media;
    media.intent = intent;
    media.motor_intent = intent;
    media.audio_path = VoiceIntentAudioPath(intent);
    media.gif_path = VoiceIntentGifPath(intent);
    return media;
}

ActionMedia CustomMedia(const char* audio_path, const char* gif_path)
{
    ActionMedia media;
    media.audio_path = audio_path ? audio_path : "";
    media.gif_path = gif_path ? gif_path : "";
    return media;
}

ActionMedia CustomMedia(const char* audio_path, const char* gif_path,
    VoiceIntent motor_intent)
{
    ActionMedia media = CustomMedia(audio_path, gif_path);
    media.motor_intent = motor_intent;
    return media;
}

LedCue NfcSuccessLedCue()
{
    LedCue cue;
    cue.enabled = true;
    cue.effect = LedEffect::kSoftBlink;
    cue.color = {160, 255, 64};
    cue.brightness = 128;
    cue.count = 3;
    cue.period_ms = 700;
    cue.on_ms = 250;
    return cue;
}

}  // namespace

ActionMedia ActionController::ResolveActionMedia(ToyAction action)
{
    switch (action) {
    case ToyAction::kWakeUp:
        return FromIntent(VoiceIntent::kHeadUp);
    case ToyAction::kHappyTouchHead:
    case ToyAction::kAffectionateHeadLongTouch:
        return FromIntent(VoiceIntent::kNodHead);
    case ToyAction::kNoseReject:
        return FromIntent(VoiceIntent::kShakeHead);
    case ToyAction::kNoseSurprise:
        return CustomMedia("010_funny.wav", "09_funny.gif");
    case ToyAction::kFootPlay:
    case ToyAction::kFootExcited:
        return FromIntent(VoiceIntent::kWagTail);
    case ToyAction::kBootReady:
        return CustomMedia("009_greet_move.wav", "02_expect.gif");
    case ToyAction::kListen:
        return CustomMedia("", "06_blink.gif");
    case ToyAction::kStartProvisioning:
        return CustomMedia("", "06_blink.gif");
    case ToyAction::kWifiConnected:
        return CustomMedia("008_happy.wav", "05_heart.gif");
    case ToyAction::kWifiError:
        return CustomMedia("013_shake_head.wav", "03_diz.gif");
    case ToyAction::kConversationStart:
        return CustomMedia("009_greet_move.wav", "02_expect.gif");
    case ToyAction::kConversationStop:
    case ToyAction::kIdle:
        return CustomMedia("", "02_expect.gif");
    case ToyAction::kSleep:
        return CustomMedia("", "10_close_eye.gif");
    case ToyAction::kNfcDetected:
        {
            ActionMedia media = CustomMedia("009_greet_move.wav", "08_explore.gif",
                VoiceIntent::kShakeHead);
            media.led = NfcSuccessLedCue();
            return media;
        }
    case ToyAction::kNightMode:
        return CustomMedia("", "10_close_eye.gif");
    case ToyAction::kDayMode:
        return CustomMedia("", "01_daily.gif");
    case ToyAction::kCharging:
        return CustomMedia("006_im_here.wav", "06_blink.gif");
    case ToyAction::kPowerFull:
        return CustomMedia("008_happy.wav", "05_heart.gif");
    case ToyAction::kLowBattery:
        return CustomMedia("013_shake_head.wav", "03_diz.gif");
    case ToyAction::kFallDetected:
        return CustomMedia("013_shake_head.wav", "03_diz.gif");
    case ToyAction::kShakeDetected:
        return CustomMedia("010_funny.wav", "09_funny.gif",
            VoiceIntent::kShakeHead);
    case ToyAction::kNone:
    default:
        return ActionMedia{};
    }
}

}  // namespace ai_cubpet
