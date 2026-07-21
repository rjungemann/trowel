#include "app/single_instance.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTextStream>

namespace trowel::single_instance {

namespace {

// Connect timeout: short — a live instance answers a local Unix socket almost
// instantly, and we don't want to stall the CLI if the socket is stale.
constexpr int kConnectTimeoutMs = 400;
constexpr int kWriteTimeoutMs = 2000;
constexpr int kReadTimeoutMs = 3000;

// Send one control request and wait for its `{"ok": ...}` reply. Returns true
// only on an affirmative reply; on any transport failure or a non-ok reply,
// fills `errOut` (if given) and returns false.
bool CallCommand(QLocalSocket& sock, int id, const QString& cmd,
                 const QJsonObject& args, QString* errOut) {
    QJsonObject req;
    req["id"] = id;
    req["cmd"] = cmd;
    req["args"] = args;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');

    if (sock.write(line) != line.size() || !sock.waitForBytesWritten(kWriteTimeoutMs)) {
        if (errOut) *errOut = QStringLiteral("write failed");
        return false;
    }

    QByteArray buf;
    int nl = -1;
    while ((nl = buf.indexOf('\n')) < 0) {
        if (!sock.waitForReadyRead(kReadTimeoutMs)) {
            if (errOut) *errOut = QStringLiteral("no reply from running instance");
            return false;
        }
        buf.append(sock.readAll());
    }

    const QJsonObject reply = QJsonDocument::fromJson(buf.left(nl)).object();
    if (!reply.value("ok").toBool()) {
        if (errOut) *errOut = reply.value("error").toObject().value("message").toString();
        return false;
    }
    return true;
}

}  // namespace

QString WellKnownSocketPath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    const QString dir = QDir(base).filePath("trowel");
    QDir().mkpath(dir);

    // Fold the active display into the name so two distinct GUI sessions of the
    // same user (e.g. a local seat plus a remote/nested session) don't share a
    // single-instance socket and steal each other's file-open requests.
    QString display = qEnvironmentVariable("WAYLAND_DISPLAY");
    if (display.isEmpty()) display = qEnvironmentVariable("DISPLAY");
    display.replace('/', '_').replace(':', '_');

    const QString name = display.isEmpty()
        ? QStringLiteral("single.sock")
        : QStringLiteral("single-%1.sock").arg(display);
    return QDir(dir).filePath(name);
}

bool ForwardToRunningInstance(const QStringList& absPaths) {
    QLocalSocket sock;
    sock.connectToServer(WellKnownSocketPath());
    if (!sock.waitForConnected(kConnectTimeoutMs)) {
        // No live instance (nothing listening, or a stale socket file). The
        // caller becomes the primary; ControlServer::start() clears the stale
        // file before it listens.
        return false;
    }

    int id = 0;
    for (const QString& path : absPaths) {
        QJsonObject args;
        args["path"] = path;
        QString err;
        if (!CallCommand(sock, ++id, QStringLiteral("editor.open"), args, &err)) {
            QTextStream(stderr) << "trowel: could not open " << path << ": " << err << '\n';
        }
    }

    // Bring the existing window forward even when no files were given (bare
    // `trowel` == "focus the running editor").
    QString err;
    CallCommand(sock, ++id, QStringLiteral("window.activate"), {}, &err);

    sock.disconnectFromServer();
    return true;
}

}  // namespace trowel::single_instance
