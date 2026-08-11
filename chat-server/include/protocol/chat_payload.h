#pragma once

#include <string>
#include <string_view>


namespace protocol {

// ==================== 模块：聊天正文校验错误类型 ====================
// 功能：枚举聊天 JSON 正文在字段存在性、类型和内容上的校验失败原因。
enum class ChatPayloadError {
    none,
    invalid_json,

    missing_recipient,
    recipient_not_string,
    blank_recipient,

    missing_content,
    content_not_string,
    blank_content,
    content_too_long,

    forbidden_sender_id,

    missing_local_id,
    local_id_not_string,
    blank_local_id,
    local_id_too_long,

    missing_send_at,
    send_at_not_string,
    blank_send_at,
    send_at_too_long,
};

// ==================== 模块：聊天正文校验结果 ====================
// 功能：保存校验成功的聊天字段，或保存失败原因及可用于关联错误的 local_id。
struct chatPayloadResult {
    // 功能：标识本次正文校验是否成功；只有值为 none 时其他业务字段才可路由。
    ChatPayloadError error{ChatPayloadError::none};
    // 功能：保存已校验的聊天接收者用户名。
    std::string to;
    // 功能：保存已校验的聊天文本内容。
    std::string content;
    // 功能：保存用于关联确认、失败与重试的客户端本地消息标识。
    std::string local_id;
    // 功能：保存客户端提交的 ISO 格式发送时间。
    std::string send_at;
};
// ==================== 模块：聊天 JSON 正文校验 ====================
// 功能：解析并校验聊天帧 JSON 正文，返回可安全用于服务端路由的字段。
// 失败：字段缺失、类型错误、空白、超长或客户端伪造 sender_id 时返回相应错误。
chatPayloadResult parseChatPayload(std::string_view body);


enum class DeliveryReceiptPayloadError {
    none,
    invalid_json,
    missing_local_id,
    local_id_not_string,
    blank_local_id,
    local_id_too_long
 };

struct  DeliveryReceiptPayloadResult {
    DeliveryReceiptPayloadError error{DeliveryReceiptPayloadError::none};
    std::string local_id;
};

DeliveryReceiptPayloadResult parseDeliveryReceiptPayload(std::string_view body);
} // protocol 命名空间结束
