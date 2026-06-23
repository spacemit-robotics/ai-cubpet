#include "cubpet_doa_runtime.hpp"

#include "doa_service.h"

#include <algorithm>
#include <iostream>

namespace ai_cubpet {

DoaRuntime::DoaRuntime(DoaRuntimeConfig config) : config_(config) {}

DoaRuntime::~DoaRuntime() = default;

bool DoaRuntime::Initialize()
{
    if (config_.sample_rate <= 0) {
        std::cerr << "[doa] invalid sample_rate: " << config_.sample_rate << std::endl;
        return false;
    }
    if (config_.mic_distance_m <= 0.0f) {
        std::cerr << "[doa] invalid mic_distance_m: " << config_.mic_distance_m << std::endl;
        return false;
    }

    SpacemitAudio::SoundLocatorConfig cfg;
    cfg.sample_rate = config_.sample_rate;
    cfg.mic_distance = config_.mic_distance_m;
    cfg.frame_size = config_.frame_size;
    cfg.avg_frames = config_.avg_frames;
    cfg.confidence_threshold = config_.confidence_threshold;

    locator_ = std::make_unique<SpacemitAudio::SoundLocator>(cfg);
    if (!locator_->Initialize()) {
        std::cerr << "[doa] SoundLocator initialize failed" << std::endl;
        locator_.reset();
        return false;
    }

    initialized_ = true;
    has_angle_ = false;
    latest_angle_degrees_ = 90.0f;
    latest_confidence_ = 0.0f;
    result_count_ = 0;

    std::cout << "[doa] init: sample_rate=" << config_.sample_rate
            << ", mic_distance_m=" << config_.mic_distance_m
            << ", frame_size=" << config_.frame_size
            << ", avg_frames=" << config_.avg_frames
            << ", threshold=" << config_.confidence_threshold
            << ", flip_angle=" << (config_.flip_angle ? "true" : "false")
            << std::endl;

    return true;
}

void DoaRuntime::Reset()
{
    if (locator_) {
        locator_->Reset();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    has_angle_ = false;
    latest_angle_degrees_ = 90.0f;
    latest_confidence_ = 0.0f;
    result_count_ = 0;
}

bool DoaRuntime::ProcessInterleavedPcm16(const int16_t* interleaved,
                                        size_t frames,
                                        int channels)
{
    if (!initialized_ || !locator_ || !interleaved || frames == 0 || channels < 2) {
        return false;
    }

    const int16_t* stereo = interleaved;
    if (channels != 2) {
        stereo_buffer_.resize(frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            stereo_buffer_[i * 2] = interleaved[i * static_cast<size_t>(channels)];
            stereo_buffer_[i * 2 + 1] = interleaved[i * static_cast<size_t>(channels) + 1];
        }
        stereo = stereo_buffer_.data();
    }

    if (!locator_->Process(stereo, frames)) {
        return false;
    }

    const bool valid = locator_->IsValid();
    const float raw_angle = locator_->GetDOA();
    const float angle = config_.flip_angle ? 180.0f - raw_angle : raw_angle;

    std::lock_guard<std::mutex> lock(mutex_);
    latest_angle_degrees_ = std::max(0.0f, std::min(180.0f, angle));
    latest_confidence_ = locator_->GetConfidence();
    has_angle_ = valid;
    result_count_ = locator_->GetResultCount();
    return true;
}

bool DoaRuntime::ProcessInterleavedFloat(const float* interleaved,
                                        size_t frames,
                                        int channels)
{
    if (!initialized_ || !locator_ || !interleaved || frames == 0 || channels < 2) {
        return false;
    }

    const float* stereo = interleaved;
    std::vector<float> selected;
    if (channels != 2) {
        selected.resize(frames * 2);
        for (size_t i = 0; i < frames; ++i) {
            selected[i * 2] = interleaved[i * static_cast<size_t>(channels)];
            selected[i * 2 + 1] = interleaved[i * static_cast<size_t>(channels) + 1];
        }
        stereo = selected.data();
    }

    if (!locator_->Process(stereo, frames)) {
        return false;
    }

    const bool valid = locator_->IsValid();
    const float raw_angle = locator_->GetDOA();
    const float angle = config_.flip_angle ? 180.0f - raw_angle : raw_angle;

    std::lock_guard<std::mutex> lock(mutex_);
    latest_angle_degrees_ = std::max(0.0f, std::min(180.0f, angle));
    latest_confidence_ = locator_->GetConfidence();
    has_angle_ = valid;
    result_count_ = locator_->GetResultCount();
    return true;
}

bool DoaRuntime::HasAngle() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && has_angle_;
}

float DoaRuntime::LatestAngleDegrees() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_angle_degrees_;
}

float DoaRuntime::LatestConfidence() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_confidence_;
}

int DoaRuntime::ResultCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return result_count_;
}

}  // namespace ai_cubpet
