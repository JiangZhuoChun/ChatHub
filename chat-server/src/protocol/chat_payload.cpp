#include "protocol/chat_payload.h"

#include <boost/json/src.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cctype>
#include <limits>

namespace {

constexpr std::size_t kMaxChatContentBytes = 1024;
constexpr std::size_t kMaxLocalIdLength = 64;
constexpr std::size_t kMaxDeliveryReceiptMessageIdLength = 64;
constexpr std::size_t kMaxSendAtLength = 64;
constexpr std::size_t kMaxHistoryRequestIdLength = 64;
constexpr std::size_t kMaxHistoryCursorMessageIdLength = 64;
constexpr std::int64_t kMinHistoryLimit = 1;
constexpr std::int64_t kMaxHistoryLimit = 50;

bool isBlank(const std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](const unsigned char character) {
        return std::isspace(character);
    });
}

bool tryNormalizeHistoryLimit(const boost::json::value& value, int& out_limit)
{
    if (value.is_int64()) {
        const auto requested = value.as_int64();
        out_limit = static_cast<int>(std::clamp(
            requested, kMinHistoryLimit, kMaxHistoryLimit));
        return true;
    }

    if (value.is_uint64()) {
        const auto requested = value.as_uint64();
        if (requested < static_cast<std::uint64_t>(kMinHistoryLimit)) {
            out_limit = static_cast<int>(kMinHistoryLimit);
        } else if (requested > static_cast<std::uint64_t>(kMaxHistoryLimit)) {
            out_limit = static_cast<int>(kMaxHistoryLimit);
        } else {
            out_limit = static_cast<int>(requested);
        }
        return true;
    }

    return false;
}

} // namespace

namespace protocol {

// ==================== 聊天 JSON 正文校验 ====================
ChatPayloadResult parseChatPayload(const std::string_view body)
{
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

// ==================== 送达回执 JSON 正文校验 ====================
DeliveryReceiptPayloadResult parseDeliveryReceiptPayload(const std::string_view body)
{
    boost::system::error_code error;
    const auto value = boost::json::parse(
        boost::json::string_view(body.data(), body.size()), error);

    if (error || !value.is_object()) {
        return {DeliveryReceiptPayloadError::invalid_json, {}};
    }

    const auto* message_id = value.as_object().if_contains("message_id");
    if (!message_id) {
        return {DeliveryReceiptPayloadError::missing_message_id, {}};
    }
    if (!message_id->is_string()) {
        return {DeliveryReceiptPayloadError::message_id_not_string, {}};
    }
    const auto& json_message_id = message_id->as_string();
    const std::string message_id_str(json_message_id.data(), json_message_id.size());
    if (isBlank(message_id_str)) {
        return {DeliveryReceiptPayloadError::blank_message_id, {}};
    }
    if (message_id_str.size() > kMaxDeliveryReceiptMessageIdLength) {
        return {DeliveryReceiptPayloadError::message_id_too_long, {}};
    }

    return {DeliveryReceiptPayloadError::none, message_id_str};
}

// ==================== 历史查询 JSON 正文校验 ====================
HistoryQueryPayloadResult parseHistoryQueryPayload(const std::string_view body)
{
    boost::system::error_code error;
    const auto value = boost::json::parse(
        boost::json::string_view(body.data(), body.size()), error);

    if (error || !value.is_object()) {
        return {HistoryQueryPayloadError::invalid_json};
    }

    const auto& object = value.as_object();
    if (object.if_contains("sender") || object.if_contains("recipient") ||
        object.if_contains("username")) {
        return {HistoryQueryPayloadError::forbidden_identity_field};
    }

    const auto* request_id = object.if_contains("request_id");
    if (!request_id) {
        return {HistoryQueryPayloadError::missing_request_id};
    }
    if (!request_id->is_string()) {
        return {HistoryQueryPayloadError::request_id_not_string};
    }
    const auto& json_request_id = request_id->as_string();
    const std::string request_id_str(json_request_id.data(), json_request_id.size());
    if (isBlank(request_id_str)) {
        return {HistoryQueryPayloadError::blank_request_id};
    }
    if (request_id_str.size() > kMaxHistoryRequestIdLength) {
        return {HistoryQueryPayloadError::request_id_too_long};
    }

    const auto* limit = object.if_contains("limit");
    if (!limit) {
        return {HistoryQueryPayloadError::missing_limit};
    }
    int effective_limit = 0;
    if (!tryNormalizeHistoryLimit(*limit, effective_limit)) {
        return {HistoryQueryPayloadError::limit_not_integer};
    }

    std::optional<HistoryQueryCursor> before_cursor = std::nullopt;
    const auto* before_value = object.if_contains("before");
    if (before_value) {
        if (!before_value->is_object()) {
            return {HistoryQueryPayloadError::before_not_object};
        }

        const auto& before_object = before_value->as_object();
        const auto* timestamp = before_object.if_contains("server_received_at_ms");
        if (!timestamp) {
            return {HistoryQueryPayloadError::missing_before_timestamp};
        }

        std::int64_t before_timestamp{};
        if (timestamp->is_int64()) {
            if (timestamp->as_int64() < 0) {
                return {HistoryQueryPayloadError::negative_before_timestamp};
            }
            before_timestamp = timestamp->as_int64();
        } else if (timestamp->is_uint64()) {
            if (timestamp->as_uint64() >
                (std::numeric_limits<std::int64_t>::max)()) {
                return {HistoryQueryPayloadError::before_timestamp_not_integer};
            }
            before_timestamp = static_cast<std::int64_t>(timestamp->as_uint64());
        } else {
            return {HistoryQueryPayloadError::before_timestamp_not_integer};
        }

        const auto* message_id = before_object.if_contains("message_id");
        if (!message_id) {
            return {HistoryQueryPayloadError::missing_before_message_id};
        }
        if (!message_id->is_string()) {
            return {HistoryQueryPayloadError::before_message_id_not_string};
        }
        const auto& json_message_id = message_id->as_string();
        const std::string message_id_str(json_message_id.data(), json_message_id.size());
        if (isBlank(message_id_str)) {
            return {HistoryQueryPayloadError::blank_before_message_id};
        }
        if (message_id_str.size() > kMaxHistoryCursorMessageIdLength) {
            return {HistoryQueryPayloadError::before_message_id_too_long};
        }

        before_cursor = HistoryQueryCursor{before_timestamp, message_id_str};
    }

    return {HistoryQueryPayloadError::none,
            request_id_str,
            effective_limit,
            before_cursor};
}

} // namespace protocol
