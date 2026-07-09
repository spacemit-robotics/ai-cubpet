#include "cubpet_voice_demo.hpp"

#include "cubpet_audio_pipeline.hpp"
#include "cubpet_action_rate_limiter.hpp"
#include "cubpet_action_controller.hpp"
#ifdef AI_CUBPET_USE_WEBRTC_AEC
#include "cubpet_aec_pipeline.hpp"
#endif
#include "cubpet_doa_runtime.hpp"
#include "cubpet_environment_monitors.hpp"
#include "cubpet_keywords.hpp"
#include "cubpet_led_controller.hpp"
#include "cubpet_motor_actions.hpp"
#include "cubpet_peripheral_manager.hpp"
#include "cubpet_toy_state_machine.hpp"
#include "cubpet_touch_gpio_monitor.hpp"
#include "cubpet_voice_interaction_gate.hpp"
#include "cubpet_wake_gpio_monitor.hpp"
#include "cubpet_wifi_provisioning.hpp"
#ifdef AI_CUBPET_USE_DDS
#include "cubpet_ui_publisher.hpp"
#endif

#include "asr_service.h"
#include "audio_base.hpp"
#include "audio_duplex.hpp"
#include "audio_resampler.hpp"
#include "vad_service.h"
#ifdef AI_CUBPET_USE_WEBRTC_AEC
#include <webrtc/modules/audio_processing/include/audio_processing.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>  // NOLINT(build/c++11)
#include <cmath>
#include <condition_variable>  // NOLINT(build/c++11)
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>  // NOLINT(build/c++11)
#include <queue>
#include <string>
#include <sstream>
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ai_cubpet {
namespace {

constexpr int kModelSampleRate = 16000;
constexpr int kDefaultCaptureRate = 48000;
constexpr int kDefaultCaptureChannels = 2;
constexpr int kDefaultFramesPerBuffer = 480;
constexpr int kPlaybackSampleRate = 16000;
constexpr int kPlaybackChannels = 2;
constexpr size_t kVadFrameSize = 512;
constexpr auto kContinuousIdleTimeout = std::chrono::minutes(3);
constexpr auto kStateTimerInterval = std::chrono::seconds(1);
constexpr auto kMotorActionInterval = std::chrono::seconds(2);

std::atomic<bool> g_running{true};

void SignalHandler(int)
{
    g_running = false;
}

std::string Timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_now{};
    localtime_r(&tt, &tm_now);

    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << tm_now.tm_hour << ":"
        << std::setw(2) << tm_now.tm_min << ":" << std::setw(2) << tm_now.tm_sec
        << "." << std::setw(3) << ms.count() << "]";
    return oss.str();
}

bool ReadNextArg(int argc, char** argv, int* index, const char** value)
{
    if (*index + 1 >= argc) {
        return false;
    }
    ++(*index);
    *value = argv[*index];
    return true;
}

bool ParseIntArg(int argc, char** argv, int* index, const char* name, int* out)
{
    const char* value = nullptr;
    if (!ReadNextArg(argc, argv, index, &value)) {
        std::cerr << name << " requires a value" << std::endl;
        return false;
    }
    *out = std::atoi(value);
    return true;
}

bool ParseFloatArg(int argc, char** argv, int* index, const char* name, float* out)
{
    const char* value = nullptr;
    if (!ReadNextArg(argc, argv, index, &value)) {
        std::cerr << name << " requires a value" << std::endl;
        return false;
    }
    *out = std::strtof(value, nullptr);
    return true;
}

bool ParseNoiseSuppressionLevel(const std::string& value, int* out)
{
    if (value == "low") {
        *out = 0;
        return true;
    }
    if (value == "moderate") {
        *out = 1;
        return true;
    }
    if (value == "high") {
        *out = 2;
        return true;
    }
    if (value == "very-high" || value == "very_high") {
        *out = 3;
        return true;
    }
    return false;
}

struct VoiceDemoOptions {
    int input_device = -1;
    std::vector<std::string> input_device_hints = {"SPV Composite", "USB Audio"};
    int output_device = -1;
    std::vector<std::string> output_device_hints = {"SPV Composite", "USB Audio"};
    int capture_rate = kDefaultCaptureRate;
    int capture_channels = kDefaultCaptureChannels;
    int speech_channel = 1;
    int frames_per_buffer = kDefaultFramesPerBuffer;
    int duration_seconds = 0;
    int queue_chunks = 96;
    bool enable_aec = false;
    bool enable_ui_dds = true;
    int reference_device = 1;
    int reference_channels = 1;
    int reference_channel = 1;
    int aec_delay_ms = 0;
    bool aec_noise_suppression = true;
    int aec_noise_suppression_level = 0;
    bool aec_high_pass_filter = true;
    bool aec_gain_control = false;

    float mic_distance_m = 0.0375f;
    bool doa_flip = false;
    float doa_confidence_threshold = 0.1f;

    float vad_threshold = 0.3f;
    float vad_stop_threshold = 0.2f;
    int silence_ms = 500;
    int min_speech_ms = 250;
    int pre_speech_ms = 500;
    int max_utterance_seconds = 8;

    std::string asr_provider = "spacemit";
    std::string asr_language = "zh";
    std::string asr_model_dir;
    bool enable_agc = true;
    float agc_headroom_db = 6.0f;
    float agc_max_gain_db = 18.0f;
    float agc_initial_gain_db = 8.0f;
    float agc_max_gain_change_db_per_second = 12.0f;
    float agc_max_output_noise_level_dbfs = -50.0f;
    bool warmup = true;
    bool list_devices = false;
    bool enable_wake_gpio = true;
    bool enable_touch_gpio = true;
    bool enable_environment_monitors = true;
    bool enable_playback = true;
    std::string audio_dir;
    std::string test_playback_file;
    int test_playback_count = 1;
    int test_playback_interval_ms = 3000;
    std::string test_intent_name;
    int test_intent_count = 1;
    int test_intent_interval_ms = 3000;
    std::string save_wav;
    std::string save_raw_wav;
};

void PrintUsage(const char* argv0)
{
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "  -l, --list-devices          List input/output devices\n"
        << "  -i, --input <N>             Input device index (-1 default)\n"
        << "  --input-device-hint <TEXT>  Prefer input device whose name contains TEXT\n"
        << "  --clear-input-device-hints  Disable built-in input device hints\n"
        << "  -o, --output <N>            Output device index (-1 default)\n"
        << "  --output-device-hint <TEXT> Prefer output device whose name contains TEXT\n"
        << "  --clear-output-device-hints Disable built-in output device hints\n"
        << "  --audio-dir <DIR>           Audio asset directory\n"
        << "  --no-playback               Disable local intent audio playback\n"
        << "  --test-playback <WAV>       Enqueue one WAV after audio starts\n"
        << "  --test-playback-count <N>   Repeat --test-playback N times\n"
        << "  --test-playback-interval-ms <N> Delay between repeated test playbacks\n"
        << "  --test-intent <NAME>        Queue one intent after audio starts\n"
        << "  --test-intent-count <N>     Repeat --test-intent N times\n"
        << "  --test-intent-interval-ms <N> Delay between repeated test intents\n"
        << "  -t, --time <sec>            Capture duration (0 = until Ctrl+C)\n"
        << "  --rate <hz>                 Capture rate (default 48000)\n"
        << "  --channels <N>              Capture channels (default 2)\n"
        << "  --speech-channel <N>        1-based channel for VAD/ASR (default 1)\n"
        << "  --frames <N>                Capture frames per callback (default 480)\n"
        << "  --queue-chunks <N>          Bounded audio queue size (default 96)\n"
        << "  --no-ui-dds                 Disable DDS GIF updates\n"
        << "  --aec                       Enable WebRTC AEC with reference capture\n"
        << "  --no-aec                    Disable WebRTC AEC (default)\n"
        << "  --ref-input <N>             Reference input device for AEC (default 1)\n"
        << "  --ref-channels <N>          Reference capture channels (default 1)\n"
        << "  --ref-channel <N>           1-based reference channel (default 1)\n"
        << "  --aec-delay-ms <N>          AEC reference delay compensation (default 0)\n"
        << "  --ns-level <L>              WebRTC NS: low|moderate|high|very-high\n"
        << "  --no-ns                     Disable WebRTC noise suppression\n"
        << "  --aec-agc                   Enable WebRTC digital AGC2 in --aec path\n"
        << "  --mic-distance <meters>     Dual-mic distance (default 0.0375)\n"
        << "  --doa-flip                  Flip 2-mic DOA angle\n"
        << "  --doa-threshold <F>         DOA confidence threshold (default 0.1)\n"
        << "  --vad-threshold <F>         Speech trigger threshold (default 0.3)\n"
        << "  --vad-stop-threshold <F>    Speech stop threshold (default 0.2)\n"
        << "  --agc                       Enable WebRTC AGC2 before VAD/ASR (default)\n"
        << "  --no-agc                    Disable WebRTC AGC2 before VAD/ASR\n"
        << "  --agc-headroom-db <F>       AGC2 adaptive digital headroom (default 6)\n"
        << "  --agc-max-gain-db <F>       AGC2 adaptive digital max gain (default 18)\n"
        << "  --agc-initial-gain-db <F>   AGC2 adaptive digital initial gain (default 8)\n"
        << "  --silence-ms <N>            Silence before ASR (default 500)\n"
        << "  --pre-speech-ms <N>         Pre-speech buffer (default 500)\n"
        << "  --max-utterance-sec <N>     Max utterance length (default 8)\n"
        << "  --provider <cpu|spacemit>   ASR provider (default spacemit)\n"
        << "  --language <zh|auto|en>     ASR language (default zh)\n"
        << "  --model-dir <DIR>           SenseVoice model directory\n"
        << "  --save-wav <FILE>           Save ASR input 16k mono PCM to WAV\n"
        << "  --save-raw-wav <FILE>       Save raw mic speech channel 16k mono PCM to WAV\n"
        << "  --no-wake-gpio              Disable hardware wake GPIO monitoring\n"
        << "  --no-touch-gpio             Disable touch GPIO monitoring\n"
        << "  --no-environment-monitors   Disable NFC/light/PM/G-sensor/fan monitors\n"
        << "  --no-warmup                 Skip ASR warmup\n"
        << "  -h, --help                  Show this help\n";
}

void ListDevices()
{
    std::cout << "Input devices:" << std::endl;
    auto input_devices = AudioPipeline::ListInputDevices();
    if (input_devices.empty()) {
        std::cout << "  (none)" << std::endl;
    } else {
        for (const auto& dev : input_devices) {
            std::cout << "  [" << dev.first << "] " << dev.second << std::endl;
        }
    }

    std::cout << "Output devices:" << std::endl;
    auto output_devices = SpacemitAudio::AudioDuplex::ListOutputDevices();
    if (output_devices.empty()) {
        std::cout << "  (none)" << std::endl;
    } else {
        for (const auto& dev : output_devices) {
            std::cout << "  [" << dev.first << "] " << dev.second << std::endl;
        }
    }
}

std::string LowerAscii(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool ContainsCaseInsensitive(const std::string& value, const std::string& needle)
{
    if (needle.empty()) {
        return false;
    }
    return LowerAscii(value).find(LowerAscii(needle)) != std::string::npos;
}

bool ResolveInputDeviceByHints(VoiceDemoOptions* options)
{
    if (!options || options->input_device >= 0 || options->input_device_hints.empty()) {
        return true;
    }

    const auto devices = AudioPipeline::ListInputDevices();
    for (const auto& hint : options->input_device_hints) {
        for (const auto& dev : devices) {
            if (ContainsCaseInsensitive(dev.second, hint)) {
                options->input_device = dev.first;
                std::cout << Timestamp() << " selected input device ["
                        << dev.first << "] " << dev.second
                        << " by hint \"" << hint << "\"" << std::endl;
                return true;
            }
        }
    }

    std::cerr << "failed to match input device by hints:" << std::endl;
    for (const auto& hint : options->input_device_hints) {
        std::cerr << "  - " << hint << std::endl;
    }
    std::cerr << "visible input devices:" << std::endl;
    if (devices.empty()) {
        std::cerr << "  (none)" << std::endl;
    } else {
        for (const auto& dev : devices) {
            std::cerr << "  [" << dev.first << "] " << dev.second << std::endl;
        }
    }
    return false;
}

bool ResolveOutputDeviceByHints(VoiceDemoOptions* options)
{
    if (!options || options->output_device >= 0 || options->output_device_hints.empty()) {
        return true;
    }

    const auto devices = SpacemitAudio::AudioDuplex::ListOutputDevices();
    for (const auto& hint : options->output_device_hints) {
        for (const auto& dev : devices) {
            if (ContainsCaseInsensitive(dev.second, hint)) {
                options->output_device = dev.first;
                std::cout << Timestamp() << " selected output device ["
                        << dev.first << "] " << dev.second
                        << " by hint \"" << hint << "\"" << std::endl;
                return true;
            }
        }
    }

    std::cerr << Timestamp()
            << " [playback] output device hints did not match; using default output"
            << std::endl;
    if (devices.empty()) {
        std::cerr << "visible output devices: (none)" << std::endl;
    } else {
        std::cerr << "visible output devices:" << std::endl;
        for (const auto& dev : devices) {
            std::cerr << "  [" << dev.first << "] " << dev.second << std::endl;
        }
    }
    return true;
}

std::string EnvOrEmpty(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? std::string(value) : std::string();
}

std::string ExpandUserPath(const std::string& path)
{
    if (path.empty() || path[0] != '~') {
        return path;
    }
    if (path.size() > 1 && path[1] != '/') {
        return path;
    }
    const std::string home = EnvOrEmpty("HOME");
    if (home.empty()) {
        return path;
    }
    return home + path.substr(1);
}

bool IsAbsolutePath(const std::string& path)
{
    return !path.empty() && path[0] == '/';
}

std::string PathJoin(const std::string& base, const std::string& name)
{
    if (base.empty()) {
        return name;
    }
    if (name.empty()) {
        return base;
    }
    if (IsAbsolutePath(name)) {
        return name;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

bool DirectoryExists(const std::string& path)
{
    struct stat st {};
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string ExecutableDir()
{
    std::vector<char> buffer(PATH_MAX + 1, '\0');
    const ssize_t n = readlink("/proc/self/exe", buffer.data(), PATH_MAX);
    if (n <= 0) {
        return "";
    }
    buffer[static_cast<size_t>(n)] = '\0';
    std::string exe(buffer.data());
    const size_t slash = exe.find_last_of('/');
    if (slash == std::string::npos) {
        return "";
    }
    return exe.substr(0, slash);
}

std::string ResolveAudioDir(const VoiceDemoOptions& options)
{
    std::vector<std::string> candidates;
    candidates.push_back(options.audio_dir);
    candidates.push_back(EnvOrEmpty("AI_CUBPET_AUDIO_DIR"));
    candidates.push_back(EnvOrEmpty("AUDIO_PATH"));

    const std::string asset_root = EnvOrEmpty("AI_CUBPET_ASSET_ROOT");
    if (!asset_root.empty()) {
        candidates.push_back(PathJoin(asset_root, "audio"));
    }

    const std::string exe_dir = ExecutableDir();
    if (!exe_dir.empty()) {
        candidates.push_back(PathJoin(exe_dir, "../share/ai-cubpet/audio"));
    }

    const std::string xdg_cache = EnvOrEmpty("XDG_CACHE_HOME");
    if (!xdg_cache.empty()) {
        candidates.push_back(PathJoin(xdg_cache, "models/assets/audio"));
    }

    const std::string home = EnvOrEmpty("HOME");
    if (!home.empty()) {
        candidates.push_back(PathJoin(home, ".cache/models/assets/audio"));
    }
    candidates.push_back("/root/.cache/models/assets/audio");

    std::string fallback;
    for (const auto& raw : candidates) {
        if (raw.empty()) {
            continue;
        }
        const std::string path = ExpandUserPath(raw);
        if (fallback.empty()) {
            fallback = path;
        }
        if (DirectoryExists(path)) {
            return path;
        }
    }
    return fallback;
}

std::string ResolveAudioPath(const std::string& file, const std::string& audio_dir)
{
    if (file.empty()) {
        return "";
    }
    const std::string expanded = ExpandUserPath(file);
    if (IsAbsolutePath(expanded)) {
        return expanded;
    }
    return PathJoin(audio_dir, expanded);
}

VoiceIntent VoiceIntentFromName(const std::string& name)
{
    if (name == "head_up") {
        return VoiceIntent::kHeadUp;
    }
    if (name == "nod_head") {
        return VoiceIntent::kNodHead;
    }
    if (name == "shake_head") {
        return VoiceIntent::kShakeHead;
    }
    if (name == "wag_tail") {
        return VoiceIntent::kWagTail;
    }
    return VoiceIntent::kUnknown;
}

struct WavPcm16 {
    int sample_rate = 0;
    int channels = 0;
    std::vector<int16_t> samples;
};

bool ReadExact(std::ifstream& file, char* data, size_t size)
{
    file.read(data, static_cast<std::streamsize>(size));
    return file.gcount() == static_cast<std::streamsize>(size);
}

bool ReadWavPcm16(const std::string& path, WavPcm16* wav)
{
    if (!wav) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[playback] cannot open WAV: " << path << std::endl;
        return false;
    }

    char riff[4] = {};
    uint32_t riff_size = 0;
    char wave[4] = {};
    if (!ReadExact(file, riff, sizeof(riff)) ||
            !ReadExact(file, reinterpret_cast<char*>(&riff_size), sizeof(riff_size)) ||
            !ReadExact(file, wave, sizeof(wave)) ||
            std::memcmp(riff, "RIFF", 4) != 0 ||
            std::memcmp(wave, "WAVE", 4) != 0) {
        std::cerr << "[playback] invalid WAV header: " << path << std::endl;
        return false;
    }
    (void)riff_size;

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<int16_t> samples;

    while (file) {
        char chunk_id[4] = {};
        uint32_t chunk_size = 0;
        if (!ReadExact(file, chunk_id, sizeof(chunk_id))) {
            break;
        }
        if (!ReadExact(file, reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size))) {
            break;
        }

        if (std::memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                std::cerr << "[playback] invalid WAV fmt chunk: " << path << std::endl;
                return false;
            }
            uint32_t byte_rate = 0;
            uint16_t block_align = 0;
            if (!ReadExact(file, reinterpret_cast<char*>(&audio_format), sizeof(audio_format)) ||
                    !ReadExact(file, reinterpret_cast<char*>(&channels), sizeof(channels)) ||
                    !ReadExact(file, reinterpret_cast<char*>(&sample_rate), sizeof(sample_rate)) ||
                    !ReadExact(file, reinterpret_cast<char*>(&byte_rate), sizeof(byte_rate)) ||
                    !ReadExact(file, reinterpret_cast<char*>(&block_align), sizeof(block_align)) ||
                    !ReadExact(file, reinterpret_cast<char*>(&bits_per_sample), sizeof(bits_per_sample))) {
                std::cerr << "[playback] truncated WAV fmt chunk: " << path << std::endl;
                return false;
            }
            (void)byte_rate;
            (void)block_align;
            if (chunk_size > 16) {
                file.seekg(static_cast<std::streamoff>(chunk_size - 16), std::ios::cur);
            }
            have_fmt = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            const uint32_t sample_bytes =
                chunk_size - (chunk_size % sizeof(int16_t));
            if (sample_bytes == 0) {
                std::cerr << "[playback] invalid WAV data chunk: " << path << std::endl;
                return false;
            }
            samples.resize(sample_bytes / sizeof(int16_t));
            if (!ReadExact(file,
                    reinterpret_cast<char*>(samples.data()),
                    sample_bytes)) {
                std::cerr << "[playback] truncated WAV data chunk: " << path << std::endl;
                return false;
            }
            if (sample_bytes < chunk_size) {
                file.seekg(static_cast<std::streamoff>(chunk_size - sample_bytes),
                    std::ios::cur);
            }
            have_data = true;
        } else {
            file.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }

        if (chunk_size % 2 == 1) {
            file.seekg(1, std::ios::cur);
        }
        if (have_fmt && have_data) {
            break;
        }
    }

    if (!have_fmt || !have_data || audio_format != 1 || bits_per_sample != 16 ||
            channels == 0 || sample_rate == 0 || samples.empty()) {
        std::cerr << "[playback] unsupported WAV, need PCM16: " << path << std::endl;
        return false;
    }

    wav->sample_rate = static_cast<int>(sample_rate);
    wav->channels = static_cast<int>(channels);
    wav->samples = std::move(samples);
    return true;
}

std::vector<float> Pcm16ToStereoFloat(const WavPcm16& wav)
{
    if (wav.channels <= 0) {
        return {};
    }
    const size_t frames = wav.samples.size() / static_cast<size_t>(wav.channels);
    std::vector<float> stereo(frames * kPlaybackChannels);
    for (size_t i = 0; i < frames; ++i) {
        const size_t in_base = i * static_cast<size_t>(wav.channels);
        const float left = static_cast<float>(wav.samples[in_base]) / 32768.0f;
        const float right = wav.channels > 1
            ? static_cast<float>(wav.samples[in_base + 1]) / 32768.0f
            : left;
        stereo[i * kPlaybackChannels] = left;
        stereo[i * kPlaybackChannels + 1] = right;
    }
    return stereo;
}

bool LoadWavForDuplexPlayback(const std::string& path,
                            int target_sample_rate,
                            std::vector<float>* samples)
{
    if (!samples || target_sample_rate <= 0) {
        return false;
    }
    samples->clear();

    WavPcm16 wav;
    if (!ReadWavPcm16(path, &wav)) {
        return false;
    }

    std::vector<float> stereo = Pcm16ToStereoFloat(wav);
    if (stereo.empty()) {
        std::cerr << "[playback] empty WAV: " << path << std::endl;
        return false;
    }

    if (wav.sample_rate != target_sample_rate) {
        Resampler::Config config;
        config.input_sample_rate = wav.sample_rate;
        config.output_sample_rate = target_sample_rate;
        config.channels = kPlaybackChannels;
        config.method = wav.sample_rate > target_sample_rate
            ? ResampleMethod::LINEAR_DOWNSAMPLE
            : ResampleMethod::LINEAR_UPSAMPLE;
        Resampler resampler(config);
        if (!resampler.initialize()) {
            std::cerr << "[playback] failed to initialize resampler: "
                    << wav.sample_rate << " -> " << target_sample_rate << std::endl;
            return false;
        }
        stereo = resampler.process(stereo);
        if (stereo.empty()) {
            std::cerr << "[playback] resampler produced no output: " << path << std::endl;
            return false;
        }
    }

    *samples = std::move(stereo);
    return !samples->empty();
}

bool SaveWavMono16(const std::string& path,
                const std::vector<int16_t>& samples,
                int sample_rate)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "failed to open " << path << " for writing" << std::endl;
        return false;
    }

    const uint32_t data_size =
        static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;
    const uint32_t fmt_size = 16;
    const uint16_t audio_format = 1;
    const uint16_t channels = 1;
    const uint32_t sr = static_cast<uint32_t>(sample_rate);
    const uint16_t bits_per_sample = 16;
    const uint16_t block_align = channels * bits_per_sample / 8;
    const uint32_t byte_rate = sr * block_align;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&riff_size), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write(reinterpret_cast<const char*>(&fmt_size), 4);
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    file.write(reinterpret_cast<const char*>(&channels), 2);
    file.write(reinterpret_cast<const char*>(&sr), 4);
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    file.write(reinterpret_cast<const char*>(&block_align), 2);
    file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&data_size), 4);
    file.write(reinterpret_cast<const char*>(samples.data()), data_size);
    return static_cast<bool>(file);
}

