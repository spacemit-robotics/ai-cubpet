#ifndef CUBPET_ENVIRONMENT_LEVELS_HPP
#define CUBPET_ENVIRONMENT_LEVELS_HPP

#include <cstdint>

namespace ai_cubpet {

enum class LightLevel {
    kDark,
    kNormal,
    kBright,
    kStrong,
};

LightLevel ClassifyLightLevel(uint32_t lux);
const char* LightLevelName(LightLevel level);

}  // namespace ai_cubpet

#endif  // CUBPET_ENVIRONMENT_LEVELS_HPP
