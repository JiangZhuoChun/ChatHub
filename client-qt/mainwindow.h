#pragma once

#include <QDateTime>
#include <QHash>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QString>
#include <QToolButton>

class ChatClient;
class QLabel;
class QWidget;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // ==================== 模块：窗口生命周期 ====================
    // 功能：创建聊天主窗口，保存当前用户并连接 ChatClient 的业务信号。
    explicit MainWindow(ChatClient* chat_client, QString username, QWidget* parent = nullptr);

    // 功能：释放 Qt 设计器创建的主窗口界面对象。
    ~MainWindow() override;

private slots:
    // ==================== 模块：窗口与连接状态 ====================
    // 功能：聊天连接断开时更新状态栏和发送按钮状态。
    void onDisconnected() const;

    // ==================== 模块：用户发送操作 ====================
    // 功能：读取接收者和输入框正文，并交给 ChatClient 发送。
    void onSendClicked();

    // ==================== 模块：消息发送状态处理 ====================
    // 功能：为已写入发送缓冲区的消息创建或恢复待确认气泡。
    void onChatMessageQueued(const QString& to, const QString& content,
                             const QString& local_id, const QDateTime& send_at);

    // 功能：将服务端已接受的消息气泡更新为成功状态并移出待确认表。
    void onChatMessageAccepted(const QString& local_id);

    // 功能：将发送失败的消息气泡标记为失败，并显示可点击的重试按钮。
    void onChatSendFailed(const QString& local_id, const QString& reason);

    // 功能：使用原始 local_id、接收者和正文重新提交失败消息。
    void onRetryClicked(const QString& local_id);

    // ==================== 模块：接收消息处理 ====================
    // 功能：将服务端转发的聊天消息渲染为收到消息气泡。
    void onChatMessageReceived(const QString& local_id, const QString& from,
                               const QString& to, const QString& content,
                               const QDateTime& send_at);

private:
    // ==================== 模块：消息气泡与待确认类型 ====================
    // 功能：保存一条消息气泡所在行、正文标签和失败重试按钮。
    struct MessageWidgets {
        QWidget* row = nullptr;
        QLabel* bubble = nullptr;
        QToolButton* retryBtn = nullptr;
    };

    // 功能：保存尚未被服务端确认的消息及其重试所需数据。
    struct PendingMessage {
        MessageWidgets widgets;
        QString to;
        QString content;
    };

    // ==================== 模块：窗口初始化与连接状态辅助 ====================
    // 功能：设置窗口尺寸、当前用户、默认会话提示和发送按钮初始状态。
    void setupUiState();

    // 功能：将按钮和 ChatClient 信号连接到主窗口的状态更新处理函数。
    void connectSlots();

    // 功能：更新连接状态标签的文本、样式属性和发送按钮可用状态。
    void updateConnectionState(bool connected, const QString& message) const;

    // ==================== 模块：消息气泡渲染 ====================
    // 功能：创建带发送者、正文、时间、状态属性和重试按钮的聊天气泡。
    MessageWidgets appendMessageBubble(const QString& local_id, const QString& from,
                                       const QString& to, const QString& content,
                                       const QDateTime& send_at, const QString& status);

    // ==================== 模块：界面与当前用户依赖 ====================
    // 功能：保存 Qt 设计器生成的界面对象，由析构函数释放。
    Ui::MainWindow* ui;

    // 功能：保存应用注入的聊天客户端，不负责释放。
    ChatClient* m_chat;

    // 功能：保存已认证用户，用于判定消息是本人发送还是对方发送。
    QString m_username;

    // ==================== 模块：待确认消息状态 ====================
    // 功能：按 local_id 保存待服务器确认的气泡、接收者和正文，支持失败重试。
    QHash<QString, PendingMessage> m_pendingMessages;
};
