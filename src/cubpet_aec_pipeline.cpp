#include "cubpet_aec_pipeline.hpp"

#include "audio_base.hpp"

#include <webrtc/modules/audio_processing/include/audio_processing.h>

#include <algorithm>
#include <chrono>  // NOLINT(build/c++11)
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

namespace ai_cubpet {
namespace {

int ClampChannelIndex(int one_based_channel, int channels)
{
    return std::clamp(one_based_channel - 1, 0, std::max(0, channels - 1));
}

float Int16ToFloat(int16_t sample)
{
    return static_cast<float>(sample) / 32768.0f;
}

webrtc::AudioProcessing::Config::NoiseSuppression::Level NsLevelFromInt(int level)
{
    using Level = webrtc::AudioProcessing::Config::NoiseSuppression::Level;
    switch (level) {
    case 1:
        return Level::kModerate;
    case 2:
        return Level::kHigh;
    case 3:
        return Level::kVeryHigh;
    case 0:
    default:
        return Level::kLow;
    }
}

const char* NsLevelName(int level)
{
    switch (level) {
    case 1:
        return "moderate";
    case 2:
        return "high";
    case 3:
        return "very-high";
    case 0:
    default:
        return "low";
    }
}

}  // namespace

CubpetAecPipeline::CubpetAecPipeline(AecPipelineConfig config)
    : config_(config)
{
    if (config_.frames_per_buffer <= 0) {
        config_.frames_per_buffer = std::max(1, config_.sample_rate / 100);
    }
    if (config_.queue_chunks <= 0) {
        config_.queue_chunks = 2;
    }
    if (config_.reference_history_ms <= 0) {
        config_.reference_history_ms = 1000;
    }
    config_.speech_channel = std::max(1, config_.speech_channel);
    config_.reference_channel = std::max(1, config_.reference_channel);
}

CubpetAecPipeline::~CubpetAecPipeline()
{
    Close();
}

bool CubpetAecPipeline::Initialize()
{
    if (config_.sample_rate <= 0) {
        std::cerr << "[aec] invalid sample_rate: " << config_.sample_rate << std::endl;
        return false;
    }
    if (config_.capture_channels < 2) {
        std::cerr << "[aec] capture_channels must be >= 2 for DOA" << std::endl;
        return false;
    }
    if (config_.reference_channels <= 0) {
        std::cerr << "[aec] invalid reference_channels: " << config_.reference_channels
                << std::endl;
        return false;
    }
    if (config_.speech_channel > config_.capture_channels) {
        std::cerr << "[aec] speech_channel out of range: " << config_.speech_channel
                << " not in [1, " << config_.capture_channels << "]" << std::endl;
        return false;
    }
    if (config_.reference_channel > config_.reference_channels) {
        std::cerr << "[aec] reference_channel out of range: " << config_.reference_channel
                << " not in [1, " << config_.reference_channels << "]" << std::endl;
        return false;
    }

    const int expected_frames = std::max(1, config_.sample_rate / 100);
    if (config_.frames_per_buffer != expected_frames) {
        std::cerr << "[aec] warning: WebRTC APM expects 10 ms frames; requested "
                << config_.frames_per_buffer << ", expected " << expected_frames
                << " at " << config_.sample_rate << " Hz" << std::endl;
    }

    reference_max_samples_ = static_cast<size_t>(
        config_.sample_rate * config_.reference_history_ms / 1000);
    reference_max_samples_ = std::max(reference_max_samples_,
        static_cast<size_t>(config_.frames_per_buffer * 4));
    reference_delay_samples_ = static_cast<size_t>(
        std::max(0, config_.aec_delay_ms) * config_.sample_rate / 1000);
    reference_delay_samples_ = std::min(reference_delay_samples_,
        reference_max_samples_ - static_cast<size_t>(config_.frames_per_buffer));

    ConfigureApm();
    if (!apm_) {
        return false;
    }

    mic_capture_ = std::make_unique<SpacemitAudio::AudioCapture>(config_.input_device);
    reference_capture_ =
        std::make_unique<SpacemitAudio::AudioCapture>(config_.reference_device);

    initialized_ = true;
    std::cout << "[aec] init: mic_device=" << config_.input_device
            << ", mic=" << config_.sample_rate << "Hz/"
            << config_.capture_channels << "ch"
            << ", ref_device=" << config_.reference_device
            << ", ref=" << config_.sample_rate << "Hz/"
            << config_.reference_channels << "ch"
            << ", frame=" << config_.frames_per_buffer
            << ", delay_ms=" << config_.aec_delay_ms
            << std::endl;
    std::cout << "[aec] WebRTC: aec=" << (config_.echo_canceller ? "on" : "off")
            << ", ns=" << (config_.noise_suppression ? NsLevelName(config_.noise_suppression_level) : "off")
            << ", hpf=" << (config_.high_pass_filter ? "on" : "off")
            << ", agc=" << (config_.gain_control ? "on" : "off")
            << std::endl;
    return true;
}

void CubpetAecPipeline::SetCallback(ProcessedCallback callback)
{
    callback_ = std::move(callback);
}

bool CubpetAecPipeline::Start()
{
    if (!initialized_ || !mic_capture_ || !reference_capture_) {
        std::cerr << "[aec] not initialized" << std::endl;
        return false;
    }
    if (!callback_) {
        std::cerr << "[aec] processed callback is not set" << std::endl;
        return false;
    }
    if (running_.load()) {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(mic_mutex_);
        mic_stopped_ = false;
        mic_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(reference_mutex_);
        reference_queue_.clear();
    }

    mic_capture_->SetCallback([this](const uint8_t* data, size_t size) {
        OnMicData(data, size);
    });
    reference_capture_->SetCallback([this](const uint8_t* data, size_t size) {
        OnReferenceData(data, size);
    });

    const int mic_chunk_bytes = config_.frames_per_buffer *
        config_.capture_channels * static_cast<int>(sizeof(int16_t));
    const int ref_chunk_bytes = config_.frames_per_buffer *
        config_.reference_channels * static_cast<int>(sizeof(int16_t));

    processing_running_ = true;
    processing_thread_ = std::thread(&CubpetAecPipeline::ProcessingLoop, this);

    if (!reference_capture_->Start(config_.sample_rate, config_.reference_channels,
            ref_chunk_bytes)) {
        std::cerr << "[aec] failed to start reference capture" << std::endl;
        Stop();
        return false;
    }
    if (!mic_capture_->Start(config_.sample_rate, config_.capture_channels,
            mic_chunk_bytes)) {
        std::cerr << "[aec] failed to start mic capture" << std::endl;
        Stop();
        return false;
    }

    running_ = true;
    std::cout << "[aec] capture started" << std::endl;
    return true;
}

void CubpetAecPipeline::Stop()
{
    if (!running_.load() && !processing_running_.load()) {
        return;
    }

    running_ = false;
    if (mic_capture_) {
        mic_capture_->Stop();
    }
    if (reference_capture_) {
        reference_capture_->Stop();
    }

    processing_running_ = false;
    {
        std::lock_guard<std::mutex> lock(mic_mutex_);
        mic_stopped_ = true;
    }
    mic_cv_.notify_all();
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mic_mutex_);
        mic_queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(reference_mutex_);
        reference_queue_.clear();
    }

    std::cout << "[aec] capture stopped" << std::endl;
}

