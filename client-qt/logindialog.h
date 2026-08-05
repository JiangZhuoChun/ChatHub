#pragma once

#include <QDialog>
#include <QString>

class ChatClient;
class HttpClient;

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(ChatClient *chat_client, QWidget *parent = nullptr);
    ~LoginDialog() override;

   QString username() const;

protected:
    void reject() override;

private slots:
    void on_loginBtn_clicked();
    void on_registerBtn_clicked();
    void on_cancelBtn_clicked();

    void onRequestFinish(int status_code, const QByteArray &body);
    void onRequestError(const QString &error);
    void onRequestTimeOut();

    void onAuthFrameSent();
    void onAuthSucceeded();
    void onAuthFailed(const QString &reason);
    void onConnectionFailed(const QString &reason);



private:
    enum class RequestType {
        none,
        login,
        registerUser
    };

    void setupUiState();
    void connectSlots();
    void showMessage(const QString &message);
    void setRequestButtonsEnabled(bool enabled);

    Ui::LoginDialog *ui;
    HttpClient *m_http;
    ChatClient *m_chat;
    RequestType m_pending_request {RequestType::none};
    QString m_username;
};
