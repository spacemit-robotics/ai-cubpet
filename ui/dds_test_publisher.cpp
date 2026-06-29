#include "ToyCommand.hpp"

#include <dds/dds.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

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
        std::cerr << "invalid AI_CUBPET_DDS_DOMAIN_ID=" << env
                << ", using domain 0" << std::endl;
        return 0;
    }
    return static_cast<uint32_t>(value);
}

long long NowMilliseconds()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

void PrintUsage(const char* argv0)
{
    std::cout << "Usage: " << argv0
            << " [--topic ToyCommand_Msg] [--audio FILE] [--gif FILE]"
            << " [--repeat N] [--delay-ms N]\n";
}

}  // namespace

int main(int argc, char** argv)
{
    std::string topic_name = "ToyCommand_Msg";
    std::string audio_file;
    std::string gif_file;
    int repeat = 3;
    int delay_ms = 300;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << std::endl;
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--topic") {
            topic_name = next_value("--topic");
        } else if (arg == "--audio") {
            audio_file = next_value("--audio");
        } else if (arg == "--gif") {
            gif_file = next_value("--gif");
        } else if (arg == "--repeat") {
            repeat = std::stoi(next_value("--repeat"));
        } else if (arg == "--delay-ms") {
            delay_ms = std::stoi(next_value("--delay-ms"));
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 2;
        }
    }

    if (repeat < 1) {
        repeat = 1;
    }
    if (delay_ms < 0) {
        delay_ms = 0;
    }

    try {
        dds::domain::DomainParticipant participant(DdsDomainId());
        dds::topic::Topic<ToyCommand::Msg> topic(participant, topic_name);
        dds::pub::Publisher publisher(participant);
        dds::pub::DataWriter<ToyCommand::Msg> writer(publisher, topic);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        ToyCommand::Msg msg;
        msg.command(::ToyCommand::CommandType::CUSTOM_MEDIA);
        msg.touchValue(0.0f);
        msg.audioFile(audio_file);
        msg.gifFile(gif_file);

        for (int i = 0; i < repeat; ++i) {
            msg.timestamp(NowMilliseconds());
            writer.write(msg);
            std::cout << "published CUSTOM_MEDIA audio=" << audio_file
                    << " gif=" << gif_file
                    << " topic=" << topic_name << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    } catch (const dds::core::Exception& e) {
        std::cerr << "DDS publish failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
