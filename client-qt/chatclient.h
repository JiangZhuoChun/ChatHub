#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QTimer>
class ChatClient : public QObject {
    Q_OBJECT
public:
    explicit ChatClient(QObject *parent = nullptr);

    void connectWithToken(const QString &token);
    void disconnectFromServer();
    void connectSlots();
    bool isAuthenticated() const;

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

    void sendAuthFrame();
    void processReceivedFrames();

    QTcpSocket m_socket;
    QString m_token;
    QByteArray m_received_buffer;
    QTimer m_connect_timer;
    AuthState m_state = {AuthState::idle};
};



