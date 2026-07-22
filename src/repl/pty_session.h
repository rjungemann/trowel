#pragma once

#include <QObject>
#include <QStringList>

class QSocketNotifier;

namespace trowel {

class PtySession : public QObject {
    Q_OBJECT
public:
    explicit PtySession(QObject* parent = nullptr);
    ~PtySession() override;

    // `extraEnv` holds "KEY=VALUE" entries applied (overwriting) in the child
    // before exec — used to pin TUR_STDLIB_DIR to the launched binary's stdlib.
    bool start(const QString& program, const QStringList& args,
               const QString& workingDir = {}, const QStringList& extraEnv = {});
    bool isRunning() const { return pid_ > 0; }

    void write(const QByteArray& bytes);
    void resize(int rows, int cols);
    void terminate();

signals:
    void dataReceived(const QByteArray& bytes);
    void finished(int exitStatus);
    void startFailed(const QString& message);

private slots:
    void onMasterReadable();

private:
    void reap();

    int master_ = -1;
    long pid_ = -1;
    QSocketNotifier* notifier_ = nullptr;
};

}
