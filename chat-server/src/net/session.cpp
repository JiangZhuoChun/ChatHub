#include "net/session.h"

#include <iostream>
#include <boost/json.hpp>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/defaults.h>

#include "protocol/chat_payload.h"

const std::string SECRET_KEY = "chathub-dev-secret";
namespace net {
    //功能::构造函数：接收 socket、连接 ID、消息回调、断开回调，初始化成员
    Session::Session(
        asio::ip::tcp::socket  socket,
        SessionId session_id,
        MessageCallback on_message,
        DisconnectCallback on_disconnect,
        AuthenticatedCallback on_authenticated) :
    m_socket( std::move(socket)),          //功能::转移 socket 所有权给 Session
    m_strand(m_socket.get_executor()),     //功能::用 socket 的执行器创建串行通道
    m_on_message(std::move(on_message)),   //功能::保存消息回调（收到完整消息时调用）
    m_id(session_id),                      //功能::保存连接 ID
    m_on_disconnect(std::move(on_disconnect)),//功能::保存断开回调（断开时通知 Server）
    m_on_authenticated(std::move(on_authenticated))//功能::保存认证成功回调
    {
    }
    //功能::日志：统一输出事件信息，方便排查多客户端问题
    void Session::log(const std::string_view  event) {
        std::cout << event << std::endl;
    }

    //功能::关闭连接：幂等，只通知 Server 一次从在线表移除
    void Session::close() {
        if (m_disconnected) {   //功能::已断开则直接返回，避免重复通知
            return;
        }
        m_disconnected = true;  //功能::标记已断开
        m_on_disconnect(m_id);  //功能::通知 Server 移除在线表
    }

    //功能::启动 Session：投递第一个异步读取到串行通道
    void Session::start() {
        auto self = shared_from_this();   //功能::持有自身，防止异步期间被析构
        asio::post(m_strand,[self] {
            self->doRead();
        });
    }
    //功能::异步读取：读数据→解码→处理，循环持续读
    void Session::doRead() {
        auto self = shared_from_this();   //功能::持有自身，回调期间 Session 存活
        m_socket.async_read_some(asio::buffer(m_read_buffer),  //功能::异步读入缓冲
            asio::bind_executor(m_strand,[self, this](const std::error_code error, const std::size_t bytes_transferred)
            {
                if (error) {   //功能::读取出错，按错误类型处理
                    if (error == asio::error::eof) {
                        net::Session::log("正常断开连接");   //功能::客户端正常关闭（EOF）
                    }
                    else if (error == asio::error::operation_aborted) {
                        return;   //功能::主动取消，正常收尾直接返回
                    }
                    else {
                        std::cerr << "错误：" << error.message() << std::endl;
                    }
                    self->close();   //功能::出错统一关闭
                    return;
                }
                //功能::解码本次读取的字节，可能拼出多条完整消息
                const auto result = m_decoder.append(m_read_buffer.data(),bytes_transferred,
                    [self](const protocol::Message &message) {
                        self->handlerMessage(message);   //功能::把完整消息交给业务处理
                    });
                if (result != protocol::DecodeResult::ok) {   //功能::协议错误（非法magic/超长等）
                    log("协议错误，关闭当前连接");
                    self->close();
                    return;
                }
                self->doRead();   //功能::继续读取下一条
            }));
    }

    //功能::发送消息：任意线程可调用，投递到串行通道后入队
    void Session::send(protocol::MessageType type, std::string body) {
        auto self = shared_from_this();   //功能::持有自身，防止投递任务执行前被析构
        asio::post(m_strand,[self,type, body = std::move(body)] {
            self->enqueueAndWrite(type,body);
        });
    }
    //功能::编码并入队：把消息编码成帧放进写队列，必要时启动写
    void Session::enqueueAndWrite(const protocol::MessageType type, const std::string& body) {
        if (body.size() > protocol::FrameDecoder::kMaxBodyLength) {   //功能::body超长限制
            std::cerr << "错误：body长度超出限制" << std::endl;
            return;
        }
        //功能::只有队列从空变为非空时才启动一次 async_write，避免同一 socket 并发写。
        //功能::设置最大发送队列为3
        const bool was_empty = m_write_queue.empty();

        if (m_write_queue.size() >= kMaxWriteQueueSize) {   //功能::队列满：慢客户端，关闭
            std::cout << "错误：发送队列已满：" << m_write_queue.size() << "/" <<kMaxWriteQueueSize
                    << "关闭慢客户端" << std::endl;
            close();

            return;
        }
        m_write_queue.push_back({type,protocol::makeFrame(type,body)});   //功能::编码成帧入队
        if (was_empty) {   //功能::队列从空变非空，启动写
            writeFrame();
        }
    }

