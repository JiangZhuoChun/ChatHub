#include "chatclient.h"
#include <QHostAddress>
namespace {
    constexpr int kHeaderLength = 8;
    constexpr  quint16 kMagic = 0x4348;//"CH"
    constexpr  quint8 kVersion = 1;
    constexpr  quint8 kAuthType = 5;
    constexpr  quint8 kErrorType = 4;
    constexpr  quint32 kMaxBodyLength = 1024;
}

bool isKnownType(quint8  type) {
    return type >= 1 && type <= 5;
}

ChatClient::ChatClient(QObject *parent) :
    QObject(parent),
    m_socket(this),
    m_connect_timer(this)
{
    m_connect_timer.setSingleShot(true);

    connectSlots();

}

void ChatClient::connectSlots() {
    connect(&m_socket,&QTcpSocket::connected,this,&ChatClient::onSocketConnected);

    connect(&m_socket,&QTcpSocket::readyRead,this,&ChatClient::onSocketReady);

    connect(&m_socket,&QTcpSocket::errorOccurred,this,&ChatClient::onSocketError);

    connect(&m_socket,&QTcpSocket::disconnected,this,&ChatClient::onSocketDisconnected);

    connect(&m_connect_timer,&QTimer::timeout,this,[this] {
        if (m_state != AuthState::connecting) {
            return;
        }
        m_socket.abort();
        m_state = AuthState::idle;
        emit connectionFailed("连接 chat-server超时");
    });
}
void ChatClient::connectWithToken(const QString &token) {
    if (token.isEmpty()) {
        emit authFailed("登录响应中没有 token");
        return;
    }
    //如果有旧连接存在，先中止
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_state = AuthState::idle;
        m_socket.abort();
    }
    //初始化
    m_token = token;
    m_received_buffer.clear();
    m_state = AuthState::connecting;

    m_connect_timer.start(5000);//5秒超时

    // 使用 IPv4，避免 localhost 被解析为 ::1，
    // 而 chat-server 只监听 IPv4 的情况。
    m_socket.connectToHost(QHostAddress::LocalHost,9000);
}

void ChatClient::disconnectFromServer() {
    m_connect_timer.stop();
    m_state = AuthState::idle;
    m_socket.disconnectFromHost();//优雅断开，等待数据发完再断
}

bool ChatClient::isAuthenticated() const {
    return m_state == AuthState::authenticated;
}

//槽函数实现
void ChatClient::onSocketConnected() {
    m_connect_timer.stop();
    sendAuthFrame();
}
void ChatClient::sendAuthFrame() {
    const QByteArray body = m_token.toUtf8();
    if (body.size() > static_cast<int>(kMaxBodyLength)) {
        m_state = AuthState::idle;
        emit authFailed("token超过协议允许的1024字节");
        m_socket.disconnectFromHost();
        return;
    }
    QByteArray frame;
    frame.reserve(kHeaderLength + body.size());
    frame.append(static_cast<char>(kMagic >> 8));   //[0x43] 高字节
    frame.append(static_cast<char>(kMagic & 0xFF)); //[0x48] 低字节
    frame.append(static_cast<char>(kVersion));// [0x01]
    frame.append(static_cast<char>(kAuthType));// [0x05]
    //大端存储
    const auto length = static_cast<quint32>(body.size());
    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(body);

    if (m_socket.write(frame) == -1) {
        m_state = AuthState::idle;
        emit authFailed("发送认证帧失败" + m_socket.errorString());
        return;
    }
    m_state = AuthState::waitingAuthResult;
    emit authFrameSent();
}

void ChatClient::onSocketReady() {
    m_received_buffer.append(m_socket.readAll());
    processReceivedFrames();

}
void ChatClient::processReceivedFrames() {
    while (m_received_buffer.size() >= kHeaderLength) {
        const auto* header =
            reinterpret_cast<const unsigned char*>(m_received_buffer.constData());
        const auto magic = static_cast<quint16> (header[0] << 8 | header[1]);
        const quint8  version = header[2];
        const quint8  type = header[3];
        const auto body_length = static_cast<quint32>(
            header[4] << 24 | header[5] << 16 | header[6] << 8 | header[7]);

        if (magic != kMagic || version != kVersion ||
            !isKnownType(type) || body_length > kMaxBodyLength) {
            m_state = AuthState::idle;
            emit authFailed("收到非法聊天协议帧");
            m_socket.disconnectFromHost();
            return;
        }
        const int frame_length = kHeaderLength + static_cast<int> (body_length);
        if ( m_received_buffer.size() < frame_length) {
            return;//帧不完整,半包等待下一次readyRead
        }
        const QByteArray body = m_received_buffer.mid(kHeaderLength, body_length);
        m_received_buffer.remove(0, frame_length);

        if (type == kAuthType && m_state == AuthState::waitingAuthResult) {
            m_state = AuthState::authenticated;
            emit authSucceeded();
            continue;
        }
        if (type == kErrorType) {
            m_state = AuthState::idle;
            const QString reason = body.isEmpty() ? "服务器拒绝认证" : QString::fromUtf8(body);
            emit authFailed(reason);
            m_socket.disconnectFromHost();
            return;
        }
        // 后续聊天消息、ping/pong 会在这里继续分派。

    }
}

void ChatClient::onSocketError(QAbstractSocket::SocketError socket_error) {
    if (socket_error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }
    m_connect_timer.stop();
    const AuthState old_state = m_state;
    m_state = AuthState::idle;
    if (old_state == AuthState::waitingAuthResult) {
        emit authFailed("认证连接异常：" + m_socket.errorString());
    }
    else if (old_state == AuthState::authenticated) {
        emit disconnected();
    }
    else {
        emit connectionFailed(m_socket.errorString());
    }
}
void ChatClient:: onSocketDisconnected() {
    m_connect_timer.stop();
    const AuthState old_state = m_state;
    m_state = AuthState::idle;
    if (old_state == AuthState::waitingAuthResult) {
        emit authFailed("服务器在认证前断开连接");
    }
    else if (old_state == AuthState::authenticated) {
        emit disconnected();
    }
    else if (old_state == AuthState::connecting) {
        emit connectionFailed("服务端在连接建立前断开了连接");
    }
}
