#include "cubpet_voice_demo.hpp"

#include "cubpet_audio_pipeline.hpp"
#ifdef AI_CUBPET_USE_WEBRTC_AEC
#include "cubpet_aec_pipeline.hpp"
#endif
#include "cubpet_doa_runtime.hpp"
#include "cubpet_keywords.hpp"
#include "cubpet_motor_actions.hpp"
#ifdef AI_CUBPET_USE_DDS
#include "cubpet_ui_publisher.hpp"
#endif

#include "asr_service.h"
#include "audio_resampler.hpp"
#include "vad_service.h"
#ifdef AI_CUBPET_USE_WEBRTC_AEC
#include <webrtc/modules/audio_processing/include/audio_processing.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace ai_cubpet {
namespace {

constexpr int kModelSampleRate = 16000;
constexpr int kDefaultCaptureRate = 48000;
constexpr int kDefaultCaptureChannels = 2;
constexpr int kDefaultFramesPerBuffer = 480;
constexpr size_t kVadFrameSize = 512;

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
    std::string save_wav;
    std::string save_raw_wav;
};

void PrintUsage(const char* argv0)
{
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "  -l, --list-devices          List input devices\n"
        << "  -i, --input <N>             Input device index (-1 default)\n"
        << "  --input-device-hint <TEXT>  Prefer input device whose name contains TEXT\n"
        << "  --clear-input-device-hints  Disable built-in input device hints\n"
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
        << "  --no-warmup                 Skip ASR warmup\n"
        << "  -h, --help                  Show this help\n";
}

void ListDevices()
{
    std::cout << "Input devices:" << std::endl;
    auto devices = AudioPipeline::ListInputDevices();
    if (devices.empty()) {
        std::cout << "  (none)" << std::endl;
        return;
    }
    for (const auto& dev : devices) {
        std::cout << "  [" << dev.first << "] " << dev.second << std::endl;
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
    const bool use_webrtc_agc = options.enable_agc && !options.enable_aec;

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    g_running = true;

    std::cout << Timestamp() << " ai-cubpet local voice demo" << std::endl;
    std::cout << Timestamp() << " capture=" << options.capture_rate << "Hz/"
            << options.capture_channels << "ch, frame=" << options.frames_per_buffer
            << ", speech=ch" << options.speech_channel
            << ", agc=" << (use_webrtc_agc ? "on" : "off") << std::endl;
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
    std::vector<int16_t> saved_capture;
    std::vector<int16_t> saved_raw_capture;

    CubpetMotorActions motor_actions;
    const bool actions_enabled = motor_actions.Initialize();
#ifdef AI_CUBPET_USE_DDS
    CubpetUiPublisher ui_publisher;
    const bool ui_dds_enabled = options.enable_ui_dds && ui_publisher.Initialize();
#else
    const bool ui_dds_enabled = false;
#endif
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
                VoiceIntent intent = MatchVoiceIntent(text);
                std::cout << Timestamp() << " [ASR] #" << utterance.index << " result: \""
                        << text << "\" intent=" << VoiceIntentName(intent)
                        << " time=" << elapsed_ms << "ms" << std::endl;
                if (intent != VoiceIntent::kUnknown) {
                    action_queue.Push({intent, utterance.has_doa, utterance.doa_angle});
                    std::cout << Timestamp() << " [ACTION] queued "
                            << VoiceIntentName(intent) << std::endl;
#ifdef AI_CUBPET_USE_DDS
                    if (ui_dds_enabled) {
                        ui_publisher.PublishIntent(intent);
                    }
#endif
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
        motor_actions.Shutdown();
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
#endif
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
    stop_workers();

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
            << ", dropped_actions=" << action_queue.dropped() << std::endl;
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
}

}  // namespace ai_cubpet

extern "C" int run_voice_demo(int argc, char** argv)
{
    return ai_cubpet::RunVoiceDemo(argc, argv);
}