struct AudioChunk {
    std::vector<int16_t> samples;
    std::vector<float> aec_mono;
    size_t frames = 0;
    int channels = 0;
    bool has_aec = false;
};

class AudioChunkQueue {
public:
    explicit AudioChunkQueue(size_t capacity) : capacity_(std::max<size_t>(capacity, 2)) {}

    void Push(AudioChunk chunk)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= capacity_) {
                queue_.pop_front();
                dropped_++;
            }
            queue_.push_back(std::move(chunk));
        }
        cv_.notify_one();
    }

    bool WaitPop(AudioChunk* chunk)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return stopped_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        *chunk = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    size_t dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<AudioChunk> queue_;
    bool stopped_ = false;
    size_t dropped_ = 0;
};

struct Utterance {
    int index = 0;
    std::vector<float> audio_16k;
    bool has_doa = false;
    float doa_angle = 90.0f;
    float doa_confidence = 0.0f;
    float max_vad_prob = 0.0f;
    bool asr_allowed = false;
    ToyState gate_state = ToyState::kBooting;
    ConversationMode gate_mode = ConversationMode::kWakeTriggered;
};

class UtteranceQueue {
public:
    explicit UtteranceQueue(size_t capacity) : capacity_(std::max<size_t>(capacity, 1)) {}

    void Push(Utterance utterance)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= capacity_) {
                queue_.pop();
                dropped_++;
            }
            queue_.push(std::move(utterance));
        }
        cv_.notify_one();
    }

    bool WaitPop(Utterance* utterance)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return stopped_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        *utterance = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    size_t dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Utterance> queue_;
    bool stopped_ = false;
    size_t dropped_ = 0;
};

