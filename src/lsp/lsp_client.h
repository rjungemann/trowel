#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

namespace trowel {

class LspTransport;

struct LspError {
    int code = 0;
    QString message;
};

// JSON-RPC 2.0 over an LspTransport: request/response correlation by id,
// fire-and-forget notifications, and server-pushed notifications surfaced as a
// signal.
//
// Request correlation mirrors the async-reply pattern in control_handlers.cpp
// (Reply + WaitCtx + ArmTimeout): every Reply is invoked exactly once — with a
// result, with an error, on timeout, or on the server dying — and never after
// the client is destroyed.
//
// Timeouts are not optional here. The Turmeric server runs a full compile
// inline on its single thread with no $/cancelRequest, so a slow analysis can
// stall every later request behind it.
class LspClient : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonValue& result, const LspError* error)>;

    static constexpr int kDefaultTimeoutMs = 2000;

    explicit LspClient(QObject* parent = nullptr);
    ~LspClient() override;

    bool start(const QString& program, const QStringList& args,
               const QString& workingDir = {}, const QStringList& extraEnv = {});
    bool isRunning() const;
    void stop();

    // Returns the request id, so callers can drop a stale reply via forget().
    int request(const QString& method, const QJsonObject& params, Reply reply,
                int timeoutMs = kDefaultTimeoutMs);
    void notify(const QString& method, const QJsonObject& params);

    // Stop caring about a reply. The Reply is discarded without being called —
    // the one exception to call-exactly-once, and the caller's explicit choice.
    // The server has no $/cancelRequest, so this is purely client-side.
    void forget(int id);

    int pendingCount() const { return int(pending_.size()); }

signals:
    void notificationReceived(const QString& method, const QJsonObject& params);
    void started();
    void finished(int exitCode);
    void startFailed(const QString& message);

private:
    struct Pending {
        Reply reply;
        QTimer* timer = nullptr;
    };

    void onMessage(const QJsonObject& msg);
    // Take the pending entry out of the table and tear down its timer. Returns
    // false if the id is unknown (late reply after a timeout) — callers must
    // not invoke anything in that case.
    bool takePending(int id, Pending& out);
    void failAllPending(const QString& why);

    LspTransport* transport_;
    QHash<int, Pending> pending_;
    int nextId_ = 1;
};

}
