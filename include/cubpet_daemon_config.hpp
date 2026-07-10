#ifndef CUBPET_DAEMON_CONFIG_HPP
#define CUBPET_DAEMON_CONFIG_HPP

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace ai_cubpet {

struct AudioConfig {
    int input = -1;
    std::vector<std::string> input_device_hints = {"SPV Composite", "USB Audio"};
    int output = -1;
    std::vector<std::string> output_device_hints = {"SPV Composite", "USB Audio"};
    int rate = 16000;
    int channels = 4;
    int speech_channel = 1;
    int frames = 480;
    int queue_chunks = 96;
};

struct VadConfig {
    float threshold = 0.3f;
    float stop_threshold = 0.2f;
    int silence_ms = 500;
    int pre_speech_ms = 500;
    int max_utterance_sec = 8;
};

struct AgcConfig {
    bool enabled = true;
    float headroom_db = 6.0f;
    float max_gain_db = 18.0f;
    float initial_gain_db = 8.0f;
};

struct AsrConfig {
    std::string provider = "spacemit";
    std::string language = "zh";
    std::string model_dir;
    bool warmup = true;
};

struct DdsConfig {
    bool enabled = true;
    int domain_id = 0;
};

struct UiConfig {
    bool enabled = true;
    std::string user = "initer";
    std::string qt_qpa_platform = "wayland";
    std::string xdg_runtime_dir = "/run/user/996";
    std::string wayland_display = "wayland-0";
    std::string display = ":0";
    std::string gif_dir;
    std::string audio_dir;
    std::map<std::string, std::string> extra_env = {
        {"MESA_LOADER_DRIVER_OVERRIDE", "pvr"},
        {"GST_GL_PLATFORM", "egl"},
        {"COGL_DRIVER", "gles2"},
        {"GDK_GL", "gles"},
        {"SAL_DISABLEGL", "1"},
    };
};

struct DebugConfig {
    bool save_wav = false;
    std::string save_wav_file = "~/.cache/ai-cubpet/asr_input.wav";
    bool save_raw_wav = false;
    std::string save_raw_wav_file = "~/.cache/ai-cubpet/raw_input.wav";
};

struct DaemonConfig {
    AudioConfig audio;
    VadConfig vad;
    AgcConfig agc;
    AsrConfig asr;
    DdsConfig dds;
    UiConfig ui;
    DebugConfig debug;
    std::string log_dir = "~/.cache/ai-cubpet/logs";
    std::string pid_file = "~/.cache/ai-cubpet/ai_cubpet_daemon.pid";
};

struct DaemonConfigLoadOptions {
    std::string user_config_path;
    std::string default_config_path;
    std::string board_config_dir;
    std::string device_tree_model_path = "/proc/device-tree/model";
    bool enable_board_config = true;
};

std::string DefaultDaemonConfigDir();
std::string DefaultDaemonConfigPath();

nlohmann::json BuiltinDefaultDaemonConfigJson();
nlohmann::json LoadMergedDaemonConfigJson(
    const DaemonConfigLoadOptions& options);
DaemonConfig ParseDaemonConfigJson(const nlohmann::json& root);
DaemonConfig LoadDaemonConfig(const DaemonConfigLoadOptions& options);
bool WriteDefaultDaemonConfig(const std::string& path,
    bool overwrite,
    const DaemonConfigLoadOptions& options);

}  // namespace ai_cubpet

#endif  // CUBPET_DAEMON_CONFIG_HPP
