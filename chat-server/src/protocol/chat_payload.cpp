#include <protocol/chat_payload.h>

#include <boost/json/src.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cctype>

namespace {

// ==================== 模块：正文校验限制与工具 ====================
constexpr std::size_t kMaxChatContentBytes = 1024;
constexpr std::size_t kMaxLocalIdLength = 64;
constexpr std::size_t kMaxSendAtLength = 64;

// 功能：判断文本是否全部由空格、制表符、换行等空白字符组成。
bool isBlank(const std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       // 功能：判断单个字符是否为空白字符。
                       [](const unsigned char character) {
        return std::isspace(character);
    });
}

} // 匿名命名空间结束

namespace protocol {

// ==================== 模块：聊天 JSON 正文校验 ====================
// 功能：解析聊天 JSON 并逐项校验接收者、消息、消息标识和发送时间。
// 失败：任一字段不符合协议要求时返回对应错误，不向调用方提供可路由的聊天正文。
chatPayloadResult parseChatPayload(const std::string_view body) {
    boost::system::error_code error;
    const auto value = boost::json::parse(
        boost::json::string_view(body.data(), body.size()), error);

    if (error || !value.is_object()) {
        return {ChatPayloadError::invalid_json, {}};
    }

    const auto& object = value.as_object();
    if (object.if_contains("sender_id")) {
        return {ChatPayloadError::forbidden_sender_id, {}};
    }

    const auto* to = object.if_contains("to");
    if (!to) {
        return {ChatPayloadError::missing_recipient, {}};
    }
    if (!to->is_string()) {
        return {ChatPayloadError::recipient_not_string, {}};
    }
    const auto& json_to = to->as_string();
    const std::string to_str(json_to.data(), json_to.size());
    if (isBlank(to_str)) {
        return {ChatPayloadError::blank_recipient, {}};
    }

    const auto* local_id = object.if_contains("local_id");
    if (!local_id) {
        return {ChatPayloadError::missing_local_id, {}};
    }
    if (!local_id->is_string()) {
        return {ChatPayloadError::local_id_not_string, {}};
    }
    const auto& json_local_id = local_id->as_string();
    const std::string local_id_str(json_local_id.data(), json_local_id.size());
    if (isBlank(local_id_str)) {
        return {ChatPayloadError::blank_local_id, {}};
    }
    if (local_id_str.size() > kMaxLocalIdLength) {
        return {ChatPayloadError::local_id_too_long, {}};
    }

    const auto* send_at = object.if_contains("send_at");
    if (!send_at) {
        return {ChatPayloadError::missing_send_at, {}, {}, local_id_str};
    }
    if (!send_at->is_string()) {
        return {ChatPayloadError::send_at_not_string, {}, {}, local_id_str};
    }
    const auto& json_send_at = send_at->as_string();
    const std::string send_at_str(json_send_at.data(), json_send_at.size());
    if (isBlank(send_at_str)) {
        return {ChatPayloadError::blank_send_at, {}, {}, local_id_str};
    }
    if (send_at_str.size() > kMaxSendAtLength) {
        return {ChatPayloadError::send_at_too_long, {}, {}, local_id_str};
    }

    const auto* content = object.if_contains("content");
    if (!content) {
        return {ChatPayloadError::missing_content, {}};
    }
    if (!content->is_string()) {
        return {ChatPayloadError::content_not_string, {}};
    }
    const auto& json_content = content->as_string();
    const std::string content_str(json_content.data(), json_content.size());
    if (isBlank(content_str)) {
        return {ChatPayloadError::blank_content, {}};
    }
    if (content_str.size() > kMaxChatContentBytes) {
        return {ChatPayloadError::content_too_long, {}};
    }

    return {ChatPayloadError::none,
            to_str,
            content_str,
            local_id_str,
            send_at_str};
}


DeliveryReceiptPayloadResult parseDeliveryReceiptPayload(const std::string_view body)
{
    boost::system::error_code error;
    const auto value = boost::json::parse(
        boost::json::string_view(body.data(), body.size()), error);

    if (error || !value.is_object()) {
        return {DeliveryReceiptPayloadError::invalid_json, {}};
    }
    const auto* local_id = value.as_object().if_contains("local_id");
    if (!local_id) {
        return {DeliveryReceiptPayloadError::missing_local_id, {}};
    }
    if (!local_id->is_string()) {
        return {DeliveryReceiptPayloadError::local_id_not_string, {}};
    }
    const auto& json_local_id = local_id->as_string();
    const std::string local_id_str(json_local_id.data(), json_local_id.size());
    if (isBlank(local_id_str)) {
        return {DeliveryReceiptPayloadError::blank_local_id, {}};
    }
    if (local_id_str.size() > kMaxLocalIdLength) {
        return {DeliveryReceiptPayloadError::local_id_too_long, {}};
    }
    return {DeliveryReceiptPayloadError::none, local_id_str};
}

} // protocol 命名空间结束
