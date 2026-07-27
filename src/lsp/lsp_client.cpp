#include "lsp/lsp_client.h"

#include "lsp/lsp_transport.h"

#include <QTimer>

namespace trowel {

LspClient::LspClient(QObject* parent)
    : QObject(parent)
    , transport_(new LspTransport(this))
{
    connect(transport_, &LspTransport::messageReceived, this, &LspClient::onMessage);
    connect(transport_, &LspTransport::started, this, &LspClient::started);
    connect(transport_, &LspTransport::startFailed, this, [this](const QString& why) {
        failAllPending(why);
        emit startFailed(why);
    });
    connect(transport_, &LspTransport::finished, this, [this](int exitCode) {
        failAllPending(QStringLiteral("server exited (%1)").arg(exitCode));
        emit finished(exitCode);
    });
}

LspClient::~LspClient() {
    // Drop replies without calling them: the owner is going away, so invoking
    // callbacks now would re-enter a half-destroyed object.
    for (Pending& p : pending_) {
        if (p.timer) p.timer->stop();
    }
    pending_.clear();
}

bool LspClient::isRunning() const {
    return transport_->isRunning();
}

bool LspClient::start(const QString& program, const QStringList& args,
                      const QString& workingDir, const QStringList& extraEnv) {
    return transport_->start(program, args, workingDir, extraEnv);
}

void LspClient::stop() {
    transport_->terminate();
}

int LspClient::request(const QString& method, const QJsonObject& params, Reply reply,
                       int timeoutMs) {
    const int id = nextId_++;

    if (!isRunning()) {
        // Answer synchronously rather than leaving the caller hanging on a
        // server that isn't there.
        const LspError err{-32003, QStringLiteral("language server is not running")};
        if (reply) reply(QJsonValue(), &err);
        return id;
    }

    Pending p;
    p.reply = std::move(reply);
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    p.timer->setInterval(timeoutMs);
    connect(p.timer, &QTimer::timeout, this, [this, id] {
        Pending taken;
        if (!takePending(id, taken)) return;
        const LspError err{-32000, QStringLiteral("request timed out")};
        if (taken.reply) taken.reply(QJsonValue(), &err);
    });
    pending_.insert(id, p);
    p.timer->start();

    QJsonObject msg{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", params},
    };
    transport_->send(msg);
    return id;
}

void LspClient::notify(const QString& method, const QJsonObject& params) {
    if (!isRunning()) return;
    transport_->send(QJsonObject{
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params},
    });
}

void LspClient::forget(int id) {
    Pending taken;
    takePending(id, taken);  // discarded without invoking
}

bool LspClient::takePending(int id, Pending& out) {
    const auto it = pending_.find(id);
    if (it == pending_.end()) return false;
    out = it.value();
    pending_.erase(it);
    if (out.timer) {
        out.timer->stop();
        out.timer->deleteLater();
    }
    return true;
}

void LspClient::failAllPending(const QString& why) {
    // Swap first: a Reply may itself issue a new request, which would otherwise
    // mutate the table we're iterating.
    QHash<int, Pending> taken;
    taken.swap(pending_);
    const LspError err{-32002, why};
    for (Pending& p : taken) {
        if (p.timer) {
            p.timer->stop();
            p.timer->deleteLater();
        }
        if (p.reply) p.reply(QJsonValue(), &err);
    }
}

void LspClient::onMessage(const QJsonObject& msg) {
    const bool hasId = msg.contains("id") && !msg.value("id").isNull();

    if (!hasId) {
        // Server → client notification (publishDiagnostics, logMessage, …).
        const QString method = msg.value("method").toString();
        if (!method.isEmpty()) {
            emit notificationReceived(method, msg.value("params").toObject());
        }
        return;
    }

    if (msg.contains("method")) {
        // A server → client *request*. Turmeric's server issues none, but the
        // protocol allows them and leaving one unanswered would hang a
        // conforming server, so refuse explicitly.
        transport_->send(QJsonObject{
            {"jsonrpc", "2.0"},
            {"id", msg.value("id")},
            {"error", QJsonObject{{"code", -32601}, {"message", "method not found"}}},
        });
        return;
    }

    Pending taken;
    if (!takePending(msg.value("id").toInt(), taken)) return;  // late/duplicate
    if (!taken.reply) return;

    if (msg.contains("error")) {
        const QJsonObject e = msg.value("error").toObject();
        const LspError err{e.value("code").toInt(), e.value("message").toString()};
        taken.reply(QJsonValue(), &err);
        return;
    }
    taken.reply(msg.value("result"), nullptr);
}

}
