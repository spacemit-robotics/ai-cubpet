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

enum class ProductCommand {
    kUnknown,
    kStartProvisioning,
    kExitProvisioning,
    kEnterContinuousConversation,
    kExitContinuousConversation,
    kSleep,
    kWake,
};

VoiceIntent MatchVoiceIntent(const std::string& transcript);
ProductCommand MatchProductCommand(const std::string& transcript);
const char* VoiceIntentName(VoiceIntent intent);
const char* ProductCommandName(ProductCommand command);
std::string VoiceIntentGifPath(VoiceIntent intent);
std::string VoiceIntentAudioPath(VoiceIntent intent);

}  // namespace ai_cubpet

#endif  // CUBPET_KEYWORDS_HPP