void CubpetAecPipeline::Close()
{
    Stop();
    if (mic_capture_) {
        mic_capture_->Close();
        mic_capture_.reset();
    }
    if (reference_capture_) {
        reference_capture_->Close();
        reference_capture_.reset();
    }
    ReleaseApm();
    initialized_ = false;
}

AecPipelineStats CubpetAecPipeline::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

std::vector<std::pair<int, std::string>> CubpetAecPipeline::ListInputDevices()
{
    return SpacemitAudio::AudioCapture::ListDevices();
}

void CubpetAecPipeline::OnMicData(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || !processing_running_.load()) {
        return;
    }

    const size_t sample_count = size / sizeof(int16_t);
    const size_t frames = sample_count / static_cast<size_t>(config_.capture_channels);
    if (frames == 0) {
        return;
    }

    MicChunk chunk;
    chunk.frames = frames;
    chunk.samples.resize(frames * static_cast<size_t>(config_.capture_channels));
    std::memcpy(chunk.samples.data(), data, chunk.samples.size() * sizeof(int16_t));

    {
        std::lock_guard<std::mutex> lock(mic_mutex_);
        if (mic_queue_.size() >= static_cast<size_t>(config_.queue_chunks)) {
            mic_queue_.pop_front();
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.mic_drops++;
        }
        mic_queue_.push_back(std::move(chunk));
    }
    mic_cv_.notify_one();
}

void CubpetAecPipeline::OnReferenceData(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || !processing_running_.load()) {
        return;
    }

    const size_t sample_count = size / sizeof(int16_t);
    const size_t frames = sample_count / static_cast<size_t>(config_.reference_channels);
    if (frames == 0) {
        return;
    }

    const int ref_idx = ClampChannelIndex(config_.reference_channel, config_.reference_channels);
    const int16_t* pcm = reinterpret_cast<const int16_t*>(data);

    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(reference_mutex_);
        for (size_t i = 0; i < frames; ++i) {
            reference_queue_.push_back(
                pcm[i * static_cast<size_t>(config_.reference_channels) + ref_idx]);
        }
        while (reference_queue_.size() > reference_max_samples_) {
            reference_queue_.pop_front();
            dropped++;
        }
    }

    if (dropped > 0) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.reference_drops += dropped;
    }
}

