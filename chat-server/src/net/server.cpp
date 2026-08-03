#include "net/server.h"

#include <iostream>

namespace net {
    Server::Server(asio::io_context &io_context, std::uint16_t port):
    m_strand(asio::make_strand(io_context)),
    m_acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
    m_pending_socket(io_context)
    {
    }

    void Server::start() {
        std::cout << "正在监听端口：" << m_acceptor.local_endpoint() << std::endl;
        doAccept();
    }
    void Server::doAccept() {
        // accept 完成回调进入 Server strand，在线表登记与下一次 accept 都在同一通道中执行。
        m_acceptor.async_accept(m_pending_socket,asio::bind_executor(m_strand,
            [this](const std::error_code error) {
            if ( error == asio::error::operation_aborted) {
                return;
            }
            if (error){
                std::cerr << "接受连接失败：" << error.message();
            }
            else {
                const SessionId session_id = m_next_session_id++;
                std::error_code endpoint_error;
                const auto endpoint = m_pending_socket.remote_endpoint(endpoint_error);
                if (endpoint_error) {
                    std::cerr << "客户端#" << session_id << "地址读取失败：" << endpoint_error.message() << std::endl;
                }
                else {
                    std::cout << "客户端#" << session_id << "已连接：" << endpoint << std::endl;
                }

                // 回调会在 Session strand 触发，因此必须 post 回 Server strand 再访问在线表。
                auto on_message = [this](SessionId sender_id, protocol::Message message) {
                    std::cout << ">>> on_message 被调用了! sender=" << sender_id << std::endl;
                    asio::post(m_strand,[this, sender_id, message = std::move(message)]() {
                        onSessionMessage(sender_id, std::move(message));
                    });
                };
                auto on_disconnect = [this](SessionId session_id) {
                  asio::post(m_strand,[this, session_id]() {
                     removeSession(session_id);
                  });
                };
                // Server 持有一份 shared_ptr；Session 的异步回调也会暂时持有自己。
                auto session = std::make_shared<Session>(
                    std::move(m_pending_socket),
                    session_id,
                    std::move(on_message),
                    std::move(on_disconnect));
                addSession(session_id, session);
                session->start();
            }
        m_pending_socket = asio::ip::tcp::socket(m_acceptor.get_executor());
        // ③ 无论成功失败，继续接受下一个连接
        doAccept();
        }));
    }

    void Server::onSessionMessage(SessionId sender_id, const protocol::Message& message) {
        std::cout << "客户端#" << sender_id << "发送：" << message.body << std::endl;
        broadcast(sender_id,message);
    }
    void Server::broadcast(SessionId sender_id,const protocol::Message& message) {
        for (auto& [id, session] : m_sessions) {
            if (id != sender_id) {
                session->send(message.type,message.body);
            }
        }
    }
    void Server::addSession(SessionId session_id, const SessionPtr& session) {
        m_sessions.emplace(session_id, session);
        std::cout << "客户端#" << session_id << "已登记,当前在线:" << m_sessions.size() << std::endl;
    }
    void Server::removeSession(SessionId session_id) {
        if (const auto rm_count = m_sessions.erase(session_id); rm_count != 0) {
            std::cout << "客户端 #" << session_id << " 已移出在线表，当前在线："<< m_sessions.size() << std::endl;
        }
    }
}
