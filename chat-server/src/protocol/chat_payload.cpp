#include <protocol/chat_payload.h>
#include <boost/json/src.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cctype>
namespace {
    //遍历整个字符串，只要全部字符都是空格 / 制表 / 换行等空白，函数返回 true，用来校验输入框空值
    bool isBlank(const std::string_view text) {
        return std::all_of(text.begin(), text.end(), [](const unsigned char c) {
            return std::isspace(c);
        });
    }

    //功能::协议常量
    constexpr std::size_t kMaxChatContentBytes = 1024;
    constexpr std::size_t kMaxLocalIdLength = 64;
}
namespace protocol {
    chatPayloadResult parseChatPayload(const std::string_view& body) {
        boost::system::error_code error;
        // 解析JSON
        const auto value = boost::json::parse(
            boost::json::string_view(body.data(),body.size()),error);

        if (error || !value.is_object()) {
            return {ChatPayloadError::invalid_json,{}};
        }
        //身份由server认证后的session决定，不允许用户自己设置
        const auto& obj =value.as_object();
        if (obj.if_contains("sender_id")) {
            return {ChatPayloadError::forbidden_sender_id,{}};
        }

        // 校验接收者：Server 后续会用 to 查找在线 Session。
        const auto* to = obj.if_contains("to");
        if (!to) {
            return {ChatPayloadError::missing_recipient,{}};
        }
        if (!to->is_string()) {
            return {ChatPayloadError::recipient_not_string, {}};
        }
        const auto& json_to = to->as_string();
        const std::string to_str(json_to.data(),json_to.size());
        if (isBlank(to_str)) {
            return {ChatPayloadError::blank_recipient, {}};
        }

        // 校验客户端生成的消息关联 ID；它用于错误响应和重试定位。
        const auto* local_id = obj.if_contains("local_id");
        if (!local_id) {
            return {ChatPayloadError::missing_local_id,{}};
        }
        if (!local_id->is_string()) {
            return {ChatPayloadError::local_id_not_string,{}};
        }
        if (isBlank(local_id->as_string())) {
            return {ChatPayloadError::blank_local_id,{}};
        }
        if (local_id->as_string().size() > kMaxLocalIdLength) {
            return {ChatPayloadError::local_id_too_long,{}};
        }
        const auto& json_local_id = local_id->as_string();
        const std::string local_id_str(json_local_id.data(),json_local_id.size());

        // 校验聊天正文；长度使用 std::string::size()，单位是 UTF-8 字节数。
        const auto* content = obj.if_contains("content");
        if (!content) {
            return {ChatPayloadError::missing_content,{}};
        }
        if (!content->is_string()) {
            return {ChatPayloadError::content_not_string,{}};
        }
        // 校验内容
        const auto& json_text = content->as_string();
        const std::string text(json_text.data(),json_text.size());
        if (isBlank(text)){
            return {ChatPayloadError::blank_content,{}};
        }
        if (text.size() > kMaxChatContentBytes) {
            return {ChatPayloadError::content_too_long,{}};
        }
        return {ChatPayloadError::none,text,local_id_str};
    }

}