    //功能::写帧：写队列中的下一条帧，写完继续写下一条
    void Session::writeFrame() {
        if (m_write_queue.empty()) {   //功能::队列空则无需写
            return;
        }
        auto self = shared_from_this();   //功能::持有自身，写回调期间 Session 存活
        asio::async_write(m_socket,asio::buffer(m_write_queue.front().frame),  //功能::写队首帧
            asio::bind_executor(m_strand,[self, this](const std::error_code error, const std::size_t bytes_transferred) {
                if ( error) {   //功能::写失败，关闭
                    std::cerr << "错误,发送失败" << error.message() << std::endl;
                    self->close();
                    return;
                }
                const auto send_type = m_write_queue.front().type;   //功能::记录已发送类型（预留广播用）
                m_write_queue.pop_front();   //功能::写完成，移出队首

                std::cout << "发送成功：" << bytes_transferred << "字节" << std::endl;

                self->writeFrame();   //功能::继续写下一条
            }));
    }

    std::string Session::makeChatError(const std::string &local_id, const std::string &code, const std::string &message)
    {
        // 聊天错误使用 JSON，客户端才能根据 local_id 找回失败的消息气泡。
        boost::json::object obj;
        obj["scope"] = "chat";
        obj["code"] = code;
        obj["message"] = message;

        if (!local_id.empty()) {
            obj["local_id"] = local_id;
        }
        return boost::json::serialize(obj);
    }

    //功能::业务消息处理：按消息类型分派
    void Session::handlerMessage(const protocol::Message&  message) {
        if (!m_authenticated) {
            if (message.type == protocol::MessageType::auth) {
                if (std::string username; verifyJwt(message.body,username)) {
                    m_authenticated = true;
                    m_username = username;
                    //JWT认证成功,发送认证成功消息
                    send(protocol::MessageType::auth, R"({"ok":true})");
                    if (m_on_authenticated) {
                        m_on_authenticated(m_id,m_username);
                    }
                }else {
                    log("认证失败，关闭连接");
                    close();
                }
            }else {
                log("未认证先发消息，关闭连接");
                close();
            }
            return;
        }
        switch (message.type) {
            case protocol::MessageType::chat://功能::聊天消息：交给 Server 广播
            {
                // Session 只负责接收、校验和错误反馈；只有校验成功才交给 Server 路由。
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
                    // 正文校验失败只反馈给发送者
                    send(protocol::MessageType::error,makeChatError(result.local_id,error_code,error_message));
                    break;
                }
                    m_on_message(m_id, message);
                    break;
            }
            case protocol::MessageType::ping:   //功能::心跳请求：回复 pong
                std::cout << "收到ping" << std::endl;
                send(protocol::MessageType::pong,message.body);
                break;
            case protocol::MessageType::pong:   //功能::心跳响应：记录即可
                std::cout << "收到pong" << std::endl;
                break;
            case protocol::MessageType::error:  //功能::错误消息：输出错误内容
                std::cerr << "错误：" << message.body << std::endl;
                break;
            case protocol::MessageType::auth:
                break;
            case protocol::MessageType::chat_ack:
                break;
        }

    }

    //验证函数
    bool Session::verifyJwt(const std::string &token, std::string &out_username) {
        try {
            const auto decoded = jwt::decode(token);
            const auto verifier = jwt::verify().allow_algorithm(jwt::algorithm::hs256{SECRET_KEY});
            verifier.verify(decoded);
            out_username = decoded.get_payload_claim("username").as_string();
            return true;
        }
        catch (const std::exception &) {
            return false;
        }
    }
}
