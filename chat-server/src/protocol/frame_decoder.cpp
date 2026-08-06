#include "protocol/frame_decoder.h"

namespace {
    //功能::读2字节大端序：把 data[0],data[1] 拼成 uint16_t
    std::uint16_t readUint16BigEndian(const char* data) {
        const auto high = static_cast<std::uint16_t>(static_cast<unsigned char>(data[0]));
        const auto low = static_cast<std::uint16_t>(static_cast<unsigned char>(data[1]));
        return (high << 8) | low;
    }
    //功能::读4字节大端序：把 data[0..3] 拼成 uint32_t
    std::uint32_t readUint32BigEndian(const char* data) {
        const auto byte0 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[0]));
        const auto byte1 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[1]));
        const auto byte2 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[2]));
        const auto byte3 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[3]));
        return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }

    //功能::校验消息类型：raw_type 是否在已知类型范围内
    bool isKnownMessageType(std::uint8_t raw_type) {
        return raw_type >= static_cast<std::uint8_t>(protocol::MessageType::chat) &&
            raw_type <= static_cast<std::uint8_t>(protocol::MessageType::chat_ack);
    }
}

namespace protocol {
    //功能::解码：把本次收到的字节块追加进缓存，解析出所有完整帧
    DecodeResult FrameDecoder::append(const char* data, std::size_t size, const MessageHandler &on_message) {
        m_cache.insert(m_cache.end(),data,data+size);   //功能::新数据追加进缓存（跨多次读取拼包）

        while (m_cache.size() >= kHeaderLength)   //功能::缓存够读出一个头（8字节）才继续
        {
            //功能::读协议头各字段
            const auto magic = readUint16BigEndian(m_cache.data());   //功能::魔数 0x4348
            const auto version = static_cast<std::uint8_t>(static_cast<unsigned char> (m_cache[2]));   //功能::版本
            const auto raw_type = static_cast<std::uint8_t>(static_cast<unsigned char> (m_cache[3]));   //功能::消息类型
            const auto body_length = readUint32BigEndian(m_cache.data() + 4);   //功能::body长度

            //功能::协议校验：任一非法则清空缓存并返回对应错误
            if (magic != kMagic) { m_cache.clear();
                return DecodeResult::invalid_magic;   //功能::魔数不对
            }
            if (version != kVersion) { m_cache.clear();
                return DecodeResult::unsupported_version;   //功能::版本不支持
            }
            if (!isKnownMessageType(raw_type)) { m_cache.clear();
                return DecodeResult::unknown_message_type;   //功能::类型未知
            }
            if (body_length > kMaxBodyLength) { m_cache.clear();
                return DecodeResult::message_too_large;   //功能::body超长
            }

            const auto frame_length = kHeaderLength + body_length;   //功能::完整帧长度=头+body
            if (m_cache.size() < frame_length) {
                //功能::body还没收齐，等待下一次append
                return DecodeResult::ok;
            }

            //功能::取出完整消息：跳过8字节头，取 body_length 字节
            Message message{
                static_cast<MessageType> (raw_type),
                std::string (m_cache.data() + kHeaderLength, body_length)
            };
            //功能::一个完整帧处理完成后，从缓存中删除已消费部分
            m_cache.erase(m_cache.begin(), m_cache.begin() + frame_length);
            on_message(message);   //功能::回调交给上层处理
        }
        return DecodeResult::ok;
    }
//功能::编码：把 type+body 打包成完整帧 [magic][version][type][length][body]
std::string makeFrame(MessageType type, const std::string_view body) {
        const auto body_length = static_cast<std::uint32_t>(body.size());   //功能::body长度
        std::string frame (FrameDecoder::kHeaderLength , '\0');   //功能::先造8字节头
        //功能::写魔数（大端 2 字节）
        frame[0] = static_cast<char>((FrameDecoder::kMagic >> 8) & 0xFFU);
        frame[1] = static_cast<char>(FrameDecoder::kMagic & 0xFFU);
        //功能::写版本
        frame[2] = FrameDecoder::kVersion;
        //功能::写消息类型
        frame[3] = static_cast<char>(type);
        //功能::写 body 长度（大端 4 字节）
        frame[4] = static_cast<char>((body_length >> 24U) & 0xFFU);
        frame[5] = static_cast<char>((body_length >> 16U) & 0xFFU);
        frame[6] = static_cast<char>((body_length >> 8U) & 0xFFU);
        frame[7] = static_cast<char>(body_length & 0xFFU);
        frame.append(body);   //功能::末尾拼上 body
        return frame;
    }

}
