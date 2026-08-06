#pragma once
#include <string>
#include <string_view>
namespace protocol
{
// 业务层校验失败的原因，由 Session 转换成仅发给发送者的错误消息。
enum class ChatPayloadError {
    none,
    invalid_json,// json 解析失败
//to
    missing_recipient,// recipient 字段不存在
    recipient_not_string,// recipient 字段不是字符串
    blank_recipient,// recipient 字段为空
//content
    missing_content,// content 字段不存在
    content_not_string,// content 字段不是字符串
    blank_content,// content 字段为空
    content_too_long,// content 字段过长
//from
    forbidden_sender_id,// 发送者 id 不允许
//消息id
    missing_local_id,// local_id 字段不存在
    local_id_not_string,// local_id 字段不是字符串
    blank_local_id,// local_id 字段为空
    local_id_too_long,// local_id 字段过长
//发送时间
    missing_send_at,
    send_at_not_string,
    blank_send_at,
    send_at_too_long,
};

    // chatPayloadResult 只保存聊天业务层已经通过校验的字段。
    // error 不为 none 时，to/content/local_id 仅用于错误关联，不应继续路由。
    struct chatPayloadResult {
        //只有error为none,才保存已校验的聊天正文
        ChatPayloadError error{ChatPayloadError::none};
        std::string to;
        std::string content;
        std::string local_id;
        std::string send_at;
    };

    //校验chat帧的JSON正文及业务字段
    chatPayloadResult parseChatPayload(const std::string_view& body);
}
