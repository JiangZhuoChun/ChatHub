#pragma once

#include <QMainWindow>
#include <QString>

class ChatClient;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ChatClient *chat_client,
                        const QString &username,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onDisconnected();

private:
    void setupUiState();
    void connectSlots();
    void updateConnectionState(bool connected, const QString &message);

    Ui::MainWindow *ui;
    ChatClient *m_chat;
    QString m_username;
};
