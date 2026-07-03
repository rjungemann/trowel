#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace trowel {

class MainWindow;

namespace control {

class ControlServer : public QObject {
    Q_OBJECT
public:
    ControlServer(MainWindow* window, QObject* parent = nullptr);
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

    MainWindow* window_;
    QLocalServer* server_ = nullptr;
    QString socketPath_;
};

}
}
