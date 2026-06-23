#ifndef CUBPET_UI_DDS_SUBSCRIBER_HPP
#define CUBPET_UI_DDS_SUBSCRIBER_HPP

#include "ToyCommand.hpp"

#include <QObject>
#include <QString>
#include <atomic>
#include <dds/dds.hpp>
#include <string>

class CubpetUiDdsSubscriber : public QObject
{
    Q_OBJECT

public:
    explicit CubpetUiDdsSubscriber(const std::string& topic_name = "ToyCommand_Msg");
    ~CubpetUiDdsSubscriber() override = default;

    void stop();

public slots:
    void run();

signals:
    void shortTouchReceived(float value);
    void longTouchReceived(float value);
    void throwReceived();
    void awakeModeReceived();
    void sleepModeReceived();
    void customMediaReceived(QString audio_file, QString gif_file);
    void leftFootShortTouchReceived(float value);
    void leftFootLongTouchReceived(float value);
    void rightFootShortTouchReceived(float value);
    void rightFootLongTouchReceived(float value);
    void noseShortTouchReceived(float value);
    void noseLongTouchReceived(float value);

private:
    dds::domain::DomainParticipant participant_;
    dds::topic::Topic<ToyCommand::Msg> topic_;
    dds::sub::Subscriber subscriber_;
    dds::sub::DataReader<ToyCommand::Msg> reader_;
    dds::sub::cond::ReadCondition read_condition_;
    dds::core::cond::WaitSet waitset_;
    std::atomic<bool> running_{true};
};

#endif  // CUBPET_UI_DDS_SUBSCRIBER_HPP
