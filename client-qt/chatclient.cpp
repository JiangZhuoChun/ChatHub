#include "chatclient.h"
#include <QHostAddress>
#include <QJsonObject>
namespace {
    constexpr int kHeaderLength = 8;
    constexpr quint16 kMagic = 0x4348;//"CH"
    constexpr quint8 kVersion = 1;

    constexpr quint8 kChatType = 1;
    constexpr quint8 kPingType = 2;
    constexpr quint8 kPongType = 3;
    constexpr quint8 kErrorType = 4;
    constexpr quint8 kAuthType = 5;
    constexpr quint8 kChatAckType = 6;
    constexpr quint32 kMaxBodyLength = 1024;
}

bool isKnownType(const quint8  type) {
    return type >= 1 && type <= 6;
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

void ChatClient::sendChatMessage(const QString &to, const QString &content, const QString &local_id) {
    // local_id 只属于 chat body，用于服务端错误响应、确认响应和 UI 重试关联。
    //默认 UUID 带大括号：{550e8400-e29b-41d4-a716-446655440000}，WithoutBraces 无大括号
    const QString actual_local_id = local_id.trimmed().isEmpty()
    ? QUuid::createUuid().toString(QUuid::WithoutBraces) : local_id;

    const QString normalized_to = to.trimmed();

    if (m_state != AuthState::authenticated) {
        emit chatSendFailed(actual_local_id, "未认证");
        return;
    }
    if (normalized_to.isEmpty() || content.trimmed().isEmpty()) {
        emit chatSendFailed(actual_local_id, "消息不能为空");
        return;
    }

    const QDateTime send_at = QDateTime::currentDateTimeUtc();
    QJsonObject obj;
    obj["to"] = normalized_to;
    obj["content"] = content;
    obj["local_id"] = actual_local_id;
    obj["send_at"] = send_at.toString(Qt::ISODateWithMs);
    //Compact（压缩模式）无换行、无空格，紧凑单行，减少网络流量：
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (QString error; !writeFrame(kChatType, body, error))
    {
       emit chatSendFailed(actual_local_id,error);
       return;
    }

    emit chatMessageQueued(normalized_to, content, actual_local_id,send_at);
}

//槽函数实现
void ChatClient::onSocketConnected() {
    m_connect_timer.stop();
    sendAuthFrame();
}

QByteArray ChatClient::makeFrame(const quint8 type, const QByteArray &body) {
    QByteArray frame;
    const auto length = static_cast<quint32>(body.size());

    frame.reserve(kHeaderLength + body.size());
    frame.append(static_cast<char>(kMagic >> 8));   //[0x43] 高字节
    frame.append(static_cast<char>(kMagic & 0xFF)); //[0x48] 低字节
    frame.append(static_cast<char>(kVersion));// [0x01]
    frame.append(static_cast<char>(type));
    //大端存储

    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(body);

    return frame;
}

bool ChatClient::writeFrame(quint8 type, const QByteArray &body, QString &error) {
    // 所有业务帧共用这里的协议头组装结果；不同 type 的 body 由上层分别构造。
    error.clear();
    if (body.size() > static_cast<int>(kMaxBodyLength)) {
        error = QStringLiteral("消息体超过协议允许的长度");
        return false;
    }
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        error = QStringLiteral("TCP尚未连接");
        return false;
    }
    const QByteArray frame = makeFrame(type, body);
    if (m_socket.write(frame) == -1) {
        error = m_socket.errorString();
        return false;
    }
    return true;
}
void ChatClient::sendAuthFrame() {
    const QByteArray body = m_token.toUtf8();

    if (QString error; !writeFrame(kAuthType, body, error))
    {
        m_state = AuthState::idle;
        emit authFailed(QStringLiteral("认证帧发送失败:") + error);
        return;
    }

    m_state = AuthState::waitingAuthResult;
    emit authFrameSent();
}

void ChatClient::sendPing() {
    if (m_state != AuthState::authenticated) {
        return;
    }
    if (QString error; !writeFrame(kPingType, QByteArray(), error)) {
        // 心跳失败不属于聊天消息失败
        // 让 socket 的错误/断开信号处理连接状态
        m_socket.abort();
    }
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

        dispatchFrame(type,body);
    }
}

void ChatClient::onSocketError(const QAbstractSocket::SocketError socket_error) {
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

void ChatClient::dispatchFrame(const quint8 type, const QByteArray &body) {
    // 先按 type 分派，再由对应 handler 解释 body，避免把心跳/认证当作聊天 JSON。
    switch (type)
    {
        case kAuthType:
            handleAuthBody(body);
            break;
        case kPingType:
            handlePingBody(body);
            break;
        case kChatType:
            handleChatBody(body);
            break;
        case kErrorType:
            handleErrorBody(body);
            break;
        case kPongType:
            handlePongBody(body);
            break;
        case kChatAckType:
            handleChatAckBody(body);
            break;
        default:
            emit serverError(QStringLiteral("收到未知消息类型"));
            break;
    }
}

void ChatClient::handleAuthBody(const QByteArray &body) {
    if (m_state != AuthState::waitingAuthResult) {
        return;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body,&parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value("ok").toBool())
    {
        m_state = AuthState::idle;
        emit authFailed(QStringLiteral("认证响应格式错误"));
        m_socket.disconnectFromHost();
        return;
    }
    m_state = AuthState::authenticated;
    emit authSucceeded();
}

void ChatClient::handleChatBody(const QByteArray &body) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body,&parse_error);

    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QStringLiteral("聊天信息JSON格式错误"));
        return;
    }
    const QJsonObject obj = document.object();
    const QString local_id = obj.value("local_id").toString();
    const QString from = obj.value("from").toString();
    const QString to = obj.value("to").toString();
    const QString content = obj.value("content").toString();

    if (local_id.isEmpty() || from.isEmpty() || to.isEmpty() || content.isEmpty()) {
        emit serverError(QStringLiteral("聊天信息必要缺少字段"));
        return;
    }

    const QDateTime send_at = QDateTime::fromString(obj.value("send_at").toString(),Qt::ISODate);
    emit chatMessageReceived(local_id,from,to,content,send_at);
}

void ChatClient::handleErrorBody(const QByteArray &body) {
    QJsonParseError parse_error;
    const QJsonDocument document =QJsonDocument::fromJson(body, &parse_error);

    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QString::fromUtf8(body));
        return;
    }
    const QJsonObject ojb = document.object();
    const QString local_id = ojb.value("local_id").toString();
    const QString reason = ojb.value("message").toString(QStringLiteral("服务器返回错误"));

    if (!local_id.isEmpty()) {
        emit chatSendFailed(local_id,reason);
        return;
    }
    if (m_state == AuthState::waitingAuthResult) {
        m_state = AuthState::idle;
        emit authFailed(reason);
        m_socket.disconnectFromHost();
        return;
    }
    emit serverError(reason);
}

void ChatClient::handlePingBody(const QByteArray &body) {
    if (QString error; !writeFrame(kPongType,body,error)) {
        m_socket.abort();
    }
}

void ChatClient::handlePongBody(const QByteArray &body) {
    Q_UNUSED(body);
}

void ChatClient::handleChatAckBody(const QByteArray &body) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body,&parse_error);

    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QStringLiteral("聊天确认消息格式错误"));
        return;
    }
    const QJsonObject obj = document.object();
    const QString local_id = obj.value("local_id").toString();
    const QString status = obj.value("status").toString();
    if (local_id.isEmpty() || status != QStringLiteral("accepted")) {
        emit serverError(QStringLiteral("聊天确认消息字段错误"));
        return;
    }
    emit chatMessageAccepted(local_id);
}
