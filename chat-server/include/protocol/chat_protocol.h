#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol {

// ==================== 模块：聊天帧公共定义 ====================
// 功能：集中定义客户端和服务端都必须遵守的帧类型与固定帧头常量。
enum class MessageType : std::uint8_t {
    chat = 1,
    ping = 2,
    pong = 3,
    error = 4,
    auth = 5,
    chat_ack = 6,
};

// 功能：标识 ChatHub 协议帧，接收端据此拒绝非本协议字节流。
inline constexpr std::uint16_t kFrameMagic = 0x4348;
// 功能：标识当前帧格式版本，用于拒绝不兼容的协议帧。
inline constexpr std::uint8_t kProtocolVersion = 1;
// 功能：定义固定帧头的字节数，用于定位正文长度字段和正文起始位置。
inline constexpr std::size_t kFrameHeaderLength = 8;
// 功能：限制单帧正文最大字节数，防止异常数据无限占用内存。
inline constexpr std::size_t kMaxFrameBodyLength = 1024;

// 功能：判断帧头中的原始 type 值是否属于当前协议支持的类型范围。
constexpr bool isKnownMessageType(const std::uint8_t raw_type) {
    return raw_type >= static_cast<std::uint8_t>(MessageType::chat) &&
           raw_type <= static_cast<std::uint8_t>(MessageType::chat_ack);
}

} // protocol 命名空间结束
