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
void MainWindow::connectSlots() {
    connect(m_http, &HttpClient::requestFinish, this, &MainWindow::onRequestFinish);
    connect(m_http, &HttpClient::requestError, this, &MainWindow::onRequestError);
    connect(m_http, &HttpClient::requestTimeOut, this, &MainWindow::onRequestTimeOut);

    connect(ui->loginBtn, &QPushButton::clicked, this, &MainWindow::on_loginBtn_clicked);
    connect(ui->registerBtn, &QPushButton::clicked, this, &MainWindow::on_registerBtn_clicked);
}

//槽函数实现
void MainWindow::on_loginBtn_clicked() {
    QString username = ui->userNameEdit->text().trimmed();
    QString password = ui->pwdEdit->text();
    QJsonObject obj;
    //1.组装JSON body
    obj["username"] = username;
    obj["password"] = password;
    QJsonDocument doc(obj);
    QByteArray body = doc.toJson();

    m_http->post(QUrl("http://localhost:3000/login"), body);

}

void MainWindow::on_registerBtn_clicked() {
    QString username = ui->userNameEdit->text().trimmed();
    QString password = ui->pwdEdit->text();
    QJsonObject obj;
    //1.组装JSON body
    obj["username"] = username;
    obj["password"] = password;
    QJsonDocument doc(obj);
    QByteArray body = doc.toJson();

    m_http->post(QUrl("http://localhost:3000/register"), body);
}

void MainWindow::onRequestFinish(int statusCode, const QByteArray &body) {
    //解析响应JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        auto msg = obj["message"].toString();
        auto  error = obj["error"].toString();
        auto token = obj["token"].toString();

        if (statusCode == 200 || statusCode == 201) {
            QMessageBox::information(this, "提示", msg);
        }
        else{
            QMessageBox::warning(this, "错误", error);
        }
    }
    }
void MainWindow::onRequestError(const QString &error) {
    ui->messageLabel->setText(error);
}

void MainWindow::onRequestTimeOut() {
    QMessageBox::warning(this,"请求超时","请求超时");
}