bool CubpetAecPipeline::WaitMicChunk(MicChunk* chunk)
{
    std::unique_lock<std::mutex> lock(mic_mutex_);
    mic_cv_.wait(lock, [this]() {
        return mic_stopped_ || !mic_queue_.empty() || !processing_running_.load();
    });
    if (mic_queue_.empty()) {
        return false;
    }
    *chunk = std::move(mic_queue_.front());
    mic_queue_.pop_front();
    return true;
}

std::vector<int16_t> CubpetAecPipeline::TakeReferenceFrame(size_t frames)
{
    std::vector<int16_t> reference(frames, 0);
    const size_t target = reference_delay_samples_ + frames;
    size_t dropped = 0;
    bool underrun = false;

    {
        std::lock_guard<std::mutex> lock(reference_mutex_);
        while (reference_queue_.size() > target) {
            reference_queue_.pop_front();
            dropped++;
        }
        if (reference_queue_.size() >= target) {
            for (size_t i = 0; i < frames; ++i) {
                reference[i] = reference_queue_.front();
                reference_queue_.pop_front();
            }
        } else {
            underrun = true;
        }
    }

    if (dropped > 0 || underrun) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.reference_drops += dropped;
        if (underrun) {
            stats_.reference_underruns++;
        }
    }

    return reference;
}

void CubpetAecPipeline::ProcessingLoop()
{
    std::vector<int16_t> mic_mono;
    std::vector<int16_t> processed_int16;
    std::vector<float> processed_float;

    while (processing_running_.load()) {
        MicChunk chunk;
        if (!WaitMicChunk(&chunk)) {
            continue;
        }
        if (chunk.samples.empty() || chunk.frames == 0) {
            continue;
        }

        const auto t0 = std::chrono::steady_clock::now();
        const int speech_idx = ClampChannelIndex(config_.speech_channel, config_.capture_channels);
        mic_mono.resize(chunk.frames);
        for (size_t i = 0; i < chunk.frames; ++i) {
            mic_mono[i] =
                chunk.samples[i * static_cast<size_t>(config_.capture_channels) + speech_idx];
        }

        auto reference = TakeReferenceFrame(chunk.frames);
        processed_int16 = mic_mono;

        bool ok = true;
        if (apm_) {
            webrtc::StreamConfig stream_config(config_.sample_rate, 1);
            apm_->set_stream_delay_ms(std::max(0, config_.aec_delay_ms));
            int reverse_ret = apm_->ProcessReverseStream(reference.data(), stream_config,
                stream_config, reference.data());
            int stream_ret = apm_->ProcessStream(processed_int16.data(), stream_config,
                stream_config, processed_int16.data());
            ok = reverse_ret == 0 && stream_ret == 0;
        }

        processed_float.resize(processed_int16.size());
        for (size_t i = 0; i < processed_int16.size(); ++i) {
            processed_float[i] = Int16ToFloat(processed_int16[i]);
        }

        const auto t1 = std::chrono::steady_clock::now();
        const double process_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.processed_frames++;
            stats_.max_process_ms = std::max(stats_.max_process_ms, process_ms);
            if (!ok) {
                stats_.aec_errors++;
            }
        }

        if (callback_) {
            AecAudioFrame frame;
            frame.raw_interleaved = std::move(chunk.samples);
            frame.processed_mono = processed_float;
            frame.frames = chunk.frames;
            frame.channels = config_.capture_channels;
            callback_(std::move(frame));
        }
    }
}

void CubpetAecPipeline::ConfigureApm()
{
    ReleaseApm();

    rtc::scoped_refptr<webrtc::AudioProcessing> apm_ref =
        webrtc::AudioProcessingBuilder().Create();
    if (!apm_ref) {
        std::cerr << "[aec] failed to create WebRTC AudioProcessing" << std::endl;
        return;
    }

    apm_ = apm_ref.get();
    apm_->AddRef();

    webrtc::AudioProcessing::Config apm_config;
    apm_config.echo_canceller.enabled = config_.echo_canceller;
    apm_config.echo_canceller.mobile_mode = false;
    apm_config.high_pass_filter.enabled = config_.high_pass_filter;
    apm_config.noise_suppression.enabled = config_.noise_suppression;
    apm_config.noise_suppression.level =
        NsLevelFromInt(config_.noise_suppression_level);
    apm_config.gain_controller1.enabled = false;
    apm_config.gain_controller2.enabled = false;
    if (config_.gain_control) {
        apm_config.gain_controller2.enabled = true;
        apm_config.gain_controller2.fixed_digital.gain_db = 0.0f;
    }
    apm_->ApplyConfig(apm_config);
}

void CubpetAecPipeline::ReleaseApm()
{
    if (apm_) {
        apm_->Release();
        apm_ = nullptr;
    }
}

}  // namespace ai_cubpet
