#pragma once
#include "protocol/frame_decoder.h"
#include <asio.hpp>
#include <deque>
namespace net {

class Session;

//功能::写队列项：一条完整帧（编码后）+ 它的类型
struct WriteItem {
    protocol::MessageType type;   //功能::消息类型
    std::string frame;            //功能::编码好的完整帧
};

//功能::类型别名
using SessionId = uint32_t;                    //功能::连接 ID
using SessionPtr = std::shared_ptr<Session>;   //功能::Session 共享指针
//功能::消息回调：Session 收到完整消息后调用，把消息交给 Server 处理
using MessageCallback = std::function<void(SessionId,protocol::Message)>;
//功能::断开回调：Session 关闭后调用，通知 Server 从在线表移除该连接
using DisconnectCallback = std::function<void(const SessionId)>;

    //功能::Session：一个 Session 只管理一个客户端 socket（读、解码、写、生命周期）
class Session : public std::enable_shared_from_this<Session> {
public:
    //功能::构造函数：接收 socket、连接 ID、消息回调、断开回调
    Session(asio::ip::tcp::socket,SessionId session_id, MessageCallback on_message,DisconnectCallback on_disconnect);

    //功能::启动：投递第一个异步读取
    void start();
    //功能::发送消息：任意线程可调用，入队后异步写
    void send(protocol::MessageType type, std::string body);
private:
    //功能::以下函数只在 m_strand 中调用，安全访问本 Session 的可变状态
    void doRead();   //功能::异步读取：读→解码→处理
    void enqueueAndWrite(protocol::MessageType type, const std::string& body);   //功能::编码并入队，必要时启动写
    void writeFrame();   //功能::写队列中的下一条帧

    //功能::业务消息处理：chat 上交 Server，ping/pong/error 留在当前连接处理
    void processMessage(const protocol::Message&  message);

    //功能::关闭连接：幂等，只通知 Server 一次
    void close();
    //功能::日志：输出事件信息，自动携带上下文
    static void log(std::string_view event);
    //功能::成员变量
    asio::ip::tcp::socket m_socket;   //功能::客户端 socket
    asio::strand<asio::any_io_executor> m_strand;   //功能::串行通道，保护本连接状态
    protocol::FrameDecoder m_decoder;   //功能::帧解码器（半包/粘包）
    std::array<char, 1024> m_read_buffer{};   //功能::读缓冲
    std::deque<WriteItem> m_write_queue;   //功能::写队列（待发送的完整帧）

    //验证函数
    static bool verifyJwt(const std::string& token,std::string& out_username);

    bool m_disconnected {false};   //功能::是否已断开（保证只通知 Server 一次）
    MessageCallback m_on_message;   //功能::消息回调
    SessionId m_id;   //功能::连接 ID
    DisconnectCallback m_on_disconnect;   //功能::断开回调

    bool m_authenticated{false}; //功能::是否已认证
    std::string m_username;// 认证后存的用户名



    static constexpr std::size_t kMaxWriteQueueSize = 3;

};
}