struct ActionItem {
    VoiceIntent intent = VoiceIntent::kUnknown;
    bool has_doa = false;
    float doa_angle = 90.0f;
};

class ActionQueue {
public:
    explicit ActionQueue(size_t capacity) : capacity_(std::max<size_t>(capacity, 1)) {}

    void Push(ActionItem action)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= capacity_) {
                queue_.pop();
                dropped_++;
            }
            queue_.push(std::move(action));
        }
        cv_.notify_one();
    }

    bool WaitPop(ActionItem* action)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return stopped_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        *action = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    size_t dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<ActionItem> queue_;
    bool stopped_ = false;
    size_t dropped_ = 0;
};

struct DuplexAudioConfig {
    int sample_rate = 16000;
    int capture_channels = 4;
    int playback_channels = 2;
    int frames_per_buffer = 480;
    int input_device = -1;
    int output_device = -1;
};

std::vector<float> NormalizePlaybackChannels(
    const std::vector<float>& interleaved,
    int channels,
    int output_channels)
{
    if (interleaved.empty() || channels <= 0 || output_channels <= 0) {
        return {};
    }

    const size_t frames = interleaved.size() / static_cast<size_t>(channels);
    std::vector<float> normalized(frames * static_cast<size_t>(output_channels));
    for (size_t frame = 0; frame < frames; ++frame) {
        for (int ch = 0; ch < output_channels; ++ch) {
            float sample = 0.0f;
            if (channels == 1) {
                sample = interleaved[frame];
            } else {
                const int src_ch = std::min(ch, channels - 1);
                sample = interleaved[frame * static_cast<size_t>(channels) +
                    static_cast<size_t>(src_ch)];
            }
            normalized[frame * static_cast<size_t>(output_channels) +
                static_cast<size_t>(ch)] = std::clamp(sample, -1.0f, 1.0f);
        }
    }
    return normalized;
}

std::vector<int16_t> FloatToPcm16(const std::vector<float>& samples)
{
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::clamp(samples[i], -1.0f, 1.0f);
        pcm[i] = static_cast<int16_t>(std::lround(clamped * 32767.0f));
    }
    return pcm;
}

class DuplexAudioPipeline {
public:
    using CaptureCallback = std::function<void(const int16_t* interleaved,
                                            size_t frames,
                                            int channels)>;

    explicit DuplexAudioPipeline(DuplexAudioConfig config)
        : config_(config)
    {
    }

    ~DuplexAudioPipeline()
    {
        Close();
    }

    bool Initialize()
    {
        if (config_.sample_rate <= 0 || config_.capture_channels <= 0 ||
                config_.playback_channels <= 0 || config_.frames_per_buffer <= 0) {
            std::cerr << "[audio-duplex] invalid config" << std::endl;
            return false;
        }

        capture_slot_samples_ = static_cast<size_t>(config_.frames_per_buffer) *
            static_cast<size_t>(config_.capture_channels);
        capture_ring_.assign(kCaptureRingChunks * capture_slot_samples_, 0);
        capture_frame_counts_.assign(kCaptureRingChunks, 0);
        capture_channel_counts_.assign(kCaptureRingChunks, 0);
        capture_write_.store(0);
        capture_read_.store(0);
        capture_dropped_.store(0);

        duplex_ = std::make_unique<SpacemitAudio::AudioDuplex>(
            config_.input_device, config_.output_device);
        duplex_->SetCallbackEx([this](const float* input,
                                    float* output,
                                    size_t frames,
                                    int input_channels,
                                    int output_channels) {
            OnAudio(input, output, frames, input_channels, output_channels);
        });

        initialized_ = true;
        std::cout << "[audio-duplex] init: sample_rate=" << config_.sample_rate
                << ", capture_channels=" << config_.capture_channels
                << ", playback_channels=" << config_.playback_channels
                << ", frames_per_buffer=" << config_.frames_per_buffer
                << ", input_device=" << config_.input_device
                << ", output_device=" << config_.output_device
                << std::endl;
        return true;
    }

    void SetCaptureCallback(CaptureCallback callback)
    {
        callback_ = std::move(callback);
    }

    bool Start()
    {
        if (!initialized_ || !duplex_) {
            return false;
        }
        if (!callback_) {
            std::cerr << "[audio-duplex] capture callback is not set" << std::endl;
            return false;
        }
        processing_running_ = true;
        capture_thread_ = std::thread(&DuplexAudioPipeline::CaptureLoop, this);
        if (!duplex_->Start(config_.sample_rate, config_.capture_channels,
                config_.playback_channels, config_.frames_per_buffer)) {
            std::cerr << "[audio-duplex] failed to start" << std::endl;
            processing_running_ = false;
            if (capture_thread_.joinable()) {
                capture_thread_.join();
            }
            return false;
        }
        running_ = true;
        std::cout << "[audio-duplex] started" << std::endl;
        return true;
    }

    void Stop()
    {
        const bool was_running = running_;
        if (running_ && duplex_) {
            duplex_->Stop();
        }
        running_ = false;
        processing_running_ = false;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (was_running) {
            std::cout << "[audio-duplex] stopped" << std::endl;
        }
    }

    void Close()
    {
        Stop();
        if (duplex_) {
            duplex_->Close();
            duplex_.reset();
        }
        initialized_ = false;
    }

    void EnqueuePlayback(std::vector<float> interleaved, int channels)
    {
        if (interleaved.empty() || channels <= 0) {
            return;
        }
        std::vector<float> normalized =
            NormalizePlaybackChannels(interleaved, channels, config_.playback_channels);
        if (normalized.empty()) {
            return;
        }

        std::lock_guard<std::mutex> lock(pending_playback_mutex_);
        if (!pending_playback_.empty()) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        pending_playback_ = std::move(normalized);
        pending_playback_frames_ =
            pending_playback_.size() / static_cast<size_t>(config_.playback_channels);
    }

    bool is_playing() const
    {
        std::lock_guard<std::mutex> lock(playback_mutex_);
        return is_playing_;
    }

    size_t dropped() const
    {
        return dropped_.load(std::memory_order_relaxed);
    }

    int sample_rate() const { return config_.sample_rate; }

private:
    void PullPendingPlaybackLocked()
    {
        std::unique_lock<std::mutex> lock(pending_playback_mutex_, std::try_to_lock);
        if (!lock.owns_lock() || pending_playback_.empty()) {
            return;
        }
        if (!playback_.empty() && playback_pos_frames_ < playback_frames_) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        playback_ = std::move(pending_playback_);
        playback_frames_ = pending_playback_frames_;
        playback_pos_frames_ = 0;
        pending_playback_frames_ = 0;
        is_playing_ = playback_frames_ > 0;
    }

    void FillOutput(float* output, size_t frames, int output_channels)
    {
        if (!output || frames == 0 || output_channels <= 0) {
            return;
        }
        std::fill(output, output + frames * static_cast<size_t>(output_channels), 0.0f);

        std::unique_lock<std::mutex> lock(playback_mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }
        PullPendingPlaybackLocked();
        if (playback_.empty() || playback_pos_frames_ >= playback_frames_) {
            is_playing_ = false;
            return;
        }

        const size_t frames_available = playback_frames_ - playback_pos_frames_;
        const size_t frames_to_copy = std::min(frames, frames_available);
        for (size_t frame = 0; frame < frames_to_copy; ++frame) {
            const size_t src_base = (playback_pos_frames_ + frame) *
                static_cast<size_t>(config_.playback_channels);
            const size_t dst_base = frame * static_cast<size_t>(output_channels);
            for (int ch = 0; ch < output_channels; ++ch) {
                const int src_ch = std::min(ch, config_.playback_channels - 1);
                output[dst_base + static_cast<size_t>(ch)] =
                    playback_[src_base + static_cast<size_t>(src_ch)];
            }
        }

        playback_pos_frames_ += frames_to_copy;
        if (playback_pos_frames_ >= playback_frames_) {
            playback_.clear();
            playback_frames_ = 0;
            playback_pos_frames_ = 0;
            is_playing_ = false;
        } else {
            is_playing_ = true;
        }
    }

    void QueueInput(const float* input, size_t frames, int input_channels)
    {
        if (!input || frames == 0 || input_channels <= 0 || capture_slot_samples_ == 0) {
            return;
        }
        const size_t samples = frames * static_cast<size_t>(input_channels);
        if (samples > capture_slot_samples_) {
            capture_dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const size_t write = capture_write_.load(std::memory_order_relaxed);
        const size_t next = (write + 1) % kCaptureRingChunks;
        if (next == capture_read_.load(std::memory_order_acquire)) {
            capture_dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        int16_t* slot = capture_ring_.data() + write * capture_slot_samples_;
        for (size_t i = 0; i < samples; ++i) {
            const float clamped = std::clamp(input[i], -1.0f, 1.0f);
            slot[i] = static_cast<int16_t>(clamped * 32767.0f);
        }
        capture_frame_counts_[write] = frames;
        capture_channel_counts_[write] = input_channels;
        capture_write_.store(next, std::memory_order_release);
    }

    void CaptureLoop()
    {
        while (processing_running_.load(std::memory_order_acquire) ||
                capture_read_.load(std::memory_order_acquire) !=
                    capture_write_.load(std::memory_order_acquire)) {
            const size_t read = capture_read_.load(std::memory_order_relaxed);
            if (read == capture_write_.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            const size_t frames = capture_frame_counts_[read];
            const int channels = capture_channel_counts_[read];
            const int16_t* slot = capture_ring_.data() + read * capture_slot_samples_;
            if (callback_ && frames > 0 && channels > 0) {
                callback_(slot, frames, channels);
            }
            capture_read_.store((read + 1) % kCaptureRingChunks,
                std::memory_order_release);
        }
    }

    void OnAudio(const float* input,
                float* output,
                size_t frames,
                int input_channels,
                int output_channels)
    {
        FillOutput(output, frames, output_channels);
        QueueInput(input, frames, input_channels);
    }

    static constexpr size_t kCaptureRingChunks = 128;

    DuplexAudioConfig config_;
    std::unique_ptr<SpacemitAudio::AudioDuplex> duplex_;
    CaptureCallback callback_;
    std::thread capture_thread_;
    std::atomic<bool> processing_running_{false};
    std::vector<int16_t> capture_ring_;
    std::vector<size_t> capture_frame_counts_;
    std::vector<int> capture_channel_counts_;
    size_t capture_slot_samples_ = 0;
    std::atomic<size_t> capture_write_{0};
    std::atomic<size_t> capture_read_{0};
    std::atomic<size_t> capture_dropped_{0};
    mutable std::mutex playback_mutex_;
    std::vector<float> playback_;
    size_t playback_frames_ = 0;
    size_t playback_pos_frames_ = 0;
    std::mutex pending_playback_mutex_;
    std::vector<float> pending_playback_;
    size_t pending_playback_frames_ = 0;
    bool is_playing_ = false;
    bool initialized_ = false;
    bool running_ = false;
    std::atomic<size_t> dropped_{0};
};

class OutputAudioQueue {
public:
    OutputAudioQueue(int output_device, int frames_per_buffer)
        : output_device_(output_device),
            frames_per_buffer_(frames_per_buffer)
    {
    }

    bool Start()
    {
        if (running_.load()) {
            return true;
        }

        player_ = std::make_unique<SpacemitAudio::AudioPlayer>(output_device_);
        if (!player_->Start(kPlaybackSampleRate, kPlaybackChannels, frames_per_buffer_)) {
            std::cerr << "[playback] failed to start output device "
                    << output_device_ << std::endl;
            player_->Close();
            player_.reset();
            return false;
        }

        running_ = true;
        worker_ = std::thread(&OutputAudioQueue::WorkerLoop, this);
        std::cout << "[playback] output queue started: device=" << output_device_
                << ", sample_rate=" << kPlaybackSampleRate
                << ", channels=" << kPlaybackChannels
                << ", frames=" << frames_per_buffer_ << std::endl;
        return true;
    }

    void Stop()
    {
        if (!running_.exchange(false)) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (player_) {
            player_->Stop();
            player_->Close();
            player_.reset();
        }
        std::cout << "[playback] output queue stopped" << std::endl;
    }

    void EnqueuePlayback(std::vector<float> interleaved, int channels)
    {
        std::vector<float> normalized =
            NormalizePlaybackChannels(interleaved, channels, kPlaybackChannels);
        if (normalized.empty()) {
            return;
        }

        PlaybackItem item;
        item.samples = FloatToPcm16(normalized);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopped_) {
                return;
            }
            if (!queue_.empty()) {
                queue_.pop();
                dropped_++;
            }
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    size_t dropped() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    struct PlaybackItem {
        std::vector<int16_t> samples;
    };

    void WorkerLoop()
    {
        while (true) {
            PlaybackItem item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&]() { return stopped_ || !queue_.empty(); });
                if (queue_.empty()) {
                    break;
                }
                item = std::move(queue_.front());
                queue_.pop();
            }

            if (!player_ || item.samples.empty()) {
                continue;
            }
            const size_t chunk_frames = static_cast<size_t>(std::max(1, frames_per_buffer_));
            const size_t chunk_samples = chunk_frames * kPlaybackChannels;
            for (size_t offset = 0; offset < item.samples.size() && running_.load();
                    offset += chunk_samples) {
                const size_t samples =
                    std::min(chunk_samples, item.samples.size() - offset);
                const uint8_t* data =
                    reinterpret_cast<const uint8_t*>(item.samples.data() + offset);
                if (!player_->Write(data, samples * sizeof(int16_t))) {
                    std::cerr << "[playback] output write failed" << std::endl;
                    break;
                }
            }
        }
    }

