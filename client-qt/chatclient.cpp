#include "chatclient.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

// ==================== 模块：协议常量与类型校验 ====================
constexpr int kHeaderLength = 8;
constexpr quint16 kMagic = 0x4348;
constexpr quint8 kVersion = 1;

constexpr quint8 kChatType = 1;
constexpr quint8 kPingType = 2;
constexpr quint8 kPongType = 3;
constexpr quint8 kErrorType = 4;
constexpr quint8 kAuthType = 5;
constexpr quint8 kChatAckType = 6;

constexpr quint32 kMaxBodyLength = 1024;

// 功能：判断原始 type 值是否属于当前客户端支持的协议消息类型。
bool isKnownType(const quint8 type) {
    return type >= kChatType && type <= kChatAckType;
}

} // 匿名命名空间结束

// ==================== 模块：生命周期 ====================
// 功能：创建套接字和连接计时器，并完成所有内部信号槽连接。
ChatClient::ChatClient(QObject* parent)
    : QObject(parent),
      m_socket(this),
      m_connect_timer(this) {
    m_connect_timer.setSingleShot(true);
    connectSlots();
}

// ==================== 模块：连接与认证对外接口 ====================
// 功能：保存令牌、重置接收缓存，并连接本机 9000 端口的聊天服务器。
// 失败：令牌为空时直接通知认证失败；连接失败或超时由 Socket 和计时器事件通知。
void ChatClient::connectWithToken(const QString& token) {
    if (token.isEmpty()) {
        emit authFailed("登录响应中没有 token");
        return;
    }

    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_state = AuthState::idle;
        m_socket.abort();
    }

    m_token = token;
    m_received_buffer.clear();
    m_state = AuthState::connecting;
    m_connect_timer.start(5000);

    m_socket.connectToHost(QHostAddress::LocalHost, 9000);
}

// 功能：停止连接超时检查，并请求 Socket 在已发送数据处理完成后正常断开。
void ChatClient::disconnectFromServer() {
    m_connect_timer.stop();
    m_state = AuthState::idle;
    m_socket.disconnectFromHost();
}

// 功能：根据认证状态机返回当前连接是否已通过服务器认证。
bool ChatClient::isAuthenticated() const {
    return m_state == AuthState::authenticated;
}

// ==================== 模块：聊天发送对外接口 ====================
// 功能：创建包含接收者、正文、local_id 和协调世界时发送时间的聊天帧并写入套接字。
// 失败：未认证、必要字段为空或写入失败时通知 local_id 对应的聊天消息失败。
void ChatClient::sendChatMessage(const QString& to, const QString& content, const QString& local_id) {
    const QString actual_local_id = local_id.trimmed().isEmpty()
                                        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                        : local_id;
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
    QJsonObject object;
    object["to"] = normalized_to;
    object["content"] = content;
    object["local_id"] = actual_local_id;
    object["send_at"] = send_at.toString(Qt::ISODateWithMs);
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QString error;
    if (!writeFrame(kChatType, body, error)) {
        emit chatSendFailed(actual_local_id, error);
        return;
    }

    emit chatMessageQueued(normalized_to, content, actual_local_id, send_at);
}

// ==================== 模块：Socket 事件处理 ====================
// 功能：TCP 连接成功后停止连接计时器，并立即发送认证帧。
void ChatClient::onSocketConnected() {
    m_connect_timer.stop();
    sendAuthFrame();
}

// 功能：读取套接字当前所有可用数据，并尝试从缓存中解析完整帧。
void ChatClient::onSocketReady() {
    m_received_buffer.append(m_socket.readAll());
    processReceivedFrames();
}

// 功能：根据错误发生前的认证状态向界面发出连接失败、认证失败或断开通知。
void ChatClient::onSocketError(const QAbstractSocket::SocketError socket_error) {
    if (socket_error == QAbstractSocket::RemoteHostClosedError) {
        return;
    }

    m_connect_timer.stop();
    const AuthState old_state = m_state;
    m_state = AuthState::idle;

    if (old_state == AuthState::waitingAuthResult) {
        emit authFailed("认证连接异常：" + m_socket.errorString());
    } else if (old_state == AuthState::authenticated) {
        emit disconnected();
    } else {
        emit connectionFailed(m_socket.errorString());
    }
}

