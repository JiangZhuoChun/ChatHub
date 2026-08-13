#include "protocol/frame_decoder.h"
#include "protocol/chat_payload.h"
#include <iostream>

// ==================== 模块：帧解码场景测试 ====================
// 功能：验证半包分两次到达时，解码器只在正文完整后回调一条聊天消息。
bool testEmptyChatFrame() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;

    const auto frame = protocol::makeFrame(protocol::MessageType::chat, "Hello");
    const auto on_message =
        // 功能：记录解码器回调的完整消息，供测试断言数量和类型。
        [&received](const protocol::Message& message) {
            received.push_back(message);
        };
    const auto first_result = decoder.append(frame.data(), 3, on_message);
    const bool first_passed = first_result == protocol::DecodeResult::ok && received.empty();
    const auto second_result =
        decoder.append(frame.data() + 3, frame.size() - 3, on_message);
    return first_passed && second_result == protocol::DecodeResult::ok &&
           received.size() == 1 && received[0].type == protocol::MessageType::chat;
}

// 功能：验证同一次读取包含两帧时，解码器可以依次回调两条聊天消息。
bool testStickyFrames() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;
    const auto first = protocol::makeFrame(protocol::MessageType::chat, "Hello");
    const auto second = protocol::makeFrame(protocol::MessageType::chat, "World");
    const std::string frame = first + second;
    const auto on_message =
        // 功能：记录粘包解析出的每条完整消息，供测试验证顺序和正文。
        [&received](const protocol::Message& message) {
            received.push_back(message);
        };
    const auto result = decoder.append(frame.data(), frame.size(), on_message);
    return result == protocol::DecodeResult::ok && received.size() == 2 &&
           received[0].type == protocol::MessageType::chat &&
           received[1].type == protocol::MessageType::chat &&
           received[0].body == "Hello" && received[1].body == "World";
}

// 功能：验证帧魔数被篡改后，解码器返回 invalid_magic 且不交付消息。
bool testInvalidMagic() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;
    auto frame = protocol::makeFrame(protocol::MessageType::chat, "Hello");
    frame[0] = '\x00';
    const auto result = decoder.append(
        frame.data(), frame.size(),
        // 功能：记录意外解码出的消息，确保非法魔数场景下回调不会发生。
        [&received](const protocol::Message& message) {
            received.push_back(message);
        });
    return result == protocol::DecodeResult::invalid_magic && received.empty();
}

// 功能：验证正文长度超过协议上限时，解码器返回 message_too_large 且不交付消息。
bool testMaxBodyLength() {
    protocol::FrameDecoder decoder;
    std::vector<protocol::Message> received;
    const std::string body(2049, 'x');
    const auto frame = protocol::makeFrame(protocol::MessageType::chat, body);
    const auto result = decoder.append(
        frame.data(), frame.size(),
        // 功能：记录意外解码出的消息，确保超长正文场景下回调不会发生。
        [&received](const protocol::Message& message) {
            received.push_back(message);
        });
    return result == protocol::DecodeResult::message_too_large && received.empty();
}

// 功能：验证不同类型的收据载荷解析结果。
bool testDeliverReceiptPayload() {
    const auto invalid_json =
    protocol::parseDeliveryReceiptPayload(R"({"message_id":})");
    const auto missing_id =
        protocol::parseDeliveryReceiptPayload(R"({})");
    const auto numeric_id =
        protocol::parseDeliveryReceiptPayload(R"({"message_id":123})");
    const auto blank_id =
        protocol::parseDeliveryReceiptPayload(R"({"message_id":" \t\n"})");
    const std::string too_long_body =
        "{\"message_id\":\"" + std::string(65, 'x') + "\"}";
    const auto too_long_id =
        protocol::parseDeliveryReceiptPayload(too_long_body);

    const auto result =
    protocol::parseDeliveryReceiptPayload(R"({"message_id":"message-1"})");

    return result.error ==protocol::DeliveryReceiptPayloadError::none &&
           result.message_id == "message-1" &&
           invalid_json.error == protocol::DeliveryReceiptPayloadError::invalid_json &&
           missing_id.error == protocol::DeliveryReceiptPayloadError::missing_message_id &&
           numeric_id.error == protocol::DeliveryReceiptPayloadError::message_id_not_string &&
           blank_id.error == protocol::DeliveryReceiptPayloadError::blank_message_id &&
           too_long_id.error == protocol::DeliveryReceiptPayloadError::message_id_too_long;
}

// ==================== 模块：测试结果汇总 ====================
// 功能：输出单个测试用例的通过或失败结果，并返回其布尔状态。
bool runTest(const char* name, const bool passed) {
    if (passed) {
        std::cout << "PASS: " << name << '\n';
        return true;
    }

    std::cerr << "FAIL: " << name << '\n';
    return false;
}

// 功能：依次执行全部帧解码场景，并聚合最终测试结果。
bool testAll() {
    bool all_pass = true;
    if (!runTest("empty chat frame", testEmptyChatFrame())) {
        all_pass = false;
    }
    if (!runTest("sticky frames", testStickyFrames())) {
        all_pass = false;
    }
    if (!runTest("invalid magic", testInvalidMagic())) {
        all_pass = false;
    }
    if (!runTest("max body length", testMaxBodyLength())) {
        all_pass = false;
    }
    if (!runTest("delivery receipt payload", testDeliverReceiptPayload())) {
        all_pass = false;
    }
    return all_pass;
}

// ==================== 模块：帧解码测试入口 ====================
// 功能：执行全部帧解码测试，并按结果返回进程成功或失败状态。
int main() {
    if (const bool all_passed = testAll()) {
        std::cout << "PASS: split chat frame\n";
        return EXIT_SUCCESS;
    }

    std::cerr << "FAIL: split chat frame\n";
    return EXIT_FAILURE;
}
