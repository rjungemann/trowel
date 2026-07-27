#pragma once

#include <QJsonValue>
#include <QObject>
#include <QByteArray>

class QLocalSocket;

namespace trowel {

class WindowManager;

namespace control {

struct ControlError {
    QString code;
    QString message;
};

class ControlConnection : public QObject {
    Q_OBJECT
public:
    ControlConnection(QLocalSocket* socket, WindowManager* windows, QObject* parent = nullptr);

    // Send a completed reply for a queued request id.
    void sendReply(const QJsonValue& id, const QJsonValue& result);
    void sendError(const QJsonValue& id, const ControlError& error);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void processLine(const QByteArray& line);
    void writeFrame(const class QJsonObject& obj);

    QLocalSocket* socket_;
    WindowManager* windows_;
    QByteArray buffer_;
};

}
}
