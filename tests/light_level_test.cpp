#include "cubpet_environment_levels.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using ai_cubpet::ClassifyLightLevel;
using ai_cubpet::LightLevelName;

void TestLightLevelsUseProductThresholds()
{
    assert(std::string(LightLevelName(ClassifyLightLevel(0))) == "dark");
    assert(std::string(LightLevelName(ClassifyLightLevel(399))) == "dark");
    assert(std::string(LightLevelName(ClassifyLightLevel(400))) == "normal");
    assert(std::string(LightLevelName(ClassifyLightLevel(999))) == "normal");
    assert(std::string(LightLevelName(ClassifyLightLevel(1000))) == "bright");
    assert(std::string(LightLevelName(ClassifyLightLevel(1599))) == "bright");
    assert(std::string(LightLevelName(ClassifyLightLevel(1600))) == "strong");
}

}  // namespace

int main()
{
    TestLightLevelsUseProductThresholds();
    std::cout << "light level tests passed\n";
    return 0;
}
