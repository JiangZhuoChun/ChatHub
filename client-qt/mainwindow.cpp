#include "mainwindow.h"

#include "chatclient.h"
#include "ui_mainwindow.h"

#include <QLabel>
#include <QStyle>

MainWindow::MainWindow(ChatClient *chat_client,
                       const QString &username,
                       QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_chat(chat_client)
    , m_username(username)
{
    Q_ASSERT(m_chat != nullptr);
    ui->setupUi(this);
    setupUiState();
    connectSlots();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupUiState()
{
    setWindowTitle(QStringLiteral("ChatHub"));
    setMinimumSize(960, 640);
    resize(1120, 720);
    ui->currentUserLabel->setText(QStringLiteral("当前用户：") + m_username);
    ui->peerNameLabel->setText(QStringLiteral("选择一个会话开始聊天"));
    ui->sendBtn->setEnabled(false);
}

void MainWindow::connectSlots()
{
    connect(m_chat, &ChatClient::disconnected,
            this, &MainWindow::onDisconnected);

    updateConnectionState(m_chat->isAuthenticated(),
                          m_chat->isAuthenticated()
                              ? QStringLiteral("连接正常")
                              : QStringLiteral("连接未认证"));
}

void MainWindow::updateConnectionState(bool connected, const QString &message)
{
    ui->connectionStateLabel->setProperty("status", connected ? "ok" : "error");
    ui->connectionStateLabel->style()->unpolish(ui->connectionStateLabel);
    ui->connectionStateLabel->style()->polish(ui->connectionStateLabel);
    ui->connectionStateLabel->setText(message);
    // 聊天发送协议尚未接入；本阶段只展示连接状态，按钮保持禁用。
    ui->sendBtn->setEnabled(false);
}

void MainWindow::onDisconnected()
{
    updateConnectionState(false, QStringLiteral("连接已断开，请重新登录"));
}
