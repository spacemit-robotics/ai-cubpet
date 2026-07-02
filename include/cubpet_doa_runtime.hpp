#ifndef CUBPET_DOA_RUNTIME_HPP
#define CUBPET_DOA_RUNTIME_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>  // NOLINT(build/c++11)
#include <vector>

namespace SpacemitAudio {
class SoundLocator;
}

namespace ai_cubpet {

struct DoaRuntimeConfig {
    int sample_rate = 16000;
    float mic_distance_m = 0.058f;
    float confidence_threshold = 0.1f;
    int frame_size = 512;
    int avg_frames = 4;
    bool flip_angle = false;
};

class DoaRuntime {
public:
    explicit DoaRuntime(DoaRuntimeConfig config);
    ~DoaRuntime();

    DoaRuntime(const DoaRuntime&) = delete;
    DoaRuntime& operator=(const DoaRuntime&) = delete;

    bool Initialize();
    void Reset();

    bool ProcessInterleavedPcm16(const int16_t* interleaved,
                                size_t frames,
                                int channels);
    bool ProcessInterleavedFloat(const float* interleaved,
                                size_t frames,
                                int channels);

    bool HasAngle() const;
    float LatestAngleDegrees() const;
    float LatestConfidence() const;
    int ResultCount() const;

private:
    DoaRuntimeConfig config_;
    std::unique_ptr<SpacemitAudio::SoundLocator> locator_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool has_angle_ = false;
    float latest_angle_degrees_ = 90.0f;
    float latest_confidence_ = 0.0f;
    int result_count_ = 0;
    std::vector<int16_t> stereo_buffer_;
};

}  // namespace ai_cubpet

#endif  // CUBPET_DOA_RUNTIME_HPP
