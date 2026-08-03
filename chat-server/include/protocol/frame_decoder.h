#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace protocol {
    //协议头中的type字段:同一条tcp连接可传递不同信息的类型
    enum class MessageType : std::uint8_t {
        chat = 1,
        ping = 2,
        pong = 3,
        error = 4
    };
    //解码完成后的信息，只包含业务需要的body和type，不包含头网络头
    struct Message {
        MessageType type;
        std::string body;
    };
    //协议错误由解码器返回,Session决定记录日志并直接关闭连接
    enum class DecodeResult {
        ok,
        message_too_large,
        invalid_magic,
        unsupported_version,
        unknown_message_type
    };
    //负责把Tcp字节流还原成包含网络头的＋body的完整协议帧
    class FrameDecoder {
    public:
        static constexpr std::uint16_t kMagic = 0x4348; // "CH"
        static constexpr std::uint8_t kVersion = 1;// 协议版本号
        static constexpr std::size_t kHeaderLength = 8;// 协议头长度（字节）
        static constexpr std::size_t kMaxBodyLength = 1024;// 最大body长度（字节）

        // 命名消息处理函数
        using MessageHandler = std::function<void(const Message&)>;

        //读取函数---data 是本次异步读取到的字节块，可能只有半帧，也可能含有多帧。
        DecodeResult append(const char* data, std::size_t size,const MessageHandler &on_message);

    private:
        //缓存必须多次读取保存，才能拼接tcp半包，直到拼接完整帧
        std::vector<char> m_cache;
    };

    // 将业务消息编码为：[magic:2][version:1][type:1][length:4][body:N]。
    std::string makeFrame(MessageType type, const std::string_view body);

}
