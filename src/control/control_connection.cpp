#include "control/control_connection.h"

#include "control/control_handlers.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>

namespace trowel::control {

ControlConnection::ControlConnection(QLocalSocket* socket, MainWindow* window, QObject* parent)
    : QObject(parent), socket_(socket), window_(window) {
    socket_->setParent(this);
    connect(socket_, &QLocalSocket::readyRead, this, &ControlConnection::onReadyRead);
    connect(socket_, &QLocalSocket::disconnected, this, &ControlConnection::onDisconnected);
}

void ControlConnection::onDisconnected() {
    deleteLater();
}

void ControlConnection::onReadyRead() {
    buffer_.append(socket_->readAll());
    int newline;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        QByteArray line = buffer_.left(newline);
        buffer_.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line);
    }
}

void ControlConnection::processLine(const QByteArray& line) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject reply;
        reply["id"] = QJsonValue::Null;
        reply["ok"] = false;
        QJsonObject e;
        e["code"] = "invalid_json";
        e["message"] = err.errorString();
        reply["error"] = e;
        writeFrame(reply);
        return;
    }
    const QJsonObject req = doc.object();
    const QJsonValue id = req.value("id");
    const QString cmd = req.value("cmd").toString();
    const QJsonObject args = req.value("args").toObject();

    if (cmd.isEmpty()) {
        sendError(id, {"missing_cmd", "request has no `cmd` field"});
        return;
    }

    QPointer<ControlConnection> self(this);
    Reply reply = [self, id](const QJsonValue& result, const ControlError* error) {
        if (!self) return;
        if (error) self->sendError(id, *error);
        else self->sendReply(id, result);
    };
    Dispatch(window_, self, cmd, args, std::move(reply));
}

void ControlConnection::sendReply(const QJsonValue& id, const QJsonValue& result) {
    QJsonObject o;
    o["id"] = id;
    o["ok"] = true;
    if (!result.isUndefined()) o["result"] = result;
    writeFrame(o);
}

void ControlConnection::sendError(const QJsonValue& id, const ControlError& error) {
    QJsonObject o;
    o["id"] = id;
    o["ok"] = false;
    QJsonObject e;
    e["code"] = error.code;
    e["message"] = error.message;
    o["error"] = e;
    writeFrame(o);
}

void ControlConnection::writeFrame(const QJsonObject& obj) {
    if (!socket_) return;
    QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    socket_->write(bytes);
    socket_->flush();
}

}
