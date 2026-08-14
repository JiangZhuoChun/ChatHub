#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace protocol {

// ==================== Chat JSON payload validation ====================
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

// Successful results contain only fields validated for server routing.
struct ChatPayloadResult {
    ChatPayloadError error{ChatPayloadError::none};
    std::string to;
    std::string content;
    std::string local_id;
    std::string send_at;
};

ChatPayloadResult parseChatPayload(std::string_view body);

// ================= Delivery receipt JSON payload validation =================
enum class DeliveryReceiptPayloadError {
    none,
    invalid_json,
    missing_message_id,
    message_id_not_string,
    blank_message_id,
    message_id_too_long,
};

struct DeliveryReceiptPayloadResult {
    DeliveryReceiptPayloadError error{DeliveryReceiptPayloadError::none};
    std::string message_id;
};

DeliveryReceiptPayloadResult parseDeliveryReceiptPayload(std::string_view body);

// ================== History query JSON payload validation ==================
enum class HistoryQueryPayloadError {
    none,
    invalid_json,
    forbidden_identity_field,

    missing_request_id,
    request_id_not_string,
    blank_request_id,
    request_id_too_long,

    missing_limit,
    limit_not_integer,

    before_not_object,
    missing_before_timestamp,
    before_timestamp_not_integer,
    negative_before_timestamp,
    missing_before_message_id,
    before_message_id_not_string,
    blank_before_message_id,
    before_message_id_too_long,
};

// A validated protocol cursor. It is mapped to the Repository cursor later.
struct HistoryQueryCursor {
    std::int64_t server_received_at_ms;
    std::string message_id;
};

struct HistoryQueryPayloadResult {
    HistoryQueryPayloadError error{HistoryQueryPayloadError::none};
    std::string request_id;
    int limit{0};
    std::optional<HistoryQueryCursor> before{std::nullopt};
};

HistoryQueryPayloadResult parseHistoryQueryPayload(std::string_view body);

} // namespace protocol