    int output_device_;
    int frames_per_buffer_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PlaybackItem> queue_;
    bool stopped_ = false;
    size_t dropped_ = 0;
    std::atomic<bool> running_{false};
    std::unique_ptr<SpacemitAudio::AudioPlayer> player_;
    std::thread worker_;
};

std::vector<float> PcmToMonoFloat(const int16_t* pcm,
                                size_t frames,
                                int channels,
                                int speech_channel)
{
    const int speech_idx = std::clamp(speech_channel - 1, 0, channels - 1);
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        mono[i] = static_cast<float>(pcm[i * static_cast<size_t>(channels) + speech_idx]) /
            32768.0f;
    }
    return mono;
}

std::vector<float> PcmToStereoFloat(const int16_t* pcm, size_t frames, int channels)
{
    std::vector<float> stereo(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        stereo[i * 2] = static_cast<float>(pcm[i * static_cast<size_t>(channels)]) /
            32768.0f;
        stereo[i * 2 + 1] =
            static_cast<float>(pcm[i * static_cast<size_t>(channels) + 1]) / 32768.0f;
    }
    return stereo;
}

#ifdef AI_CUBPET_USE_WEBRTC_AEC
struct WebRtcAgcConfig {
    int sample_rate = kModelSampleRate;
    float headroom_db = 6.0f;
    float max_gain_db = 18.0f;
    float initial_gain_db = 8.0f;
    float max_gain_change_db_per_second = 12.0f;
    float max_output_noise_level_dbfs = -50.0f;
};

class WebRtcAgcProcessor {
public:
    ~WebRtcAgcProcessor()
    {
        Close();
    }

    bool Initialize(const WebRtcAgcConfig& config)
    {
        Close();
        if (config.sample_rate <= 0 || config.sample_rate % 100 != 0) {
            std::cerr << "[agc] invalid sample_rate: " << config.sample_rate << std::endl;
            return false;
        }

        config_ = config;
        frame_samples_ = config_.sample_rate / 100;

        rtc::scoped_refptr<webrtc::AudioProcessing> apm_ref =
            webrtc::AudioProcessingBuilder().Create();
        if (!apm_ref) {
            std::cerr << "[agc] failed to create WebRTC AudioProcessing" << std::endl;
            return false;
        }

        apm_ = apm_ref.get();
        apm_->AddRef();

        webrtc::AudioProcessing::Config apm_config;
        apm_config.echo_canceller.enabled = false;
        apm_config.high_pass_filter.enabled = false;
        apm_config.noise_suppression.enabled = false;
        apm_config.gain_controller1.enabled = false;
        apm_config.gain_controller2.enabled = true;
        apm_config.gain_controller2.input_volume_controller.enabled = false;
        apm_config.gain_controller2.adaptive_digital.enabled = true;
        apm_config.gain_controller2.adaptive_digital.headroom_db = config_.headroom_db;
        apm_config.gain_controller2.adaptive_digital.max_gain_db = config_.max_gain_db;
        apm_config.gain_controller2.adaptive_digital.initial_gain_db = config_.initial_gain_db;
        apm_config.gain_controller2.adaptive_digital.max_gain_change_db_per_second =
            config_.max_gain_change_db_per_second;
        apm_config.gain_controller2.adaptive_digital.max_output_noise_level_dbfs =
            config_.max_output_noise_level_dbfs;
        apm_config.gain_controller2.fixed_digital.gain_db = 0.0f;
        apm_->ApplyConfig(apm_config);

        frame_int16_.resize(static_cast<size_t>(frame_samples_));
        initialized_ = true;
        std::cout << "[agc] WebRTC AGC2 adaptive_digital ready: "
                << "headroom_db=" << config_.headroom_db
                << ", max_gain_db=" << config_.max_gain_db
                << ", initial_gain_db=" << config_.initial_gain_db
                << ", max_gain_change_db_per_second="
                << config_.max_gain_change_db_per_second
                << ", max_output_noise_level_dbfs="
                << config_.max_output_noise_level_dbfs << std::endl;
        return true;
    }

    bool Process(std::vector<float>* samples)
    {
        if (!initialized_ || !apm_ || !samples || samples->empty()) {
            return true;
        }

        webrtc::StreamConfig stream_config(config_.sample_rate, 1);
        const size_t frame_samples = static_cast<size_t>(frame_samples_);
        const size_t complete_samples = samples->size() - (samples->size() % frame_samples);
        for (size_t offset = 0; offset < complete_samples; offset += frame_samples) {
            for (size_t i = 0; i < frame_samples; ++i) {
                const float clamped = std::clamp((*samples)[offset + i], -1.0f, 1.0f);
                frame_int16_[i] = static_cast<int16_t>(clamped * 32767.0f);
            }

            const int ret = apm_->ProcessStream(frame_int16_.data(), stream_config,
                stream_config, frame_int16_.data());
            if (ret != 0) {
                std::cerr << "[agc] WebRTC ProcessStream failed: " << ret << std::endl;
                return false;
            }

            for (size_t i = 0; i < frame_samples; ++i) {
                (*samples)[offset + i] =
                    static_cast<float>(frame_int16_[i]) / 32768.0f;
            }
        }

        return true;
    }

    void Close()
    {
        if (apm_) {
            apm_->Release();
            apm_ = nullptr;
        }
        frame_int16_.clear();
        initialized_ = false;
    }

private:
    WebRtcAgcConfig config_;
    webrtc::AudioProcessing* apm_ = nullptr;
    std::vector<int16_t> frame_int16_;
    int frame_samples_ = 0;
    bool initialized_ = false;
};
#endif

void AppendToPreBuffer(std::deque<float>* pre_buffer,
                    const std::vector<float>& frame,
                    size_t max_samples)
{
    for (float sample : frame) {
        pre_buffer->push_back(sample);
        while (pre_buffer->size() > max_samples) {
            pre_buffer->pop_front();
        }
    }
}

void AppendInterleavedToPreBuffer(std::deque<float>* pre_buffer,
                                const std::vector<float>& samples,
                                size_t max_samples)
{
    for (float sample : samples) {
        pre_buffer->push_back(sample);
        while (pre_buffer->size() > max_samples) {
            pre_buffer->pop_front();
        }
    }
}

void AppendToWavBuffer(std::vector<int16_t>* out, const std::vector<float>& samples)
{
    if (!out) {
        return;
    }
    out->reserve(out->size() + samples.size());
    for (float sample : samples) {
        float clamped = std::max(-1.0f, std::min(1.0f, sample));
        out->push_back(static_cast<int16_t>(clamped * 32767.0f));
    }
}

}  // namespace

int RunVoiceDemo(int argc, char** argv)
{
    VoiceDemoOptions options;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            PrintUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "-l") == 0 || std::strcmp(arg, "--list-devices") == 0) {
            options.list_devices = true;
            continue;
        }
        if (std::strcmp(arg, "-i") == 0 || std::strcmp(arg, "--input") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.input_device)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--clear-input-device-hints") == 0) {
            options.input_device_hints.clear();
            continue;
        }
        if (std::strcmp(arg, "--input-device-hint") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.input_device_hints.push_back(value);
            continue;
        }
        if (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.output_device)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--clear-output-device-hints") == 0) {
            options.output_device_hints.clear();
            continue;
        }
        if (std::strcmp(arg, "--output-device-hint") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.output_device_hints.push_back(value);
            continue;
        }
        if (std::strcmp(arg, "--audio-dir") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.audio_dir = value;
            continue;
        }
        if (std::strcmp(arg, "--no-playback") == 0) {
            options.enable_playback = false;
            continue;
        }
        if (std::strcmp(arg, "--test-playback") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.test_playback_file = value;
            continue;
        }
        if (std::strcmp(arg, "--test-playback-count") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.test_playback_count)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--test-playback-interval-ms") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg,
                    &options.test_playback_interval_ms)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--test-intent") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.test_intent_name = value;
            continue;
        }
        if (std::strcmp(arg, "--test-intent-count") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.test_intent_count)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--test-intent-interval-ms") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg,
                    &options.test_intent_interval_ms)) return 2;
            continue;
        }
        if (std::strcmp(arg, "-t") == 0 || std::strcmp(arg, "--time") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.duration_seconds)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--rate") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.capture_rate)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--channels") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.capture_channels)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--speech-channel") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.speech_channel)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--frames") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.frames_per_buffer)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--queue-chunks") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.queue_chunks)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--no-ui-dds") == 0) {
            options.enable_ui_dds = false;
            continue;
        }
        if (std::strcmp(arg, "--aec") == 0) {
            options.enable_aec = true;
            continue;
        }
        if (std::strcmp(arg, "--no-aec") == 0) {
            options.enable_aec = false;
            continue;
        }
        if (std::strcmp(arg, "--ref-input") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.reference_device)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--ref-channels") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.reference_channels)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--ref-channel") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.reference_channel)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--aec-delay-ms") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.aec_delay_ms)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--ns-level") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            if (!ParseNoiseSuppressionLevel(value, &options.aec_noise_suppression_level)) {
                std::cerr << "unknown noise suppression level: " << value << std::endl;
                return 2;
            }
            options.aec_noise_suppression = true;
            continue;
        }
        if (std::strcmp(arg, "--no-ns") == 0) {
            options.aec_noise_suppression = false;
            continue;
        }
        if (std::strcmp(arg, "--aec-agc") == 0) {
            options.aec_gain_control = true;
            continue;
        }
        if (std::strcmp(arg, "--mic-distance") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.mic_distance_m)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--doa-threshold") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.doa_confidence_threshold)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--vad-threshold") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.vad_threshold)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--vad-stop-threshold") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.vad_stop_threshold)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--agc") == 0) {
            options.enable_agc = true;
            continue;
        }
        if (std::strcmp(arg, "--no-agc") == 0) {
            options.enable_agc = false;
            continue;
        }
        if (std::strcmp(arg, "--agc-headroom-db") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.agc_headroom_db)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--agc-max-gain-db") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.agc_max_gain_db)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--agc-initial-gain-db") == 0) {
            if (!ParseFloatArg(argc, argv, &i, arg, &options.agc_initial_gain_db)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--silence-ms") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.silence_ms)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--pre-speech-ms") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.pre_speech_ms)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--max-utterance-sec") == 0) {
            if (!ParseIntArg(argc, argv, &i, arg, &options.max_utterance_seconds)) return 2;
            continue;
        }
        if (std::strcmp(arg, "--provider") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.asr_provider = value;
            continue;
        }
        if (std::strcmp(arg, "--language") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.asr_language = value;
            continue;
        }
        if (std::strcmp(arg, "--model-dir") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.asr_model_dir = value;
            continue;
        }
        if (std::strcmp(arg, "--save-wav") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.save_wav = value;
            continue;
        }
        if (std::strcmp(arg, "--save-raw-wav") == 0) {
            const char* value = nullptr;
            if (!ReadNextArg(argc, argv, &i, &value)) {
                std::cerr << arg << " requires a value" << std::endl;
                return 2;
            }
            options.save_raw_wav = value;
            continue;
        }
        if (std::strcmp(arg, "--doa-flip") == 0) {
            options.doa_flip = true;
            continue;
        }
        if (std::strcmp(arg, "--no-wake-gpio") == 0) {
            options.enable_wake_gpio = false;
            continue;
        }
        if (std::strcmp(arg, "--no-touch-gpio") == 0) {
            options.enable_touch_gpio = false;
            continue;
        }
        if (std::strcmp(arg, "--no-environment-monitors") == 0) {
            options.enable_environment_monitors = false;
            continue;
        }
        if (std::strcmp(arg, "--no-warmup") == 0) {
            options.warmup = false;
            continue;
        }

        std::cerr << "unknown option: " << arg << std::endl;
        PrintUsage(argv[0]);
        return 2;
    }

    if (options.list_devices) {
        ListDevices();
        return 0;
    }

