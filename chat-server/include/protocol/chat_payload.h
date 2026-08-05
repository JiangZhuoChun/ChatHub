#pragma once
#include <string>
#include <string_view>
namespace protocol
{
// 业务层校验失败的原因，由 Session 转换成仅发给发送者的错误消息。
enum class ChatPayloadError {
    none,
    invalid_json,// json 解析失败
    missing_content,// content 字段不存在
    content_not_string,// content 字段不是字符串
    blank_content,// content 字段为空
    forbidden_sender_id,// 发送者 id 不允许
    content_too_long,// content 字段过长
};
    struct chatPayloadResult {
        //只有error为none,才保存已校验的聊天正文
        ChatPayloadError error{ChatPayloadError::none};
        std::string content;
    };

    //校验chat帧的JSON正文及业务字段
    chatPayloadResult parseChatPayload(const std::string_view& body);
    constexpr std::size_t kMaxChatContentBytes = 1024;
}
