#include "control/control_server.h"

#include "control/control_connection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTextStream>

#include <sys/stat.h>

namespace trowel::control {

ControlServer::ControlServer(MainWindow* window, QObject* parent)
    : QObject(parent), window_(window) {}

ControlServer::~ControlServer() { stop(); }

QString ControlServer::defaultSocketPath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    const QString dir = QDir(base).filePath("trowel");
    QDir().mkpath(dir);
    return QDir(dir).filePath(QString("ctl-%1.sock").arg(QCoreApplication::applicationPid()));
}

QString ControlServer::start(const QString& path) {
    stop();
    socketPath_ = path.isEmpty() ? defaultSocketPath() : path;

    QFile::remove(socketPath_);

    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);

    if (!server_->listen(socketPath_)) {
        QTextStream(stderr) << "[trowel] control socket listen failed: "
                            << server_->errorString() << '\n';
        server_->deleteLater();
        server_ = nullptr;
        socketPath_.clear();
        return {};
    }

    ::chmod(socketPath_.toLocal8Bit().constData(), 0600);

    connect(server_, &QLocalServer::newConnection,
            this, &ControlServer::onNewConnection);
    return socketPath_;
}

void ControlServer::stop() {
    if (!server_) return;
    server_->close();
    server_->deleteLater();
    server_ = nullptr;
    if (!socketPath_.isEmpty()) {
        QFile::remove(socketPath_);
        socketPath_.clear();
    }
}

void ControlServer::onNewConnection() {
    while (auto* sock = server_->nextPendingConnection()) {
        new ControlConnection(sock, window_, this);
    }
}

}
