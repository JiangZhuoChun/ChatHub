#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace protocol {
    //功能::协议头中的 type 字段：同一条 TCP 连接可传递不同信息的类型
    enum class MessageType : std::uint8_t {
        chat = 1,   //功能::聊天消息
        ping = 2,   //功能::心跳请求
        pong = 3,   //功能::心跳响应
        error = 4,  //功能::错误消息
        auth = 5,    //功能::认证消息
        chat_ack = 6 // 功能::聊天消息确认
    };
    //功能::解码完成后的消息：只包含业务需要的 type 和 body，不含网络头
    struct Message {
        MessageType type;   //功能::消息类型
        std::string body;   //功能::消息内容
    };
    //功能::协议错误：由解码器返回，Session 决定记录日志并直接关闭连接
    enum class DecodeResult {
        ok,                    //功能::正常
        message_too_large,     //功能::body 超长
        invalid_magic,         //功能::魔数错误
        unsupported_version,   //功能::版本不支持
        unknown_message_type   //功能::消息类型未知
    };
    //功能::FrameDecoder：把 TCP 字节流还原成包含网络头+body 的完整协议帧
    class FrameDecoder {
    public:
        //功能::协议常量
        static constexpr std::uint16_t kMagic = 0x4348;   //功能::魔数 "CH"
        static constexpr std::uint8_t kVersion = 1;       //功能::协议版本号
        static constexpr std::size_t kHeaderLength = 8;   //功能::协议头长度（字节）
        static constexpr std::size_t kMaxBodyLength = 1024;  //功能::最大 body 长度（字节）

        //功能::消息处理回调：解码出完整消息时调用
        using MessageHandler = std::function<void(const Message&)>;

        //功能::解码入口：data 是本次异步读取到的字节块，可能只有半帧，也可能含有多帧
        DecodeResult append(const char* data, std::size_t size,const MessageHandler &on_message);

    private:
        //功能::缓存：必须多次读取保存，才能拼接 TCP 半包，直到拼接完整帧
        std::vector<char> m_cache;
    };

    //功能::编码：将业务消息编码为 [magic:2][version:1][type:1][length:4][body:N]
    std::string makeFrame(MessageType type, std::string_view body);
}
