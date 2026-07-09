#include "cubpet_keywords.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using ai_cubpet::MatchProductCommand;
using ai_cubpet::MatchVoiceIntent;
using ai_cubpet::ProductCommand;
using ai_cubpet::ProductCommandName;
using ai_cubpet::VoiceIntent;

void TestVoiceIntentsStillWork()
{
    assert(MatchVoiceIntent("请点点头") == VoiceIntent::kNodHead);
    assert(MatchVoiceIntent("摇摇头") == VoiceIntent::kShakeHead);
    assert(MatchVoiceIntent("抬一下头") == VoiceIntent::kHeadUp);
    assert(MatchVoiceIntent("摇尾巴") == VoiceIntent::kWagTail);
}

void TestProductCommands()
{
    assert(MatchProductCommand("开始配网") == ProductCommand::kStartProvisioning);
    assert(MatchProductCommand("连接 Wi-Fi") == ProductCommand::kStartProvisioning);
    assert(MatchProductCommand("进入配网模式") == ProductCommand::kStartProvisioning);
    assert(MatchProductCommand("退出配网") == ProductCommand::kExitProvisioning);
    assert(MatchProductCommand("进入连续对话") ==
        ProductCommand::kEnterContinuousConversation);
    assert(MatchProductCommand("连续对话") ==
        ProductCommand::kEnterContinuousConversation);
    assert(MatchProductCommand("陪我聊天") ==
        ProductCommand::kEnterContinuousConversation);
    assert(MatchProductCommand("退出聊天") ==
        ProductCommand::kExitContinuousConversation);
    assert(MatchProductCommand("退出连续对话") ==
        ProductCommand::kExitContinuousConversation);
    assert(MatchProductCommand("休息一下") ==
        ProductCommand::kExitContinuousConversation);
    assert(MatchProductCommand("去睡觉") == ProductCommand::kSleep);
    assert(MatchProductCommand("小安小安") == ProductCommand::kUnknown);
    assert(MatchProductCommand("小宠小宠") == ProductCommand::kUnknown);
    assert(MatchProductCommand("随便说点什么") == ProductCommand::kUnknown);
}

void TestProductCommandNames()
{
    assert(std::string(ProductCommandName(ProductCommand::kStartProvisioning)) ==
        "start_provisioning");
    assert(std::string(ProductCommandName(ProductCommand::kExitProvisioning)) ==
        "exit_provisioning");
    assert(std::string(ProductCommandName(ProductCommand::kEnterContinuousConversation)) ==
        "enter_continuous_conversation");
    assert(std::string(ProductCommandName(ProductCommand::kExitContinuousConversation)) ==
        "exit_continuous_conversation");
    assert(std::string(ProductCommandName(ProductCommand::kSleep)) == "sleep");
    assert(std::string(ProductCommandName(ProductCommand::kWake)) == "wake");
}

}  // namespace

int main()
{
    TestVoiceIntentsStillWork();
    TestProductCommands();
    TestProductCommandNames();
    std::cout << "keyword tests passed\n";
    return 0;
}
