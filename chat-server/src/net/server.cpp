#include "net/server.h"
#include "net/session.h"
#include "protocol/chat_payload.h"

#include <boost/json.hpp>

#include <iostream>
#include <string_view>

namespace {

// ==================== 模块：路由错误正文构造 ====================
// 功能：构造包含 local_id 的聊天错误 JSON，使客户端能定位发送失败的消息气泡。
std::string makeRouteErrorBody(const std::string& local_id, const std::string& code,
                               const std::string& message) {
    boost::json::object object;
    object["scope"] = "chat";
    object["code"] = std::string(code);
    object["message"] = std::string(message);
    if (!local_id.empty()) {
        object["local_id"] = std::string(local_id);
    }
    return boost::json::serialize(object);
}
// 功能：构造包含 code 和 message 的送达回执错误 JSON，用于客户端请求送达回执失败时返回。
std::string makeDeliveryReceiptErrorBody(const std::string &code, const std::string &message)
{
    boost::json::object object;
    object["scope"] = "delivery_receipt";
    object["code"] = std::string(code);
    object["message"] = std::string(message);
    return boost::json::serialize(object);
}
} // 匿名命名空间结束

namespace net {

// ==================== 模块：生命周期与监听 ====================
// 功能：创建 Server 串行执行器，绑定 IPv4 监听端口，并准备异步接受使用的 Socket。
Server::Server(asio::io_context& io_context, const std::uint16_t port)
    : m_strand(asio::make_strand(io_context)),
      m_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
      m_pending_socket(io_context) {
}

// 功能：输出当前监听地址并启动第一个异步接受操作。
void Server::start() {
    std::cout << "正在监听端口：" << m_acceptor.local_endpoint() << std::endl;
    doAccept();
}

// 功能：持续接受新连接；每次回调结束前重建待接受 Socket 并再次监听。
// 失败：接受失败时记录错误，但不会停止后续接受操作。
void Server::doAccept() {
    m_acceptor.async_accept(
        m_pending_socket,
        asio::bind_executor(
            m_strand,
            // 功能：处理一次连接接受结果，并为成功连接创建、登记和启动 Session。
            [this](const std::error_code error) {
                if (error == asio::error::operation_aborted) {
                    return;
                }

                if (error) {
                    std::cerr << "接受连接失败：" << error.message();
                } else {
                    const SessionId session_id = m_next_session_id++;
                    std::error_code endpoint_error;
                    const auto endpoint = m_pending_socket.remote_endpoint(endpoint_error);
                    if (endpoint_error) {
                        std::cerr << "客户端#" << session_id << "地址读取失败："
                                  << endpoint_error.message() << std::endl;
                    } else {
                        std::cout << "客户端#" << session_id << "已连接：" << endpoint << std::endl;
                    }

                    // 功能：将 Session 消息投递回 Server strand，再访问路由表。
                    auto on_message = [this](SessionId sender_id, protocol::Message message) {
                        asio::post(
                            m_strand,
                            // 功能：在 Server strand 中按发送者会话标识路由聊天消息。
                            [this, sender_id, message = std::move(message)] {
                                onSessionMessage(sender_id, message);
                            });
                    };

                    // 功能：将 Session 断开事件投递回 Server strand，统一清理会话表。
                    auto on_disconnect = [this](const SessionId disconnected_session_id) {
                        asio::post(
                            m_strand,
                            // 功能：在 Server strand 中移除已断开会话的所有映射。
                            [this, disconnected_session_id] {
                                removeSession(disconnected_session_id);
                            });
                    };

                    // 功能：将认证成功事件投递回 Server strand，登记用户名与会话映射。
                    auto on_authenticated = [this](SessionId authenticated_session_id,
                                                   std::string username) {
                        asio::post(
                            m_strand,
                            // 功能：在 Server strand 中写入双向用户名和会话标识映射。
                            [this, authenticated_session_id, username = std::move(username)] {
                                m_username_to_session.emplace(username, authenticated_session_id);
                                m_session_to_username.emplace(authenticated_session_id, username);
                            });
                    };

                    const auto session = std::make_shared<Session>(
                        std::move(m_pending_socket), session_id, std::move(on_message),
                        std::move(on_disconnect), std::move(on_authenticated));
                    addSession(session_id, session);
                    session->start();
                }

                m_pending_socket = asio::ip::tcp::socket(m_acceptor.get_executor());
                doAccept();
            }));
}

// ==================== 模块：会话登记与清理 ====================
// 功能：将新会话加入在线会话表，并输出当前在线数量。
void Server::addSession(const SessionId session_id, const SessionPtr& session) {
    m_sessions.emplace(session_id, session);
    std::cout << "客户端#" << session_id << "已登记,当前在线:" << m_sessions.size() << std::endl;
}

// 功能：删除会话表、用户名到会话表和会话到用户名表中的断开连接记录。
void Server::removeSession(const SessionId session_id)
{
    std::string disconnected_username;
    if (const auto it = m_session_to_username.find(session_id);
        it != m_session_to_username.end()) {
        disconnected_username = it->second;
        m_username_to_session.erase(disconnected_username);
        m_session_to_username.erase(session_id);
    }

    removePendingDeliveriesForSession(session_id,disconnected_username);

    if (const auto rm_count = m_sessions.erase(session_id); rm_count != 0) {
        std::cout << "客户端 #" << session_id << " 已移出在线表，当前在线："
                  << m_sessions.size() << std::endl;
    }
}

// ==================== 模块：聊天消息路由 ====================
// 功能：记录收到的业务消息并交给私聊路由逻辑。
void Server::onSessionMessage(const SessionId sender_id, const protocol::Message& message) {
    switch (message.type) {
        case protocol::MessageType::chat:
            sendToUser(sender_id,message);
            break;
        case protocol::MessageType::delivery_receipt:
            handleDeliveryReceipt(sender_id,message);
            break;
        default:
            break;
    }
}

// 功能：检查发送者和接收者在线状态，转发聊天帧并向发送者发送确认帧。
// 失败：发送者未登记或接收者离线时，仅向发送者发送带 local_id 的错误帧。
void Server::sendToUser(const SessionId sender_id, const protocol::Message& message) {
    const auto payload = protocol::parseChatPayload(message.body);
    if (payload.error != protocol::ChatPayloadError::none) {
        return;
    }

    const auto sender_it = m_sessions.find(sender_id);
    const auto sender_username_it = m_session_to_username.find(sender_id);
    const auto target_it = m_username_to_session.find(payload.to);
    if (sender_it == m_sessions.end()) {
        return;
    }

    if (sender_username_it == m_session_to_username.end()) {
        const std::string error_body = makeRouteErrorBody(
            payload.local_id, "sender_not_registered", "发送者会话尚未完成注册");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }
    if (target_it == m_username_to_session.end()) {
        const std::string error_body =
            makeRouteErrorBody(payload.local_id, "recipient_offline", "接收者不在线");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }

    const auto recv_it = m_sessions.find(target_it->second);
    if (recv_it == m_sessions.end()) {
        const std::string error_body = makeRouteErrorBody(
            payload.local_id, "recipient_offline", "接收者连接已经断开");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }

    if (!rememberPendingDelivery(payload.to, payload.local_id,
        sender_username_it->second, sender_id))
    {
        const std::string error_body =
            makeRouteErrorBody(payload.local_id, "duplicate_local_id", "消息 local_id 已存在，不能重复发送");
            sender_it->second->send(protocol::MessageType::error,error_body);
        return;
    }
    boost::json::object forwarded;
    forwarded["local_id"] = payload.local_id;
    forwarded["from"] = sender_username_it->second;
    forwarded["to"] = payload.to;
    forwarded["content"] = payload.content;
    forwarded["send_at"] = payload.send_at;
    const std::string forward_body = boost::json::serialize(forwarded);
    recv_it->second->send(protocol::MessageType::chat, forward_body);

    boost::json::object ack;
    ack["local_id"] = payload.local_id;
    ack["status"] = "accepted";
    sender_it->second->send(protocol::MessageType::chat_ack, boost::json::serialize(ack));
}

// 功能：处理送达回执，目前仅检查格式，不处理具体业务。
void Server::handleDeliveryReceipt(SessionId receipt_sender_id, const protocol::Message &message)
{
    //B 的会话，用于给 B 返回错误
    const auto sender_it = m_sessions.find(receipt_sender_id);
    if (sender_it == m_sessions.end()) {
        return;
    }

    protocol::DeliveryReceiptPayloadResult payload_result = protocol::parseDeliveryReceiptPayload(message.body);

    if (payload_result.error != protocol::DeliveryReceiptPayloadError::none) {
        const std::string error_body = makeDeliveryReceiptErrorBody("invalid_delivery_receipt", "送达回执格式错误");
        sender_it->second->send(protocol::MessageType::error,error_body);
        return;
    }
    //这是理论上不应出现的防御性情况
    const auto username_it = m_session_to_username.find(receipt_sender_id);
    if (username_it == m_session_to_username.end()) {
        return;
    }
    //B 的待送达消息表，用于检查 local_id
    const auto recipient_it = m_pendingDeliveries.find(username_it->second);
    if (recipient_it == m_pendingDeliveries.end())
    {
        const std::string error_body = makeDeliveryReceiptErrorBody("unknown_delivery_receipt", "没有对应的待送达消息");
        sender_it->second->send(protocol::MessageType::error,error_body);
        return;
    }
    //pending[B][local_id] 的记录,delivery_it->second     = { A 的用户名, A 的会话 ID }
    const auto delivery_it = recipient_it->second.find(payload_result.local_id);
    if (delivery_it == recipient_it->second.end()) {
        const std::string error_body = makeDeliveryReceiptErrorBody("unknown_delivery_receipt", "没有对应的待送达消息");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }
    //A 的会话，用于通知“已送达”
    const auto original_sender_it = m_sessions.find(delivery_it->second.sender_session_id);
    if (original_sender_it == m_sessions.end()) {
        return;
    }

    boost::json::object delivered;
    delivered["local_id"] = payload_result.local_id;
    delivered["status"] = "delivered";
    original_sender_it->second->send(protocol::MessageType::delivery_receipt,boost::json::serialize(delivered));

    // 从 B 的内层表删除 delivery_it 指向的那条记录。
    // 若 B 的内层表已空，再从 m_pendingDeliveries 删除 recipient_it。
    recipient_it->second.erase(delivery_it);
    if (recipient_it->second.empty()) {
        m_pendingDeliveries.erase(recipient_it);
    }
}

// 功能：记录待投递消息，避免重复投递。
bool Server::rememberPendingDelivery(const std::string &recipient_username, const std::string &local_id,
    const std::string &sender_username, SessionId sender_session_id)
{
    if (recipient_username.empty() || local_id.empty() || sender_username.empty() || sender_session_id == 0)
    {
        return false;
    }
        auto& deliveries_for_recipient = m_pendingDeliveries[recipient_username];
        const auto [it,inserted] =
            deliveries_for_recipient.emplace(local_id,PendingDelivery{sender_username,sender_session_id});

    //inserted == true：登记成功。
    //inserted == false：同一个 B 已有相同 local_id，后续必须拒绝，不能覆盖。
    return inserted;
}

// 功能：删除与断开连接会话相关的所有待投递消息。
void Server::removePendingDeliveriesForSession(SessionId disconnected_session_id,
    const std::string &disconnected_username)
{
    for (auto recipient_it = m_pendingDeliveries.begin(); recipient_it != m_pendingDeliveries.end();)
    {
        auto& deliveries = recipient_it->second;
        for (auto it = deliveries.begin(); it != deliveries.end();)
        {
           const bool recipient_disconnected =
               !disconnected_username.empty() && recipient_it->first == disconnected_username;
            const bool sender_disconnected =
                it->second.sender_session_id == disconnected_session_id;

            if (recipient_disconnected || sender_disconnected) {
                it = deliveries.erase(it);
            }else {
                ++it;
            }
        }
        if (deliveries.empty()) {
            recipient_it = m_pendingDeliveries.erase(recipient_it);
        }else {
            ++recipient_it;
        }
    }
}
} // net 命名空间结束
