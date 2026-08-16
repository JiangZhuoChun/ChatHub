#include "net/session.h"

#include "protocol/chat_payload.h"

#include <boost/json.hpp>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/defaults.h>

#include <iostream>

// ==================== 模块：令牌验证配置 ====================
// 功能：保存当前开发环境用于验证 HS256 令牌的密钥。
const std::string SECRET_KEY = "chathub-dev-secret";

namespace net {

// ==================== 模块：生命周期与对外发送 ====================
// 功能：接管 Socket，创建会话 strand，并保存由 Server 注入的三个回调。
Session::Session(asio::ip::tcp::socket socket, SessionId session_id,
                 MessageCallback on_message, DisconnectCallback on_disconnect,
                 AuthenticatedCallback on_authenticated)
    : m_socket(std::move(socket)),
      m_strand(m_socket.get_executor()),
      m_on_message(std::move(on_message)),
      m_id(session_id),
      m_on_disconnect(std::move(on_disconnect)),
      m_on_authenticated(std::move(on_authenticated)) {
}

// 功能：将第一次读取任务投递到会话 strand，确保异步回调开始前会话对象已被持有。
void Session::start() {
    const auto self = shared_from_this();
    asio::post(m_strand,
               // 功能：在会话串行执行器中启动持续读取流程。
               [self] {
                   self->doRead();
               });
}

// 功能：将发送请求投递到会话 strand，保证写队列只被串行访问。
void Session::send(const protocol::MessageType type, std::string body) {
    const auto self = shared_from_this();
    asio::post(m_strand,
               // 功能：在会话串行执行器中将消息编码后压入写队列。
               [self, type, body = std::move(body)] {
                   self->enqueueAndWrite(type, body);
               });
}

// 功能：将关闭请求投递到会话 strand，避免 Server 线程直接并发修改 Session 状态。
void Session::requestClose() {
    const auto self = shared_from_this();
    asio::post(m_strand, [self] {
        self->closeOnStrand();
    });
}

// ==================== 模块：异步读取与帧解码 ====================
// 功能：异步读取 Socket 数据，解码完整帧并递归安排下一次读取。
// 失败：读取错误或协议错误时关闭会话，触发 Server 的在线表清理回调。
void Session::doRead() {
    const auto self = shared_from_this();
    m_socket.async_read_some(
        asio::buffer(m_read_buffer),
        asio::bind_executor(
            m_strand,
            // 功能：处理本次读取结果、协议解码结果并决定是否继续读取。
            [self, this](const std::error_code error, const std::size_t bytes_transferred) {
                if (error) {
                    if (error == asio::error::eof) {
                        Session::log("正常断开连接");
                    } else if (error == asio::error::operation_aborted) {
                        return;
                    } else {
                        std::cerr << "错误：" << error.message() << std::endl;
                    }
                    self->closeOnStrand();
                    return;
                }

                const auto result = m_decoder.append(
                    m_read_buffer.data(), bytes_transferred,
                    // 功能：将每条完整协议消息交给当前会话的业务分派函数。
                    [self](const protocol::Message& message) {
                        self->handlerMessage(message);
                    });
                if (result != protocol::DecodeResult::ok) {
                    log("协议错误，关闭当前连接");
                    self->closeOnStrand();
                    return;
                }

                self->doRead();
            }));
}

// ==================== 模块：串行写队列 ====================
// 功能：校验正文大小后将完整帧放入队列；空队列首次入队时启动异步写。
void Session::enqueueAndWrite(const protocol::MessageType type, const std::string& body) {
    if (body.size() > protocol::kMaxFrameBodyLength) {
        std::cerr << "错误：body长度超出限制" << std::endl;
        return;
    }

    const bool was_empty = m_write_queue.empty();
    if (m_write_queue.size() >= kMaxWriteQueueSize) {
        std::cout << "错误：发送队列已满：" << m_write_queue.size() << "/"
                  << kMaxWriteQueueSize << "关闭慢客户端" << std::endl;
        closeOnStrand();
        return;
    }

    m_write_queue.push_back({type, protocol::makeFrame(type, body)});
    if (was_empty) {
        writeFrame();
    }
}

// 功能：异步写出队首帧；写入成功后移除队首并继续处理剩余队列。
// 失败：写入失败时关闭会话，避免继续向失效 Socket 发送数据。
void Session::writeFrame() {
    if (m_write_queue.empty()) {
        return;
    }

    const auto self = shared_from_this();
    asio::async_write(
        m_socket, asio::buffer(m_write_queue.front().frame),
        asio::bind_executor(
            m_strand,
            // 功能：处理当前队首帧的写入结果，并按顺序继续下一个帧。
            [self, this](const std::error_code error, const std::size_t bytes_transferred) {
                if (error) {
                    std::cerr << "错误,发送失败" << error.message() << std::endl;
                    self->closeOnStrand();
                    return;
                }

                const auto send_type = m_write_queue.front().type;
                m_write_queue.pop_front();
                std::cout << "发送成功：" << bytes_transferred << "字节" << std::endl;
                self->writeFrame();
            }));
}

// ==================== 模块：认证与业务消息分派 ====================
// 功能：使用服务端密钥验证 HS256 令牌，并从载荷中提取用户名。
// 失败：令牌解码、签名校验或用户名提取抛出异常时返回 false。
bool Session::verifyJwt(const std::string& token, std::string& out_username) {
    try {
        const auto decoded = jwt::decode(token);
        const auto verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{SECRET_KEY});
        verifier.verify(decoded);
        out_username = decoded.get_payload_claim("username").as_string();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// 功能：构造包含聊天错误范围、错误码、错误说明和可选 local_id 的 JSON 正文。
std::string Session::makeChatError(const std::string& local_id, const std::string& code,
                                   const std::string& message) {
    boost::json::object object;
    object["scope"] = "chat";
    object["code"] = code;
    object["message"] = message;
    if (!local_id.empty()) {
        object["local_id"] = local_id;
    }
    return boost::json::serialize(object);
}

// 功能：在未认证时只处理认证帧；认证完成后分派聊天、心跳和错误帧。
// 失败：令牌无效、未认证发送业务帧或聊天正文校验失败时关闭会话或返回错误帧。
void Session::handlerMessage(const protocol::Message& message) {
    if (!m_authenticated)
    {
        if (message.type == protocol::MessageType::auth)
        {
            std::string username;
            if (verifyJwt(message.body, username)) {
                m_authenticated = true;
                m_username = username;
                send(protocol::MessageType::auth, R"({"ok":true})");
                if (m_on_authenticated) {
                    m_on_authenticated(m_id, m_username);
                }
            } else {
                log("认证失败，关闭连接");
                closeOnStrand();
            }
        }

        else if (message.type == protocol::MessageType::history_query)
        {
            send(protocol::MessageType::error,
                makeHistoryError("authentication_required","历史查询需要先完成认证"));
        }
        else {
            send(protocol::MessageType::error,
                makeChatError("", "authentication_required", "未认证"));
            closeOnStrand();
        }
        return;
    }

    switch (message.type) {
    case protocol::MessageType::chat: {
        const auto result = protocol::parseChatPayload(message.body);
        if (result.error != protocol::ChatPayloadError::none) {
            std::string error_message = "聊天消息校验失败";
            std::string error_code = "chat_validation_failed";
            switch (result.error) {
            case protocol::ChatPayloadError::none:
                break;
            case protocol::ChatPayloadError::invalid_json:
                error_message = "聊天 JSON 格式错误";
                error_code = "invalid_json";
                break;
            case protocol::ChatPayloadError::missing_content:
                error_message = "聊天消息缺少 content";
                error_code = "missing_content";
                break;
            case protocol::ChatPayloadError::content_not_string:
                error_message = "content 必须是字符串";
                error_code = "content_not_string";
                break;
            case protocol::ChatPayloadError::blank_content:
                error_message = "聊天内容不能为空";
                error_code = "blank_content";
                break;
            case protocol::ChatPayloadError::forbidden_sender_id:
                error_message = "客户端不能指定 sender_id";
                error_code = "forbidden_sender_id";
                break;
            case protocol::ChatPayloadError::content_too_long:
                error_message = "聊天内容不能超过 1024 字节";
                error_code = "content_too_long";
                break;
            case protocol::ChatPayloadError::missing_local_id:
                error_message = "聊天消息缺少 local_id";
                error_code = "missing_local_id";
                break;
            case protocol::ChatPayloadError::local_id_not_string:
                error_message = "local_id 必须是字符串";
                error_code = "local_id_not_string";
                break;
            case protocol::ChatPayloadError::blank_local_id:
                error_message = "local_id 不能为空";
                error_code = "blank_local_id";
                break;
            case protocol::ChatPayloadError::local_id_too_long:
                error_message = "local_id 不能超过 64 字节";
                error_code = "local_id_too_long";
                break;
            case protocol::ChatPayloadError::missing_recipient:
                error_message = "聊天消息缺少 to";
                error_code = "missing_recipient";
                break;
            case protocol::ChatPayloadError::recipient_not_string:
                error_message = "to 必须是字符串";
                error_code = "recipient_not_string";
                break;
            case protocol::ChatPayloadError::blank_recipient:
                error_message = "to 不能为空";
                error_code = "blank_recipient";
                break;
            case protocol::ChatPayloadError::missing_send_at:
                error_message = "聊天消息缺少 send_at";
                error_code = "missing_send_at";
                break;
            case protocol::ChatPayloadError::send_at_not_string:
                error_message = "send_at 必须是字符串";
                error_code = "send_at_not_string";
                break;
            case protocol::ChatPayloadError::blank_send_at:
                error_message = "send_at 不能为空";
                error_code = "blank_send_at";
                break;
            case protocol::ChatPayloadError::send_at_too_long:
                error_message = "send_at 不能超过 64 字节";
                error_code = "send_at_too_long";
                break;
            }
            send(protocol::MessageType::error,
                 makeChatError(result.local_id, error_code, error_message));
            break;
        }
        m_on_message(m_id, message);
        break;
    }
    case protocol::MessageType::ping:
        std::cout << "收到ping" << std::endl;
        send(protocol::MessageType::pong, message.body);
        break;
    case protocol::MessageType::pong:
        std::cout << "收到pong" << std::endl;
        break;
    case protocol::MessageType::error:
        std::cerr << "错误：" << message.body << std::endl;
        break;
    case protocol::MessageType::auth:
        break;
    case protocol::MessageType::chat_ack:
        break;
    case protocol::MessageType::delivery_receipt:
        m_on_message(m_id, message);
        break;
    case protocol::MessageType::online_users:
        break;
        case protocol::MessageType::history_query: {
            const auto result = protocol::parseHistoryQueryPayload(message.body);

            if (result.error != protocol::HistoryQueryPayloadError::none)
            {
                std::string error_code = "history_validation_failed";
                std::string error_message = "历史查询消息校验失败";

                switch (result.error) {
                    case protocol::HistoryQueryPayloadError::none:
                        break;
                    case protocol::HistoryQueryPayloadError::invalid_json:
                        error_code = "invalid_json";
                        error_message = "历史查询 JSON 格式错误";
                        break;
                    case protocol::HistoryQueryPayloadError::forbidden_identity_field:
                        error_code = "forbidden_identity_field";
                        error_message = "历史查询消息包含禁止的 identity 字段";
                        break;
                    case protocol::HistoryQueryPayloadError::missing_request_id:
                        error_code = "missing_request_id";
                        error_message = "历史查询消息缺少 request_id";
                        break;
                    case protocol::HistoryQueryPayloadError::request_id_not_string:
                        error_code = "request_id_not_string";
                        error_message = "历史查询消息的 request_id 必须是字符串";
                        break;
                    case protocol::HistoryQueryPayloadError::blank_request_id:
                        error_code = "blank_request_id";
                        error_message = "历史查询消息的 request_id 不能为空";
                        break;
                    case protocol::HistoryQueryPayloadError::request_id_too_long:
                        error_code = "request_id_too_long";
                        error_message = "历史查询消息的 request_id 不能超过 64 字节";
                        break;
                    case protocol::HistoryQueryPayloadError::missing_limit:
                        error_code = "missing_limit";
                        error_message = "历史查询消息缺少 limit";
                        break;
                    case protocol::HistoryQueryPayloadError::limit_not_integer:
                        error_code = "limit_not_integer";
                        error_message = "历史查询消息的 limit 必须是整数";
                        break;
                    case protocol::HistoryQueryPayloadError::before_not_object:
                        error_code = "before_not_object";
                        error_message = "历史查询消息的 before 必须是对象";
                        break;
                    case protocol::HistoryQueryPayloadError::missing_before_timestamp:
                        error_code = "missing_before_timestamp";
                        error_message = "历史查询消息缺少 before.timestamp";
                        break;
                    case protocol::HistoryQueryPayloadError::before_timestamp_not_integer:
                        error_code = "before_timestamp_not_integer";
                        error_message = "历史查询消息的 before.timestamp 必须是整数";
                        break;
                    case protocol::HistoryQueryPayloadError::negative_before_timestamp:
                        error_code = "negative_before_timestamp";
                        error_message = "历史查询消息的 before.timestamp 不能为负数";
                        break;
                    case protocol::HistoryQueryPayloadError::missing_before_message_id:
                        error_code = "missing_before_message_id";
                        error_message = "历史查询消息缺少 before.message_id";
                        break;
                    case protocol::HistoryQueryPayloadError::before_message_id_not_string:
                        error_code = "before_message_id_not_string";
                        error_message = "历史查询消息的 before.message_id 必须是字符串";
                        break;
                    case protocol::HistoryQueryPayloadError::blank_before_message_id:
                        error_code = "blank_before_message_id";
                        error_message = "历史查询消息的 before.message_id 不能为空";
                        break;
                    case protocol::HistoryQueryPayloadError::before_message_id_too_long:
                        error_code = "before_message_id_too_long";
                        error_message = "历史查询消息的 before.message_id 不能超过 64 字节";
                        break;
                }
                send(protocol::MessageType::error,makeHistoryError(error_code,error_message));
                break;
            }
            // 到这里说明请求体已经通过协议校验。
            m_on_message(m_id,message);
            break;
        }
    default: ;
    }
}

std::string Session::makeHistoryError(const std::string &code, const std::string &message) {
    boost::json::object object;
    object["scope"] = "history";
    object["code"] = code;
    object["message"] = message;

    return boost::json::serialize(object);
}


// ==================== 模块：关闭与日志 ====================
// 功能：仅第一次调用时标记会话已断开，并通知 Server 清理对应会话记录。
void Session::closeOnStrand() {
    if (m_disconnected) {
        return;
    }
    m_disconnected = true;
    //优雅地关闭 socket
    std::error_code ignored_error;
    m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_error);
    m_socket.close(ignored_error);

    m_on_disconnect(m_id);
}

// 功能：统一输出会话生命周期与协议处理日志。
void Session::log(const std::string_view event) {
    std::cout << event << std::endl;
}

} // net 命名空间结束