#ifndef AI_CUBPET_USE_WEBRTC_AEC
    if (options.enable_aec) {
        std::cerr << "this binary was built without AI_CUBPET_USE_AEC=ON" << std::endl;
        return 2;
    }
    if (options.enable_agc) {
        std::cerr << "this binary was built without WebRTC AGC support" << std::endl;
        return 2;
    }
#endif
#ifndef AI_CUBPET_USE_DDS
    if (options.enable_ui_dds) {
        std::cerr << "this binary was built without AI_CUBPET_USE_DDS=ON" << std::endl;
        options.enable_ui_dds = false;
    }
#endif

    if (options.capture_channels < 2) {
        std::cerr << "DOA requires at least 2 capture channels" << std::endl;
        return 2;
    }
    if (options.speech_channel < 1 || options.speech_channel > options.capture_channels) {
        std::cerr << "speech channel out of range" << std::endl;
        return 2;
    }
    if (!std::isfinite(options.agc_headroom_db) || options.agc_headroom_db < 0.0f ||
            options.agc_headroom_db > 25.0f) {
        std::cerr << "agc-headroom-db must be in [0, 25]" << std::endl;
        return 2;
    }
    if (!std::isfinite(options.agc_max_gain_db) || options.agc_max_gain_db < 0.0f ||
            options.agc_max_gain_db > 50.0f) {
        std::cerr << "agc-max-gain-db must be in [0, 50]" << std::endl;
        return 2;
    }
    if (!std::isfinite(options.agc_initial_gain_db) || options.agc_initial_gain_db < 0.0f ||
            options.agc_initial_gain_db > options.agc_max_gain_db) {
        std::cerr << "agc-initial-gain-db must be in [0, agc-max-gain-db]" << std::endl;
        return 2;
    }
    if (!std::isfinite(options.agc_max_gain_change_db_per_second) ||
            options.agc_max_gain_change_db_per_second <= 0.0f) {
        std::cerr << "agc max gain change must be positive" << std::endl;
        return 2;
    }
    if (!std::isfinite(options.agc_max_output_noise_level_dbfs) ||
            options.agc_max_output_noise_level_dbfs >= 0.0f) {
        std::cerr << "agc max output noise level must be negative dBFS" << std::endl;
        return 2;
    }
    if (options.enable_aec && options.reference_channels < 1) {
        std::cerr << "reference channels must be >= 1" << std::endl;
        return 2;
    }
    if (options.enable_aec &&
            (options.reference_channel < 1 ||
            options.reference_channel > options.reference_channels)) {
        std::cerr << "reference channel out of range" << std::endl;
        return 2;
    }
    if (options.enable_aec && options.capture_rate != 16000 &&
            options.capture_rate != 32000 && options.capture_rate != 48000) {
        std::cerr << "AEC sample rate must be 16000, 32000, or 48000" << std::endl;
        return 2;
    }
    if (options.enable_aec) {
        const int aec_frames = std::max(1, options.capture_rate / 100);
        if (options.frames_per_buffer != aec_frames) {
            std::cerr << "AEC uses 10 ms frames; overriding --frames "
                    << options.frames_per_buffer << " -> " << aec_frames
                    << std::endl;
            options.frames_per_buffer = aec_frames;
        }
    }
    if (!ResolveInputDeviceByHints(&options)) {
        return 1;
    }
    if (options.enable_playback) {
        if (!ResolveOutputDeviceByHints(&options)) {
            return 1;
        }
        options.audio_dir = ResolveAudioDir(options);
        if (options.audio_dir.empty()) {
            std::cerr << Timestamp()
                    << " [playback] audio directory is empty; intent audio disabled"
                    << std::endl;
            options.enable_playback = false;
        } else {
            std::cout << Timestamp() << " [playback] audio_dir="
                    << options.audio_dir << std::endl;
        }
    }
    const bool use_webrtc_agc = options.enable_agc && !options.enable_aec;

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    g_running = true;

    std::cout << Timestamp() << " ai-cubpet local voice demo" << std::endl;
    std::cout << Timestamp() << " capture=" << options.capture_rate << "Hz/"
            << options.capture_channels << "ch, frame=" << options.frames_per_buffer
            << ", speech=ch" << options.speech_channel
            << ", agc=" << (use_webrtc_agc ? "on" : "off") << std::endl;
    std::cout << Timestamp() << " playback="
            << (options.enable_playback ? "on" : "off")
            << ", output=" << options.output_device << std::endl;
    if (options.enable_aec) {
        std::cout << Timestamp() << " AEC: mic=input " << options.input_device
                << ", ref=input " << options.reference_device << " "
                << options.reference_channels << "ch/ch" << options.reference_channel
                << ", delay=" << options.aec_delay_ms << "ms" << std::endl;
    }
    std::cout << Timestamp() << " pipeline: "
            << (options.enable_aec ? "dual capture -> WebRTC AEC worker -> " : "audio callback -> ")
            << "bounded queue -> DOA/VAD thread -> ASR thread" << std::endl;

    SpacemiT::VadConfig vad_config = SpacemiT::VadConfig::Preset("silero")
        .withSampleRate(kModelSampleRate)
        .withWindowSize(static_cast<int>(kVadFrameSize))
        .withTriggerThreshold(options.vad_threshold)
        .withStopThreshold(options.vad_stop_threshold)
        .withMinSpeechDuration(options.min_speech_ms)
        .withMinSilenceDuration(options.silence_ms)
        .withNumThreads(1);
    auto vad = std::make_shared<SpacemiT::VadEngine>(vad_config);
    if (!vad->IsInitialized()) {
        std::cerr << "failed to initialize Silero VAD" << std::endl;
        return 1;
    }
    std::cout << Timestamp() << " VAD initialized: " << vad->GetEngineName() << std::endl;

    SpacemiT::AsrConfig asr_config = SpacemiT::AsrConfig::Preset("sensevoice");
    asr_config.language = options.asr_language;
    asr_config.punctuation = true;
    asr_config.provider = options.asr_provider;
    if (!options.asr_model_dir.empty()) {
        asr_config.model_dir = options.asr_model_dir;
    }
    auto asr = std::make_shared<SpacemiT::AsrEngine>(asr_config);
    if (!asr->IsInitialized()) {
        std::cerr << "failed to initialize SenseVoice ASR" << std::endl;
        return 1;
    }
    std::cout << Timestamp() << " ASR initialized: provider=" << options.asr_provider
            << ", language=" << options.asr_language << std::endl;

    if (options.warmup) {
        std::vector<float> silence(kModelSampleRate / 2, 0.0f);
        auto t0 = std::chrono::steady_clock::now();
        asr->Recognize(silence, kModelSampleRate);
        auto t1 = std::chrono::steady_clock::now();
        std::cout << Timestamp() << " ASR warmup "
                << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
                << " ms" << std::endl;
    }

    DoaRuntimeConfig doa_config;
    doa_config.sample_rate = kModelSampleRate;
    doa_config.mic_distance_m = options.mic_distance_m;
    doa_config.confidence_threshold = options.doa_confidence_threshold;
    doa_config.frame_size = 512;
    doa_config.avg_frames = 4;
    doa_config.flip_angle = options.doa_flip;
    DoaRuntime doa(doa_config);
    if (!doa.Initialize()) {
        std::cerr << "failed to initialize DOA" << std::endl;
        return 1;
    }

    Resampler::Config mono_resampler_config;
    mono_resampler_config.input_sample_rate = options.capture_rate;
    mono_resampler_config.output_sample_rate = kModelSampleRate;
    mono_resampler_config.channels = 1;
    mono_resampler_config.method = options.capture_rate > kModelSampleRate
        ? ResampleMethod::LINEAR_DOWNSAMPLE
        : ResampleMethod::LINEAR_UPSAMPLE;
    Resampler mono_resampler(mono_resampler_config);
    if (!mono_resampler.initialize()) {
        std::cerr << "failed to initialize mono resampler" << std::endl;
        return 1;
    }

    Resampler raw_mono_resampler(mono_resampler_config);
    if (!raw_mono_resampler.initialize()) {
        std::cerr << "failed to initialize raw mono resampler" << std::endl;
        return 1;
    }

    Resampler::Config stereo_resampler_config;
    stereo_resampler_config.input_sample_rate = options.capture_rate;
    stereo_resampler_config.output_sample_rate = kModelSampleRate;
    stereo_resampler_config.channels = 2;
    stereo_resampler_config.method = options.capture_rate > kModelSampleRate
        ? ResampleMethod::LINEAR_DOWNSAMPLE
        : ResampleMethod::LINEAR_UPSAMPLE;
    Resampler stereo_resampler(stereo_resampler_config);
    if (!stereo_resampler.initialize()) {
        std::cerr << "failed to initialize stereo resampler" << std::endl;
        return 1;
    }

#ifdef AI_CUBPET_USE_WEBRTC_AEC
    WebRtcAgcProcessor agc_processor;
    if (use_webrtc_agc) {
        WebRtcAgcConfig agc_config;
        agc_config.sample_rate = kModelSampleRate;
        agc_config.headroom_db = options.agc_headroom_db;
        agc_config.max_gain_db = options.agc_max_gain_db;
        agc_config.initial_gain_db = options.agc_initial_gain_db;
        agc_config.max_gain_change_db_per_second =
            options.agc_max_gain_change_db_per_second;
        agc_config.max_output_noise_level_dbfs =
            options.agc_max_output_noise_level_dbfs;
        if (!agc_processor.Initialize(agc_config)) {
            return 1;
        }
    }
#endif

    AudioChunkQueue audio_queue(static_cast<size_t>(options.queue_chunks));
    UtteranceQueue utterance_queue(4);
    ActionQueue action_queue(4);
    std::unique_ptr<DuplexAudioPipeline> duplex_audio;
    std::unique_ptr<OutputAudioQueue> output_audio;
    size_t dropped_playbacks = 0;
    std::vector<int16_t> saved_capture;
    std::vector<int16_t> saved_raw_capture;

    ToyStateMachine toy_state_machine;
    toy_state_machine.HandleEvent(ToyEvent::BootComplete());
    VoiceInteractionGate voice_gate(std::chrono::seconds(12));
    std::mutex toy_state_mutex;

    CubpetMotorActions motor_actions;
    const bool actions_enabled = motor_actions.Initialize();
    CubpetLedController led_controller;
    bool led_enabled = false;
    ActionRateLimiter action_rate_limiter(kMotorActionInterval);
    CubpetWifiProvisioningRuntime wifi_provisioning;
    EnvironmentMonitorSet environment_monitors;
    std::atomic<bool> environment_monitors_active{false};
    std::mutex continuous_activity_mutex;
    auto continuous_last_activity = std::chrono::steady_clock::now();
    auto mark_continuous_activity = [&]() {
        std::lock_guard<std::mutex> lock(continuous_activity_mutex);
        continuous_last_activity = std::chrono::steady_clock::now();
    };
    auto continuous_idle_expired = [&]() {
        std::lock_guard<std::mutex> lock(continuous_activity_mutex);
        return std::chrono::steady_clock::now() - continuous_last_activity >=
            kContinuousIdleTimeout;
    };
#ifdef AI_CUBPET_USE_DDS
    CubpetUiPublisher ui_publisher;
    const bool ui_dds_enabled = options.enable_ui_dds && ui_publisher.Initialize();
