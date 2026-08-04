#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "HttpClient.h"
#include "chatclient.h"
QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;


private slots:
    void on_loginBtn_clicked();
    void on_registerBtn_clicked();

    //http请求
    void onRequestFinish(int statusCode, const QByteArray &body);
    void onRequestError(const QString & error);
    void onRequestTimeOut();

private:
    enum class RequestType {
        none,
        login,
        registerUser
    };

    void setUi();
    void connectSlots();
    void showMessage(const QString &message);
    void setRequestBtnEnabled(bool  enabled);

    Ui::MainWindow *ui;
    HttpClient *m_http;
    ChatClient *m_chat;
    RequestType m_pending_request {RequestType::none};
};
#endif // MAINWINDOW_H
