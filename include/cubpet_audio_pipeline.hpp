#ifndef CUBPET_AUDIO_PIPELINE_HPP
#define CUBPET_AUDIO_PIPELINE_HPP

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace SpacemitAudio {
class AudioCapture;
}

namespace ai_cubpet {

struct AudioPipelineConfig {
    int sample_rate = 16000;
    int capture_channels = 2;
    int speech_channel = 1;
    int frames_per_buffer = 512;
    int input_device = -1;
};

class AudioPipeline {
public:
    using CaptureCallback = std::function<void(const int16_t* interleaved,
                                            size_t frames,
                                            int channels)>;

    explicit AudioPipeline(AudioPipelineConfig config);
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    bool Initialize();
    void SetCallback(CaptureCallback callback);
    bool Start();
    void Stop();
    void Close();

    const AudioPipelineConfig& config() const { return config_; }
    bool running() const { return running_; }

    static std::vector<std::pair<int, std::string>> ListInputDevices();

private:
    AudioPipelineConfig config_;
    std::unique_ptr<SpacemitAudio::AudioCapture> capture_;
    CaptureCallback callback_;
    bool initialized_ = false;
    bool running_ = false;
};

}  // namespace ai_cubpet

#endif  // CUBPET_AUDIO_PIPELINE_HPP
