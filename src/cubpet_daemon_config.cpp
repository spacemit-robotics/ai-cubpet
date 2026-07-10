#include "cubpet_daemon_config.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace ai_cubpet {
namespace {

using json = nlohmann::json;

std::string HomeDir()
{
    if (const char* h = std::getenv("HOME")) {
        return std::string(h);
    }
    struct passwd pwbuf;
    struct passwd* pw = nullptr;
    char buf[4096];
    if (getpwuid_r(getuid(), &pwbuf, buf, sizeof(buf), &pw) == 0 && pw) {
        return std::string(pw->pw_dir);
    }
    return "/root";
}

std::string ExpandUser(const std::string& path)
{
    if (path.empty()) {
        return path;
    }
    if (path == "~") {
        return HomeDir();
    }
    if (path.size() > 1 && path[0] == '~' && path[1] == '/') {
        return HomeDir() + path.substr(1);
    }
    return path;
}

std::string ParentDir(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool MakeDirs(const std::string& path)
{
    if (path.empty() || path == "/") {
        return true;
    }
    std::string cur;
    cur.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' && i != 0) {
            if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        }
    }
    if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

bool FileExists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool DirectoryExists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string PathJoin(const std::string& lhs, const std::string& rhs)
{
    if (lhs.empty() || lhs == "/") {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

std::string ReadTextFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    while (!content.empty() &&
            (content.back() == '\0' || content.back() == '\n' ||
            content.back() == '\r' || content.back() == ' ' ||
            content.back() == '\t')) {
        content.pop_back();
    }
    return content;
}

std::vector<std::string> JsonFilesInDir(const std::string& dir)
{
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return files;
    }
    while (dirent* ent = readdir(d)) {
        const std::string name = ent->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            files.push_back(PathJoin(dir, name));
        }
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

json LoadJsonFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in.good()) {
        throw std::runtime_error("failed to open config: " + path);
    }
    return json::parse(in, nullptr, true, true);
}

json LoadDefaultJson(const std::string& path)
{
    if (!path.empty() && FileExists(path)) {
        return LoadJsonFile(path);
    }
    return BuiltinDefaultDaemonConfigJson();
}

bool ModelMatches(const json& root, const std::string& model)
{
    const auto it = root.find("model_matches");
    if (model.empty() || it == root.end() || !it->is_array()) {
        return false;
    }
    for (const auto& item : *it) {
        if (!item.is_string()) {
            continue;
        }
        const std::string needle = item.get<std::string>();
        if (!needle.empty() && model.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool LoadMatchingBoardJson(const DaemonConfigLoadOptions& options, json* out)
{
    if (!options.enable_board_config || options.board_config_dir.empty() ||
            !DirectoryExists(options.board_config_dir)) {
        return false;
    }
    const std::string model = ReadTextFile(options.device_tree_model_path);
    if (model.empty()) {
        return false;
    }
    for (const auto& file : JsonFilesInDir(options.board_config_dir)) {
        json root = LoadJsonFile(file);
        if (ModelMatches(root, model)) {
            *out = std::move(root);
            return true;
        }
    }
    return false;
}

bool HasOldDefaultInputWithoutHints(const std::string& path)
{
    if (path.empty() || !FileExists(path)) {
        return false;
    }
    json root = LoadJsonFile(path);
    const auto it = root.find("audio");
    if (it == root.end() || !it->is_object()) {
        return false;
    }
    if (it->find("input_device_hints") != it->end()) {
        return false;
    }
    const auto input_it = it->find("input");
    return input_it != it->end() && input_it->is_number_integer() &&
        input_it->get<int>() == 1;
}

template <typename T>
void GetOpt(const json& j, const char* key, T& out)  // NOLINT(runtime/references)
{
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        out = it->get<T>();
    }
}

}  // namespace

std::string DefaultDaemonConfigDir()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg) {
            return std::string(xdg) + "/ai-cubpet";
        }
    }
    return HomeDir() + "/.config/ai-cubpet";
}

