#pragma once
#include "net/session.h"
#include <cstdint>
#include <unordered_map>
namespace net {
    //功能::Server：管理监听、在线连接表和广播路由；不直接管理单个 socket 的读写队列
class Server {
public:
    //功能::构造函数：创建串行通道、绑定端口
    Server(asio::io_context &io_context, std::uint16_t port);

    //功能::启动：输出监听信息并开始持续接受新连接
    void start();

private:
    //功能::以下函数均在 m_strand 中运行，安全访问 m_sessions
    void doAccept();   //功能::持续接受连接
    //功能::收到客户端消息：打印并交给广播
    void onSessionMessage(SessionId sender_id, const protocol::Message& message);
    //功能::广播：遍历在线表，跳过发送者，发给其他所有客户端
    void broadcast(SessionId sender_id,const protocol::Message& message);
    //功能::登记在线表
    void addSession(SessionId session_id, const SessionPtr& session);
    //功能::移除在线表（客户端断开时）
    void removeSession(SessionId session_id);

    //功能::成员变量
    asio::strand<asio::any_io_executor> m_strand;   //功能::Server 串行通道，保护在线表
    asio::ip::tcp::acceptor m_acceptor;   //功能::监听器
    asio::ip::tcp::socket m_pending_socket;   //功能::待接受 socket（成员保证异步期间存活）

    SessionId m_next_session_id {1};   //功能::连接 ID 计数器
    std::unordered_map<SessionId,SessionPtr> m_sessions;   //功能::在线表：ID → Session
};
}
