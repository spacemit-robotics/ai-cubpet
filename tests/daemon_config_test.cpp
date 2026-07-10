#include "cubpet_daemon_config.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using ai_cubpet::DaemonConfigLoadOptions;
using ai_cubpet::LoadDaemonConfig;
using ai_cubpet::LoadMergedDaemonConfigJson;

std::string TestTmpDir()
{
    const char* tmp = std::getenv("TMPDIR");
    return std::string(tmp && tmp[0] != '\0' ? tmp : "/tmp") +
        "/ai-cubpet-daemon-config-test";
}

void WriteTextFile(const std::string& path, const std::string& content)
{
    std::ofstream out(path, std::ios::trunc);
    assert(out.good());
    out << content;
}

DaemonConfigLoadOptions MakeOptions(const std::string& model)
{
    const std::string tmp_dir = TestTmpDir();
    std::system((std::string("mkdir -p ") + tmp_dir).c_str());
    const std::string model_path = tmp_dir + "/device-tree-model";
    WriteTextFile(model_path, model);

    DaemonConfigLoadOptions options;
    options.default_config_path = "config/daemon/ai_cubpet.default.json";
    options.board_config_dir = "config/daemon/boards";
    options.device_tree_model_path = model_path;
    options.user_config_path = tmp_dir + "/missing-user-config.json";
    return options;
}

void TestMuseUsesDefaultConfig()
{
    auto config = LoadDaemonConfig(MakeOptions("spacemit k1-x MUSE-Pi-Pro board"));

    assert(config.audio.rate == 16000);
    assert(config.audio.channels == 4);
    assert(config.audio.speech_channel == 1);
    assert(config.audio.input_device_hints.size() == 2);
    assert(config.audio.input_device_hints[0] == "SPV Composite");
}

void TestElephantBoardOverride()
{
    auto config = LoadDaemonConfig(
        MakeOptions("pacemit k1-x companion mipi robot board"));

    assert(config.audio.rate == 48000);
    assert(config.audio.channels == 2);
    assert(config.audio.speech_channel == 1);
    assert(config.audio.input_device_hints.size() == 2);
    assert(config.audio.input_device_hints[0] == "snd-es8326");
    assert(config.audio.output_device_hints[0] == "snd-es8326");
    assert(config.vad.threshold == 0.3f);
    assert(config.agc.enabled);
}

void TestUserConfigOverridesBoardConfig()
{
    auto options = MakeOptions("pacemit k1-x companion mipi robot board");
    options.user_config_path = TestTmpDir() + "/user-config.json";
    WriteTextFile(options.user_config_path,
        "{\n"
        "  \"audio\": {\n"
        "    \"rate\": 44100\n"
        "  },\n"
        "  \"debug\": {\n"
        "    \"save_wav\": true\n"
        "  }\n"
        "}\n");

    auto config = LoadDaemonConfig(options);

    assert(config.audio.rate == 44100);
    assert(config.audio.channels == 2);
    assert(config.debug.save_wav);
}

void TestMergedJsonIncludesBoardMetadataButKeepsRuntimeSections()
{
    auto merged = LoadMergedDaemonConfigJson(
        MakeOptions("pacemit k1-x companion mipi robot board"));

    assert(merged["board_name"] == "k1-x_Elephant_mipi");
    assert(merged["audio"]["rate"] == 48000);
    assert(merged["ui"]["qt_qpa_platform"] == "wayland");
}

}  // namespace

int main()
{
    TestMuseUsesDefaultConfig();
    TestElephantBoardOverride();
    TestUserConfigOverridesBoardConfig();
    TestMergedJsonIncludesBoardMetadataButKeepsRuntimeSections();
    std::cout << "daemon config tests passed\n";
    return 0;
}
