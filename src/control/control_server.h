#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace trowel {

class WindowManager;

namespace control {

class ControlServer : public QObject {
    Q_OBJECT
public:
    // Targets the registry rather than a fixed window: each request resolves
    // its own target, so the socket keeps working as windows open and close.
    ControlServer(WindowManager* windows, QObject* parent = nullptr);
    ~ControlServer() override;

    // Start listening on `path`. If empty, a default per-pid path is chosen.
    // Returns the resolved path, or an empty string on failure.
    QString start(const QString& path = {});
    void stop();

    QString socketPath() const { return socketPath_; }

private slots:
    void onNewConnection();

private:
    static QString defaultSocketPath();

    WindowManager* windows_;
    QLocalServer* server_ = nullptr;
    QString socketPath_;
};

}
}
