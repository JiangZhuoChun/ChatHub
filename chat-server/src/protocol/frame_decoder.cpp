#include "protocol/frame_decoder.h"

namespace {
    std::uint16_t readUint16BigEndian(const char* data) {
        const auto high = static_cast<std::uint16_t>(static_cast<unsigned char>(data[0]));
        const auto low = static_cast<std::uint16_t>(static_cast<unsigned char>(data[1]));
        return (high << 8) | low;
    }
    std::uint32_t readUint32BigEndian(const char* data) {
        const auto byte0 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[0]));
        const auto byte1 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[1]));
        const auto byte2 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[2]));
        const auto byte3 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[3]));
        return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }

    bool isKnownMessageType(std::uint8_t raw_type) {
        return raw_type >= static_cast<std::uint8_t>(protocol::MessageType::chat) &&
            raw_type <= static_cast<std::uint8_t>(protocol::MessageType::error);
    }
}

namespace protocol {
    DecodeResult FrameDecoder::append(const char* data, std::size_t size, const MessageHandler &on_message) {
        m_cache.insert(m_cache.end(),data,data+size);

        while (m_cache.size() >= kHeaderLength)
        {
            const auto magic = readUint16BigEndian(m_cache.data());
            const auto version = static_cast<std::uint8_t>(static_cast<unsigned char> (m_cache[2]));
            const auto raw_type = static_cast<std::uint8_t>(static_cast<unsigned char> (m_cache[3]));
            const auto body_length = readUint32BigEndian(m_cache.data() + 4);

            if (magic != kMagic) { m_cache.clear();
                return DecodeResult::invalid_magic;
            }
            if (version != kVersion) { m_cache.clear();
                return DecodeResult::unsupported_version;
            }
            if (!isKnownMessageType(raw_type)) { m_cache.clear();
                return DecodeResult::unknown_message_type;
            }
            if (body_length > kMaxBodyLength) { m_cache.clear();
                return DecodeResult::message_too_large;
            }

            const auto frame_length = kHeaderLength + body_length;
            if (m_cache.size() < frame_length) {
                //body还没收齐，等待下一次append
                return DecodeResult::ok;
            }

            Message message{
                static_cast<MessageType> (raw_type),
                std::string (m_cache.data() + kHeaderLength, body_length)
            };
            //一个完整帧处理完成后，从缓存中删除
            m_cache.erase(m_cache.begin(), m_cache.begin() + frame_length);
            on_message(message);

        }
        return DecodeResult::ok;
    }
std::string makeFrame(MessageType type, const std::string_view body) {
        const auto body_length = static_cast<std::uint32_t>(body.size());
        std::string frame (FrameDecoder::kHeaderLength , '\0');
        //magic
        frame[0] = static_cast<char>((FrameDecoder::kMagic >> 8) & 0xFFU);
        frame[1] = static_cast<char>(FrameDecoder::kMagic & 0xFFU);
        // version
        frame[2] = FrameDecoder::kVersion;
        // type
        frame[3] = static_cast<char>(type);
        // body length
        frame[4] = static_cast<char>((body_length >> 24U) & 0xFFU);
        frame[5] = static_cast<char>((body_length >> 16U) & 0xFFU);
        frame[6] = static_cast<char>((body_length >> 8U) & 0xFFU);
        frame[7] = static_cast<char>(body_length & 0xFFU);
        frame.append(body);
        return frame;
    }

}