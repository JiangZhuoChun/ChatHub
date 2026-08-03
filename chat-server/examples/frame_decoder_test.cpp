#include <iostream>

#include "protocol/frame_decoder.h"


// //测试1.半包处理	makeFrame(chat,"Hello") 的前 3 字节先 append，剩余再 append
bool testEmptyChatFrame() {

    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;

    const auto frame = protocol::makeFrame(protocol::MessageType::chat,"Hello");
    const auto on_message = [&received](const protocol::Message &message) {
        received.push_back(message);
    };
    const auto first_result = decoder.append(frame.data(),3,on_message);
    const bool first_passed = first_result
        == protocol::DecodeResult::ok&& received.empty();
    // 第二次补齐剩余字节后，回调应收到完整聊天消息。
    const auto second_result = decoder.append(frame.data()+3,frame.size()-3,on_message);
    const bool passed = first_passed
    && second_result == protocol::DecodeResult::ok
    && received.size() == 1
    && received[0].type == protocol::MessageType::chat;
    return passed;
}
//2	粘包处理	两个 makeFrame 拼接成一段 append
bool testStickyFrames() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;
    const auto first = protocol::makeFrame(protocol::MessageType::chat,"Hello");
    const auto second = protocol::makeFrame(protocol::MessageType::chat,"World");
    std::string frame = first + second;
    const auto on_message = [&received](const protocol::Message &message) {
        received.push_back(message);
    };
    const auto result =decoder.append(frame.data(),frame.size(),on_message);
    bool passed = result == protocol::DecodeResult::ok
    && received.size() == 2
    && received[0].type == protocol::MessageType::chat
    && received[1].type == protocol::MessageType::chat
    && received[0].body == "Hello"
    && received[1].body == "World";
    return passed;
}
//3	非法 magic	makeFrame(chat,"Hi") 改第 1 字节为 \x00
bool testInvalidMagic() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;
    auto frame = protocol::makeFrame(protocol::MessageType::chat,"Hello");
    frame[0] = '\x00';
    const auto result = decoder.append(frame.data(),frame.size(),[&received](const protocol::Message &message) {
        received.push_back(message);
    });
    bool passed = result == protocol::DecodeResult::invalid_magic
    && received.empty();
    return passed;
}
//4	超长 body	makeFrame(chat, string(1025,'x'))
bool testMaxBodyLength() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;
    std::string body = std::string(1025,'x');
    auto frame = protocol::makeFrame(protocol::MessageType::chat,body);
    const auto result = decoder.append(frame.data(),frame.size(),[&received](const protocol::Message &message) {
        received.push_back(message);
    });
    bool passed = result == protocol::DecodeResult::message_too_large && received.empty();
    return passed;
}
bool runTest(const char* name, bool passed) {
    if (passed) {
        std::cout << "PASS: " << name << '\n';
        return true;
    }

    std::cerr << "FAIL: " << name << '\n';
    return false;
}
bool testAll() {
    bool all_pass = true;
    if (!runTest("empty chat frame",testEmptyChatFrame())) {
        all_pass = false;
    }
    if (!runTest("sticky frames",testStickyFrames())) {
        all_pass = false;
    }
    if (!runTest("invalid magic",testInvalidMagic())) {
        all_pass = false;
    }
    if (!runTest("max body length",testMaxBodyLength())) {
        all_pass = false;
    }
    return  all_pass;
}
int main() {
    if (bool all_passed =testAll())
    {
        std::cout << "PASS: split chat frame\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "FAIL: split chat frame\n";
    return EXIT_FAILURE;
}
