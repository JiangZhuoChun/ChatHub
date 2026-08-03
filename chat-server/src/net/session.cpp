#include "net/session.h"

#include <iostream>

namespace net {
    Session::Session(asio::ip::tcp::socket  socket) :
    m_socket( std::move(socket)),
    m_strand(m_socket.get_executor())
    {
    }
    void Session::log(std::string_view  event) {
        std::cout << event << std::endl;
    }

    void Session::close() {
        m_socket.close();
    }

    void Session::start() {
        auto self = shared_from_this();
        asio::post(m_strand,[self] {
            self->doRead();
        });
    }
    void Session::doRead() {
        auto self = shared_from_this();
        m_socket.async_read_some(asio::buffer(m_read_buffer),
            asio::bind_executor(m_strand,[self, this](std::error_code error,std::size_t bytes_transferred)
            {
                if (error) {
                    if (error == asio::error::eof) {
                        self->log("正常断开连接");
                    }
                    else if (error == asio::error::operation_aborted) {
                        return;
                    }
                    else {
                        std::cerr << "错误：" << error.message() << std::endl;
                    }
                    self->close();
                    return;
                }
                const auto result = m_decoder.append(m_read_buffer.data(),bytes_transferred,
                    [self](const protocol::Message &message) {
                        self->handleMessage(std::move(message));
                    });
                if (result != protocol::DecodeResult::ok) {
                    self->log("协议错误，关闭当前连接");
                    self->close();
                    return;
                }
                self->doRead();
            }));
    }

    void Session::send(protocol::MessageType type, std::string body) {
        auto self = shared_from_this();
        asio::post(m_strand,[self,type, body = std::move(body)] {
            self->doSend(type,body);
        });
    }
    void Session::doSend(protocol::MessageType type, const std::string& body) {
        if (body.size() > protocol::FrameDecoder::kMaxBodyLength) {
            std::cerr << "错误：body长度超出限制" << std::endl;
            return;
        }
        // 只有队列从空变为非空时才启动一次 async_write，避免同一 socket 并发写。
        //设置最大发送队列为3
        const bool was_empty = m_write_queue.empty();
        constexpr std::size_t kMaxWriteQueueSize = 3;
        if (m_write_queue.size() >= kMaxWriteQueueSize) {
            std::cout << "错误：发送队列已满：" << m_write_queue.size() << "/" << kMaxWriteQueueSize
                    << "关闭慢客户端" << std::endl;
            close();
            return;
        }
        m_write_queue.push_back({type,protocol::makeFrame(type,body)});
        if (was_empty) {
            doWrite();
        }
    }

    void Session::doWrite() {
        if (m_write_queue.empty()) {
            return;
        }
        auto self = shared_from_this();
        asio::async_write(m_socket,asio::buffer(m_write_queue.front().frame),
            asio::bind_executor(m_strand,[self, this](std::error_code error,std::size_t bytes_transferred) {
                if ( error) {
                    std::cerr << "错误,发送失败" << error.message() << std::endl;
                    self->close();
                    return;
                }
                const auto send_type = m_write_queue.front().type;
                m_write_queue.pop_front();

                std::cout << "发送成功：" << bytes_transferred << "字节" << std::endl;

                self->doWrite();
            }));
    }

    void Session::handleMessage(const protocol::Message&  message) {
        switch (message.type) {
            case protocol::MessageType::chat:
                send(protocol::MessageType::chat,message.body);
                break;
            case protocol::MessageType::ping:
                std::cout << "收到ping" << std::endl;
                send(protocol::MessageType::pong,message.body);
                break;
            case protocol::MessageType::pong:
                std::cout << "收到pong" << std::endl;
                break;
            case protocol::MessageType::error:
                std::cerr << "错误：" << message.body << std::endl;
                break;
        }

    }

}

