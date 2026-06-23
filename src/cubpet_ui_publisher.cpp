#include "cubpet_ui_publisher.hpp"

#include "ToyCommand.hpp"

#include <dds/dds.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace ai_cubpet {
namespace {

long long NowMilliseconds()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

uint32_t DdsDomainId()
{
    const char* env = std::getenv("AI_CUBPET_DDS_DOMAIN_ID");
    if (!env || env[0] == '\0') {
        return 0;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(env, &end, 10);
    if (end == env || *end != '\0' || value > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "[UI] invalid AI_CUBPET_DDS_DOMAIN_ID=" << env
                  << ", using domain 0" << std::endl;
        return 0;
    }
    return static_cast<uint32_t>(value);
}

const char* VoiceIntentGifFileName(VoiceIntent intent)
{
    switch (intent) {
    case VoiceIntent::kHeadUp:
        return "08_explore.gif";
    case VoiceIntent::kNodHead:
        return "02_expect.gif";
    case VoiceIntent::kShakeHead:
        return "03_diz.gif";
    case VoiceIntent::kWagTail:
        return "05_heart.gif";
    case VoiceIntent::kUnknown:
    default:
        return "";
    }
}

const char* VoiceIntentAudioFileName(VoiceIntent intent)
{
    switch (intent) {
    case VoiceIntent::kHeadUp:
        return "009_greet_move.wav";
    case VoiceIntent::kNodHead:
        return "008_happy.wav";
    case VoiceIntent::kShakeHead:
        return "013_shake_head.wav";
    case VoiceIntent::kWagTail:
        return "012_heart.wav";
    case VoiceIntent::kUnknown:
    default:
        return "";
    }
}

}  // namespace

struct CubpetUiPublisher::Impl {
    dds::domain::DomainParticipant participant;
    dds::topic::Topic<ToyCommand::Msg> topic;
    dds::pub::Publisher publisher;
    dds::pub::DataWriter<ToyCommand::Msg> writer;

    explicit Impl(const std::string& topic_name)
        : participant(DdsDomainId())
        , topic(participant, topic_name)
        , publisher(participant)
        , writer(publisher, topic)
    {
    }
};

CubpetUiPublisher::CubpetUiPublisher(std::string topic_name)
    : topic_name_(std::move(topic_name))
{
}

CubpetUiPublisher::~CubpetUiPublisher() = default;

bool CubpetUiPublisher::Initialize()
{
    if (initialized_) {
        return true;
    }
    try {
        impl_ = std::make_unique<Impl>(topic_name_);
        initialized_ = true;
        std::cout << "[UI] DDS publisher ready: " << topic_name_ << std::endl;
        return true;
    } catch (const dds::core::Exception& e) {
        std::cerr << "[UI] DDS publisher init failed: " << e.what() << std::endl;
        impl_.reset();
        initialized_ = false;
        return false;
    }
}

bool CubpetUiPublisher::PublishIntent(VoiceIntent intent)
{
    const std::string gif_path = VoiceIntentGifPath(intent);
    const std::string audio_path = VoiceIntentAudioPath(intent);
    if (gif_path.empty() && audio_path.empty()) {
        return false;
    }
    return PublishMedia(audio_path, gif_path);
}

bool CubpetUiPublisher::PublishGif(const std::string& gif_path)
{
    return PublishMedia("", gif_path);
}

bool CubpetUiPublisher::PublishMedia(const std::string& audio_path, const std::string& gif_path)
{
    if (!initialized_ || !impl_) {
        return false;
    }
    try {
        ToyCommand::Msg msg;
        msg.command(::ToyCommand::CommandType::CUSTOM_MEDIA);
        msg.touchValue(0.0f);
        msg.audioFile(audio_path);
        msg.gifFile(gif_path);
        msg.timestamp(NowMilliseconds());
        impl_->writer.write(msg);
        std::cout << "[UI] published audio=" << audio_path
                  << " gif=" << gif_path << std::endl;
        return true;
    } catch (const dds::core::Exception& e) {
        std::cerr << "[UI] DDS publish failed: " << e.what() << std::endl;
        return false;
    }
}

std::string VoiceIntentGifPath(VoiceIntent intent)
{
    const char* file_name = VoiceIntentGifFileName(intent);
    if (!file_name || file_name[0] == '\0') {
        return "";
    }
    return file_name;
}

std::string VoiceIntentAudioPath(VoiceIntent intent)
{
    const char* file_name = VoiceIntentAudioFileName(intent);
    if (!file_name || file_name[0] == '\0') {
        return "";
    }
    return file_name;
}

}  // namespace ai_cubpet