#endif

    auto enqueue_playback_samples = [&](std::vector<float> samples) {
        if (duplex_audio) {
            duplex_audio->EnqueuePlayback(std::move(samples), kPlaybackChannels);
            return true;
        }
        if (output_audio) {
            output_audio->EnqueuePlayback(std::move(samples), kPlaybackChannels);
            return true;
        }
        return false;
    };

    auto current_playback_sample_rate = [&]() {
        if (duplex_audio) {
            return duplex_audio->sample_rate();
        }
        return kPlaybackSampleRate;
    };

    auto enqueue_intent_audio = [&](VoiceIntent intent, const std::string& audio_path) {
        if (!options.enable_playback || audio_path.empty()) {
            return;
        }
        std::cout << Timestamp() << " [playback] "
                << VoiceIntentName(intent) << " " << audio_path << std::endl;
        std::vector<float> samples;
        if (!LoadWavForDuplexPlayback(
                    audio_path, current_playback_sample_rate(), &samples) ||
                !enqueue_playback_samples(std::move(samples))) {
            std::cerr << Timestamp() << " [playback] failed: "
                    << audio_path << std::endl;
            return;
        }
    };

    auto queue_intent = [&](VoiceIntent intent, bool has_doa, float doa_angle) {
        if (intent == VoiceIntent::kUnknown) {
            return;
        }
        if (options.enable_playback) {
            enqueue_intent_audio(
                intent, ResolveAudioPath(VoiceIntentAudioPath(intent), options.audio_dir));
        }
#ifdef AI_CUBPET_USE_DDS
        if (ui_dds_enabled) {
            ui_publisher.PublishGif(VoiceIntentGifPath(intent));
        }
#endif
        if (!actions_enabled) {
            return;
        }
        if (!motor_actions.Supports(intent)) {
            std::cout << Timestamp() << " [ACTION] motor skipped unsupported "
                    << VoiceIntentName(intent) << std::endl;
            return;
        }
        if (!action_rate_limiter.ShouldRunMotor(intent, std::chrono::steady_clock::now())) {
            std::cout << Timestamp() << " [ACTION] motor rate_limited "
                    << VoiceIntentName(intent) << std::endl;
            return;
        }
        action_queue.Push({intent, has_doa, doa_angle});
        std::cout << Timestamp() << " [ACTION] queued motor "
                << VoiceIntentName(intent) << std::endl;
    };

    auto queue_motor_only = [&](VoiceIntent intent, bool has_doa, float doa_angle) {
        if (intent == VoiceIntent::kUnknown) {
            return;
        }
        if (!actions_enabled) {
            return;
        }
        if (!motor_actions.Supports(intent)) {
            std::cout << Timestamp() << " [ACTION] motor skipped unsupported "
                    << VoiceIntentName(intent) << std::endl;
            return;
        }
        if (!action_rate_limiter.ShouldRunMotor(intent, std::chrono::steady_clock::now())) {
            std::cout << Timestamp() << " [ACTION] motor rate_limited "
                    << VoiceIntentName(intent) << std::endl;
            return;
        }
        action_queue.Push({intent, has_doa, doa_angle});
        std::cout << Timestamp() << " [ACTION] queued motor-only "
                << VoiceIntentName(intent) << std::endl;
    };

    std::function<void(const WifiProvisioningResult&)> handle_wifi_provisioning_result;
    std::function<void(ToyAction, bool, float)> run_product_action;
    run_product_action = [&](ToyAction action, bool has_doa, float doa_angle) {
        const ActionMedia media = ActionController::ResolveActionMedia(action);
        std::cout << Timestamp() << " [TOY] action=" << ToyActionName(action)
                << " media_intent=" << VoiceIntentName(media.intent) << std::endl;
        if (action == ToyAction::kStartProvisioning) {
            if (environment_monitors_active.load()) {
                environment_monitors.SetFanSpeed(35);
            }
            const auto status = wifi_provisioning.Start(WifiProvisioningConfig{},
                [](const std::string& line) {
                    std::cout << Timestamp() << " " << line << std::endl;
                },
                handle_wifi_provisioning_result);
            std::cout << Timestamp() << " [wifi][linkd] start "
                    << (status.started ? "started" :
                        (status.already_running ? "already_running" : "failed"))
                    << " message=" << status.message << std::endl;
        } else if (action == ToyAction::kConversationStart) {
            mark_continuous_activity();
            if (environment_monitors_active.load()) {
                environment_monitors.SetFanSpeed(35);
            }
        } else if (action == ToyAction::kIdle || action == ToyAction::kWifiConnected ||
                action == ToyAction::kWifiError || action == ToyAction::kConversationStop ||
                action == ToyAction::kSleep || action == ToyAction::kLowBattery ||
                action == ToyAction::kFallDetected) {
            if (environment_monitors_active.load()) {
                environment_monitors.StopFan();
            }
            if (wifi_provisioning.IsRunning()) {
                wifi_provisioning.Stop([](const std::string& line) {
                    std::cout << Timestamp() << " " << line << std::endl;
                });
            }
        }
        if (media.intent != VoiceIntent::kUnknown) {
            queue_intent(media.intent, has_doa, doa_angle);
        }
        if (options.enable_playback && !media.audio_path.empty()) {
            enqueue_intent_audio(VoiceIntent::kUnknown,
                ResolveAudioPath(media.audio_path, options.audio_dir));
        }
#ifdef AI_CUBPET_USE_DDS
        if (ui_dds_enabled && !media.gif_path.empty()) {
            ui_publisher.PublishGif(media.gif_path);
        }
#endif
        if (media.motor_intent != VoiceIntent::kUnknown &&
                media.motor_intent != media.intent) {
            queue_motor_only(media.motor_intent, has_doa, doa_angle);
        }
        if (media.led.enabled && led_enabled) {
            led_controller.PlayCue(media.led);
        } else if (media.led.enabled) {
            std::cout << Timestamp() << " [led] cue skipped: led disabled"
                    << std::endl;
        }
    };

    handle_wifi_provisioning_result = [&](const WifiProvisioningResult& result) {
        ToyReaction reaction;
        {
            std::lock_guard<std::mutex> lock(toy_state_mutex);
            reaction = toy_state_machine.HandleEvent(
                result.success ? ToyEvent::WifiConnected() : ToyEvent::WifiError());
        }
        std::cout << Timestamp() << " [TOY] wifi_provisioning_result="
                << (result.success ? "connected" : "failed")
                << " ssid=" << (result.ssid.empty() ? "--" : result.ssid)
                << " ip=" << (result.ip_addr.empty() ? "--" : result.ip_addr)
                << " reason=\"" << result.message << "\""
                << " state=" << ToyStateName(reaction.state)
                << " wifi=" << WifiStateName(reaction.wifi_state)
                << " mode=" << ConversationModeName(reaction.conversation_mode)
                << " handled=" << (reaction.handled ? "yes" : "no") << std::endl;
        if (reaction.action != ToyAction::kNone) {
            run_product_action(reaction.action, false, 90.0f);
        }
    };

    auto handle_product_command = [&](ProductCommand command,
                                    bool has_doa,
                                    float doa_angle) {
        ToyEvent event;
        switch (command) {
        case ProductCommand::kStartProvisioning:
            event = ToyEvent::StartProvisioning("voice");
            break;
        case ProductCommand::kExitProvisioning:
            event = ToyEvent::ExitProvisioning();
            break;
        case ProductCommand::kEnterContinuousConversation:
            event = ToyEvent::EnterContinuousConversation();
            break;
        case ProductCommand::kExitContinuousConversation:
            event = ToyEvent::ExitContinuousConversation("voice");
            break;
        case ProductCommand::kSleep:
            event = ToyEvent::Sleep();
            break;
        case ProductCommand::kWake:
        case ProductCommand::kUnknown:
        default:
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        ToyReaction reaction;
        {
            std::lock_guard<std::mutex> lock(toy_state_mutex);
            reaction = toy_state_machine.HandleEvent(event);
            if (reaction.handled) {
                voice_gate.NotifyProductCommandHandled(command, now);
            }
        }
        std::cout << Timestamp() << " [TOY] command=" << ProductCommandName(command)
                << " state=" << ToyStateName(reaction.state)
                << " wifi=" << WifiStateName(reaction.wifi_state)
                << " mode=" << ConversationModeName(reaction.conversation_mode)
                << " handled=" << (reaction.handled ? "yes" : "no") << std::endl;
        if (reaction.action != ToyAction::kNone) {
            run_product_action(reaction.action, has_doa, doa_angle);
        }
        return reaction.handled;
    };

    auto handle_hardware_wake = [&]() {
        const auto now = std::chrono::steady_clock::now();
        ToyReaction reaction;
        {
            std::lock_guard<std::mutex> lock(toy_state_mutex);
            reaction = toy_state_machine.HandleEvent(ToyEvent::Wake("gpio"));
            if (reaction.handled) {
                voice_gate.NotifyWake(now);
            }
        }
        std::cout << Timestamp() << " [TOY] wake source=gpio"
                << " state=" << ToyStateName(reaction.state)
                << " wifi=" << WifiStateName(reaction.wifi_state)
                << " mode=" << ConversationModeName(reaction.conversation_mode)
                << " handled=" << (reaction.handled ? "yes" : "no") << std::endl;
        if (reaction.action != ToyAction::kNone) {
            run_product_action(reaction.action, false, 90.0f);
        }
    };

    auto handle_peripheral_event = [&](const PeripheralEvent& event) {
        ToyEvent toy_event;
        std::ostringstream detail;
        if (event.type == PeripheralEventType::kTouch) {
            toy_event = ToyEvent::Touch(event.role, event.long_press);
            detail << "role=" << event.role
                << " long=" << (event.long_press ? "yes" : "no")
                << " hold_ms=" << event.hold_duration.count();
        } else if (event.type == PeripheralEventType::kTouchCombo) {
            toy_event = ToyEvent::TouchCombo(event.roles, event.hold_duration);
            detail << "roles=";
            for (size_t i = 0; i < event.roles.size(); ++i) {
                if (i > 0) {
                    detail << "+";
                }
                detail << event.roles[i];
            }
            detail << " hold_ms=" << event.hold_duration.count();
        } else if (event.type == PeripheralEventType::kNfc) {
            toy_event = ToyEvent::NfcDetected(event.value);
            detail << "uid=" << (event.value.empty() ? "--" : event.value);
        } else if (event.type == PeripheralEventType::kLight) {
            toy_event = ToyEvent::LightChanged(event.value);
            detail << "level=" << (event.value.empty() ? "--" : event.value);
        } else if (event.type == PeripheralEventType::kPower) {
            toy_event = ToyEvent::Power(PowerStateFromPeripheralValue(event.value));
            detail << "state=" << (event.value.empty() ? "--" : event.value);
        } else if (event.type == PeripheralEventType::kMotion) {
            if (event.value == "fall") {
                toy_event = ToyEvent::MotionFall();
            } else if (event.value == "shake") {
                toy_event = ToyEvent::MotionShake();
            } else {
                return;
            }
            detail << "type=" << event.value;
        } else {
            return;
        }

        ToyReaction reaction;
        {
            std::lock_guard<std::mutex> lock(toy_state_mutex);
            reaction = toy_state_machine.HandleEvent(toy_event);
        }
        std::cout << Timestamp() << " [TOY] peripheral="
                << PeripheralEventTypeName(event.type)
                << " " << detail.str()
                << " state=" << ToyStateName(reaction.state)
                << " wifi=" << WifiStateName(reaction.wifi_state)
                << " mode=" << ConversationModeName(reaction.conversation_mode)
                << " handled=" << (reaction.handled ? "yes" : "no") << std::endl;
        if (reaction.action != ToyAction::kNone) {
            run_product_action(reaction.action, false, 90.0f);
        }
    };

    PeripheralManager peripheral_manager;
    const cubpet_gpio_input_config* wake_gpio = nullptr;
    std::vector<cubpet_gpio_input_config> touch_gpios;
    const bool need_peripheral_config = options.enable_wake_gpio ||
        options.enable_touch_gpio ||
        options.enable_environment_monitors;
    const int peripheral_rc = need_peripheral_config
        ? peripheral_manager.LoadAuto()
        : 0;
    if (need_peripheral_config && peripheral_rc != 0) {
        std::cerr << Timestamp()
                << " [peripheral] failed to load config, rc="
                << peripheral_rc << std::endl;
    }

    if (options.enable_wake_gpio && peripheral_rc == 0) {
        wake_gpio = peripheral_manager.FindGpioByRole("wake");
        if (!wake_gpio) {
            std::cerr << Timestamp()
                    << " [wake] role=wake not found in peripheral config"
                    << std::endl;
        }
    }

    if (options.enable_touch_gpio && peripheral_rc == 0) {
        for (const std::string role : {"head", "nose", "foot"}) {
            const auto* touch_gpio = peripheral_manager.FindGpioByRole(role);
            if (touch_gpio) {
                touch_gpios.push_back(*touch_gpio);
            } else {
                std::cerr << Timestamp()
                        << " [touch] role=" << role
                        << " not found in peripheral config" << std::endl;
            }
        }
    }

    WakeGpioMonitor wake_monitor;
    const bool wake_monitor_enabled = options.enable_wake_gpio && wake_gpio &&
        wake_monitor.Start(*wake_gpio, std::chrono::milliseconds(50),
            [&]() { handle_hardware_wake(); });
    if (options.enable_wake_gpio && !wake_monitor_enabled) {
        std::cerr << Timestamp()
                << " [wake] hardware wake monitor disabled; voice commands remain gated"
                << std::endl;
    }

    TouchGpioMonitor touch_monitor;
    const bool touch_monitor_enabled = options.enable_touch_gpio && !touch_gpios.empty() &&
        touch_monitor.Start(touch_gpios, std::chrono::milliseconds(50),
            [&](const PeripheralEvent& event) { handle_peripheral_event(event); });
    if (options.enable_touch_gpio && !touch_monitor_enabled) {
        std::cerr << Timestamp()
                << " [touch] hardware touch monitor disabled" << std::endl;
    }

    const bool environment_monitor_enabled = options.enable_environment_monitors &&
        peripheral_rc == 0 &&
        environment_monitors.Start(peripheral_manager.config(), EnvironmentMonitorOptions{},
            [&](const PeripheralEvent& event) { handle_peripheral_event(event); },
            [](const std::string& line) {
                std::cout << Timestamp() << " " << line << std::endl;
            });
    environment_monitors_active.store(environment_monitor_enabled);
    if (options.enable_environment_monitors && !environment_monitor_enabled) {
        std::cerr << Timestamp()
                << " [environment] hardware monitors disabled" << std::endl;
    }
    if (peripheral_rc == 0) {
        led_enabled = led_controller.Initialize(peripheral_manager.config().led);
    }

    std::thread state_timer_thread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(kStateTimerInterval);
            if (!g_running) {
                break;
            }

            ToyReaction reaction;
            std::string timer_name;
            {
                std::lock_guard<std::mutex> lock(toy_state_mutex);
                const auto now = std::chrono::steady_clock::now();
                const ToyState state = toy_state_machine.state();
                if ((state == ToyState::kListening || state == ToyState::kAwake) &&
                        !voice_gate.AllowsVad(toy_state_machine, now)) {
                    reaction = toy_state_machine.HandleEvent(ToyEvent::ListeningTimeout());
                    timer_name = "listening_timeout";
                } else if (toy_state_machine.conversation_mode() ==
                            ConversationMode::kContinuous &&
                        continuous_idle_expired()) {
                    reaction = toy_state_machine.HandleEvent(
                        ToyEvent::ConversationIdleTimeout());
                    timer_name = "continuous_idle_timeout";
                }
            }

            if (reaction.handled) {
                std::cout << Timestamp() << " [TOY] timer=" << timer_name
                        << " state=" << ToyStateName(reaction.state)
                        << " wifi=" << WifiStateName(reaction.wifi_state)
                        << " mode=" << ConversationModeName(reaction.conversation_mode)
                        << " handled=yes" << std::endl;
                if (reaction.action != ToyAction::kNone) {
                    run_product_action(reaction.action, false, 90.0f);
                }
            }
        }
    });

    std::thread action_thread([&]() {
        while (true) {
            ActionItem action;
            if (!action_queue.WaitPop(&action)) {
                break;
            }
            if (!actions_enabled || action.intent == VoiceIntent::kUnknown) {
                continue;
            }
            const bool ok = motor_actions.Execute(
                action.intent, action.has_doa, action.doa_angle);
            std::cout << Timestamp() << " [ACTION] "
                    << VoiceIntentName(action.intent)
                    << (ok ? " done" : " failed") << std::endl;
        }
    });

    std::thread asr_thread([&]() {
        while (true) {
            Utterance utterance;
            if (!utterance_queue.WaitPop(&utterance)) {
                break;
            }

            const double audio_sec =
                utterance.audio_16k.size() / static_cast<double>(kModelSampleRate);
            if (!utterance.asr_allowed) {
                std::cout << Timestamp() << " [ASR] gated #" << utterance.index
                        << ", " << std::fixed << std::setprecision(2) << audio_sec
                        << "s state=" << ToyStateName(utterance.gate_state)
                        << " mode=" << ConversationModeName(utterance.gate_mode)
                        << std::endl;
                continue;
            }
            if (utterance.gate_mode == ConversationMode::kContinuous ||
                    utterance.gate_state == ToyState::kContinuousConversation) {
                mark_continuous_activity();
            }

            std::cout << Timestamp() << " [ASR] #" << utterance.index << " start, "
                    << std::fixed << std::setprecision(2) << audio_sec << "s";
            if (utterance.has_doa) {
                std::cout << ", DOA=" << std::setprecision(1) << utterance.doa_angle
                        << "deg conf=" << std::setprecision(3)
                        << utterance.doa_confidence;
            } else {
                std::cout << ", DOA=--";
            }
            std::cout << std::endl;

            auto t0 = std::chrono::steady_clock::now();
            auto result = asr->Recognize(utterance.audio_16k, kModelSampleRate);
            auto t1 = std::chrono::steady_clock::now();
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

            if (result && !result->IsEmpty()) {
                std::string text = result->GetText();
                ProductCommand command = MatchProductCommand(text);
                VoiceIntent intent = command == ProductCommand::kUnknown
                    ? MatchVoiceIntent(text) : VoiceIntent::kUnknown;
                std::cout << Timestamp() << " [ASR] #" << utterance.index << " result: \""
                        << text << "\" command=" << ProductCommandName(command)
                        << " intent=" << VoiceIntentName(intent)
                        << " time=" << elapsed_ms << "ms" << std::endl;
                const auto now = std::chrono::steady_clock::now();
                if (command != ProductCommand::kUnknown) {
                    bool allowed = false;
                    ToyState current_state = ToyState::kBooting;
                    ConversationMode current_mode = ConversationMode::kWakeTriggered;
                    {
                        std::lock_guard<std::mutex> lock(toy_state_mutex);
                        allowed = utterance.asr_allowed ||
                            voice_gate.AllowsProductCommand(toy_state_machine, command, now);
                        current_state = toy_state_machine.state();
                        current_mode = toy_state_machine.conversation_mode();
                    }
                    if (allowed) {
                        handle_product_command(command, utterance.has_doa, utterance.doa_angle);
                    } else {
                        std::cout << Timestamp() << " [VOICE] gated command="
                                << ProductCommandName(command)
                                << " state=" << ToyStateName(current_state)
                                << " mode=" << ConversationModeName(current_mode)
                                << std::endl;
                    }
                } else if (intent != VoiceIntent::kUnknown) {
                    bool allowed = false;
                    ToyState current_state = ToyState::kBooting;
                    ConversationMode current_mode = ConversationMode::kWakeTriggered;
                    {
                        std::lock_guard<std::mutex> lock(toy_state_mutex);
                        allowed = utterance.asr_allowed ||
                            voice_gate.AllowsVoiceIntent(toy_state_machine, intent, now);
                        current_state = toy_state_machine.state();
                        current_mode = toy_state_machine.conversation_mode();
                    }
                    if (allowed) {
                        queue_intent(intent, utterance.has_doa, utterance.doa_angle);
                        std::lock_guard<std::mutex> lock(toy_state_mutex);
                        voice_gate.NotifyVoiceIntentHandled(now);
                    } else {
                        std::cout << Timestamp() << " [VOICE] gated intent="
                                << VoiceIntentName(intent)
                                << " state=" << ToyStateName(current_state)
                                << " mode=" << ConversationModeName(current_mode)
                                << std::endl;
                    }
                }
            } else {
                std::cout << Timestamp() << " [ASR] #" << utterance.index
                        << " no result, time=" << elapsed_ms << "ms" << std::endl;
            }
        }
    });

    std::thread processing_thread([&]() {
        std::deque<float> vad_accumulator;
        std::deque<float> pre_buffer;
        std::deque<float> stereo_pre_buffer;
        std::vector<float> vad_frame(kVadFrameSize);
        std::vector<float> utterance_audio;
        utterance_audio.reserve(static_cast<size_t>(options.max_utterance_seconds) *
            kModelSampleRate);

        const size_t pre_buffer_max = static_cast<size_t>(
            options.pre_speech_ms * kModelSampleRate / 1000);
        const size_t stereo_pre_buffer_max = pre_buffer_max * 2;
        const int silence_frames_threshold = std::max<int>(
            1, options.silence_ms / static_cast<int>(kVadFrameSize * 1000 / kModelSampleRate));
        const size_t min_speech_samples = static_cast<size_t>(
            options.min_speech_ms * kModelSampleRate / 1000);
        const size_t max_utterance_samples = static_cast<size_t>(
            options.max_utterance_seconds * kModelSampleRate);

        bool speaking = false;
        bool pending_end = false;
        int silence_frames = 0;
        int utterance_index = 0;
        float segment_max_vad = 0.0f;
        bool current_utterance_asr_allowed = false;
        ToyState current_utterance_gate_state = ToyState::kBooting;
        ConversationMode current_utterance_gate_mode = ConversationMode::kWakeTriggered;
        bool vad_gate_previously_open = false;

        auto finish_utterance = [&]() {
            if (utterance_audio.size() < min_speech_samples) {
                utterance_audio.clear();
                return;
            }

            Utterance utterance;
            utterance.index = ++utterance_index;
            utterance.audio_16k = std::move(utterance_audio);
            utterance_audio.clear();
            utterance_audio.reserve(max_utterance_samples);
            utterance.max_vad_prob = segment_max_vad;
            utterance.has_doa = doa.HasAngle();
            utterance.doa_angle = doa.LatestAngleDegrees();
            utterance.doa_confidence = doa.LatestConfidence();
            utterance.asr_allowed = current_utterance_asr_allowed;
            utterance.gate_state = current_utterance_gate_state;
            utterance.gate_mode = current_utterance_gate_mode;

            std::cout << Timestamp() << " [VAD] end #" << utterance.index
                    << " samples=" << utterance.audio_16k.size()
                    << " max_prob=" << std::fixed << std::setprecision(3)
                    << utterance.max_vad_prob;
            if (utterance.has_doa) {
                std::cout << " DOA=" << std::setprecision(1) << utterance.doa_angle
                        << "deg";
            } else {
                std::cout << " DOA=--";
            }
            std::cout << std::endl;

            utterance_queue.Push(std::move(utterance));
            segment_max_vad = 0.0f;
            current_utterance_asr_allowed = false;
        };

        while (g_running) {
            AudioChunk chunk;
            if (!audio_queue.WaitPop(&chunk)) {
                break;
            }
            if (chunk.samples.empty() || chunk.frames == 0) {
                continue;
            }

            const int chunk_channels = chunk.channels > 0
                ? chunk.channels
                : options.capture_channels;
            const int16_t* pcm = chunk.samples.data();
            std::vector<float> mono;
            if (chunk.has_aec) {
                mono = std::move(chunk.aec_mono);
            } else {
                mono = PcmToMonoFloat(
                    pcm, chunk.frames, chunk_channels, options.speech_channel);
            }
            std::vector<float> mono_16k = options.capture_rate == kModelSampleRate
                ? std::move(mono)
                : mono_resampler.processStreaming(mono, false);

            if (mono_16k.empty()) {
                continue;
            }
#ifdef AI_CUBPET_USE_WEBRTC_AEC
            if (use_webrtc_agc && !agc_processor.Process(&mono_16k)) {
                continue;
            }
#endif
            if (!options.save_wav.empty()) {
                AppendToWavBuffer(&saved_capture, mono_16k);
            }
            if (!options.save_raw_wav.empty()) {
                auto raw_mono = PcmToMonoFloat(
                    pcm, chunk.frames, chunk_channels, options.speech_channel);
                std::vector<float> raw_mono_16k = options.capture_rate == kModelSampleRate
                    ? std::move(raw_mono)
                    : raw_mono_resampler.processStreaming(raw_mono, false);
                AppendToWavBuffer(&saved_raw_capture, raw_mono_16k);
            }

            const auto vad_gate_now = std::chrono::steady_clock::now();
            bool allow_vad = false;
            ToyState vad_gate_state = ToyState::kBooting;
            ConversationMode vad_gate_mode = ConversationMode::kWakeTriggered;
            {
                std::lock_guard<std::mutex> lock(toy_state_mutex);
                allow_vad = voice_gate.AllowsVad(toy_state_machine, vad_gate_now);
                vad_gate_state = toy_state_machine.state();
                vad_gate_mode = toy_state_machine.conversation_mode();
            }
            if (!allow_vad && !speaking) {
                vad_accumulator.clear();
                pre_buffer.clear();
                stereo_pre_buffer.clear();
                utterance_audio.clear();
                pending_end = false;
                silence_frames = 0;
                segment_max_vad = 0.0f;
                current_utterance_asr_allowed = false;
                current_utterance_gate_state = vad_gate_state;
                current_utterance_gate_mode = vad_gate_mode;
                if (vad_gate_previously_open) {
                    vad->Reset();
                    vad_gate_previously_open = false;
                }
                continue;
            }
            vad_gate_previously_open = true;

            auto stereo = PcmToStereoFloat(pcm, chunk.frames, chunk_channels);
            std::vector<float> stereo_16k = options.capture_rate == kModelSampleRate
                ? std::move(stereo)
                : stereo_resampler.processStreaming(stereo, false);

            bool chunk_touched_speech = speaking;
            bool chunk_started_speech = false;

            for (float sample : mono_16k) {
                vad_accumulator.push_back(sample);
            }

            while (vad_accumulator.size() >= kVadFrameSize) {
                for (size_t i = 0; i < kVadFrameSize; ++i) {
                    vad_frame[i] = vad_accumulator.front();
                    vad_accumulator.pop_front();
                }

                auto vad_result = vad->Detect(vad_frame.data(), vad_frame.size(),
                    kModelSampleRate);
                const float prob = vad_result ? vad_result->GetProbability() : 0.0f;
                const bool speech = prob >= (speaking
                    ? options.vad_stop_threshold
                    : options.vad_threshold);

                if (speech) {
                    chunk_touched_speech = true;
                    silence_frames = 0;
                    segment_max_vad = std::max(segment_max_vad, prob);

                    if (!speaking) {
                        speaking = true;
                        chunk_started_speech = true;
                        pending_end = false;
                        doa.Reset();
                        current_utterance_asr_allowed = allow_vad;
                        current_utterance_gate_state = vad_gate_state;
                        current_utterance_gate_mode = vad_gate_mode;
                        if (!stereo_pre_buffer.empty()) {
                            std::vector<float> stereo_preroll(
                                stereo_pre_buffer.begin(), stereo_pre_buffer.end());
                            const size_t frames = stereo_preroll.size() / 2;
                            if (frames > 0) {
                                doa.ProcessInterleavedFloat(stereo_preroll.data(), frames, 2);
                            }
                            stereo_pre_buffer.clear();
                        }
                        utterance_audio.clear();
                        utterance_audio.reserve(max_utterance_samples);
                        for (float pre : pre_buffer) {
                            utterance_audio.push_back(pre);
                        }
                        pre_buffer.clear();
                        std::cout << "\n" << Timestamp() << " [VAD] speech start prob="
                                << std::fixed << std::setprecision(3) << prob
                                << std::endl;
                    }

                    utterance_audio.insert(
                        utterance_audio.end(), vad_frame.begin(), vad_frame.end());
                    if (utterance_audio.size() >= max_utterance_samples) {
                        std::cout << Timestamp()
                                << " [VAD] max utterance reached, forcing ASR"
                                << std::endl;
                        speaking = false;
                        pending_end = true;
                    }
                } else if (speaking) {
                    chunk_touched_speech = true;
                    utterance_audio.insert(
                        utterance_audio.end(), vad_frame.begin(), vad_frame.end());
                    silence_frames++;
                    if (silence_frames >= silence_frames_threshold) {
                        speaking = false;
                        pending_end = true;
                        silence_frames = 0;
                    }
                } else {
                    AppendToPreBuffer(&pre_buffer, vad_frame, pre_buffer_max);
                }
            }

            if (chunk_touched_speech || chunk_started_speech || pending_end) {
                if (!stereo_16k.empty()) {
                    const size_t frames = stereo_16k.size() / 2;
                    doa.ProcessInterleavedFloat(stereo_16k.data(), frames, 2);
                }
            } else if (!stereo_16k.empty()) {
                AppendInterleavedToPreBuffer(
                    &stereo_pre_buffer, stereo_16k, stereo_pre_buffer_max);
            }

            if (pending_end) {
                finish_utterance();
                pending_end = false;
            }
        }

        if (speaking && utterance_audio.size() >= min_speech_samples) {
            finish_utterance();
        }
    });

    auto stop_workers = [&]() {
        g_running = false;
        audio_queue.Stop();
        if (processing_thread.joinable()) {
            processing_thread.join();
        }
        utterance_queue.Stop();
        if (asr_thread.joinable()) {
            asr_thread.join();
        }
        action_queue.Stop();
        if (action_thread.joinable()) {
            action_thread.join();
        }
        if (state_timer_thread.joinable()) {
            state_timer_thread.join();
        }
        if (duplex_audio) {
            dropped_playbacks = duplex_audio->dropped();
        }
        if (output_audio) {
            dropped_playbacks += output_audio->dropped();
        }
        environment_monitors_active.store(false);
        environment_monitors.Stop();
        motor_actions.Shutdown();
        wifi_provisioning.Stop([](const std::string& line) {
            std::cout << Timestamp() << " " << line << std::endl;
        });
    };

    std::unique_ptr<AudioPipeline> audio;
