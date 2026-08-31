#include "debug/dap_client.h"

#include "lsp/lsp_transport.h"

#include <QTimer>

namespace trowel {

namespace {
// Local error codes. DAP has none of its own; these distinguish the classes of
// failure a caller can usefully branch on.
constexpr int kErrNotRunning = -1;
constexpr int kErrTimeout = -2;
constexpr int kErrAdapter = -3;       // success:false from the adapter
constexpr int kErrExited = -4;        // process died with requests in flight
}  // namespace

DapClient::DapClient(QObject* parent)
    : QObject(parent)
    , transport_(new LspTransport(this))
{
    connect(transport_, &LspTransport::messageReceived, this, &DapClient::onMessage);
    connect(transport_, &LspTransport::started, this, &DapClient::started);
    connect(transport_, &LspTransport::startFailed, this, [this](const QString& why) {
        failAllPending(why);
        emit startFailed(why);
    });
    connect(transport_, &LspTransport::finished, this, [this](int exitCode) {
        failAllPending(QStringLiteral("adapter exited (%1)").arg(exitCode));
        emit finished(exitCode);
    });
}

DapClient::~DapClient() {
    for (Pending& p : pending_) {
        if (p.timer) p.timer->stop();
    }
    pending_.clear();
}

bool DapClient::isRunning() const {
    return transport_->isRunning();
}

bool DapClient::start(const QString& program, const QStringList& args,
                      const QString& workingDir, const QStringList& extraEnv) {
    return transport_->start(program, args, workingDir, extraEnv);
}

void DapClient::stop() {
    transport_->terminate();
}

int DapClient::request(const QString& command, const QJsonObject& arguments,
                        Reply reply, int timeoutMs) {
    const int seq = nextSeq_++;

    if (!isRunning()) {
        const DapError err{kErrNotRunning, QStringLiteral("debug adapter is not running")};
        if (reply) reply(QJsonValue(), &err);
        return seq;
    }

    Pending p;
    p.reply = std::move(reply);
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    p.timer->setInterval(timeoutMs);
    connect(p.timer, &QTimer::timeout, this, [this, seq] {
        Pending taken;
        if (!takePending(seq, taken)) return;
        const DapError err{kErrTimeout, QStringLiteral("request timed out")};
        if (taken.reply) taken.reply(QJsonValue(), &err);
    });
    pending_.insert(seq, p);
    p.timer->start();

    QJsonObject msg{
        {"seq", seq},
        {"type", "request"},
        {"command", command},
    };
    if (!arguments.isEmpty()) msg.insert("arguments", arguments);
    transport_->send(msg);
    return seq;
}

void DapClient::forget(int seq) {
    Pending taken;
    takePending(seq, taken);
}

bool DapClient::takePending(int seq, Pending& out) {
    const auto it = pending_.find(seq);
    if (it == pending_.end()) return false;
    out = it.value();
    pending_.erase(it);
    if (out.timer) {
        out.timer->stop();
        out.timer->deleteLater();
    }
    return true;
}

void DapClient::failAllPending(const QString& why) {
    QHash<int, Pending> taken;
    taken.swap(pending_);
    const DapError err{kErrExited, why};
    for (Pending& p : taken) {
        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
        }
        if (p.reply) p.reply(QJsonValue(), &err);
    }
}

void DapClient::onMessage(const QJsonObject& msg) {
    const QString type = msg.value("type").toString();

    if (type == "event") {
        const QString event = msg.value("event").toString();
        emit eventReceived(event, msg.value("body").toObject());
        return;
    }

    if (type == "response") {
        const int seq = msg.value("request_seq").toInt(-1);
        Pending taken;
        if (!takePending(seq, taken)) return;
        if (!taken.reply) return;

        if (!msg.value("success").toBool(true)) {
            // `message` is optional in the spec; `tur dap` includes it on
            // failure. Fall back to the command name so the caller is never
            // left with an empty string.
            QString m = msg.value("message").toString();
            if (m.isEmpty()) m = QStringLiteral("request failed: ") + msg.value("command").toString();
            const DapError err{kErrAdapter, m};
            taken.reply(QJsonValue(), &err);
            return;
        }
        taken.reply(msg.value("body"), nullptr);
        return;
    }

    // Anything else (a server-initiated request, which `tur dap` never sends)
    // is ignored. The adapter does not issue reverse requests.
}

}
