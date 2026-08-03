#pragma once
#include "protocol/frame_decoder.h"

#include <asio.hpp>
#include <deque>

namespace net {

class Session;

// 写队列里的一项：一条完整帧（编码后）+ 它的类型
struct WriteItem {
    protocol::MessageType type;
    std::string frame;
};

// Session 收到 chat 后通知 Server；Server 决定是否广播及如何路由。
using SessionId = uint32_t;
using SessionPtr = std::shared_ptr<Session>;
// 消息回调：Session 收到完整消息后调用，把消息交给 Server 处理
using MessageCallback = std::function<void(SessionId,protocol::Message)>;

// 断开回调：Session 关闭后调用，通知 Server 从在线表移除该连接
using DisconnectCallback = std::function<void(const SessionId)>;

    //一个Session只管理一个客户端socket:读，解码，写，连接生命周期
class Session : public std::enable_shared_from_this<Session> {
public:
    //构造函数
    Session(asio::ip::tcp::socket,SessionId session_id, MessageCallback on_message,DisconnectCallback on_disconnect);

    //启动Session
    void start();
    void send(protocol::MessageType type, std::string body);
private:
    //只在m_strand中调用，安全访问本Session的可变状态
    void doRead();                  // 异步读取
    void enqueueAndWrite(protocol::MessageType type, const std::string& body);  // 编码并入队，启动写
    void writeFrame();              // 写队列中的下一条帧

    //信息处理，chat上交server处理，ping、pong、error留在当前连接处理
    void processMessage(const protocol::Message&  message);



    void close();
    static void log(std::string_view event);
    asio::ip::tcp::socket m_socket;
    asio::strand<asio::any_io_executor> m_strand;
    protocol::FrameDecoder m_decoder;
    //读缓冲
    std::array<char, 1024> m_read_buffer{};
    //写队列（待发送的完整帧）
    std::deque<WriteItem> m_write_queue;

    //是否已断开（保证只通知 Server 一次）
    bool m_disconnected {false};
    MessageCallback m_on_message;
    SessionId m_id;
    DisconnectCallback m_on_disconnect;

};
}