#ifdef AI_CUBPET_USE_WEBRTC_AEC
    std::unique_ptr<CubpetAecPipeline> aec_audio;
#endif

    if (options.enable_aec) {
#ifdef AI_CUBPET_USE_WEBRTC_AEC
        AecPipelineConfig aec_config;
        aec_config.sample_rate = options.capture_rate;
        aec_config.capture_channels = options.capture_channels;
        aec_config.speech_channel = options.speech_channel;
        aec_config.frames_per_buffer = options.frames_per_buffer;
        aec_config.input_device = options.input_device;
        aec_config.reference_device = options.reference_device;
        aec_config.reference_channels = options.reference_channels;
        aec_config.reference_channel = options.reference_channel;
        aec_config.queue_chunks = options.queue_chunks;
        aec_config.aec_delay_ms = options.aec_delay_ms;
        aec_config.noise_suppression = options.aec_noise_suppression;
        aec_config.noise_suppression_level = options.aec_noise_suppression_level;
        aec_config.high_pass_filter = options.aec_high_pass_filter;
        aec_config.gain_control = options.aec_gain_control;

        aec_audio = std::make_unique<CubpetAecPipeline>(aec_config);
        if (!aec_audio->Initialize()) {
            stop_workers();
            return 1;
        }
        aec_audio->SetCallback([&](AecAudioFrame frame) {
            AudioChunk chunk;
            chunk.frames = frame.frames;
            chunk.channels = frame.channels;
            chunk.samples = std::move(frame.raw_interleaved);
            chunk.aec_mono = std::move(frame.processed_mono);
            chunk.has_aec = true;
            audio_queue.Push(std::move(chunk));
        });
        if (!aec_audio->Start()) {
            aec_audio->Close();
            stop_workers();
            return 1;
        }
        if (options.enable_playback) {
            output_audio = std::make_unique<OutputAudioQueue>(
                options.output_device, options.frames_per_buffer);
            if (!output_audio->Start()) {
                aec_audio->Stop();
                aec_audio->Close();
                stop_workers();
                output_audio.reset();
                return 1;
            }
        }
#endif
    } else if (options.enable_playback) {
        DuplexAudioConfig duplex_config;
        duplex_config.sample_rate = options.capture_rate;
        duplex_config.capture_channels = options.capture_channels;
        duplex_config.playback_channels = kPlaybackChannels;
        duplex_config.frames_per_buffer = options.frames_per_buffer;
        duplex_config.input_device = options.input_device;
        duplex_config.output_device = options.output_device;

        duplex_audio = std::make_unique<DuplexAudioPipeline>(duplex_config);
        if (!duplex_audio->Initialize()) {
            stop_workers();
            return 1;
        }
        duplex_audio->SetCaptureCallback(
            [&](const int16_t* interleaved, size_t frames, int channels) {
                AudioChunk chunk;
                chunk.frames = frames;
                chunk.channels = channels;
                const size_t samples = frames * static_cast<size_t>(channels);
                chunk.samples.resize(samples);
                std::memcpy(chunk.samples.data(), interleaved,
                    samples * sizeof(int16_t));
                audio_queue.Push(std::move(chunk));
            });

        if (!duplex_audio->Start()) {
            duplex_audio->Close();
            stop_workers();
            return 1;
        }
    } else {
        AudioPipelineConfig audio_config;
        audio_config.sample_rate = options.capture_rate;
        audio_config.capture_channels = options.capture_channels;
        audio_config.speech_channel = options.speech_channel;
        audio_config.frames_per_buffer = options.frames_per_buffer;
        audio_config.input_device = options.input_device;

        audio = std::make_unique<AudioPipeline>(audio_config);
        if (!audio->Initialize()) {
            stop_workers();
            return 1;
        }
        audio->SetCallback([&](const int16_t* interleaved, size_t frames, int channels) {
            AudioChunk chunk;
            chunk.frames = frames;
            chunk.channels = channels;
            const size_t samples = frames * static_cast<size_t>(channels);
            chunk.samples.resize(samples);
            std::memcpy(chunk.samples.data(), interleaved, samples * sizeof(int16_t));
            audio_queue.Push(std::move(chunk));
        });

        if (!audio->Start()) {
            audio->Close();
            stop_workers();
            return 1;
        }
    }

    if (!options.test_playback_file.empty()) {
        if (!duplex_audio && !output_audio) {
            std::cerr << Timestamp()
                    << " [playback] --test-playback requires playback"
                    << std::endl;
        } else {
            const std::string audio_path =
                ResolveAudioPath(options.test_playback_file, options.audio_dir);
            const int count = std::max(1, options.test_playback_count);
            const int interval_ms = std::max(0, options.test_playback_interval_ms);
            for (int n = 0; n < count && g_running; ++n) {
                std::cout << Timestamp() << " [playback] test "
                        << (n + 1) << "/" << count << " " << audio_path << std::endl;
                std::vector<float> samples;
                if (!LoadWavForDuplexPlayback(
                            audio_path, current_playback_sample_rate(), &samples)) {
                    std::cerr << Timestamp() << " [playback] failed: "
                            << audio_path << std::endl;
                    break;
                }
                enqueue_playback_samples(std::move(samples));
                if (n + 1 < count) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
                }
            }
        }
    }

    if (!options.test_intent_name.empty()) {
        const VoiceIntent test_intent = VoiceIntentFromName(options.test_intent_name);
        if (test_intent == VoiceIntent::kUnknown) {
            std::cerr << Timestamp() << " [ACTION] unknown test intent: "
                    << options.test_intent_name << std::endl;
        } else {
            const int count = std::max(1, options.test_intent_count);
            const int interval_ms = std::max(0, options.test_intent_interval_ms);
            for (int n = 0; n < count && g_running; ++n) {
                std::cout << Timestamp() << " [ACTION] test "
                        << (n + 1) << "/" << count << " "
                        << VoiceIntentName(test_intent) << std::endl;
                queue_intent(test_intent, false, 90.0f);
                if (n + 1 < count) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
                }
            }
        }
    }

    const auto start = std::chrono::steady_clock::now();
    while (g_running) {
        if (options.duration_seconds > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= options.duration_seconds) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    g_running = false;
    wake_monitor.Stop();
    touch_monitor.Stop();
    environment_monitors_active.store(false);
    environment_monitors.Stop();
    led_controller.Stop();
    if (audio) {
        audio->Stop();
        audio->Close();
    }
#ifdef AI_CUBPET_USE_WEBRTC_AEC
    AecPipelineStats aec_stats;
    bool have_aec_stats = false;
    if (aec_audio) {
        aec_audio->Stop();
        aec_stats = aec_audio->stats();
        have_aec_stats = true;
        aec_audio->Close();
    }
#endif
    if (duplex_audio) {
        duplex_audio->Stop();
    }
    stop_workers();
    if (duplex_audio) {
        duplex_audio->Close();
    }
    if (output_audio) {
        output_audio->Stop();
        output_audio.reset();
    }

    if (!options.save_wav.empty() && !saved_capture.empty()) {
        if (SaveWavMono16(options.save_wav, saved_capture, kModelSampleRate)) {
            std::cout << Timestamp() << " saved " << options.save_wav << " ("
                    << saved_capture.size() << " samples)" << std::endl;
        }
    }
    if (!options.save_raw_wav.empty() && !saved_raw_capture.empty()) {
        if (SaveWavMono16(options.save_raw_wav, saved_raw_capture, kModelSampleRate)) {
            std::cout << Timestamp() << " saved " << options.save_raw_wav << " ("
                    << saved_raw_capture.size() << " samples)" << std::endl;
        }
    }

    std::cout << Timestamp() << " stopped. dropped_audio_chunks=" << audio_queue.dropped()
            << ", dropped_utterances=" << utterance_queue.dropped()
            << ", dropped_actions=" << action_queue.dropped()
            << ", dropped_playbacks=" << dropped_playbacks << std::endl;
#ifdef AI_CUBPET_USE_WEBRTC_AEC
    if (have_aec_stats) {
        std::cout << Timestamp() << " AEC stats: processed_frames="
                << aec_stats.processed_frames
                << ", mic_drops=" << aec_stats.mic_drops
                << ", ref_drops=" << aec_stats.reference_drops
                << ", ref_underruns=" << aec_stats.reference_underruns
                << ", aec_errors=" << aec_stats.aec_errors
                << ", max_process_ms=" << std::fixed << std::setprecision(3)
                << aec_stats.max_process_ms << std::endl;
    }
#endif
    return 0;
}  // NOLINT(readability/fn_size)

}  // namespace ai_cubpet

extern "C" int run_voice_demo(int argc, char** argv)
{
    return ai_cubpet::RunVoiceDemo(argc, argv);
}
