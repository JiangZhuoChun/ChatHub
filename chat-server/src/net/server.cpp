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
            if ( !error) {
                std::cout << "接受连接成功：" << m_pending_socket.remote_endpoint() << std::endl;
                // ① 创建 Session，把 socket 所有权移交给它
                //Session 继承 enable_shared_from_this，它必须由 shared_ptr 管理，才能 shared_from_this()
                auto session = std::make_shared<Session>(std::move(m_pending_socket));
                session->start();
                // ② 重新构造一个空 socket，供下次 accept 使用
                m_pending_socket = asio::ip::tcp::socket(m_acceptor.get_executor());
            }
            else {
                std::cerr << "接受连接失败：" << error.message();
            }
        // ③ 无论成功失败，继续接受下一个连接
        doAccept();
        }));
    }
}
