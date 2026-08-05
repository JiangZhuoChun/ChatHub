#include "HttpClient.h"
#include <QTimer>
#include <QMessageBox>

void HttpClient::setTimeOut(const unsigned int ms) {
    m_timeout_ms = ms;
}

void HttpClient::get(const QUrl &url) {
    QNetworkReply *reply = m_manager.get(QNetworkRequest(url));
    setupReply(reply);
}
void HttpClient::post(const QUrl &url,const QByteArray &body) {
    QNetworkRequest request(url);
    //设置请求头
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = m_manager.post(request, body);
    setupReply(reply);
}

void HttpClient::setupReply(QNetworkReply *reply) {
    //设置定时器
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->start(m_timeout_ms);

    //绑定定时器超时信号
    connect(timer,&QTimer::timeout,this,[this,reply] {
       if (reply->isFinished()) return;
       reply->setProperty("TimeOut", true);
       reply->abort();
        //发送超时信号
       emit requestTimeOut();
    });

    //绑定网络请求完成信号
    connect(reply,&QNetworkReply::finished,this,[this,reply,timer] {
        timer->stop();
        if (reply->property("TimeOut").toBool()) {
            reply->deleteLater();
            return;
        }
        //获取 HTTP 响应状态码
        const auto statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode >0) {
            // 有HTTP响应，无论200/4xx/5xx
            emit requestFinish(statusCode, reply->readAll());
        }else if (reply->error() != QNetworkReply::NoError) {
            //// 底层网络故障：连不上、超时、ssl错误
            emit requestError(reply->errorString());
        }
        reply->deleteLater();
    });
}
