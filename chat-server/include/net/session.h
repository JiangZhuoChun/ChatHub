#pragma once
#include "protocol/frame_decoder.h"

#include <asio.hpp>
#include <deque>

namespace net {
struct PendingWrite {
    protocol::MessageType type;
    std::string frame;

};
    //一个Session只管理一个客户端socket:读，解码，写，连接生命周期
class Session : public std::enable_shared_from_this<Session> {
public:
    //构造函数
    Session(asio::ip::tcp::socket);

    //启动Session
    void start();
    void send(protocol::MessageType type, std::string body);
private:
    //只在m_strand中调用，安全访问本Session的可变状态
    void doRead();
    void doWrite();
    void doSend(protocol::MessageType type, const std::string& body);

    //信息处理，chat上交server处理，ping、pong、error留在当前连接处理
    void handleMessage(const protocol::Message&  message);

    void close();
    static void log(std::string_view event);
    asio::ip::tcp::socket m_socket;
    asio::strand<asio::any_io_executor> m_strand;
    protocol::FrameDecoder m_decoder;
    //读缓冲
    std::array<char, 1024> m_read_buffer{};
    //写队列
    std::deque<PendingWrite> m_write_queue;

};
}
