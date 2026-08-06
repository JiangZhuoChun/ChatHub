#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QDateTime>
#include <QTimer>
class ChatClient : public QObject {
    Q_OBJECT
public:
    explicit ChatClient(QObject *parent = nullptr);

    void connectWithToken(const QString &token);
    void disconnectFromServer();
    void connectSlots();
    bool isAuthenticated() const;

    void sendChatMessage(const QString &to, const QString &content,const QString &local_id = {});

signals:
    // TCP 已连接，认证帧已进入发送缓冲区
    void authFrameSent();
    // 收到服务端 type=5 的认证成功帧
    void authSucceeded();
    // 认证阶段收到 error 帧、协议错误或服务端提前断开
    void authFailed(const QString &reason);
    // TCP 尚未建立就失败，例如 chat-server 未启动、端口不通
    void connectionFailed(const QString &reason);
    // 已认证后连接断开
    void disconnected();
    // 聊天消息发送失败
    void chatSendFailed(const QString &local_id, const QString &reason);
    //处理没有 local_id 的普通系统错误
    void serverError(const QString &reason);

    //接收消息
    void chatMessageReceived(const QString &local_id,const QString &from,
        const QString &to,const QString &content, const QDateTime &send_at);
    // 聊天消息已进入发送缓冲区
    void chatMessageQueued(const QString &to, const QString &content,
        const QString &local_id,const QDateTime &send_at);
    //客户端确认
    void chatMessageAccepted(const QString &local_id);


private slots:
    void onSocketConnected();
    void onSocketReady();
    void onSocketError(QAbstractSocket::SocketError socket_error);
    void onSocketDisconnected();

private:
    enum class AuthState {
        idle,//空闲状态，默认初始值
        connecting,//正在建立网络连接
        waitingAuthResult,//网络收到响应，等待解析认证结果。
        authenticated//认证成功，已登录。
    };

    static QByteArray  makeFrame(quint8 type, const QByteArray &body) ;
    bool writeFrame(quint8 type, const QByteArray &body, QString &error);
    void sendAuthFrame();
    void sendPing();
    void processReceivedFrames();

    void dispatchFrame(quint8 type, const QByteArray &body);
    void handleAuthBody(const QByteArray &body);
    void handleChatBody(const QByteArray &body);
    void handleErrorBody(const QByteArray &body);
    void handlePingBody(const QByteArray &body);
    static void handlePongBody(const QByteArray &body);
    void handleChatAckBody(const QByteArray &body);

    QTcpSocket m_socket{};
    QString m_token{};
    QByteArray m_received_buffer{};
    QTimer m_connect_timer{};
    AuthState m_state = {AuthState::idle};
};



