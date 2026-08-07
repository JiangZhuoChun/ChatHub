#include "mainwindow.h"

#include "chatclient.h"
#include "ui_mainwindow.h"

#include <QLabel>
#include <QStyle>
#include  <QLayoutItem>
#include <utility>
#include <QListWidgetItem>
// ==================== 模块：窗口生命周期 ====================
// 功能：创建主窗口、初始化界面状态，并连接用户操作和聊天客户端信号。
MainWindow::MainWindow(ChatClient* chat_client, QString username, QWidget* parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_chat(chat_client),
      m_username(std::move(username)) {
    Q_ASSERT(m_chat != nullptr);
    ui->setupUi(this);
    setupUiState();
    connectSlots();
}

// 功能：释放 Qt 设计器创建的主窗口界面对象。
MainWindow::~MainWindow() {
    delete ui;
}

// ==================== 模块：窗口初始化与连接状态辅助 ====================
// 功能：设置窗口大小、当前用户名、默认会话提示和初始发送按钮状态。
void MainWindow::setupUiState() {
    setWindowTitle(QStringLiteral("ChatHub"));
    setMinimumSize(960, 640);
    resize(1120, 720);
    ui->currentUserLabel->setText(QStringLiteral("当前用户：") + m_username);
    ui->peerNameLabel->setText(QStringLiteral("选择一个会话开始聊天"));
    ui->sendBtn->setEnabled(false);
}

// 功能：连接界面控件和 ChatClient 信号，使窗口随连接和消息状态自动更新。
void MainWindow::connectSlots() {
    connect(m_chat, &ChatClient::disconnected, this, &MainWindow::onDisconnected);
    connect(ui->sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_chat, &ChatClient::chatMessageQueued, this, &MainWindow::onChatMessageQueued);
    connect(m_chat, &ChatClient::chatMessageAccepted, this, &MainWindow::onChatMessageAccepted);
    connect(m_chat, &ChatClient::chatSendFailed, this, &MainWindow::onChatSendFailed);
    connect(m_chat, &ChatClient::chatMessageReceived, this, &MainWindow::onChatMessageReceived);
    connect(ui->conversationList, &QListWidget::itemClicked, this, &MainWindow::onConversationItemClicked);

    connect(m_chat, &ChatClient::authSucceeded, this,
            // 功能：认证成功后将状态标签和发送按钮切换为可用状态。
            [this] {
                updateConnectionState(true, QStringLiteral("连接正常"));
            });

    updateConnectionState(m_chat->isAuthenticated(),
                          m_chat->isAuthenticated()
                              ? QStringLiteral("连接正常")
                              : QStringLiteral("连接未认证"));
}

// 功能：根据连接状态更新状态标签的样式和文本，并同步控制发送按钮。
void MainWindow::updateConnectionState(const bool connected, const QString& message) const {
    ui->connectionStateLabel->setProperty("status", connected ? "ok" : "error");
    ui->connectionStateLabel->style()->unpolish(ui->connectionStateLabel);
    ui->connectionStateLabel->style()->polish(ui->connectionStateLabel);
    ui->connectionStateLabel->setText(message);
    ui->sendBtn->setEnabled(connected);
}

void MainWindow::ensureConversationItem(const QString& peer) const {
    const auto items =ui->conversationList->findItems(peer,Qt::MatchExactly);
    if (items.isEmpty()) {
        ui->conversationList->addItem(peer);
    }
}
//安全清理动态气泡
void MainWindow::clearMessageBubbles() const {
    while (ui->messageLayout->count() >1) {
        const QLayoutItem* item = ui->messageLayout->takeAt(1);
        if (item == nullptr) {
            break;
        }

        delete item->widget();
        delete item;
    }
}
//重新渲染当前会话
void MainWindow::renderCurrentConversation() {

    clearMessageBubbles();

    if (m_currentPeer.isEmpty()) {
        return;
    }

    //使用 constFind() 读取 QHash：它不会像 operator[] 一样在键不存在时意外创建空会话。
    const auto chatMessage_it = m_conversations.constFind(m_currentPeer);
    if (chatMessage_it == m_conversations.end()) {
        return;
    }

    for (const ChatMessage& message :chatMessage_it.value()) {
        appendMessageBubble(message.local_id,
                            message.from,
                            message.to,
                            message.content,
                            message.send_at,
                            message.status,
                            message.failure_reason);
    }
}

// 功能：将消息状态枚举转换为字符串。
QString MainWindow::chatMessageStatusTostring(ChatMessageStatus status) {
    switch (status) {
        case ChatMessageStatus::Accepted:
            return QStringLiteral("accepted");
        case ChatMessageStatus::Failed:
            return QStringLiteral("failed");
        case ChatMessageStatus::Received:
            return QStringLiteral("received");
        case ChatMessageStatus::Sending:
            return QStringLiteral("sending");
        default:
            return QStringLiteral("unknown");
    }
}

ChatMessage * MainWindow::findMessageByLocalId(const QString &local_id) {

    if (local_id.isEmpty()) {
        return nullptr;
    }

    for (auto conversation_it = m_conversations.begin();
        conversation_it != m_conversations.end(); ++conversation_it)
    {
        for (ChatMessage& message : conversation_it.value())
        {
            if (message.local_id == local_id) {
                return &message;
            }
        }
    }

    return nullptr;

}

// ==================== 模块：窗口与连接状态 ====================
// 功能：连接断开时禁用发送按钮，并提示用户重新登录。
void MainWindow::onDisconnected() const {
    updateConnectionState(false, QStringLiteral("连接已断开，请重新登录"));
}

// ==================== 模块：用户发送操作 ====================
// 功能：读取当前输入框中的接收者和正文，并请求 ChatClient 发送消息。
void MainWindow::onSendClicked() const {
    // 手填接收者优先；未填写时才使用当前正在查看的会话联系人。
    const QString to = ui->recipientEdit->text().trimmed();
    QString real_to = to;
    const QString content = ui->messageEdit->toPlainText();
    if (to.isEmpty()) {
        if (m_currentPeer.isEmpty()) {
            statusBar()->showMessage("请选择联系人或输入接收者");
            return;
        }
        real_to = m_currentPeer;
    }

    // 与 ChatClient 保持一致：空字符串、空格和换行都不允许进入发送链路。
    // 仅用 trimmed() 判断，不修改真正发出的正文，保留用户输入的有效首尾空格。
    if (content.trimmed().isEmpty()) {
        statusBar()->showMessage("信息不能为空");
        return;
    }

    // 所有校验通过后只发送一次，避免产生无关联的失败 local_id。
    m_chat->sendChatMessage(real_to, content);
}

// ==================== 模块：消息发送状态处理 ====================
// 功能：为已进入发送缓冲区的消息创建气泡，或在重试时恢复已有气泡的发送状态。
void MainWindow::onChatMessageQueued(const QString& to, const QString& content,
                                     const QString& local_id, const QDateTime& send_at) {
    if (auto* message = findMessageByLocalId(local_id);
        message != nullptr)
    {
        message->status = ChatMessageStatus::Sending;
        message->failure_reason.clear();
    }
    else
    {
        ChatMessage chatMessage;
        chatMessage.local_id = local_id;
        chatMessage.content = content;
        chatMessage.from = m_username;
        chatMessage.to = to;
        chatMessage.send_at = send_at;
        chatMessage.status = ChatMessageStatus::Sending;

        m_conversations[chatMessage.to].append(chatMessage);
        ensureConversationItem(chatMessage.to);
    }

    if (m_currentPeer == to) {
        renderCurrentConversation();
    }

    statusBar()->showMessage(QStringLiteral("消息发送中..."));
}

// 功能：将已被服务器接受的消息改为成功状态，并移除其待确认记录。
void MainWindow::onChatMessageAccepted(const QString& local_id) {
    ChatMessage* message = findMessageByLocalId(local_id);
    if (message == nullptr)
    {
        statusBar()->showMessage(QStringLiteral("收到未知消息确认"));
        return;
    }

    message->status = ChatMessageStatus::Accepted;
    message->failure_reason.clear();

    if (m_currentPeer == message->to) {
        renderCurrentConversation();
    }
    statusBar()->showMessage(QStringLiteral("服务器已接收消息"));
}

// 功能：将 local_id 对应待确认消息标记为失败，显示原因并允许用户重试。
void MainWindow::onChatSendFailed(const QString& local_id, const QString& reason) {
    ChatMessage* message = findMessageByLocalId(local_id);
    if (message == nullptr) {
        statusBar()->showMessage(QStringLiteral("未知消息发送失败：") + reason);
        return;
    }
    message->status = ChatMessageStatus::Failed;
    message->failure_reason = reason;

    if (m_currentPeer == message->to) {
        renderCurrentConversation();
    }
    statusBar()->showMessage(QStringLiteral("消息发送失败：") + reason);
}

// 功能：读取待确认表中的原消息，用相同 local_id 请求 ChatClient 再次发送。
void MainWindow::onRetryClicked(const QString& local_id) {
    ChatMessage* message = findMessageByLocalId(local_id);
    if (message == nullptr) {
        statusBar()->showMessage(QStringLiteral("未找到需要重试的消息"));
        return;
    }
    // 网络调用可能同步触发信号，因此先复制，后续不再使用 message 指针。
    const auto to = message->to;
    const auto content = message->content;

    message->status = ChatMessageStatus::Sending;
    message->failure_reason.clear();

    if (m_currentPeer == to) {
        renderCurrentConversation();
    }

    m_chat->sendChatMessage(to,content,local_id);

}

// ==================== 模块：接收消息处理 ====================
// 功能：将服务端转发的消息渲染为收到状态的聊天气泡。
void MainWindow::onChatMessageReceived(const QString& local_id, const QString& from,
                                       const QString& to, const QString& content,
                                       const QDateTime& send_at)
{
    ChatMessage message;
    message.local_id = local_id;
    message.content = content;
    message.from = from;
    message.to = to;
    message.send_at = send_at;
    message.status = ChatMessageStatus::Received;


    m_conversations[from].append(message);
    ensureConversationItem(from);

    if (m_currentPeer == from) {
        renderCurrentConversation();
    }
}

void MainWindow::onConversationItemClicked(const QListWidgetItem *item) {
    if (item == nullptr) {
        return;
    }
    m_currentPeer = item->text();
    ui->peerNameLabel->setText(m_currentPeer);
    renderCurrentConversation();
}

// ==================== 模块：消息气泡渲染 ====================
// 功能：创建消息行、正文标签、隐藏的失败重试按钮，并保存消息元数据到控件属性。
void MainWindow::appendMessageBubble(
    const QString& local_id, const QString& from, const QString& to,
    const QString& content, const QDateTime& send_at, const ChatMessageStatus& status,
    const QString& failure_reason) {


    auto* row = new QWidget(ui->messageContainer);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);

    auto* label = new QLabel(row);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QString text = from + QStringLiteral(": ") + content;
    if (send_at.isValid()) {
        text = from + QStringLiteral(": ") + content + QStringLiteral("\n") +
               QStringLiteral(" [") +
               send_at.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
               QStringLiteral("]");
    }
    label->setText(text);

    label->setProperty("local_id", local_id);
    label->setProperty("from", from);
    label->setProperty("to", to);
    const QString status_text = chatMessageStatusTostring(status);
    label->setProperty("status", status_text);

    auto* retry_button = new QToolButton(row);
    connect(retry_button, &QToolButton::clicked, this,
            // 功能：用户点击失败标记时，重试该按钮所属 local_id 的消息。
            [this, local_id] {
                onRetryClicked(local_id);
            });
    retry_button->setText(QStringLiteral("!"));
    retry_button->setFixedSize(18, 18);

    const bool is_failed = status == ChatMessageStatus::Failed;
    retry_button->setVisible(is_failed);
    retry_button->setToolTip(
    failure_reason.isEmpty()? QStringLiteral("消息发送失败"): failure_reason);

    retry_button->setStyleSheet(
        QStringLiteral(
            "QToolButton {"
            "color: white;"
            "background-color: #e53935;"
            "border: none;"
            "border-radius: 9px;"
            "font-weight: bold;"
            "}"));

    const bool is_mine = from == m_username;
    const Qt::Alignment alignment = is_mine ? Qt::AlignRight : Qt::AlignLeft;
    row_layout->addWidget(label, 1);
    row_layout->addWidget(retry_button, 0, Qt::AlignCenter);

    ui->messageLayout->addWidget(row, 0, alignment);
    ui->messageScrollArea->ensureWidgetVisible(row);


}