// 功能：根据断开前状态通知认证失败、连接建立前断开或已认证连接断开。
void ChatClient::onSocketDisconnected() {
    m_connect_timer.stop();
    const AuthState old_state = m_state;
    m_state = AuthState::idle;

    if (old_state == AuthState::waitingAuthResult) {
        emit authFailed("服务器在认证前断开连接");
    } else if (old_state == AuthState::authenticated) {
        emit disconnected();
    } else if (old_state == AuthState::connecting) {
        emit connectionFailed("服务端在连接建立前断开了连接");
    }
}

// ==================== 模块：初始化与连接辅助 ====================
// 功能：将套接字和连接超时计时器的事件连接到对应处理函数。
void ChatClient::connectSlots() {
    connect(&m_socket, &QTcpSocket::connected, this, &ChatClient::onSocketConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &ChatClient::onSocketReady);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &ChatClient::onSocketError);
    connect(&m_socket, &QTcpSocket::disconnected, this, &ChatClient::onSocketDisconnected);

    connect(&m_connect_timer, &QTimer::timeout, this,
            // 功能：仅在连接阶段中止未完成的连接，并通知界面连接超时。
            [this] {
                if (m_state != AuthState::connecting) {
                    return;
                }
                m_socket.abort();
                m_state = AuthState::idle;
                emit connectionFailed("连接 chat-server 超时");
            });
}

// ==================== 模块：协议帧编码与发送 ====================
// 功能：将 type 和正文按大端序编码为聊天服务器使用的完整协议帧。
QByteArray ChatClient::makeFrame(const quint8 type, const QByteArray& body) {
    QByteArray frame;
    const auto length = static_cast<quint32>(body.size());

    frame.reserve(kHeaderLength + body.size());
    frame.append(static_cast<char>(kMagic >> 8));
    frame.append(static_cast<char>(kMagic & 0xFF));
    frame.append(static_cast<char>(kVersion));
    frame.append(static_cast<char>(type));
    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));
    frame.append(body);

    return frame;
}

// 功能：校验正文长度和连接状态后，将完整协议帧写入 TCP 发送缓冲区。
// 失败：正文超长、套接字未连接或写入失败时返回 false，并写入错误输出参数。
bool ChatClient::writeFrame(const quint8 type, const QByteArray& body, QString& error) {
    error.clear();
    if (body.size() > static_cast<int>(kMaxBodyLength)) {
        error = QStringLiteral("消息体超过协议允许的长度");
        return false;
    }
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        error = QStringLiteral("TCP 尚未连接");
        return false;
    }
    if (m_socket.write(makeFrame(type, body)) == -1) {
        error = m_socket.errorString();
        return false;
    }

    return true;
}

// 功能：将保存的令牌作为认证帧正文发送，并转入等待认证响应状态。
// 失败：认证帧无法写入时重置状态并通知认证失败。
void ChatClient::sendAuthFrame() {
    const QByteArray body = m_token.toUtf8();
    QString error;
    if (!writeFrame(kAuthType, body, error)) {
        m_state = AuthState::idle;
        emit authFailed(QStringLiteral("认证帧发送失败：") + error);
        return;
    }

    m_state = AuthState::waitingAuthResult;
    emit authFrameSent();
}

// 功能：在认证完成后发送心跳请求；写入失败时中止套接字以触发统一断开处理。
void ChatClient::sendPing() {
    if (m_state != AuthState::authenticated) {
        return;
    }

    QString error;
    if (!writeFrame(kPingType, {}, error)) {
        m_socket.abort();
    }
}