std::string DefaultDaemonConfigPath()
{
    return DefaultDaemonConfigDir() + "/ai_cubpet.json";
}

json BuiltinDefaultDaemonConfigJson()
{
    return json{
        {"audio", {
            {"input", -1},
            {"input_device_hints", {"SPV Composite", "USB Audio"}},
            {"output", -1},
            {"output_device_hints", {"SPV Composite", "USB Audio"}},
            {"rate", 16000},
            {"channels", 4},
            {"speech_channel", 1},
            {"frames", 480},
            {"queue_chunks", 96},
        }},
        {"vad", {
            {"threshold", 0.3},
            {"stop_threshold", 0.2},
            {"silence_ms", 500},
            {"pre_speech_ms", 500},
            {"max_utterance_sec", 8},
        }},
        {"agc", {
            {"enabled", true},
            {"headroom_db", 6.0},
            {"max_gain_db", 18.0},
            {"initial_gain_db", 8.0},
        }},
        {"asr", {
            {"provider", "spacemit"},
            {"language", "zh"},
            {"model_dir", nullptr},
            {"warmup", true},
        }},
        {"dds", {
            {"enabled", true},
            {"domain_id", 0},
        }},
        {"ui", {
            {"enabled", true},
            {"user", "initer"},
            {"qt_qpa_platform", "wayland"},
            {"xdg_runtime_dir", "/run/user/996"},
            {"wayland_display", "wayland-0"},
            {"display", ":0"},
            {"gif_dir", nullptr},
            {"audio_dir", nullptr},
            {"extra_env", {
                {"MESA_LOADER_DRIVER_OVERRIDE", "pvr"},
                {"GST_GL_PLATFORM", "egl"},
                {"COGL_DRIVER", "gles2"},
                {"GDK_GL", "gles"},
                {"SAL_DISABLEGL", "1"},
            }},
        }},
        {"debug", {
            {"save_wav", false},
            {"save_wav_file", "~/.cache/ai-cubpet/asr_input.wav"},
            {"save_raw_wav", false},
            {"save_raw_wav_file", "~/.cache/ai-cubpet/raw_input.wav"},
        }},
        {"log_dir", "~/.cache/ai-cubpet/logs"},
        {"pid_file", "~/.cache/ai-cubpet/ai_cubpet_daemon.pid"},
    };
}

json LoadMergedDaemonConfigJson(const DaemonConfigLoadOptions& options)
{
    json merged = LoadDefaultJson(options.default_config_path);

    json board;
    if (LoadMatchingBoardJson(options, &board)) {
        merged.update(board, true);
    }

    if (!options.user_config_path.empty() && FileExists(options.user_config_path)) {
        merged.update(LoadJsonFile(options.user_config_path), true);
    }
    return merged;
}

