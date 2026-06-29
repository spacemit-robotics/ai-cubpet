#include "cubpet_ui_dds_subscriber.hpp"

#include <QThread>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

uint32_t DdsDomainId()
{
    const char* env = std::getenv("AI_CUBPET_DDS_DOMAIN_ID");
    if (!env || env[0] == '\0') {
        return 0;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(env, &end, 10);
    if (end == env || *end != '\0' || value > std::numeric_limits<uint32_t>::max()) {
        std::cerr << "DDS: invalid AI_CUBPET_DDS_DOMAIN_ID=" << env
                << ", using domain 0" << std::endl;
        return 0;
    }
    return static_cast<uint32_t>(value);
}

}  // namespace

CubpetUiDdsSubscriber::CubpetUiDdsSubscriber(const std::string& topic_name)
    : participant_(DdsDomainId())
    , topic_(participant_, topic_name)
    , subscriber_(participant_)
    , reader_(subscriber_, topic_)
    , read_condition_(reader_, dds::sub::status::DataState::any())
{
    waitset_ += read_condition_;
    std::cout << "DDS subscriber ready: " << topic_name << std::endl;
}

void CubpetUiDdsSubscriber::stop()
{
    running_ = false;
}

void CubpetUiDdsSubscriber::run()
{
    while (running_) {
        try {
            const dds::core::cond::WaitSet::ConditionSeq active_conditions =
                waitset_.wait(dds::core::Duration::from_millisecs(100));
            if (active_conditions.empty()) {
                continue;
            }

            const dds::sub::LoanedSamples<ToyCommand::Msg> samples = reader_.take();
            for (const auto& sample : samples) {
                if (!sample.info().valid()) {
                    continue;
                }

                const ::ToyCommand::CommandType command = sample.data().command();
                const float touch_value = sample.data().touchValue();
                switch (command) {
                case ::ToyCommand::CommandType::SHORT_TOUCH:
                    std::cout << "DDS: SHORT_TOUCH value=" << touch_value << std::endl;
                    emit shortTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::LONG_TOUCH:
                    std::cout << "DDS: LONG_TOUCH value=" << touch_value << std::endl;
                    emit longTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::THROW_TOY:
                    std::cout << "DDS: THROW_TOY" << std::endl;
                    emit throwReceived();
                    break;
                case ::ToyCommand::CommandType::AWAKE_MODE:
                    std::cout << "DDS: AWAKE_MODE" << std::endl;
                    emit awakeModeReceived();
                    break;
                case ::ToyCommand::CommandType::SLEEP_MODE:
                    std::cout << "DDS: SLEEP_MODE" << std::endl;
                    emit sleepModeReceived();
                    break;
                case ::ToyCommand::CommandType::CUSTOM_MEDIA: {
                    const std::string audio_file = sample.data().audioFile();
                    const std::string gif_file = sample.data().gifFile();
                    std::cout << "DDS: CUSTOM_MEDIA audio=" << audio_file
                            << " gif=" << gif_file << std::endl;
                    emit customMediaReceived(QString::fromStdString(audio_file),
                                            QString::fromStdString(gif_file));
                    break;
                }
                case ::ToyCommand::CommandType::LEFT_FOOT_SHORT_TOUCH:
                    emit leftFootShortTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::LEFT_FOOT_LONG_TOUCH:
                    emit leftFootLongTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::RIGHT_FOOT_SHORT_TOUCH:
                    emit rightFootShortTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::RIGHT_FOOT_LONG_TOUCH:
                    emit rightFootLongTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::NOSE_SHORT_TOUCH:
                    emit noseShortTouchReceived(touch_value);
                    break;
                case ::ToyCommand::CommandType::NOSE_LONG_TOUCH:
                    emit noseLongTouchReceived(touch_value);
                    break;
                default:
                    std::cout << "DDS: unknown command=" << static_cast<int>(command) << std::endl;
                    break;
                }
            }
        } catch (const dds::core::TimeoutError&) {
            continue;
        } catch (const dds::core::Exception& e) {
            std::cerr << "DDS subscriber error: " << e.what() << std::endl;
            break;
        }

        QThread::msleep(10);
    }
}
