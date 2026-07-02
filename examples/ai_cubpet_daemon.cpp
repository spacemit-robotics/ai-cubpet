#include <nlohmann/json.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

namespace {

using json = nlohmann::json;

volatile sig_atomic_t g_should_stop = 0;

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

struct PidRecord {
    pid_t daemon_pid = -1;
    pid_t ui_pid = -1;
    pid_t voice_pid = -1;
    std::string log_path;
};

struct UiLaunchPlan {
    std::string program;
    std::string prefix;
    std::string run_user;
};

std::string HomeDir() {
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

std::string ExpandUser(const std::string& path) {
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

std::string ParentDir(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool MakeDirs(const std::string& path) {
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

bool FileExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool DirectoryExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

template <typename T>
void GetOpt(const json& j, const char* key, T& out) {  // NOLINT(runtime/references)
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        out = it->get<T>();
    }
}

json DefaultJson() {
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

std::string DefaultConfigDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg) {
            return std::string(xdg) + "/ai-cubpet";
        }
    }
    return HomeDir() + "/.config/ai-cubpet";
}

std::string DefaultConfigPath() {
    return DefaultConfigDir() + "/ai_cubpet.json";
}

void ParseConfigJson(const json& root, DaemonConfig& cfg) {  // NOLINT(runtime/references)
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
}

DaemonConfig LoadConfig(const std::string& path) {
    DaemonConfig cfg;
    std::ifstream in(path);
    if (!in.good()) {
        ParseConfigJson(DefaultJson(), cfg);
        return cfg;
    }
    json root = json::parse(in, nullptr, true, true);
    bool migrate_old_default_input = false;
    if (auto it = root.find("audio"); it != root.end() && it->is_object()) {
        const bool has_hints = it->find("input_device_hints") != it->end();
        if (!has_hints) {
            auto input_it = it->find("input");
            if (input_it != it->end() && input_it->is_number_integer() &&
                    input_it->get<int>() == 1) {
                migrate_old_default_input = true;
            }
        }
    }
    json merged = DefaultJson();
    merged.update(root, true);
    ParseConfigJson(merged, cfg);
    if (migrate_old_default_input) {
        cfg.audio.input = -1;
    }
    return cfg;
}

bool WriteDefaultConfig(const std::string& path, bool overwrite) {
    if (FileExists(path) && !overwrite) {
        return true;
    }
    if (!MakeDirs(ParentDir(path))) {
        std::cerr << "failed to create config dir: " << ParentDir(path) << "\n";
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) {
        std::cerr << "failed to write config: " << path << "\n";
        return false;
    }
    out << DefaultJson().dump(4) << "\n";
    return true;
}

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    localtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return std::string(buf);
}

std::string ReadlinkSelf() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "";
    }
    buf[n] = '\0';
    return std::string(buf);
}

std::string Dirname(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::string PrefixDir() {
    const std::string self = ReadlinkSelf();
    if (self.empty()) {
        return ".";
    }
    return Dirname(Dirname(self));
}

std::string BinPath(const std::string& name) {
    return PrefixDir() + "/bin/" + name;
}

std::string SharePath(const std::string& subdir) {
    return PrefixDir() + "/share/ai-cubpet/" + subdir;
}

std::string DefaultMediaCacheRoot() {
    if (const char* root = std::getenv("AI_CUBPET_ASSET_ROOT")) {
        if (*root) {
            return ExpandUser(root);
        }
    }
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        if (*xdg) {
            return ExpandUser(std::string(xdg) + "/models/assets");
        }
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) {
            return ExpandUser(std::string(home) + "/.cache/models/assets");
        }
    }
    return "/root/.cache/models/assets";
}

std::string DefaultMediaCacheDir(const std::string& subdir) {
    const std::string root = DefaultMediaCacheRoot();
    if (root.empty() || root == "/") {
        return root + subdir;
    }
    return root + "/" + subdir;
}

std::string DefaultMediaSourceDir(const std::string& subdir) {
    const std::string cached = DefaultMediaCacheDir(subdir);
    if (DirectoryExists(cached)) {
        return cached;
    }
    return SharePath(subdir);
}

std::string MediaSourceDir(const UiConfig& ui, const std::string& subdir) {
    if (subdir == "gif" && !ui.gif_dir.empty()) {
        return ExpandUser(ui.gif_dir);
    }
    if (subdir == "audio" && !ui.audio_dir.empty()) {
        return ExpandUser(ui.audio_dir);
    }
    return DefaultMediaSourceDir(subdir);
}

std::string MediaDownloadDir(const UiConfig& ui, const std::string& subdir) {
    if (subdir == "gif" && !ui.gif_dir.empty()) {
        return ExpandUser(ui.gif_dir);
    }
    if (subdir == "audio" && !ui.audio_dir.empty()) {
        return ExpandUser(ui.audio_dir);
    }
    return DefaultMediaCacheDir(subdir);
}

std::string AudioAssetDir(const UiConfig& ui) {
    return MediaDownloadDir(ui, "audio");
}

std::string LibPath() {
    return PrefixDir() + "/lib";
}

std::string LibPathForPrefix(const std::string& prefix) {
    return prefix + "/lib";
}

std::string ShareRootForPrefix(const std::string& prefix) {
    return prefix + "/share/ai-cubpet";
}

