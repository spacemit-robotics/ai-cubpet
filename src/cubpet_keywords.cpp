#include "cubpet_keywords.hpp"

namespace ai_cubpet {
namespace {

bool IsIgnoredByte(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
        ch == '.' || ch == ',' || ch == '!' || ch == '?' ||
        ch == ':' || ch == ';' || ch == '"' || ch == '\'';
}

std::string NormalizeTranscript(const std::string& text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (unsigned char ch : text) {
        if (!IsIgnoredByte(ch)) {
            normalized.push_back(static_cast<char>(ch));
        }
    }
    return normalized;
}

bool ContainsAny(const std::string& text, const char* const* words, int count)
{
    for (int i = 0; i < count; ++i) {
        if (text.find(words[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const char* VoiceIntentGifFileName(VoiceIntent intent)
{
    switch (intent) {
    case VoiceIntent::kHeadUp:
        return "08_explore.gif";
    case VoiceIntent::kNodHead:
        return "02_expect.gif";
    case VoiceIntent::kShakeHead:
        return "03_diz.gif";
    case VoiceIntent::kWagTail:
        return "05_heart.gif";
    case VoiceIntent::kUnknown:
    default:
        return "";
    }
}

const char* VoiceIntentAudioFileName(VoiceIntent intent)
{
    switch (intent) {
    case VoiceIntent::kHeadUp:
        return "009_greet_move.wav";
    case VoiceIntent::kNodHead:
        return "008_happy.wav";
    case VoiceIntent::kShakeHead:
        return "013_shake_head.wav";
    case VoiceIntent::kWagTail:
        return "012_heart.wav";
    case VoiceIntent::kUnknown:
    default:
        return "";
    }
}

}  // namespace

VoiceIntent MatchVoiceIntent(const std::string& transcript)
{
    const std::string text = NormalizeTranscript(transcript);
    static const char* const kHeadUp[] = {"抬头", "抬起头", "抬一下头"};
    static const char* const kNodHead[] = {"点头", "点点头", "点一下头", "点点"};
    static const char* const kShakeHead[] = {"摇头", "摇摇头", "摇一下头"};
    static const char* const kWagTail[] = {"摇尾巴", "摇摇尾巴", "尾巴摇一下", "尾巴", "摇尾"};

    if (ContainsAny(text, kHeadUp, 3)) {
        return VoiceIntent::kHeadUp;
    }
    if (ContainsAny(text, kNodHead, 4)) {
        return VoiceIntent::kNodHead;
    }
    if (ContainsAny(text, kShakeHead, 3)) {
        return VoiceIntent::kShakeHead;
    }
    if (ContainsAny(text, kWagTail, 5)) {
        return VoiceIntent::kWagTail;
    }

    return VoiceIntent::kUnknown;
}


ProductCommand MatchProductCommand(const std::string& transcript)
{
    const std::string text = NormalizeTranscript(transcript);
    static const char* const kStartProvisioning[] = {
        "开始配网", "进入配网", "进入配网模式", "配网模式",
        "连接Wi-Fi", "连接wifi", "连接WiFi", "重新配网",
        "更换Wi-Fi", "更换wifi", "配置网络", "网络配置"
    };
    static const char* const kExitProvisioning[] = {
        "退出配网", "取消配网", "停止配网"
    };
    static const char* const kEnterContinuous[] = {
        "进入连续对话", "连续对话", "开始连续对话",
        "陪我聊天", "开始聊天模式", "开始聊天", "聊天模式",
        "聊聊天", "陪我聊聊天"
    };
    static const char* const kExitContinuous[] = {
        "退出聊天", "休息一下", "停止聊天", "结束聊天", "退出连续对话"
    };
    static const char* const kSleep[] = {
        "去睡觉", "睡觉吧", "休息吧"
    };
    if (ContainsAny(text, kStartProvisioning,
            static_cast<int>(sizeof(kStartProvisioning) / sizeof(kStartProvisioning[0])))) {
        return ProductCommand::kStartProvisioning;
    }
    if (ContainsAny(text, kExitProvisioning,
            static_cast<int>(sizeof(kExitProvisioning) / sizeof(kExitProvisioning[0])))) {
        return ProductCommand::kExitProvisioning;
    }
    if (ContainsAny(text, kExitContinuous,
            static_cast<int>(sizeof(kExitContinuous) / sizeof(kExitContinuous[0])))) {
        return ProductCommand::kExitContinuousConversation;
    }
    if (ContainsAny(text, kEnterContinuous,
            static_cast<int>(sizeof(kEnterContinuous) / sizeof(kEnterContinuous[0])))) {
        return ProductCommand::kEnterContinuousConversation;
    }
    if (ContainsAny(text, kSleep,
            static_cast<int>(sizeof(kSleep) / sizeof(kSleep[0])))) {
        return ProductCommand::kSleep;
    }
    return ProductCommand::kUnknown;
}

const char* VoiceIntentName(VoiceIntent intent)
{
    switch (intent) {
    case VoiceIntent::kHeadUp:
        return "head_up";
    case VoiceIntent::kNodHead:
        return "nod_head";
    case VoiceIntent::kShakeHead:
        return "shake_head";
    case VoiceIntent::kWagTail:
        return "wag_tail";
    case VoiceIntent::kUnknown:
    default:
        return "unknown";
    }
}


const char* ProductCommandName(ProductCommand command)
{
    switch (command) {
    case ProductCommand::kStartProvisioning:
        return "start_provisioning";
    case ProductCommand::kExitProvisioning:
        return "exit_provisioning";
    case ProductCommand::kEnterContinuousConversation:
        return "enter_continuous_conversation";
    case ProductCommand::kExitContinuousConversation:
        return "exit_continuous_conversation";
    case ProductCommand::kSleep:
        return "sleep";
    case ProductCommand::kWake:
        return "wake";
    case ProductCommand::kUnknown:
    default:
        return "unknown";
    }
}

std::string VoiceIntentGifPath(VoiceIntent intent)
{
    const char* file_name = VoiceIntentGifFileName(intent);
    if (!file_name || file_name[0] == '\0') {
        return "";
    }
    return file_name;
}

std::string VoiceIntentAudioPath(VoiceIntent intent)
{
    const char* file_name = VoiceIntentAudioFileName(intent);
    if (!file_name || file_name[0] == '\0') {
        return "";
    }
    return file_name;
}

}  // namespace ai_cubpet
