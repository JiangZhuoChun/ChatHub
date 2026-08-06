#pragma once

#include <QMainWindow>
#include <QString>
#include <QDateTime>
#include <QHash>
#include <QHBoxLayout>
#include <QToolButton>

class ChatClient;
class QLabel;
class QWidget;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ChatClient *chat_client,
                        QString username,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onDisconnected() const;
    void onSendClicked();
    void onChatMessageQueued(const QString& to, const QString& content,
        const QString& local_id,const QDateTime &send_at);
    void onChatMessageAccepted(const QString& local_id);
    void onChatMessageReceived(const QString &local_id,const QString &from,const QString &to,
                           const QString &content,const QDateTime &send_at);

    void onChatSendFailed(const QString& local_id, const QString& reason);
    void onRetryClicked(const QString &local_id);

private:
    void setupUiState();
    void connectSlots();
    void updateConnectionState(bool connected, const QString &message) const;

    struct MessageWidgets {
        QWidget* row =  nullptr;
        QLabel* bubble = nullptr;
        QToolButton* retryBtn = nullptr;
    };
    struct PendingMessage {
        MessageWidgets widgets;
        QString to;
        QString content;
    };

    MessageWidgets appendMessageBubble(const QString &local_id,const QString &from,const QString &to,
                                           const QString &content,const QDateTime &send_at,const QString &status);

    Ui::MainWindow *ui;
    ChatClient *m_chat;
    QString m_username;
    //local_id->聊天气泡，接收者，消息正文
    QHash<QString,PendingMessage> m_pendingMessages;
};