std::string PathJoin(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty() || lhs == "/") {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

bool NonEmptyRegularFileExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

const std::vector<std::string>& RequiredAudioAssets() {
    static const std::vector<std::string> assets = {
        "001_zh_daily_weather.wav",
        "002_en_daily_weather.wav",
        "003_zh_en_search.wav",
        "004_zh_selling_sausages.wav",
        "005_what_is_the_day_today.wav",
        "006_im_here.wav",
        "007_daily_response.wav",
        "008_happy.wav",
        "009_greet_move.wav",
        "010_funny.wav",
        "011_diz.wav",
        "012_heart.wav",
        "013_shake_head.wav",
        "014_vp_register_start.wav",
        "015_vp_register_fail.wav",
        "016_vp_register_done.wav",
        "017_vp_welcome.wav",
        "018_vp_like_you.wav",
        "019_vp_come_here.wav",
        "020_vp_good_night.wav",
        "021_vp_great.wav",
    };
    return assets;
}

const std::vector<std::string>& RequiredGifAssets() {
    static const std::vector<std::string> assets = {
        "01_daily.gif",
        "02_expect.gif",
        "03_diz.gif",
        "04_moist.gif",
        "05_heart.gif",
        "06_blink.gif",
        "07_angry.gif",
        "08_explore.gif",
        "09_funny.gif",
        "10_close_eye.gif",
        "11_squint.gif",
        "12_sleep.png",
    };
    return assets;
}

size_t CurlWriteFile(void* ptr, size_t size, size_t nmemb, void* userdata) {
    FILE* fp = static_cast<FILE*>(userdata);
    return std::fwrite(ptr, size, nmemb, fp) * size;
}

bool DownloadFile(const std::string& url, const std::string& dst) {
    if (!MakeDirs(ParentDir(dst))) {
        std::cerr << "failed to create asset directory: " << ParentDir(dst) << "\n";
        return false;
    }

    const std::string tmp = dst + ".download";
    FILE* fp = std::fopen(tmp.c_str(), "wb");
    if (!fp) {
        std::cerr << "failed to open asset temp file: " << tmp
                << ": " << std::strerror(errno) << "\n";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(fp);
        std::remove(tmp.c_str());
        std::cerr << "failed to initialize curl\n";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ai-cubpet-daemon/1.0");

    const CURLcode rc = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    const int close_rc = std::fclose(fp);

    if (rc != CURLE_OK || close_rc != 0 || !NonEmptyRegularFileExists(tmp)) {
        std::cerr << "failed to download asset: " << url
                << " (curl=" << curl_easy_strerror(rc)
                << ", http=" << response_code << ")\n";
        std::remove(tmp.c_str());
        return false;
    }

    if (std::rename(tmp.c_str(), dst.c_str()) != 0) {
        std::cerr << "failed to install asset: " << dst
                << ": " << std::strerror(errno) << "\n";
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool EnsureAssetGroup(const char* label,
                    const std::string& dir,
                    const std::string& base_url,
                    const std::vector<std::string>& required) {
    if (!MakeDirs(dir)) {
        std::cerr << "failed to create " << label << " asset directory: "
                << dir << "\n";
        return false;
    }

    bool ok = true;
    bool downloaded = false;
    for (const auto& name : required) {
        const std::string path = PathJoin(dir, name);
        if (NonEmptyRegularFileExists(path)) {
            continue;
        }
        const std::string url = base_url + name;
        std::cerr << "[info] downloading " << label << " asset: "
                << name << "\n";
        if (!DownloadFile(url, path)) {
            ok = false;
            break;
        }
        downloaded = true;
    }

    if (ok) {
        std::cerr << "[info] " << label << " assets ready: " << dir;
        if (downloaded) {
            std::cerr << " (downloaded missing files)";
        }
        std::cerr << "\n";
    }
    return ok;
}

bool EnsureMediaAssets(const UiConfig& ui, bool include_audio, bool include_gif) {
    constexpr const char* kAudioBaseUrl =
        "https://archive.spacemit.com/spacemit-ai/model_zoo/assets/audio/";
    constexpr const char* kGifBaseUrl =
        "https://archive.spacemit.com/spacemit-ai/model_zoo/assets/gif/";

    const CURLcode init_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init_rc != CURLE_OK) {
        std::cerr << "failed to initialize curl global state: "
                << curl_easy_strerror(init_rc) << "\n";
        return false;
    }

    bool ok = true;
    if (include_audio) {
        ok = EnsureAssetGroup("audio",
                AudioAssetDir(ui),
                kAudioBaseUrl,
                RequiredAudioAssets()) && ok;
    }
    if (include_gif) {
        ok = EnsureAssetGroup("GIF",
                MediaDownloadDir(ui, "gif"),
                kGifBaseUrl,
                RequiredGifAssets()) && ok;
    }
    curl_global_cleanup();
    return ok;
}

bool EnsureAudioAssets(const UiConfig& ui) {
    return EnsureMediaAssets(ui, true, false);
}

bool EnsureGifAssets(const UiConfig& ui) {
    return EnsureMediaAssets(ui, false, true);
}

bool StartsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool LibraryNameNeededForUi(const std::string& name) {
    return StartsWith(name, "libddsc") ||
            StartsWith(name, "libcyclonedds") ||
            StartsWith(name, "libdds_security");
}

std::vector<std::string> GlobPaths(const std::string& pattern) {
    std::vector<std::string> paths;
    glob_t g {};
    if (glob(pattern.c_str(), 0, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; ++i) {
            paths.emplace_back(g.gl_pathv[i]);
        }
    }
    globfree(&g);
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool IsDigits(const char* s) {
    if (!s || !*s) {
        return false;
    }
    for (; *s; ++s) {
        if (!std::isdigit(static_cast<unsigned char>(*s))) {
            return false;
        }
    }
    return true;
}

std::string XSocketPath(const std::string& display) {
    if (display.empty() || display[0] != ':') {
        return "";
    }
    size_t end = 1;
    while (end < display.size() &&
            std::isdigit(static_cast<unsigned char>(display[end]))) {
        ++end;
    }
    if (end == 1) {
        return "";
    }
    return "/tmp/.X11-unix/X" + display.substr(1, end - 1);
}

bool XDisplayAvailable(const std::string& display) {
    const std::string socket = XSocketPath(display);
    return !socket.empty() && FileExists(socket);
}

std::string DetectXDisplay() {
    DIR* dir = opendir("/tmp/.X11-unix");
    if (!dir) {
        return "";
    }
    std::vector<int> displays;
    while (dirent* ent = readdir(dir)) {
        if (ent->d_name[0] == 'X' && IsDigits(ent->d_name + 1)) {
            displays.push_back(std::atoi(ent->d_name + 1));
        }
    }
    closedir(dir);
    if (displays.empty()) {
        return "";
    }
    std::sort(displays.begin(), displays.end());
    return ":" + std::to_string(displays.front());
}

std::string SelectXDisplay(const UiConfig& cfg) {
    if (XDisplayAvailable(cfg.display)) {
        return cfg.display;
    }
    return DetectXDisplay();
}

std::string DetectXAuthority() {
    const std::vector<std::string> mutter = GlobPaths("/run/user/*/.mutter-Xwaylandauth.*");
    if (!mutter.empty()) {
        return mutter.front();
    }
    const std::string root_auth = HomeDir() + "/.Xauthority";
    if (FileExists(root_auth)) {
        return root_auth;
    }
    return "";
}

std::string RuntimeDirFromPath(const std::string& path) {
    if (path.rfind("/run/user/", 0) != 0) {
        return "";
    }
    const size_t slash = path.find('/', std::string("/run/user/").size());
    if (slash == std::string::npos) {
        return "";
    }
    return path.substr(0, slash);
}

std::string DetectWaylandRuntimeDir() {
    const std::vector<std::string> sockets = GlobPaths("/run/user/*/wayland-0");
    if (sockets.empty()) {
        return "";
    }
    return ParentDir(sockets.front());
}

std::string EnvWithLibDir(const std::string& lib_dir) {
    std::string value = lib_dir;
    if (const char* old = std::getenv("LD_LIBRARY_PATH")) {
        if (*old) {
            value += ":";
            value += old;
        }
    }
    return value;
}

void SetEnvValue(std::vector<std::pair<std::string, std::string>>& env,  // NOLINT(runtime/references)
                const std::string& key,
                const std::string& value) {
    for (auto& kv : env) {
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    }
    env.push_back({key, value});
}

std::string WaylandSocketPath(const UiConfig& cfg) {
    if (cfg.xdg_runtime_dir.empty() || cfg.wayland_display.empty()) {
        return "";
    }
    return cfg.xdg_runtime_dir + "/" + cfg.wayland_display;
}

bool WaylandSocketAvailable(const UiConfig& cfg) {
    const std::string socket = WaylandSocketPath(cfg);
    return !socket.empty() && FileExists(socket);
}

std::string UiPlatformForLaunch(const UiConfig& cfg) {
    if (cfg.qt_qpa_platform == "wayland") {
        if (WaylandSocketAvailable(cfg)) {
            return "wayland";
        }
        if (!SelectXDisplay(cfg).empty()) {
            return "xcb";
        }
        if (!DetectWaylandRuntimeDir().empty()) {
            return "wayland";
        }
        return "eglfs";
    }
    return cfg.qt_qpa_platform;
}

std::vector<std::string> UiPlatformCandidates(const UiConfig& cfg) {
    std::vector<std::string> platforms;
    auto add = [&platforms](const std::string& platform) {
        if (platform.empty()) {
            return;
        }
        for (const auto& existing : platforms) {
            if (existing == platform) {
                return;
            }
        }
        platforms.push_back(platform);
    };

    const std::string first = UiPlatformForLaunch(cfg);
    add(first);
    if (first != "wayland" && (WaylandSocketAvailable(cfg) ||
            !DetectWaylandRuntimeDir().empty())) {
        add("wayland");
    }
    if (first != "xcb" && !SelectXDisplay(cfg).empty()) {
        add("xcb");
    }
    add("eglfs");
    add("linuxfb");
    return platforms;
}

bool ProcessAlive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

bool WritePidFile(const std::string& path, const PidRecord& rec) {
    if (!MakeDirs(ParentDir(path))) {
        return false;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    json j = {
        {"daemon_pid", rec.daemon_pid},
        {"ui_pid", rec.ui_pid},
        {"voice_pid", rec.voice_pid},
        {"log_path", rec.log_path},
    };
    out << j.dump(4) << "\n";
    return true;
}

bool ReadPidFile(const std::string& path, PidRecord& rec) {  // NOLINT(runtime/references)
    std::ifstream in(path);
    if (!in.good()) {
        return false;
    }
    try {
        json j = json::parse(in);
        rec.daemon_pid = j.value("daemon_pid", -1);
        rec.ui_pid = j.value("ui_pid", -1);
        rec.voice_pid = j.value("voice_pid", -1);
        rec.log_path = j.value("log_path", "");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void SignalHandler(int) {
    g_should_stop = 1;
}

std::vector<char*> BuildArgv(const std::string& program,
                            const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

void ApplyEnv(const std::vector<std::pair<std::string, std::string>>& env) {
    for (const auto& kv : env) {
        setenv(kv.first.c_str(), kv.second.c_str(), 1);
    }
}

bool DropToUser(const std::string& user) {
    if (user.empty() || geteuid() != 0) {
        return true;
    }
    struct passwd pwbuf;
    struct passwd* pw = nullptr;
    char buf[4096];
    if (getpwnam_r(user.c_str(), &pwbuf, buf, sizeof(buf), &pw) != 0 || !pw) {
        std::cerr << "failed to resolve user: " << user << "\n";
        return false;
    }
    if (initgroups(user.c_str(), pw->pw_gid) != 0) {
        std::perror("initgroups");
        return false;
    }
    if (setgid(pw->pw_gid) != 0) {
        std::perror("setgid");
        return false;
    }
    if (setuid(pw->pw_uid) != 0) {
        std::perror("setuid");
        return false;
    }
    setenv("HOME", pw->pw_dir, 1);
    return true;
}

bool ResolveUser(const std::string& user, struct passwd& out_pw,  // NOLINT(runtime/references)
                std::string& reason) {  // NOLINT(runtime/references)
    struct passwd* pw = nullptr;
    char buf[4096];
    const int rc = getpwnam_r(user.c_str(), &out_pw, buf, sizeof(buf), &pw);
    if (rc != 0 || !pw) {
        reason = "user not found: " + user;
        return false;
    }
    return true;
}

bool RemoveTree(const std::string& path) {
    if (!StartsWith(path, "/tmp/ai-cubpet-ui-runtime-")) {
        std::cerr << "refuse to remove unexpected runtime path: " << path << "\n";
        return false;
    }

    struct stat st {};
    if (lstat(path.c_str(), &st) != 0) {
        return errno == ENOENT;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            return false;
        }
        bool ok = true;
        while (dirent* ent = readdir(dir)) {
            const std::string name = ent->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            if (!RemoveTree(PathJoin(path, name))) {
                ok = false;
                break;
            }
        }
        closedir(dir);
        if (!ok) {
            return false;
        }
        return rmdir(path.c_str()) == 0;
    }

    return unlink(path.c_str()) == 0;
}

bool CopyRegularFile(const std::string& src, const std::string& dst, mode_t mode) {
    const int in = open(src.c_str(), O_RDONLY);
    if (in < 0) {
        return false;
    }
    const int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (out < 0) {
        close(in);
        return false;
    }

    char buf[64 * 1024];
    bool ok = true;
    while (true) {
        const ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ok = false;
            break;
        }
        ssize_t written = 0;
        while (written < n) {
            const ssize_t m = write(out, buf + written, n - written);
            if (m < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ok = false;
                break;
            }
            written += m;
        }
        if (!ok) {
            break;
        }
    }
    if (close(out) != 0) {
        ok = false;
    }
    close(in);
    return ok;
}

bool CopySymlink(const std::string& src, const std::string& dst) {
    char target[4096];
    const ssize_t n = readlink(src.c_str(), target, sizeof(target) - 1);
    if (n < 0) {
        return false;
    }
    target[n] = '\0';
    unlink(dst.c_str());
    return symlink(target, dst.c_str()) == 0;
}

bool CopyPathRecursive(const std::string& src, const std::string& dst) {
    struct stat st {};
    if (lstat(src.c_str(), &st) != 0) {
        return false;
    }

    if (S_ISLNK(st.st_mode)) {
        return CopySymlink(src, dst);
    }
    if (S_ISREG(st.st_mode)) {
        return CopyRegularFile(src, dst, st.st_mode);
    }
    if (!S_ISDIR(st.st_mode)) {
        return true;
    }

    if (mkdir(dst.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
    }
    DIR* dir = opendir(src.c_str());
    if (!dir) {
        return false;
    }
    bool ok = true;
    while (dirent* ent = readdir(dir)) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!CopyPathRecursive(PathJoin(src, name), PathJoin(dst, name))) {
            ok = false;
            break;
        }
    }
    closedir(dir);
    return ok;
}

bool CopyUiLibraries(const std::string& src_lib, const std::string& dst_lib) {
    if (!MakeDirs(dst_lib)) {
        return false;
    }
    DIR* dir = opendir(src_lib.c_str());
    if (!dir) {
        return false;
    }
    bool ok = true;
    while (dirent* ent = readdir(dir)) {
        const std::string name = ent->d_name;
        if (name == "." || name == ".." || !LibraryNameNeededForUi(name)) {
            continue;
        }
        if (!CopyPathRecursive(PathJoin(src_lib, name), PathJoin(dst_lib, name))) {
            ok = false;
            break;
        }
    }
    closedir(dir);
    return ok;
}

bool PrepareUiRuntimeMirror(const struct passwd& pw,
                            const DaemonConfig& cfg,
                            UiLaunchPlan& plan) {  // NOLINT(runtime/references)
    const std::string runtime_prefix =
        "/tmp/ai-cubpet-ui-runtime-" + std::to_string(static_cast<long long>(pw.pw_uid));
    if (!RemoveTree(runtime_prefix)) {
        std::cerr << "failed to clean UI runtime mirror: " << runtime_prefix << "\n";
        return false;
    }
    if (!MakeDirs(PathJoin(runtime_prefix, "bin")) ||
            !MakeDirs(PathJoin(runtime_prefix, "lib")) ||
            !MakeDirs(PathJoin(runtime_prefix, "share"))) {
        std::cerr << "failed to create UI runtime mirror: " << runtime_prefix << "\n";
        return false;
    }

    if (!CopyPathRecursive(BinPath("ai-cubpet-ui"),
                            PathJoin(PathJoin(runtime_prefix, "bin"), "ai-cubpet-ui"))) {
        std::cerr << "failed to copy ai-cubpet-ui into runtime mirror\n";
        return false;
    }
    if (!CopyUiLibraries(LibPath(), PathJoin(runtime_prefix, "lib"))) {
        std::cerr << "failed to copy UI DDS libraries into runtime mirror\n";
        return false;
    }

    const std::string runtime_share = ShareRootForPrefix(runtime_prefix);
    if (!MakeDirs(runtime_share)) {
        std::cerr << "failed to create UI runtime share directory\n";
        return false;
    }

    const std::string gif_src = MediaSourceDir(cfg.ui, "gif");
    std::cerr << "[info] ai-cubpet-ui gif source=" << gif_src << "\n";
    if (!CopyPathRecursive(gif_src, PathJoin(runtime_share, "gif"))) {
        std::cerr << "failed to copy UI GIF assets into runtime mirror\n";
        return false;
    }
    const std::string idl_src = SharePath("idl");
    if (DirectoryExists(idl_src) &&
            !CopyPathRecursive(idl_src, PathJoin(runtime_share, "idl"))) {
        std::cerr << "failed to copy UI IDL assets into runtime mirror\n";
        return false;
    }

    plan.program = PathJoin(PathJoin(runtime_prefix, "bin"), "ai-cubpet-ui");
    plan.prefix = runtime_prefix;
    return true;
}

bool CanAccessAsUser(const std::string& user,
                    const std::string& program,
                    const std::string& lib_dir,
                    std::string& reason) {  // NOLINT(runtime/references)
    if (user.empty() || user == "root" || geteuid() != 0) {
        return true;
    }

    struct passwd pw {};
    if (!ResolveUser(user, pw, reason)) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        reason = "fork failed while checking user access";
        return false;
    }
    if (pid == 0) {
        if (initgroups(user.c_str(), pw.pw_gid) != 0 ||
                setgid(pw.pw_gid) != 0 ||
                setuid(pw.pw_uid) != 0) {
            _exit(126);
        }
        if (access(program.c_str(), X_OK) != 0) {
            _exit(127);
        }
        if (access(lib_dir.c_str(), X_OK) != 0) {
            _exit(125);
        }
        _exit(0);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            reason = "waitpid failed while checking user access";
            return false;
        }
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }
    if (WIFEXITED(status)) {
        switch (WEXITSTATUS(status)) {
            case 125:
                reason = "user cannot access SDK lib directory: " + lib_dir;
                break;
            case 126:
                reason = "failed to switch to user: " + user;
                break;
            case 127:
                reason = "user cannot execute UI binary: " + program;
                break;
            default:
                reason = "user access check failed";
                break;
        }
    } else {
        reason = "user access check terminated unexpectedly";
    }
    return false;
}

UiLaunchPlan BuildUiLaunchPlan(const DaemonConfig& cfg,
                                const std::string& ui_bin) {
    UiLaunchPlan plan;
    plan.program = ui_bin;
    plan.prefix = PrefixDir();

    if (cfg.ui.user.empty() || cfg.ui.user == "root" || geteuid() != 0) {
        plan.run_user = cfg.ui.user;
        return plan;
    }

    std::string reason;
    if (CanAccessAsUser(cfg.ui.user, ui_bin, LibPath(), reason)) {
        plan.run_user = cfg.ui.user;
        return plan;
    }

    struct passwd pw {};
    std::string user_reason;
    if (ResolveUser(cfg.ui.user, pw, user_reason)) {
        std::cerr << "[warn] UI user '" << cfg.ui.user
                << "' cannot access current SDK prefix: " << reason << "\n";
        std::cerr << "[info] preparing ai-cubpet-ui runtime mirror for user "
                << cfg.ui.user << "\n";
        if (PrepareUiRuntimeMirror(pw, cfg, plan)) {
            std::string mirror_reason;
            if (CanAccessAsUser(cfg.ui.user,
                                plan.program,
                                LibPathForPrefix(plan.prefix),
                                mirror_reason)) {
                plan.run_user = cfg.ui.user;
                std::cerr << "[info] ai-cubpet-ui runtime mirror="
                        << plan.prefix << "\n";
                return plan;
            }
            std::cerr << "[warn] UI runtime mirror is not usable: "
                    << mirror_reason << "\n";
        }
    } else {
        reason = user_reason;
    }

    std::cerr << "[warn] UI user '" << cfg.ui.user
            << "' is not usable for this SDK prefix: " << reason << "\n";
    std::cerr << "[warn] falling back to current user for ai-cubpet-ui\n";
    plan.program = ui_bin;
    plan.prefix = PrefixDir();
    plan.run_user.clear();
    return plan;
}

pid_t Spawn(const std::string& program,
            const std::vector<std::string>& args,
            const std::vector<std::pair<std::string, std::string>>& env,
            const std::string& run_as_user = "") {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        setpgid(0, getppid());
        ApplyEnv(env);
        if (!DropToUser(run_as_user)) {
            _exit(126);
        }
        std::vector<char*> argv = BuildArgv(program, args);
        execv(program.c_str(), argv.data());
        std::perror(("execv " + program).c_str());
        _exit(127);
    }
    return pid;
}

void WriteStartupStatus(int fd, char status) {
    if (fd >= 0) {
        (void)write(fd, &status, 1);
    }
}

std::vector<std::pair<std::string, std::string>>
CommonEnv(const DaemonConfig& cfg) {
    std::vector<std::pair<std::string, std::string>> env = {
        {"LD_LIBRARY_PATH", EnvWithLibDir(LibPath())},
        {"AI_CUBPET_DDS_DOMAIN_ID", std::to_string(cfg.dds.domain_id)},
    };
    return env;
}

std::vector<std::pair<std::string, std::string>>
UiEnv(const DaemonConfig& cfg,
    const std::string& ui_prefix,
    const std::string& platform_override = "") {
    std::vector<std::pair<std::string, std::string>> env = {
        {"LD_LIBRARY_PATH", EnvWithLibDir(LibPathForPrefix(ui_prefix))},
        {"AI_CUBPET_DDS_DOMAIN_ID", std::to_string(cfg.dds.domain_id)},
    };
    const std::string share_root = ShareRootForPrefix(ui_prefix);
    const bool using_runtime_mirror = StartsWith(ui_prefix, "/tmp/ai-cubpet-ui-runtime-");
    const std::string gif_dir = using_runtime_mirror
        ? PathJoin(share_root, "gif")
        : MediaSourceDir(cfg.ui, "gif");
    const std::string platform = platform_override.empty() ? UiPlatformForLaunch(cfg.ui) : platform_override;
    env.push_back({"AI_CUBPET_GIF_DIR", gif_dir});
    env.push_back({"GIF_PATH", gif_dir});
    if (!platform.empty()) {
        env.push_back({"QT_QPA_PLATFORM", platform});
    }

    if (platform == "xcb") {
        const std::string display = SelectXDisplay(cfg.ui);
        const std::string xauthority = DetectXAuthority();
        if (!display.empty()) {
            env.push_back({"DISPLAY", display});
        }
        if (!xauthority.empty()) {
            env.push_back({"XAUTHORITY", xauthority});
            const std::string runtime_dir = RuntimeDirFromPath(xauthority);
            if (!runtime_dir.empty()) {
                env.push_back({"XDG_RUNTIME_DIR", runtime_dir});
                env.push_back({"DBUS_SESSION_BUS_ADDRESS", "unix:path=" + runtime_dir + "/bus"});
            }
        }
    } else if (platform == "wayland") {
        std::string runtime_dir = cfg.ui.xdg_runtime_dir;
        std::string wayland_display = cfg.ui.wayland_display;
        if (!WaylandSocketAvailable(cfg.ui)) {
            const std::string detected_runtime = DetectWaylandRuntimeDir();
            if (!detected_runtime.empty()) {
                runtime_dir = detected_runtime;
                wayland_display = "wayland-0";
            }
        }
        if (!runtime_dir.empty()) {
            env.push_back({"XDG_RUNTIME_DIR", runtime_dir});
            env.push_back({"DBUS_SESSION_BUS_ADDRESS", "unix:path=" + runtime_dir + "/bus"});
        }
        if (!wayland_display.empty()) {
            env.push_back({"WAYLAND_DISPLAY", wayland_display});
        }
    } else {
        if (!cfg.ui.xdg_runtime_dir.empty()) {
            env.push_back({"XDG_RUNTIME_DIR", cfg.ui.xdg_runtime_dir});
        }
    }
    for (const auto& kv : cfg.ui.extra_env) {
        env.push_back(kv);
    }
    if (platform == "eglfs") {
        SetEnvValue(env, "QT_QPA_EGLFS_KMS_NO_EVENT_READER_THREAD", "1");
        SetEnvValue(env, "QT_QPA_EGLFS_KMS_ATOMIC", "1");
        SetEnvValue(env, "QT_QPA_EGLFS_MIPI_SWITCH", "1");
    }
    return env;
}

std::vector<std::string> VoiceArgs(const DaemonConfig& cfg) {
    std::vector<std::string> args = {
        "--input", std::to_string(cfg.audio.input),
        "--output", std::to_string(cfg.audio.output),
        "--rate", std::to_string(cfg.audio.rate),
        "--channels", std::to_string(cfg.audio.channels),
        "--speech-channel", std::to_string(cfg.audio.speech_channel),
        "--frames", std::to_string(cfg.audio.frames),
        "--queue-chunks", std::to_string(cfg.audio.queue_chunks),
        "--vad-threshold", std::to_string(cfg.vad.threshold),
        "--vad-stop-threshold", std::to_string(cfg.vad.stop_threshold),
        "--silence-ms", std::to_string(cfg.vad.silence_ms),
        "--pre-speech-ms", std::to_string(cfg.vad.pre_speech_ms),
        "--max-utterance-sec", std::to_string(cfg.vad.max_utterance_sec),
        "--provider", cfg.asr.provider,
        "--language", cfg.asr.language,
        cfg.agc.enabled ? "--agc" : "--no-agc",
        "--agc-headroom-db", std::to_string(cfg.agc.headroom_db),
        "--agc-max-gain-db", std::to_string(cfg.agc.max_gain_db),
        "--agc-initial-gain-db", std::to_string(cfg.agc.initial_gain_db),
    };
    if (cfg.audio.input < 0) {
        args.push_back("--clear-input-device-hints");
        for (const auto& hint : cfg.audio.input_device_hints) {
            if (!hint.empty()) {
                args.push_back("--input-device-hint");
                args.push_back(hint);
            }
        }
    }
    if (cfg.audio.output < 0) {
        args.push_back("--clear-output-device-hints");
        for (const auto& hint : cfg.audio.output_device_hints) {
            if (!hint.empty()) {
                args.push_back("--output-device-hint");
                args.push_back(hint);
            }
        }
    }
    if (!cfg.dds.enabled) {
        args.push_back("--no-ui-dds");
    }
    if (!cfg.asr.model_dir.empty()) {
        args.push_back("--model-dir");
        args.push_back(ExpandUser(cfg.asr.model_dir));
    }
    if (!cfg.asr.warmup) {
        args.push_back("--no-warmup");
    }
    if (cfg.debug.save_wav) {
        args.push_back("--save-wav");
        args.push_back(cfg.debug.save_wav_file);
    }
    if (cfg.debug.save_raw_wav) {
        args.push_back("--save-raw-wav");
        args.push_back(cfg.debug.save_raw_wav_file);
    }
    return args;
}

std::vector<std::pair<std::string, std::string>>
VoiceEnv(const DaemonConfig& cfg) {
    std::vector<std::pair<std::string, std::string>> env = CommonEnv(cfg);
    const std::string gif_dir = MediaSourceDir(cfg.ui, "gif");
    const std::string audio_dir = AudioAssetDir(cfg.ui);
    env.push_back({"AI_CUBPET_GIF_DIR", gif_dir});
    env.push_back({"AI_CUBPET_AUDIO_DIR", audio_dir});
    return env;
}

pid_t SpawnUiWithFallbacks(const DaemonConfig& cfg,
                            const UiLaunchPlan& ui_plan) {
    const std::vector<std::string> platforms = UiPlatformCandidates(cfg.ui);
    for (const auto& platform : platforms) {
        std::cerr << "[info] starting ai-cubpet-ui with QT_QPA_PLATFORM="
                << platform << "\n";
        pid_t pid = Spawn(ui_plan.program,
                        {},
                        UiEnv(cfg, ui_plan.prefix, platform),
                        ui_plan.run_user);
        if (pid < 0) {
            std::perror("spawn ai-cubpet-ui");
            continue;
        }

        usleep(1200 * 1000);
        int status = 0;
        pid_t dead = waitpid(pid, &status, WNOHANG);
        if (dead == 0) {
            std::cerr << "[info] ai-cubpet-ui started, pid=" << pid
                    << ", platform=" << platform << "\n";
            return pid;
        }
        if (dead == pid) {
            std::cerr << "[warn] ai-cubpet-ui exited during startup, platform="
                    << platform << ", status=" << status << "\n";
            continue;
        }
        if (dead < 0 && errno == ECHILD) {
            std::cerr << "[warn] ai-cubpet-ui startup state unavailable, platform="
                    << platform << "\n";
            continue;
        }
        std::cerr << "[warn] waitpid failed for ai-cubpet-ui, platform="
                << platform << ": " << std::strerror(errno) << "\n";
    }
    return -1;
}

int WaitForStartup(int startup_read_fd,
                    const std::string& pid_file,
                    const std::string& log_path) {
    bool startup_failed = false;
    constexpr int kStartupWaitIterations = 3000;
    for (int i = 0; i < kStartupWaitIterations; ++i) {
        char status = 0;
        ssize_t n = read(startup_read_fd, &status, 1);
        if (n == 1) {
            startup_failed = status != '1';
            break;
        }
        if (n == 0) {
            startup_failed = true;
            break;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            startup_failed = true;
            break;
        }
        usleep(100 * 1000);
    }
    close(startup_read_fd);

    PidRecord rec;
    if (startup_failed || !ReadPidFile(pid_file, rec) ||
            !ProcessAlive(rec.daemon_pid)) {
        std::cerr << "ai_cubpet_daemon failed to start\n";
        std::cerr << "  log: " << log_path << "\n";
        return 1;
    }

    std::cout << "ai_cubpet_daemon started.\n";
    std::cout << "  daemon pid: " << rec.daemon_pid << "\n";
    if (rec.ui_pid > 0) {
        std::cout << "  ui pid:     " << rec.ui_pid << "\n";
    }
    std::cout << "  voice pid:  " << rec.voice_pid << "\n";
    std::cout << "  log:        " << log_path << "\n";
    return 0;
}

void ShutdownChildren(pid_t ui_pid, pid_t voice_pid) {
    if (voice_pid > 0) {
        kill(voice_pid, SIGTERM);
    }
    if (ui_pid > 0) {
        kill(ui_pid, SIGTERM);
    }
    for (int i = 0; i < 20; ++i) {
        bool voice_alive = voice_pid > 0 && ProcessAlive(voice_pid);
        bool ui_alive = ui_pid > 0 && ProcessAlive(ui_pid);
        if (!voice_alive && !ui_alive) {
            break;
        }
        usleep(250 * 1000);
    }
    if (voice_pid > 0 && ProcessAlive(voice_pid)) {
        kill(voice_pid, SIGKILL);
    }
    if (ui_pid > 0 && ProcessAlive(ui_pid)) {
        kill(ui_pid, SIGKILL);
    }
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

int DaemonMain(const DaemonConfig& cfg, int startup_write_fd,
                const std::string& pid_file, const std::string& log_path) {
    static_cast<void>(chdir("/"));
    umask(022);

    int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        WriteStartupStatus(startup_write_fd, '0');
        return 1;
    }
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    int devnull = open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
    }
    close(fd);

    setpgid(0, 0);

    struct sigaction sa {};
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    std::cerr << "[info] ai_cubpet_daemon starting\n";
    std::cerr << "[info] prefix=" << PrefixDir() << "\n";

    if (!EnsureAudioAssets(cfg.ui)) {
        std::cerr << "failed to prepare ai-cubpet audio assets\n";
        WriteStartupStatus(startup_write_fd, '0');
        return 1;
    }

    pid_t ui_pid = -1;
    if (cfg.ui.enabled) {
        if (!EnsureGifAssets(cfg.ui)) {
            std::cerr << "failed to prepare ai-cubpet GIF assets\n";
            WriteStartupStatus(startup_write_fd, '0');
            return 1;
        }
        const std::string ui_bin = BinPath("ai-cubpet-ui");
        if (!FileExists(ui_bin)) {
            std::cerr << "missing UI binary: " << ui_bin << "\n";
            WriteStartupStatus(startup_write_fd, '0');
            return 1;
        }
        const UiLaunchPlan ui_plan = BuildUiLaunchPlan(cfg, ui_bin);
        if (!ui_plan.run_user.empty()) {
            std::cerr << "[info] ai-cubpet-ui run user=" << ui_plan.run_user << "\n";
        }
        if (ui_plan.program != ui_bin) {
            std::cerr << "[info] ai-cubpet-ui program=" << ui_plan.program << "\n";
        }
        const std::string ui_platform = UiPlatformForLaunch(cfg.ui);
        if (ui_platform != cfg.ui.qt_qpa_platform) {
            std::cerr << "[warn] QT_QPA_PLATFORM fallback: "
                    << cfg.ui.qt_qpa_platform << " -> " << ui_platform
                    << " (missing " << WaylandSocketPath(cfg.ui) << ")\n";
        }
        ui_pid = SpawnUiWithFallbacks(cfg, ui_plan);
        if (ui_pid < 0) {
            std::cerr << "failed to start ai-cubpet-ui with any Qt platform\n";
            WriteStartupStatus(startup_write_fd, '0');
            return 1;
        }
    }

    const std::string voice_bin = BinPath("ai-cubpet");
    if (!FileExists(voice_bin)) {
        std::cerr << "missing voice binary: " << voice_bin << "\n";
        WriteStartupStatus(startup_write_fd, '0');
        ShutdownChildren(ui_pid, -1);
        return 1;
    }

    pid_t voice_pid = Spawn(voice_bin, VoiceArgs(cfg), VoiceEnv(cfg));
    if (voice_pid < 0) {
        std::perror("spawn ai-cubpet");
        WriteStartupStatus(startup_write_fd, '0');
        ShutdownChildren(ui_pid, -1);
        return 1;
    }
    std::cerr << "[info] ai-cubpet started, pid=" << voice_pid << "\n";

    PidRecord rec;
    rec.daemon_pid = getpid();
    rec.ui_pid = ui_pid;
    rec.voice_pid = voice_pid;
    rec.log_path = log_path;
    if (!WritePidFile(pid_file, rec)) {
        std::cerr << "failed to write pid file: " << pid_file << "\n";
        WriteStartupStatus(startup_write_fd, '0');
        ShutdownChildren(ui_pid, voice_pid);
        return 1;
    }
    WriteStartupStatus(startup_write_fd, '1');
    close(startup_write_fd);

    while (!g_should_stop) {
        int status = 0;
        pid_t dead = waitpid(-1, &status, 0);
        if (dead < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (dead == ui_pid) {
            std::cerr << "[warn] ai-cubpet-ui exited, status=" << status << "\n";
            ui_pid = -1;
            if (voice_pid > 0) {
                kill(voice_pid, SIGTERM);
            }
            break;
        }
        if (dead == voice_pid) {
            std::cerr << "[warn] ai-cubpet exited, status=" << status << "\n";
            voice_pid = -1;
            if (ui_pid > 0) {
                kill(ui_pid, SIGTERM);
            }
            break;
        }
    }

    std::cerr << "[info] ai_cubpet_daemon shutting down\n";
    ShutdownChildren(ui_pid, voice_pid);
    unlink(pid_file.c_str());
    return 0;
}

int CmdStart(const std::string& config_path) {
    if (!WriteDefaultConfig(config_path, false)) {
        return 1;
    }

    DaemonConfig cfg;
    try {
        cfg = LoadConfig(config_path);
    } catch (const std::exception& e) {
        std::cerr << "failed to load config: " << e.what() << "\n";
        return 1;
    }

    PidRecord existing;
    if (ReadPidFile(cfg.pid_file, existing)) {
        if (ProcessAlive(existing.daemon_pid)) {
            std::cerr << "ai_cubpet_daemon already running (pid="
                    << existing.daemon_pid << ")\n";
            return 1;
        }
        unlink(cfg.pid_file.c_str());
    }

    if (!MakeDirs(cfg.log_dir)) {
        std::cerr << "failed to create log dir: " << cfg.log_dir << "\n";
        return 1;
    }
    if (cfg.debug.save_wav) {
        MakeDirs(ParentDir(cfg.debug.save_wav_file));
    }
    if (cfg.debug.save_raw_wav) {
        MakeDirs(ParentDir(cfg.debug.save_raw_wav_file));
    }
    const std::string log_path = cfg.log_dir + "/ai_cubpet-" + Timestamp() + ".log";

    int startup_pipe[2];
    if (pipe(startup_pipe) != 0) {
        std::perror("pipe");
        return 1;
    }
    fcntl(startup_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(startup_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t p1 = fork();
    if (p1 < 0) {
        std::perror("fork");
        close(startup_pipe[0]);
        close(startup_pipe[1]);
        return 1;
    }
    if (p1 > 0) {
        close(startup_pipe[1]);
        return WaitForStartup(startup_pipe[0], cfg.pid_file, log_path);
    }

    close(startup_pipe[0]);
    if (setsid() < 0) {
        WriteStartupStatus(startup_pipe[1], '0');
        _exit(1);
    }
    pid_t p2 = fork();
    if (p2 < 0) {
        WriteStartupStatus(startup_pipe[1], '0');
        _exit(1);
    }
    if (p2 > 0) {
        close(startup_pipe[1]);
        _exit(0);
    }
    int rc = DaemonMain(cfg, startup_pipe[1], cfg.pid_file, log_path);
    _exit(rc);
}

int CmdStop(const std::string& config_path) {
    DaemonConfig cfg = LoadConfig(config_path);
    PidRecord rec;
    if (!ReadPidFile(cfg.pid_file, rec)) {
        std::cout << "ai_cubpet_daemon: not running\n";
        return 0;
    }
    if (!ProcessAlive(rec.daemon_pid)) {
        std::cout << "ai_cubpet_daemon: not running, cleaning stale pid file\n";
        if (rec.ui_pid > 0 && ProcessAlive(rec.ui_pid)) {
            kill(rec.ui_pid, SIGKILL);
        }
        if (rec.voice_pid > 0 && ProcessAlive(rec.voice_pid)) {
            kill(rec.voice_pid, SIGKILL);
        }
        unlink(cfg.pid_file.c_str());
        return 0;
    }

    std::cout << "stopping ai_cubpet_daemon pid=" << rec.daemon_pid << " ...";
    std::cout.flush();
    kill(rec.daemon_pid, SIGTERM);
    killpg(rec.daemon_pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        if (!ProcessAlive(rec.daemon_pid)) {
            break;
        }
        usleep(250 * 1000);
    }
    if (ProcessAlive(rec.daemon_pid)) {
        std::cout << " force";
        kill(rec.daemon_pid, SIGKILL);
        killpg(rec.daemon_pid, SIGKILL);
        usleep(500 * 1000);
    }
    if (rec.ui_pid > 0 && ProcessAlive(rec.ui_pid)) {
        kill(rec.ui_pid, SIGKILL);
    }
    if (rec.voice_pid > 0 && ProcessAlive(rec.voice_pid)) {
        kill(rec.voice_pid, SIGKILL);
    }
    unlink(cfg.pid_file.c_str());
    std::cout << " OK\n";
    return 0;
}

int CmdStatus(const std::string& config_path) {
    DaemonConfig cfg = LoadConfig(config_path);
    PidRecord rec;
    if (!ReadPidFile(cfg.pid_file, rec) || !ProcessAlive(rec.daemon_pid)) {
        std::cout << "ai_cubpet_daemon: not running\n";
        return 1;
    }
    std::cout << "ai_cubpet_daemon: running\n";
    std::cout << "  daemon pid: " << rec.daemon_pid << "\n";
    std::cout << "  ui pid:     " << rec.ui_pid
            << (ProcessAlive(rec.ui_pid) ? "" : " (DEAD)") << "\n";
    std::cout << "  voice pid:  " << rec.voice_pid
            << (ProcessAlive(rec.voice_pid) ? "" : " (DEAD)") << "\n";
    std::cout << "  log:        " << rec.log_path << "\n";
    return 0;
}

int CmdLogs(const std::string& config_path) {
    DaemonConfig cfg = LoadConfig(config_path);
    PidRecord rec;
    if (!ReadPidFile(cfg.pid_file, rec) || rec.log_path.empty()) {
        std::cerr << "ai_cubpet_daemon: not running or no log path\n";
        return 1;
    }
    execlp("tail", "tail", "-n", "200", "-f", rec.log_path.c_str(),
            static_cast<char*>(nullptr));
    std::perror("execlp tail");
    return 127;
}

std::string ConfigPathFromArgs(int argc, char** argv, int* cmd_index) {
    std::string config_path = DefaultConfigPath();
    *cmd_index = 1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = ExpandUser(argv[++i]);
            *cmd_index = i + 1;
        } else {
            *cmd_index = i;
            break;
        }
    }
    return config_path;
}

void PrintUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [--config FILE] <command>\n"
        << "\n"
        << "Commands:\n"
        << "  start                 Start UI and voice processes\n"
        << "  stop                  Stop daemon and child processes\n"
        << "  restart               Stop then start\n"
        << "  status                Show daemon state\n"
        << "  logs                  Follow current daemon log\n"
        << "  config-init [--force] Write default config\n"
        << "  config-show           Print merged config\n"
        << "\n"
        << "Default config: " << DefaultConfigPath() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    int cmd_index = 1;
    std::string config_path = ConfigPathFromArgs(argc, argv, &cmd_index);

    if (cmd_index >= argc || std::string(argv[cmd_index]) == "-h" ||
            std::string(argv[cmd_index]) == "--help") {
        PrintUsage(argv[0]);
        return cmd_index >= argc ? 1 : 0;
    }

    const std::string cmd = argv[cmd_index];
    try {
        if (cmd == "start") {
            return CmdStart(config_path);
        }
        if (cmd == "stop") {
            return CmdStop(config_path);
        }
        if (cmd == "restart") {
            int rc = CmdStop(config_path);
            return rc == 0 ? CmdStart(config_path) : rc;
        }
        if (cmd == "status") {
            return CmdStatus(config_path);
        }
        if (cmd == "logs") {
            return CmdLogs(config_path);
        }
        if (cmd == "config-init") {
            bool force = false;
            for (int i = cmd_index + 1; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--force") {
                    force = true;
                } else {
                    std::cerr << "unknown argument: " << arg << "\n";
                    return 2;
                }
            }
            if (!WriteDefaultConfig(config_path, force)) {
                return 1;
            }
            std::cout << "config: " << config_path << "\n";
            return 0;
        }
        if (cmd == "config-show") {
            json merged = DefaultJson();
            std::ifstream in(config_path);
            if (in.good()) {
                json user = json::parse(in, nullptr, true, true);
                merged.update(user, true);
            }
            std::cout << merged.dump(4) << "\n";
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    std::cerr << "unknown command: " << cmd << "\n";
    PrintUsage(argv[0]);
    return 2;
}
