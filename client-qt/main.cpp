#include "logindialog.h"
#include "mainwindow.h"
#include "chatclient.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ChatClient chat_client;

    QString username;
    {
        LoginDialog login_dialog(&chat_client);
        if (login_dialog.exec() != QDialog::Accepted) {
            return 0;
        }
        username = login_dialog.username();
    }

    MainWindow chat_window(&chat_client, username);
    chat_window.show();
    return app.exec();
}
