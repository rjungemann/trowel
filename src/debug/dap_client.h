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

// A Debug Adapter Protocol error carried back from the adapter.
//
// DAP responses report failure as `success: false` plus a `message`; there is
// no numeric error code (unlike LSP's `error.code`). We synthesize a small set
// of codes locally so callers can branch on the class of failure rather than
// parsing free text.
struct DapError {
    int code = 0;
    QString message;
};

// A narrow DAP client over an `LspTransport`. DAP uses the same
// `Content-Length` framing as LSP, so the transport is reused unchanged — it
// carries no LSP semantics. This class owns only the DAP envelope
// (`type`/`command`/`arguments`, `request_seq`/`body`/`success`) and the
// id→Reply correlation table, mirroring `LspClient`.
//
// Built to speak exactly the requests `tur dap` handles (initialize, launch,
// setBreakpoints, configurationDone, threads, stackTrace, scopes, variables,
// evaluate, continue/next/stepIn/stepOut, pause, disconnect, terminate) and
// nothing else. A general DAP client is a different, larger thing.
class DapClient : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonValue& body, const DapError* error)>;

    static constexpr int kDefaultTimeoutMs = 5000;

    explicit DapClient(QObject* parent = nullptr);
    ~DapClient() override;

    bool start(const QString& program, const QStringList& args,
               const QString& workingDir = {}, const QStringList& extraEnv = {});
    bool isRunning() const;
    void stop();

    // Send a request; `reply` is called exactly once with the response `body`
    // or an error. Returns the sequence id (mostly for `forget`).
    int request(const QString& command, const QJsonObject& arguments, Reply reply,
                int timeoutMs = kDefaultTimeoutMs);

    // Forget a pending request without firing its reply. Used when the caller
    // no longer cares about the answer (e.g. the session is being torn down).
    void forget(int seq);

    int pendingCount() const { return int(pending_.size()); }

signals:
    // A DAP event (`type: "event"`). `event` is the event name; `body` is its
    // payload (possibly empty). Emitted as soon as the message arrives, in
    // arrival order — DAP interleaves events with responses, and the session
    // state machine depends on seeing `stopped`/`exited`/`terminated` exactly
    // when the adapter sends them.
    void eventReceived(const QString& event, const QJsonObject& body);

    void started();
    void finished(int exitCode);
    void startFailed(const QString& message);

private:
    struct Pending {
        Reply reply;
        QTimer* timer = nullptr;
    };

    void onMessage(const QJsonObject& msg);
    bool takePending(int seq, Pending& out);
    void failAllPending(const QString& why);

    LspTransport* transport_;
    QHash<int, Pending> pending_;
    int nextSeq_ = 1;
};

}
