#include "net/server.h"

#include <iostream>

namespace net {
    //功能::构造函数：创建串行通道、监听端口、准备待接受 socket
    Server::Server(asio::io_context &io_context, std::uint16_t port):
    m_strand(asio::make_strand(io_context)),   //功能::Server 串行通道，保护在线表
    m_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),  //功能::绑定端口
    m_pending_socket(io_context)   //功能::待接受的 socket（成员保证异步期间存活）
    {
    }

    //功能::启动 Server：打印监听信息并开始接受第一个连接
    void Server::start() {
        std::cout << "正在监听端口：" << m_acceptor.local_endpoint() << std::endl;
        doAccept();
    }
    //功能::持续接受连接：每次接受后重新登记下一次，伪递归不爆栈
    void Server::doAccept() {
        //功能::accept 完成回调进入 Server strand，在线表登记与下一次 accept 都在同一通道中执行。
        m_acceptor.async_accept(m_pending_socket,asio::bind_executor(m_strand,
            [this](const std::error_code error) {
            if ( error == asio::error::operation_aborted) {
                return;   //功能::主动关闭，正常收尾直接退出
            }
            if (error){   //功能::接受失败，记录但不创建 Session
                std::cerr << "接受连接失败：" << error.message();
            }
            else {   //功能::有新客户端连接
                const SessionId session_id = m_next_session_id++;   //功能::分配连接 ID
                std::error_code endpoint_error;
                const auto endpoint = m_pending_socket.remote_endpoint(endpoint_error);   //功能::读客户端地址
                if (endpoint_error) {
                    std::cerr << "客户端#" << session_id << "地址读取失败：" << endpoint_error.message() << std::endl;
                }
                else {
                    std::cout << "客户端#" << session_id << "已连接：" << endpoint << std::endl;
                }

                //功能::消息回调：Session 收到 chat 后调用，post 回 Server strand 再访问在线表
                auto on_message = [this](SessionId sender_id, protocol::Message message) {
                    asio::post(m_strand,[this, sender_id, message = std::move(message)]() {
                        onSessionMessage(sender_id, std::move(message));
                    });
                };
                //功能::断开回调：Session 断开后调用，post 回 Server strand 移除在线表
                auto on_disconnect = [this](SessionId session_id) {
                  asio::post(m_strand,[this, session_id]() {
                     removeSession(session_id);
                  });
                };
                //功能::创建 Session，传入 socket/ID/两个回调；Server 持有一份 shared_ptr，异步回调也会持有
                auto session = std::make_shared<Session>(
                    std::move(m_pending_socket),
                    session_id,
                    std::move(on_message),
                    std::move(on_disconnect));
                addSession(session_id, session);   //功能::登记进在线表
                session->start();   //功能::启动 Session 开始读取
            }
        m_pending_socket = asio::ip::tcp::socket(m_acceptor.get_executor());   //功能::重建空 socket 供下次 accept
        doAccept();   //功能::无论成功失败，继续接受下一个连接
        }));
    }

    //功能::收到客户端消息：打印并交给广播
    void Server::onSessionMessage(SessionId sender_id, const protocol::Message& message) {
        std::cout << "客户端#" << sender_id << "发送：" << message.body << std::endl;
        broadcast(sender_id,message);
    }
    //功能::广播：遍历在线表，跳过发送者，把消息发给其他所有客户端
    void Server::broadcast(SessionId sender_id,const protocol::Message& message) {
        for (auto& [id, session] : m_sessions) {
            if (id != sender_id) {   //功能::跳过发送者
                session->send(message.type,message.body);   //功能::发给其他客户端
            }
        }
    }
    //功能::登记在线表：把新连接的 Session 存入 map
    void Server::addSession(SessionId session_id, const SessionPtr& session) {
        m_sessions.emplace(session_id, session);
        std::cout << "客户端#" << session_id << "已登记,当前在线:" << m_sessions.size() << std::endl;
    }
    //功能::移除在线表：客户端断开时删除对应项，防止广播给死连接
    void Server::removeSession(SessionId session_id) {
        if (const auto rm_count = m_sessions.erase(session_id); rm_count != 0) {
            std::cout << "客户端 #" << session_id << " 已移出在线表，当前在线："<< m_sessions.size() << std::endl;
        }
    }
}
