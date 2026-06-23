#include "cubpet_audio_pipeline.hpp"

#include "audio_base.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace ai_cubpet {

AudioPipeline::AudioPipeline(AudioPipelineConfig config) : config_(config) {}

AudioPipeline::~AudioPipeline()
{
    Close();
}

bool AudioPipeline::Initialize()
{
    if (config_.sample_rate <= 0) {
        std::cerr << "[audio] invalid sample_rate: " << config_.sample_rate << std::endl;
        return false;
    }
    if (config_.capture_channels <= 0) {
        std::cerr << "[audio] invalid capture_channels: " << config_.capture_channels << std::endl;
        return false;
    }
    if (config_.speech_channel < 1 || config_.speech_channel > config_.capture_channels) {
        std::cerr << "[audio] speech_channel out of range: " << config_.speech_channel
                << " not in [1, " << config_.capture_channels << "]" << std::endl;
        return false;
    }
    if (config_.frames_per_buffer <= 0) {
        std::cerr << "[audio] invalid frames_per_buffer: " << config_.frames_per_buffer
                << std::endl;
        return false;
    }

    capture_ = std::make_unique<SpacemitAudio::AudioCapture>(config_.input_device);
    initialized_ = true;
    std::cout << "[audio] init: sample_rate=" << config_.sample_rate
            << ", capture_channels=" << config_.capture_channels
            << ", speech_channel=" << config_.speech_channel
            << ", frames_per_buffer=" << config_.frames_per_buffer
            << ", input_device=" << config_.input_device
            << std::endl;

    return true;
}

void AudioPipeline::SetCallback(CaptureCallback callback)
{
    callback_ = std::move(callback);
}

bool AudioPipeline::Start()
{
    if (!initialized_ || !capture_) {
        return false;
    }
    if (!callback_) {
        std::cerr << "[audio] capture callback is not set" << std::endl;
        return false;
    }

    capture_->SetCallback([this](const uint8_t* data, size_t size) {
        if (!callback_ || !data || size == 0) {
            return;
        }
        const size_t sample_count = size / sizeof(int16_t);
        const size_t frames = sample_count / static_cast<size_t>(config_.capture_channels);
        if (frames == 0) {
            return;
        }
        callback_(reinterpret_cast<const int16_t*>(data), frames, config_.capture_channels);
    });

    const int chunk_bytes = config_.frames_per_buffer *
        config_.capture_channels * static_cast<int>(sizeof(int16_t));
    if (!capture_->Start(config_.sample_rate, config_.capture_channels, chunk_bytes)) {
        std::cerr << "[audio] failed to start capture" << std::endl;
        return false;
    }

    running_ = true;
    std::cout << "[audio] capture started" << std::endl;
    return true;
}

void AudioPipeline::Stop()
{
    if (running_) {
        capture_->Stop();
        std::cout << "[audio] capture stopped" << std::endl;
    }
    running_ = false;
}

void AudioPipeline::Close()
{
    Stop();
    if (capture_) {
        capture_->Close();
        capture_.reset();
    }
    initialized_ = false;
}

std::vector<std::pair<int, std::string>> AudioPipeline::ListInputDevices()
{
    return SpacemitAudio::AudioCapture::ListDevices();
}

}  // namespace ai_cubpet
