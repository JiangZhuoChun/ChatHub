#include "mainwindow.h"

#include "chatclient.h"
#include "ui_mainwindow.h"

#include <QLabel>
#include <QStyle>
#include <utility>

MainWindow::MainWindow(ChatClient *chat_client,
                       QString username,
                       QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_chat(chat_client)
    , m_username(std::move(username))
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
    connect(m_chat, &ChatClient::disconnected,this, &MainWindow::onDisconnected);
    connect(ui->sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_chat,&ChatClient::chatMessageQueued,this, &MainWindow::onChatMessageQueued);
    connect(m_chat,&ChatClient::chatMessageAccepted,this, &MainWindow::onChatMessageAccepted);
    connect(m_chat,&ChatClient::chatMessageReceived,this, &MainWindow::onChatMessageReceived);
    connect(m_chat,&ChatClient::chatSendFailed,this, &MainWindow::onChatSendFailed);
    connect(m_chat,&ChatClient::authSucceeded,this,[this] {
       updateConnectionState(true,QStringLiteral("连接正常"));
    });
    updateConnectionState(m_chat->isAuthenticated(),
                          m_chat->isAuthenticated()
                              ? QStringLiteral("连接正常")
                              : QStringLiteral("连接未认证"));
}

void MainWindow::updateConnectionState(const bool connected, const QString &message) const {
    ui->connectionStateLabel->setProperty("status", connected ? "ok" : "error");
    //unpolish + polish 强制刷新样式
    ui->connectionStateLabel->style()->unpolish(ui->connectionStateLabel);
    ui->connectionStateLabel->style()->polish(ui->connectionStateLabel);
    ui->connectionStateLabel->setText(message);
    ui->sendBtn->setEnabled(connected);
}
//气泡加入布局后返回
MainWindow::MessageWidgets MainWindow::appendMessageBubble
(const QString &local_id, const QString &from, const QString &to,
const QString &content, const QDateTime &send_at, const QString &status)
{
    MessageWidgets widgets;

    auto *row = new QWidget(ui->messageContainer);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0,0,0,0);

    auto *label = new QLabel(row);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QString text = from + QStringLiteral(": ") + content;
    if (send_at.isValid()) {
        text = from + QStringLiteral(": ")
        + content + QStringLiteral("\n")
        + QStringLiteral(" [") + send_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QStringLiteral("]");
    }

    label->setText(text);
    // 技术数据保留在控件属性中，不直接展示 local_id
    label->setProperty("local_id", local_id);
    label->setProperty("from", from);
    label->setProperty("to", to);
    label->setProperty("status", status);

    auto *retryBtn = new QToolButton(row);
    connect(retryBtn,&QToolButton::clicked,this, [this,local_id] {
       onRetryClicked(local_id);
    });
    retryBtn->setText(QStringLiteral("!"));
    retryBtn->setFixedSize(18,18);
    retryBtn->setVisible(false);
    retryBtn->setToolTip(QStringLiteral("消息发送失败"));
    retryBtn->setStyleSheet(
        QStringLiteral(
                "QToolButton {"
                "color: white;"
                "background-color: #e53935;"
                "border: none;"
                "border-radius: 9px;"
                "font-weight: bold;"
         "}"));
    const bool is_mine = (from == m_username);
    const Qt::Alignment alignment = is_mine ? Qt::AlignRight : Qt::AlignLeft;

    rowLayout->addWidget(label,1);
    rowLayout->addWidget(retryBtn,0,Qt::AlignCenter);

    ui->messageLayout->addWidget(row,0,alignment);
    ui->messageScrollArea->ensureWidgetVisible(row);

    widgets.row = row;
    widgets.bubble = label;
    widgets.retryBtn = retryBtn;

    return widgets;
}

void MainWindow::onDisconnected() const {
    updateConnectionState(false, QStringLiteral("连接已断开，请重新登录"));
}

void MainWindow::onSendClicked() {
    const QString to = ui->recipientEdit->text().trimmed();
    const QString content = ui->messageEdit->toPlainText();
    m_chat->sendChatMessage(to,content);
}

void MainWindow::onChatMessageQueued(const QString &to, const QString &content, const QString &local_id, const QDateTime &send_at)
{
    const auto it = m_pendingMessages.find(local_id);
    //防止重试创建新气泡
    if (it != m_pendingMessages.end())
    {
        it->widgets.retryBtn->setVisible(false);
        it->widgets.bubble->setProperty("status","sending");
        return;
    }
   const MessageWidgets widgets = appendMessageBubble(local_id,m_username,to,content,send_at,QStringLiteral("sending"));
    //发送成功后进入缓冲区时保存映射
    m_pendingMessages.insert(local_id,PendingMessage{widgets,to,content});
    statusBar()->showMessage(QStringLiteral("消息发送中..."));
}

void MainWindow::onChatMessageAccepted(const QString &local_id)
{
    const auto it = m_pendingMessages.find(local_id);
    if (it == m_pendingMessages.end())
    {
        statusBar()->showMessage(QStringLiteral("收到未知消息确认"));
        return;
    }
    it->widgets.retryBtn->setVisible(false);
    it->widgets.bubble->setProperty("status","accepted");
    it->widgets.bubble->style()->unpolish(it->widgets.bubble);
    it->widgets.bubble->style()->polish(it->widgets.bubble);

    m_pendingMessages.erase(it);
    statusBar()->showMessage(QStringLiteral("服务器已接收:") + local_id);
}

void MainWindow::onChatMessageReceived(const QString &local_id, const QString &from, const QString &to,const QString &content, const QDateTime &send_at)
{
    appendMessageBubble(local_id,from,to,content,send_at,QStringLiteral("received"));
}

void MainWindow::onChatSendFailed(const QString &local_id, const QString  &reason) {
    const auto it = m_pendingMessages.find(local_id);
    if (it != m_pendingMessages.end())
    {
        it->widgets.retryBtn->setVisible(true);
        it->widgets.bubble->setProperty("status","failed");
        it->widgets.bubble->setToolTip(reason);
        it->widgets.bubble->style()->unpolish(it->widgets.bubble);
        it->widgets.bubble->style()->polish(it->widgets.bubble);
    }
    statusBar()->showMessage(QStringLiteral("消息发送失败:") + reason);
}

void MainWindow::onRetryClicked(const QString &local_id) {
    const auto it = m_pendingMessages.find(local_id);
    if (it == m_pendingMessages.end())
    {
        return;
    }
    it->widgets.retryBtn->setVisible(false);
    it->widgets.bubble->setProperty("status","sending");
    it->widgets.bubble->style()->unpolish(it->widgets.bubble);
    it->widgets.bubble->style()->polish(it->widgets.bubble);

    m_chat->sendChatMessage(it->to,it->content,local_id);
}
