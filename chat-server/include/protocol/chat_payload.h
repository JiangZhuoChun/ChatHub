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


// ==================== 模块：送达回执正文校验结果 ====================
// 功能：枚举 delivery_receipt 正文中 local_id 的校验失败原因。
enum class DeliveryReceiptPayloadError {
    none,
    invalid_json,
    missing_local_id,
    local_id_not_string,
    blank_local_id,
    local_id_too_long
 };

// 功能：保存送达回执解析后的错误码或可安全查询的 local_id。
struct DeliveryReceiptPayloadResult {
    // 功能：表示正文校验结果；只有 none 时 local_id 可用于查询待送达记录。
    DeliveryReceiptPayloadError error{DeliveryReceiptPayloadError::none};
    // 功能：保存已校验、非空且长度合规的客户端本地消息标识。
    std::string local_id;
};

// 功能：解析并校验 delivery_receipt JSON 正文中的 local_id。
// 失败：JSON、字段类型、空白或长度不合法时返回对应错误码。
DeliveryReceiptPayloadResult parseDeliveryReceiptPayload(std::string_view body);
} // protocol 命名空间结束