// ==================== 模块：收帧与类型分派 ====================
// 功能：从接收缓存中循环解析完整帧，支持 TCP 半包和一次收到多帧的情况。
// 失败：帧头不符合协议时清空认证状态、通知认证失败并断开连接。
void ChatClient::processReceivedFrames() {
    while (m_received_buffer.size() >= kHeaderLength) {
        const auto* header =
            reinterpret_cast<const unsigned char*>(m_received_buffer.constData());
        const auto magic = static_cast<quint16>(header[0] << 8 | header[1]);
        const quint8 version = header[2];
        const quint8 type = header[3];
        const auto body_length = static_cast<quint32>(
            header[4] << 24 | header[5] << 16 | header[6] << 8 | header[7]);

        if (magic != kMagic || version != kVersion ||
            !isKnownType(type) || body_length > kMaxBodyLength) {
            m_state = AuthState::idle;
            emit authFailed("收到非法聊天协议帧");
            m_socket.disconnectFromHost();
            return;
        }

        const int frame_length = kHeaderLength + static_cast<int>(body_length);
        if (m_received_buffer.size() < frame_length) {
            return;
        }

        const QByteArray body = m_received_buffer.mid(kHeaderLength, body_length);
        m_received_buffer.remove(0, frame_length);
        dispatchFrame(type, body);
    }
}

// 功能：按协议消息类型分派正文，避免将认证和心跳正文错误当作聊天 JSON 解析。
void ChatClient::dispatchFrame(const quint8 type, const QByteArray& body) {
    switch (type) {
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

// ==================== 模块：各类型正文处理 ====================
// 功能：解析 type=5 认证响应；认证成功后更新状态并通知界面。
// 失败：正文不是成功 JSON 时断开连接并通知认证失败。
void ChatClient::handleAuthBody(const QByteArray& body) {
    if (m_state != AuthState::waitingAuthResult) {
        return;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value("ok").toBool()) {
        m_state = AuthState::idle;
        emit authFailed(QStringLiteral("认证响应格式错误"));
        m_socket.disconnectFromHost();
        return;
    }

    m_state = AuthState::authenticated;
    emit authSucceeded();
}

// 功能：解析 type=1 聊天正文，并将完整的消息字段转为界面可用的信号。
// 失败：JSON 或必要字段不合法时通知普通服务端错误。
void ChatClient::handleChatBody(const QByteArray& body) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QStringLiteral("聊天信息 JSON 格式错误"));
        return;
    }

    const QJsonObject object = document.object();
    const QString local_id = object.value("local_id").toString();
    const QString from = object.value("from").toString();
    const QString to = object.value("to").toString();
    const QString content = object.value("content").toString();
    if (local_id.isEmpty() || from.isEmpty() || to.isEmpty() || content.isEmpty()) {
        emit serverError(QStringLiteral("聊天信息缺少必要字段"));
        return;
    }

    const QDateTime send_at =
        QDateTime::fromString(object.value("send_at").toString(), Qt::ISODate);
    emit chatMessageReceived(local_id, from, to, content, send_at);
}

// 功能：解析 type=4 错误正文，并按 local_id 或认证状态转换为对应错误信号。
void ChatClient::handleErrorBody(const QByteArray& body) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QString::fromUtf8(body));
        return;
    }

    const QJsonObject object = document.object();
    const QString local_id = object.value("local_id").toString();
    const QString reason = object.value("message").toString(QStringLiteral("服务器返回错误"));
    if (!local_id.isEmpty()) {
        emit chatSendFailed(local_id, reason);
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

// 功能：收到 type=2 心跳请求后原样发送 type=3 心跳响应。
void ChatClient::handlePingBody(const QByteArray& body) {
    QString error;
    if (!writeFrame(kPongType, body, error)) {
        m_socket.abort();
    }
}

// 功能：接收 type=3 心跳响应；当前版本不需要保存额外状态。
void ChatClient::handlePongBody(const QByteArray& body) {
    Q_UNUSED(body);
}

// 功能：解析 type=6 聊天确认正文，并通知界面 local_id 对应消息已被服务器接受。
// 失败：JSON 或确认字段不合法时通知普通服务端错误。
void ChatClient::handleChatAckBody(const QByteArray& body) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        emit serverError(QStringLiteral("聊天确认消息格式错误"));
        return;
    }

    const QJsonObject object = document.object();
    const QString local_id = object.value("local_id").toString();
    const QString status = object.value("status").toString();
    if (local_id.isEmpty() || status != QStringLiteral("accepted")) {
        emit serverError(QStringLiteral("聊天确认消息字段错误"));
        return;
    }

    emit chatMessageAccepted(local_id);
}
