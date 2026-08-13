#include "net/server.h"
#include "net/session.h"
#include "protocol/chat_payload.h"

#include <boost/json.hpp>

#include <iostream>
#include <string_view>
#include <algorithm>
#include <vector>
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
      m_pending_socket(io_context)
{
    m_message_repository.open("chathub.db");
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
                            // 功能: 注册已认证会话并广播在线用户列表。
                            [this, authenticated_session_id, username = std::move(username)] {
                                registerAuthenticatedSession(authenticated_session_id, username);
                                broadcastOnlineUsers();
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
    // 只有该用户名确实没有被新会话接管时才保存它：
    // 非空表示“接收者已离线”，供待送达记录按接收者整组清理。
    std::string recipient_username_to_clean;

    // 断开事件只带 session_id，先通过反向映射找出旧会话原本的用户名。
    if (const auto reverse_it = m_session_to_username.find(session_id);
        reverse_it != m_session_to_username.end() )
    {
        const std::string username = reverse_it->second;

        // 同名新连接可能已把 username 指向另一个 SessionId。
        // 只有正向映射仍指向本次断开的旧会话，才说明该用户名真的离线。
        if (const auto forward_it = m_username_to_session.find(username);
            forward_it != m_username_to_session.end() && forward_it->second == session_id)
        {
            m_username_to_session.erase(forward_it);
            recipient_username_to_clean = username;
        }
        // 无论是否被新会话接管，这条旧 session_id -> username 反向映射都已失效。
        m_session_to_username.erase(reverse_it);
    }

    // 始终按旧会话 ID 清理其作为发送者的记录；
    // 只有普通断开才传入用户名，避免顶替场景误删新会话作为接收者的记录。
    removePendingDeliveriesForSession(session_id,recipient_username_to_clean);
    if (const auto rm_count = m_sessions.erase(session_id);
        rm_count != 0){
        std::cout << "客户端 #" << session_id << " 已移出在线表，当前在线："
                  << m_sessions.size() << std::endl;
    }

    if (!recipient_username_to_clean.empty()) {
        broadcastOnlineUsers();
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
    //1. 解析消息正文
    const auto payload = protocol::parseChatPayload(message.body);
    if (payload.error != protocol::ChatPayloadError::none) {
        return;
    }
    const auto sender_it = m_sessions.find(sender_id);
    const auto sender_username_it = m_session_to_username.find(sender_id);
    const auto target_it = m_username_to_session.find(payload.to);
    //2. 确认发送 Session 还存在
    if (sender_it == m_sessions.end()) {
        return;
    }
    //3. 确认它有认证身份
    if (sender_username_it == m_session_to_username.end()) {
        const std::string error_body = makeRouteErrorBody(
            payload.local_id, "sender_not_registered", "发送者会话尚未完成注册");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }
    //4. 确认它仍是该身份的当前活动会话
    if (!isCurrentAuthenticatedSession(sender_id,sender_username_it->second)) {
        sender_it->second->send(
            protocol::MessageType::error,
            makeRouteErrorBody(payload.local_id,"session_replaced","当前登录已在其他连接接管"));
        return;
    }
    //5. 确认接收者在线
    if (target_it == m_username_to_session.end()) {
        const std::string error_body =
            makeRouteErrorBody(payload.local_id, "recipient_offline", "接收者不在线");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }
    //6. 确认接收 Session 还存在
    const auto recv_it = m_sessions.find(target_it->second);
    if (recv_it == m_sessions.end()) {
        const std::string error_body = makeRouteErrorBody(
            payload.local_id, "recipient_offline", "接收者连接已经断开");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }
    //7. SQLite 插入并提交
    std::string sender = sender_username_it->second;
    std::string recipient = payload.to;
    std::string content = payload.content;
    std::string local_id = payload.local_id;
    std::string send_at = payload.send_at;
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    repository::NewMessage msg{sender, recipient, content, send_at,local_id,  now_ms};
    auto outcome =m_message_repository.storeOrGetExisting(msg);
    switch (outcome.result) {
        case repository::StoreResult::Stored:
            break;
        case repository::StoreResult::DuplicateSame: {
            boost::json::object ack;
            ack["message_id"] = outcome.message_id;
            ack["local_id"] = payload.local_id;
            ack["status"] = "accepted";
            sender_it->second->send(
                protocol::MessageType::chat_ack, boost::json::serialize(ack));
            return;
        }
        case repository::StoreResult::IdempotencyConflict:
            sender_it->second->send(
                protocol::MessageType::error, makeRouteErrorBody(payload.local_id, "idempotency_conflict", "消息部分冲突"));
            return;
        case repository::StoreResult::DatabaseError:
            sender_it->second->send(
                protocol::MessageType::error, makeRouteErrorBody(payload.local_id, "database_error", "数据库错误"));
            return;
    }
    // 8. 以本次持久化生成的 message_id 登记待送达记录，避免回执关联到其他消息。
    if (!rememberPendingDelivery(outcome.message_id,sender_username_it->second,
        sender_id,payload.local_id,payload.to))
    {
        const std::string error_body =
            makeRouteErrorBody(payload.local_id, "pending_delivery_register_failed","消息已保存，但送达状态登记失败");
            sender_it->second->send(protocol::MessageType::error,error_body);
        return;
    }

    boost::json::object forwarded;
    forwarded["message_id"] = outcome.message_id;
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
    ack["message_id"] = outcome.message_id;
    sender_it->second->send(protocol::MessageType::chat_ack, boost::json::serialize(ack));
}

// 功能：校验接收者回执的 message_id 与认证身份，并将最终送达状态通知原发送者。
void Server::handleDeliveryReceipt(const SessionId receipt_sender_id, const protocol::Message &message)
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

    //确认它仍是该身份的当前活动会话
    if (!isCurrentAuthenticatedSession(receipt_sender_id,username_it->second)) {
        sender_it->second->send(
            protocol::MessageType::error,
            makeDeliveryReceiptErrorBody("session_replaced", "当前登录已在其他连接接管"));
        return;
    }


    const auto delivery_it = m_pendingDeliveries.find(payload_result.message_id);
    if (delivery_it == m_pendingDeliveries.end() ||
        delivery_it->second.recipient_username != username_it->second) {
        const std::string error_body = makeDeliveryReceiptErrorBody("unknown_delivery_receipt", "没有对应的待送达消息");
        sender_it->second->send(protocol::MessageType::error, error_body);
        return;
    }
    //A 的会话，用于通知“已送达”
    const auto original_sender_it = m_sessions.find(delivery_it->second.sender_session_id);
    if (original_sender_it == m_sessions.end() ||
        !isCurrentAuthenticatedSession(delivery_it->second.sender_session_id,
                                       delivery_it->second.sender_username)) {
        m_pendingDeliveries.erase(delivery_it);
        return;
    }

    boost::json::object delivered;
    delivered["local_id"] = delivery_it->second.sender_local_id;
    delivered["status"] = "delivered";
    original_sender_it->second->send(protocol::MessageType::delivery_receipt,boost::json::serialize(delivered));

    // 已处理的 message_id 不再接受第二次回执，重复回执会在 find() 时被拒绝。
    m_pendingDeliveries.erase(delivery_it);
}

// 功能：记录待投递消息，避免重复投递。
bool Server::rememberPendingDelivery(const std::string& message_id,const std::string &sender_username,
    SessionId sender_session_id, const std::string& sender_local_id, const std::string& recipient_username)
{
    if (message_id.empty() || sender_username.empty() || sender_session_id == 0 ||
        sender_local_id.empty() || recipient_username.empty())
    {
        return false;
    }
        const auto [it,inserted] = m_pendingDeliveries.emplace(message_id,
            PendingDelivery{sender_username,sender_session_id,sender_local_id,recipient_username});

    // inserted == true：该 message_id 首次登记成功。
    // inserted == false：理论上只可能是极小概率的 message_id 冲突，不能覆盖原记录。
    return inserted;
}

// 功能：删除与断开连接会话相关的所有待投递消息。
void Server::removePendingDeliveriesForSession(SessionId disconnected_session_id,
    const std::string &disconnected_username)
{
    for (auto it = m_pendingDeliveries.begin(); it != m_pendingDeliveries.end();) {
        const bool recipient_disconnected =
            !disconnected_username.empty() &&
            it->second.recipient_username == disconnected_username;
        const bool sender_disconnected =
            it->second.sender_session_id == disconnected_session_id;

        if (recipient_disconnected || sender_disconnected) {
            it = m_pendingDeliveries.erase(it);
        } else {
            ++it;
        }
    }
}
// 功能：构建在线用户列表帧的 body 部分。
std::optional<std::string> Server::buildOnlineUsersBody() {
    std::vector<std::string> users;
    for (const auto& it : m_username_to_session) {
        const auto& username =it.first;
        users.push_back(username);
    }
    std::sort(users.begin(),users.end());

    boost::json::array array;
    for (const auto& username : users) {
        if (username.empty()) {
            return std::nullopt;
        }
        array.emplace_back(username);
    }
    boost::json::object object;
    object["users"] = std::move(array);
    const auto& body =boost::json::serialize(object);
    if (body.size() > protocol::kMaxFrameBodyLength) {
        return std::nullopt;
    }
    return body;
}
// 功能：广播在线用户列表。
void Server::broadcastOnlineUsers() {
    const auto body = buildOnlineUsersBody();
    if (!body.has_value()) {
        std::cerr << "无正文信息" << std::endl;
        return;
    }
    for (const auto& it : m_username_to_session) {
        if (auto session_it = m_sessions.find(it.second); session_it != m_sessions.end()) {
            session_it->second->send(protocol::MessageType::online_users, body.value());
        }
    }
}
// 功能：注册已认证会话。
void Server::registerAuthenticatedSession(const SessionId session_id, const std::string &username) {
    if (const auto& old_it = m_username_to_session.find(username);
        old_it != m_username_to_session.end() && old_it->second != session_id)
    {
       if (const auto& session_it = m_sessions.find(old_it->second);
           session_it != m_sessions.end())
       {
           session_it->second->requestClose();
       }
    }

    m_username_to_session.insert_or_assign(username,session_id);
    m_session_to_username.insert_or_assign(session_id,username);
}
// 功能：检查会话是否当前用户会话。
bool Server::isCurrentAuthenticatedSession(const SessionId session_id, const std::string &username) const {
    const auto it = m_username_to_session.find(username);
    return it != m_username_to_session.end() && it->second == session_id;
}
} // net 命名空间结束
