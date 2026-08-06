#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace protocol {

// ==================== 模块：协议消息类型与解码结果 ====================
// 功能：定义帧头 type 字段允许的业务消息类型。
enum class MessageType : std::uint8_t {
    chat = 1,
    ping = 2,
    pong = 3,
    error = 4,
    auth = 5,
    chat_ack = 6
};

// 功能：保存解码完成后的业务消息，不包含网络帧头。
struct Message {
    MessageType type;
    std::string body;
};

// 功能：描述帧解码过程中发现的协议错误。
enum class DecodeResult {
    ok,
    message_too_large,
    invalid_magic,
    unsupported_version,
    unknown_message_type
};

// ==================== 模块：帧解码器接口与缓存 ====================
class FrameDecoder {
public:
    // 功能：定义 ChatHub 帧格式固定使用的魔数、版本、帧头长度和正文上限。
    static constexpr std::uint16_t kMagic = 0x4348;
    static constexpr std::uint8_t kVersion = 1;
    static constexpr std::size_t kHeaderLength = 8;
    static constexpr std::size_t kMaxBodyLength = 1024;

    // 功能：定义每解码出一条完整消息时调用的上层处理回调。
    using MessageHandler = std::function<void(const Message&)>;

    // 功能：追加本次收到的 TCP 字节，并依次回调其中包含的完整协议帧。
    // 失败：帧头非法或正文超长时清空缓存并返回对应错误。
    DecodeResult append(const char* data, std::size_t size, const MessageHandler& on_message);

private:
    // ==================== 模块：半包与粘包缓存 ====================
    // 功能：保存尚未组成完整协议帧的字节，供下一次 append() 继续拼接。
    std::vector<char> m_cache;
};

// ==================== 模块：完整帧编码 ====================
// 功能：将消息类型和正文编码为 [magic][version][type][length][body] 字节流。
std::string makeFrame(MessageType type, std::string_view body);

} // protocol 命名空间结束
