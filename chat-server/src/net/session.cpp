#include "net/session.h"

#include <iostream>

namespace net {
    //功能::构造函数：接收 socket、连接 ID、消息回调、断开回调，初始化成员
    Session::Session(
        asio::ip::tcp::socket  socket,
        SessionId session_id,
        MessageCallback on_message,
        DisconnectCallback on_disconnect) :
    m_socket( std::move(socket)),          //功能::转移 socket 所有权给 Session
    m_strand(m_socket.get_executor()),     //功能::用 socket 的执行器创建串行通道
    m_on_message(std::move(on_message)),   //功能::保存消息回调（收到完整消息时调用）
    m_id(session_id),                      //功能::保存连接 ID
    m_on_disconnect(std::move(on_disconnect)) //功能::保存断开回调（断开时通知 Server）
    {
    }
    //功能::日志：统一输出事件信息，方便排查多客户端问题
    void Session::log(std::string_view  event) {
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
            asio::bind_executor(m_strand,[self, this](std::error_code error,std::size_t bytes_transferred)
            {
                if (error) {   //功能::读取出错，按错误类型处理
                    if (error == asio::error::eof) {
                        self->log("正常断开连接");   //功能::客户端正常关闭（EOF）
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
                        self->processMessage(std::move(message));   //功能::把完整消息交给业务处理
                    });
                if (result != protocol::DecodeResult::ok) {   //功能::协议错误（非法magic/超长等）
                    self->log("协议错误，关闭当前连接");
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
    void Session::enqueueAndWrite(protocol::MessageType type, const std::string& body) {
        if (body.size() > protocol::FrameDecoder::kMaxBodyLength) {   //功能::body超长限制
            std::cerr << "错误：body长度超出限制" << std::endl;
            return;
        }
        //功能::只有队列从空变为非空时才启动一次 async_write，避免同一 socket 并发写。
        //功能::设置最大发送队列为3
        const bool was_empty = m_write_queue.empty();
        constexpr std::size_t kMaxWriteQueueSize = 3;
        if (m_write_queue.size() >= kMaxWriteQueueSize) {   //功能::队列满：慢客户端，关闭
            std::cout << "错误：发送队列已满：" << m_write_queue.size() << "/" << kMaxWriteQueueSize
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
            asio::bind_executor(m_strand,[self, this](std::error_code error,std::size_t bytes_transferred) {
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

    //功能::业务消息处理：按消息类型分派
    void Session::processMessage(const protocol::Message&  message) {
        switch (message.type) {
            case protocol::MessageType::chat:   //功能::聊天消息：交给 Server 广播
                m_on_message(m_id, message);
                break;
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
        }

    }

}
