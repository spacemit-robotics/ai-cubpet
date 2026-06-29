#ifndef CUBPET_KEYWORDS_HPP
#define CUBPET_KEYWORDS_HPP

#include <string>

namespace ai_cubpet {

enum class VoiceIntent {
    kUnknown,
    kHeadUp,
    kNodHead,
    kShakeHead,
    kWagTail,
};

VoiceIntent MatchVoiceIntent(const std::string& transcript);
const char* VoiceIntentName(VoiceIntent intent);
std::string VoiceIntentGifPath(VoiceIntent intent);
std::string VoiceIntentAudioPath(VoiceIntent intent);

}  // namespace ai_cubpet

#endif  // CUBPET_KEYWORDS_HPP
