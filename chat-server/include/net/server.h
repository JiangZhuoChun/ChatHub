#pragma once

#include "net/session.h"

#include <string>
#include <unordered_map>

namespace net {

// ==================== 模块：聊天服务器 ====================
class Server {
public:
    // ==================== 模块：生命周期与监听 ====================
    // 功能：创建 Server strand、绑定监听端口，并准备下一次接受连接的 Socket。
    Server(asio::io_context& io_context, std::uint16_t port);

    // 功能：输出监听信息并开始持续异步接受新的 TCP 连接。
    void start();

private:
    // 功能：接受一个连接、创建 Session、登记在线表，并继续等待下一次连接。
    void doAccept();

    // ==================== 模块：会话登记与清理 ====================
    // 功能：将新建 Session 放入会话表，使它可被后续路由操作找到。
    void addSession(SessionId session_id, const SessionPtr& session);

    // 功能：移除断开会话及其关联用户名，防止继续向失效连接路由消息。
    void removeSession(SessionId session_id);

    // ==================== 模块：聊天消息路由 ====================
    // 功能：接收 Session 上交的完整消息，并交给私聊路由函数处理。
    void onSessionMessage(SessionId sender_id, const protocol::Message& message);

    // 功能：校验发送者和接收者在线状态，向接收者转发消息并回复发送确认。
    void sendToUser(SessionId sender_id, const protocol::Message& message);

    // ==================== 模块：并发执行资源 ====================
    // 功能：保证在线会话表和用户名映射只在 Server 的串行执行器中访问。
    asio::strand<asio::any_io_executor> m_strand;

    // ==================== 模块：监听资源 ====================
    // 功能：监听聊天服务器端口并异步接受新连接。
    asio::ip::tcp::acceptor m_acceptor;

    // 功能：在异步接受期间保存待转交给新 Session 的 Socket。
    asio::ip::tcp::socket m_pending_socket;

    // ==================== 模块：会话表 ====================
    // 功能：为下一个连接分配递增且唯一的会话标识。
    SessionId m_next_session_id{1};

    // 功能：按会话标识保存所有仍由 Server 管理的连接。
    std::unordered_map<SessionId, SessionPtr> m_sessions;

    // ==================== 模块：用户路由表 ====================
    // 功能：将认证用户名映射到当前在线会话标识。
    std::unordered_map<std::string, SessionId> m_username_to_session;

    // 功能：将会话标识反向映射到认证用户名，供断开清理使用。
    std::unordered_map<SessionId, std::string> m_session_to_username;
};

} // net 命名空间结束
