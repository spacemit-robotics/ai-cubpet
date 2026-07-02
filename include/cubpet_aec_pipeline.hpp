#ifndef CUBPET_AEC_PIPELINE_HPP
#define CUBPET_AEC_PIPELINE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>  // NOLINT(build/c++11)
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>
#include <condition_variable>  // NOLINT(build/c++11)

namespace SpacemitAudio {
class AudioCapture;
}

namespace webrtc {
class AudioProcessing;
}

namespace ai_cubpet {

struct AecPipelineConfig {
    int sample_rate = 48000;
    int capture_channels = 2;
    int speech_channel = 1;
    int frames_per_buffer = 480;
    int input_device = 0;

    int reference_device = 1;
    int reference_channels = 1;
    int reference_channel = 1;

    int queue_chunks = 96;
    int reference_history_ms = 1000;
    int aec_delay_ms = 0;

    bool echo_canceller = true;
    bool noise_suppression = true;
    int noise_suppression_level = 0;
    bool high_pass_filter = true;
    bool gain_control = false;
};

struct AecAudioFrame {
    std::vector<int16_t> raw_interleaved;
    std::vector<float> processed_mono;
    size_t frames = 0;
    int channels = 0;
};

struct AecPipelineStats {
    size_t mic_drops = 0;
    size_t reference_drops = 0;
    size_t reference_underruns = 0;
    size_t aec_errors = 0;
    size_t processed_frames = 0;
    double max_process_ms = 0.0;
};

class CubpetAecPipeline {
public:
    using ProcessedCallback = std::function<void(AecAudioFrame frame)>;

    explicit CubpetAecPipeline(AecPipelineConfig config);
    ~CubpetAecPipeline();

    CubpetAecPipeline(const CubpetAecPipeline&) = delete;
    CubpetAecPipeline& operator=(const CubpetAecPipeline&) = delete;

    bool Initialize();
    void SetCallback(ProcessedCallback callback);
    bool Start();
    void Stop();
    void Close();

    const AecPipelineConfig& config() const { return config_; }
    bool running() const { return running_.load(); }
    AecPipelineStats stats() const;

    static std::vector<std::pair<int, std::string>> ListInputDevices();

private:
    struct MicChunk {
        std::vector<int16_t> samples;
        size_t frames = 0;
    };

    void OnMicData(const uint8_t* data, size_t size);
    void OnReferenceData(const uint8_t* data, size_t size);
    bool WaitMicChunk(MicChunk* chunk);
    std::vector<int16_t> TakeReferenceFrame(size_t frames);
    void ProcessingLoop();
    void ConfigureApm();
    void ReleaseApm();

    AecPipelineConfig config_;
    std::unique_ptr<SpacemitAudio::AudioCapture> mic_capture_;
    std::unique_ptr<SpacemitAudio::AudioCapture> reference_capture_;
    webrtc::AudioProcessing* apm_ = nullptr;

    ProcessedCallback callback_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> processing_running_{false};
    std::thread processing_thread_;

    mutable std::mutex mic_mutex_;
    std::condition_variable mic_cv_;
    std::deque<MicChunk> mic_queue_;
    bool mic_stopped_ = false;

    mutable std::mutex reference_mutex_;
    std::deque<int16_t> reference_queue_;
    size_t reference_max_samples_ = 0;
    size_t reference_delay_samples_ = 0;

    mutable std::mutex stats_mutex_;
    AecPipelineStats stats_;
};

}  // namespace ai_cubpet

#endif  // CUBPET_AEC_PIPELINE_HPP
