#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
class HttpClient : public QObject {
    Q_OBJECT
public:
    explicit HttpClient(QObject *parent = nullptr) : QObject(parent)
    {}

    void get(const QUrl &url);
    void post(const QUrl &url, const QByteArray &body);
    void setTimeOut(unsigned int ms);

signals:
    void requestTimeOut();
    void requestFinish(int statusCode, const QByteArray &body);
    void requestError(const QString & error);

private:
    void setupReply(QNetworkReply *reply);

    QNetworkAccessManager m_manager;
    unsigned int m_timeout_ms = 5000;
};