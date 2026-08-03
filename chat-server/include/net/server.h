#pragma once
#include "net/session.h"
#include <cstdint>
#include <unordered_map>
namespace net {
    // Server 管理监听、在线连接表和广播路由；不直接管理单个 socket 的读写队列。
class Server {
public:
    Server(asio::io_context &io_context, std::uint16_t port);

    void start();//输出监听信息并开始持续接受新连接。

private:
    // 所有以下函数均在 m_strand 中运行，安全访问 m_sessions。
    void doAccept();
    // 广播功能实现
    void onSessionMessage(SessionId sender_id, const protocol::Message& message);
    void broadcast(SessionId sender_id,const protocol::Message& message);
    void addSession(SessionId session_id, const SessionPtr& session);
    void removeSession(SessionId session_id);

    // Server strand 保护在线表；它与每个 Session 自己的 strand 职责不同。
    asio::strand<asio::any_io_executor> m_strand;
    asio::ip::tcp::acceptor m_acceptor;
    asio::ip::tcp::socket m_pending_socket;

    SessionId m_next_session_id {1};
    std::unordered_map<SessionId,SessionPtr> m_sessions;
};
}