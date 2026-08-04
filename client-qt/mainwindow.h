#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "HttpClient.h"

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
    void setUi();
    void connectSlots();

private slots:
    void on_loginBtn_clicked();
    void on_registerBtn_clicked();
    void onRequestFinish(int statusCode, const QByteArray &body);
    void onRequestError(const QString & error);
    void onRequestTimeOut();


private:
    Ui::MainWindow *ui;
    HttpClient *m_http;
};
#endif // MAINWINDOW_H
