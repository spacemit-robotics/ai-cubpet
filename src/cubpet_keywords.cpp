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

}  // namespace ai_cubpet
