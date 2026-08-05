#include <protocol/chat_payload.h>
#include <boost/json/src.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cctype>
#include <utility>

namespace {
    //遍历整个字符串，只要全部字符都是空格 / 制表 / 换行等空白，函数返回 true，用来校验输入框空值
    bool isBlank(std::string_view text) {
        return std::all_of(text.begin(), text.end(), [](char c) {
            return std::isspace(c);
        });
    }
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
        // 获取内容
        const auto* content = obj.if_contains("content");
        if (!content) {
            return {ChatPayloadError::missing_content,{}};
        }
        if (!content->is_string()) {
            return {ChatPayloadError::content_not_string,{}};
        }
        // 校验内容
        const auto& json_text = content->as_string();
        std::string text(json_text.data(),json_text.size());
        if (isBlank(text)){
            return {ChatPayloadError::blank_content,{}};
        }
        if (text.size() > kMaxChatContentBytes) {
            return {ChatPayloadError::content_too_long,{}};
        }
        return {ChatPayloadError::none};
    }

}