DaemonConfig ParseDaemonConfigJson(const json& root)
{
    DaemonConfig cfg;
    if (auto it = root.find("audio"); it != root.end() && it->is_object()) {
        GetOpt(*it, "input", cfg.audio.input);
        GetOpt(*it, "input_device_hints", cfg.audio.input_device_hints);
        GetOpt(*it, "output", cfg.audio.output);
        GetOpt(*it, "output_device_hints", cfg.audio.output_device_hints);
        GetOpt(*it, "rate", cfg.audio.rate);
        GetOpt(*it, "channels", cfg.audio.channels);
        GetOpt(*it, "speech_channel", cfg.audio.speech_channel);
        GetOpt(*it, "frames", cfg.audio.frames);
        GetOpt(*it, "queue_chunks", cfg.audio.queue_chunks);
    }
    if (auto it = root.find("vad"); it != root.end() && it->is_object()) {
        GetOpt(*it, "threshold", cfg.vad.threshold);
        GetOpt(*it, "stop_threshold", cfg.vad.stop_threshold);
        GetOpt(*it, "silence_ms", cfg.vad.silence_ms);
        GetOpt(*it, "pre_speech_ms", cfg.vad.pre_speech_ms);
        GetOpt(*it, "max_utterance_sec", cfg.vad.max_utterance_sec);
    }
    if (auto it = root.find("agc"); it != root.end() && it->is_object()) {
        GetOpt(*it, "enabled", cfg.agc.enabled);
        GetOpt(*it, "headroom_db", cfg.agc.headroom_db);
        GetOpt(*it, "max_gain_db", cfg.agc.max_gain_db);
        GetOpt(*it, "initial_gain_db", cfg.agc.initial_gain_db);
    }
    if (auto it = root.find("asr"); it != root.end() && it->is_object()) {
        GetOpt(*it, "provider", cfg.asr.provider);
        GetOpt(*it, "language", cfg.asr.language);
        GetOpt(*it, "model_dir", cfg.asr.model_dir);
        GetOpt(*it, "warmup", cfg.asr.warmup);
    }
    if (auto it = root.find("dds"); it != root.end() && it->is_object()) {
        GetOpt(*it, "enabled", cfg.dds.enabled);
        GetOpt(*it, "domain_id", cfg.dds.domain_id);
    }
    if (auto it = root.find("ui"); it != root.end() && it->is_object()) {
        GetOpt(*it, "enabled", cfg.ui.enabled);
        GetOpt(*it, "user", cfg.ui.user);
        GetOpt(*it, "qt_qpa_platform", cfg.ui.qt_qpa_platform);
        GetOpt(*it, "xdg_runtime_dir", cfg.ui.xdg_runtime_dir);
        GetOpt(*it, "wayland_display", cfg.ui.wayland_display);
        GetOpt(*it, "display", cfg.ui.display);
        GetOpt(*it, "gif_dir", cfg.ui.gif_dir);
        GetOpt(*it, "audio_dir", cfg.ui.audio_dir);
        if (auto env = it->find("extra_env"); env != it->end() && env->is_object()) {
            cfg.ui.extra_env.clear();
            for (auto jt = env->begin(); jt != env->end(); ++jt) {
                if (jt.value().is_string()) {
                    cfg.ui.extra_env[jt.key()] = jt.value().get<std::string>();
                }
            }
        }
    }
    if (auto it = root.find("debug"); it != root.end() && it->is_object()) {
        GetOpt(*it, "save_wav", cfg.debug.save_wav);
        GetOpt(*it, "save_wav_file", cfg.debug.save_wav_file);
        GetOpt(*it, "save_raw_wav", cfg.debug.save_raw_wav);
        GetOpt(*it, "save_raw_wav_file", cfg.debug.save_raw_wav_file);
    }
    GetOpt(root, "log_dir", cfg.log_dir);
    GetOpt(root, "pid_file", cfg.pid_file);

    cfg.debug.save_wav_file = ExpandUser(cfg.debug.save_wav_file);
    cfg.debug.save_raw_wav_file = ExpandUser(cfg.debug.save_raw_wav_file);
    cfg.log_dir = ExpandUser(cfg.log_dir);
    cfg.pid_file = ExpandUser(cfg.pid_file);
    return cfg;
}

DaemonConfig LoadDaemonConfig(const DaemonConfigLoadOptions& options)
{
    DaemonConfig cfg = ParseDaemonConfigJson(LoadMergedDaemonConfigJson(options));
    if (HasOldDefaultInputWithoutHints(options.user_config_path)) {
        cfg.audio.input = -1;
    }
    return cfg;
}

bool WriteDefaultDaemonConfig(const std::string& path,
    bool overwrite,
    const DaemonConfigLoadOptions& options)
{
    if (FileExists(path) && !overwrite) {
        return true;
    }
    if (!MakeDirs(ParentDir(path))) {
        return false;
    }

    DaemonConfigLoadOptions defaults_only = options;
    defaults_only.user_config_path.clear();

    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    out << LoadMergedDaemonConfigJson(defaults_only).dump(4) << "\n";
    return true;
}

}  // namespace ai_cubpet
