#include "cubpet_environment_levels.hpp"

namespace ai_cubpet {

LightLevel ClassifyLightLevel(uint32_t lux)
{
    if (lux < 400) {
        return LightLevel::kDark;
    }
    if (lux < 1000) {
        return LightLevel::kNormal;
    }
    if (lux < 1600) {
        return LightLevel::kBright;
    }
    return LightLevel::kStrong;
}

const char* LightLevelName(LightLevel level)
{
    switch (level) {
    case LightLevel::kDark: return "dark";
    case LightLevel::kNormal: return "normal";
    case LightLevel::kBright: return "bright";
    case LightLevel::kStrong: return "strong";
    }
    return "unknown";
}

}  // namespace ai_cubpet
