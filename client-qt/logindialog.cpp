#include "logindialog.h"

#include "HttpClient.h"
#include "chatclient.h"
#include "ui_logindialog.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLineEdit>
#include <QUrl>

LoginDialog::LoginDialog(ChatClient *chat_client, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
    , m_http(new HttpClient(this))
    , m_chat(chat_client)
{
    Q_ASSERT(m_chat != nullptr);
    ui->setupUi(this);
    setupUiState();
    connectSlots();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

QString LoginDialog::username() const
{
    return m_username;
}

void LoginDialog::setupUiState()
{
    setWindowTitle(QStringLiteral("ChatHub 登录"));
    setModal(true);
    ui->pwdEdit->setEchoMode(QLineEdit::Password);
    ui->userNameEdit->setPlaceholderText(QStringLiteral("请输入用户名"));
    ui->pwdEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    ui->messageLabel->clear();
}

void LoginDialog::connectSlots()
{
    connect(m_http, &HttpClient::requestFinish,
            this, &LoginDialog::onRequestFinish);
    connect(m_http, &HttpClient::requestError,
            this, &LoginDialog::onRequestError);
    connect(m_http, &HttpClient::requestTimeOut,
            this, &LoginDialog::onRequestTimeOut);

    connect(m_chat, &ChatClient::authFrameSent,
            this, &LoginDialog::onAuthFrameSent);
    connect(m_chat, &ChatClient::authSucceeded,
            this, &LoginDialog::onAuthSucceeded);
    connect(m_chat, &ChatClient::authFailed,
            this, &LoginDialog::onAuthFailed);
    connect(m_chat, &ChatClient::connectionFailed,
            this, &LoginDialog::onConnectionFailed);

    // loginBtn、registerBtn、cancelBtn 使用 Qt Designer 的自动连接，
    // 不再为它们额外手动 connect，避免一次点击发出两次请求。
}

void LoginDialog::on_loginBtn_clicked()
{
    const QString username = ui->userNameEdit->text().trimmed();
    const QString password = ui->pwdEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        showMessage(QStringLiteral("用户名和密码不能为空"));
        return;
    }

    QJsonObject body;
    body[QStringLiteral("username")] = username;
    body[QStringLiteral("password")] = password;

    m_pending_request = RequestType::login;
    setRequestButtonsEnabled(false);
    showMessage(QStringLiteral("正在登录..."));
    m_http->post(QUrl(QStringLiteral("http://localhost:3000/login")),
                 QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void LoginDialog::on_registerBtn_clicked()
{
    const QString username = ui->userNameEdit->text().trimmed();
    const QString password = ui->pwdEdit->text();
    if (username.isEmpty() || password.isEmpty()) {
        showMessage(QStringLiteral("用户名和密码不能为空"));
        return;
    }

    QJsonObject body;
    body[QStringLiteral("username")] = username;
    body[QStringLiteral("password")] = password;

    m_pending_request = RequestType::registerUser;
    setRequestButtonsEnabled(false);
    showMessage(QStringLiteral("正在注册..."));
    m_http->post(QUrl(QStringLiteral("http://localhost:3000/register")),
                 QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void LoginDialog::on_cancelBtn_clicked()
{
    reject();
}

void LoginDialog::onRequestFinish(int status_code, const QByteArray &body)
{
    const RequestType request_type = m_pending_request;
    m_pending_request = RequestType::none;

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        setRequestButtonsEnabled(true);
        showMessage(QStringLiteral("HTTP 响应格式错误"));
        return;
    }

    const QJsonObject object = document.object();
    const QString message = object.value(QStringLiteral("message")).toString();
    const QString error = object.value(QStringLiteral("error")).toString();

    if (request_type == RequestType::login && status_code == 200) {
        const QString token = object.value(QStringLiteral("token")).toString();
        if (token.isEmpty()) {
            setRequestButtonsEnabled(true);
            showMessage(QStringLiteral("登录失败：服务器没有返回 token"));
            return;
        }

        m_username = ui->userNameEdit->text().trimmed();
        showMessage(QStringLiteral("HTTP 登录成功，正在连接 chat-server..."));
        // 按钮继续禁用，直到 TCP 认证成功或失败。
        m_chat->connectWithToken(token);
        return;
    }

    setRequestButtonsEnabled(true);
    if (request_type == RequestType::registerUser && status_code == 201) {
        showMessage(message.isEmpty() ? QStringLiteral("注册成功") : message);
        return;
    }

    showMessage(error.isEmpty() ? QStringLiteral("请求失败") : error);
}

void LoginDialog::onRequestError(const QString &error)
{
    m_pending_request = RequestType::none;
    setRequestButtonsEnabled(true);
    showMessage(QStringLiteral("HTTP 请求失败：") + error);
}

void LoginDialog::onRequestTimeOut()
{
    m_pending_request = RequestType::none;
    setRequestButtonsEnabled(true);
    showMessage(QStringLiteral("HTTP 请求超时"));
}

void LoginDialog::onAuthFrameSent()
{
    showMessage(QStringLiteral("TCP 已连接，认证帧已发送，等待服务端确认..."));
}

void LoginDialog::onAuthSucceeded()
{
    m_pending_request = RequestType::none;
    setRequestButtonsEnabled(true);
    showMessage(QStringLiteral("聊天服务器认证成功"));
    accept();
}

void LoginDialog::onAuthFailed(const QString &reason)
{
    m_pending_request = RequestType::none;
    setRequestButtonsEnabled(true);
    showMessage(QStringLiteral("聊天认证失败：") + reason);
}

void LoginDialog::onConnectionFailed(const QString &reason)
{
    m_pending_request = RequestType::none;
    setRequestButtonsEnabled(true);
    showMessage(QStringLiteral("连接 chat-server 失败：") + reason);
}

void LoginDialog::showMessage(const QString &message)
{
    ui->messageLabel->setText(message);
}

void LoginDialog::setRequestButtonsEnabled(bool enabled)
{
    ui->loginBtn->setEnabled(enabled);
    ui->registerBtn->setEnabled(enabled);
}

void LoginDialog::reject()
{
    if (m_chat != nullptr) {
        m_chat->disconnectFromServer();
    }
    QDialog::reject();
}
