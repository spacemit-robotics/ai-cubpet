#ifndef CUBPET_UI_PUBLISHER_HPP
#define CUBPET_UI_PUBLISHER_HPP

#include "cubpet_keywords.hpp"

#include <memory>
#include <string>

namespace ai_cubpet {

class CubpetUiPublisher {
public:
    explicit CubpetUiPublisher(std::string topic_name = "ToyCommand_Msg");
    ~CubpetUiPublisher();

    CubpetUiPublisher(const CubpetUiPublisher&) = delete;
    CubpetUiPublisher& operator=(const CubpetUiPublisher&) = delete;

    bool Initialize();
    bool PublishIntent(VoiceIntent intent);
    bool PublishGif(const std::string& gif_path);
    bool PublishMedia(const std::string& audio_path, const std::string& gif_path);
    bool initialized() const { return initialized_; }

private:
    struct Impl;

    std::string topic_name_;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

}  // namespace ai_cubpet

#endif  // CUBPET_UI_PUBLISHER_HPP
