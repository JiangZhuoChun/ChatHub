#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QPushButton>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_http(new HttpClient(this))
    , m_chat(new ChatClient(this))
{
    ui->setupUi(this);
    setUi();
    connectSlots();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setUi() {
    this->setWindowTitle("ChatHub");
    this->setFixedSize(400,300);
    ui->userNameEdit->setPlaceholderText("请输入用户名");
    ui->pwdEdit->setPlaceholderText("请输入密码");
    ui->pwdEdit->setEchoMode(QLineEdit::Password);
}

void MainWindow::showMessage(const QString &message) {
    ui->messageLabel->setText(message);
}

void MainWindow::connectSlots() {

    //HTTP请求
    connect(m_http, &HttpClient::requestFinish, this, &MainWindow::onRequestFinish);
    connect(m_http, &HttpClient::requestError, this, &MainWindow::onRequestError);
    connect(m_http, &HttpClient::requestTimeOut, this, &MainWindow::onRequestTimeOut);

    //TCP请求
    connect(m_chat, &ChatClient::authFrameSent,this,[this] {
        showMessage("TCP 已连接，认证帧已发送，等待服务端确认…");
    });
    connect(m_chat,&ChatClient::authSucceeded,this,[this] {
        showMessage("聊天服务器认证成功，连接保持中");
    });
    connect(m_chat,&ChatClient::authFailed,this,[this](const QString &reason) {
        showMessage("聊天认证失败：" + reason);
    });
    connect(m_chat,&ChatClient::connectionFailed,this,[this](const QString &reason) {
       showMessage("连接服务器失败:" + reason);
    });
    connect(m_chat,&ChatClient::disconnected,this,[this] {
        showMessage("聊天服务器连接已断开");
    });

    connect(ui->loginBtn, &QPushButton::clicked, this, &MainWindow::on_loginBtn_clicked);
    connect(ui->registerBtn, &QPushButton::clicked, this, &MainWindow::on_registerBtn_clicked);
}

//槽函数实现
void MainWindow::on_loginBtn_clicked() {
    QString username = ui->userNameEdit->text().trimmed();
    QString password = ui->pwdEdit->text();

    if (username.isEmpty() || password.isEmpty()) {
        showMessage("用户名和密码不能为空");
        return;
    }
    QJsonObject obj;
    obj["username"] = username;
    obj["password"] = password;

    m_pending_request = RequestType::login;
    setRequestBtnEnabled(false);
    showMessage("正在登录...");

    m_http->post(QUrl("http://localhost:3000/login"), QJsonDocument(obj).toJson());
}

void MainWindow::on_registerBtn_clicked() {
    QString username = ui->userNameEdit->text().trimmed();
    QString password = ui->pwdEdit->text();

    if (username.isEmpty() || password.isEmpty()) {
        showMessage("用户名和密码不能为空");
        return;;
    }

    QJsonObject obj;
    obj["username"] = username;
    obj["password"] = password;

    m_pending_request = RequestType::registerUser;
    setRequestBtnEnabled(false);
    showMessage("正在注册...");
    m_http->post(QUrl("http://localhost:3000/register"), QJsonDocument(obj).toJson());
}

//HTTP响应
void MainWindow::onRequestFinish(int statusCode, const QByteArray &body) {
    const RequestType request_tpye = m_pending_request;
    setRequestBtnEnabled(true);

    //解析响应JSON
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError && !doc.isObject()) {
        showMessage("HTTP响应格式错误");
        return;
    }

    QJsonObject obj = doc.object();
    const auto msg = obj.value("message").toString();
    const auto  error = obj.value("error").toString();

    if (request_tpye == RequestType::login && statusCode == 200) {
        const QString token = obj.value("token").toString();
        if (token.isEmpty()) {
            showMessage("登录失败，服务器没有返回token");
            return;
        }
        showMessage("HTTP登录成功，正在连接chat-server");
        m_chat->connectWithToken(token);
        return;
    }
    if (request_tpye == RequestType::registerUser && statusCode == 201) {
        showMessage(msg.isEmpty() ? "注册成功" : msg);
        return;
    }
    showMessage(error.isEmpty() ? "请求失败" : error);


    }

void MainWindow::onRequestError(const QString &error) {
    m_pending_request = RequestType::none;
    setRequestBtnEnabled(true);
    showMessage("HTTP请求失败" + error);
}

void MainWindow::onRequestTimeOut() {
    m_pending_request = RequestType::none;
    setRequestBtnEnabled(true);
    showMessage("HTTP 请求超时");
}

void MainWindow::setRequestBtnEnabled(bool  enabled) {
    ui->loginBtn->setEnabled(enabled);
    ui->registerBtn->setEnabled(enabled);
}
