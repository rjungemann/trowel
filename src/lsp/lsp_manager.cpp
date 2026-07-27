#include "lsp/lsp_manager.h"

#include "editor/editor_view.h"
#include "editor/lexers.h"
#include "lsp/lsp_client.h"
#include "lsp/lsp_position.h"
#include "repl/repl_session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace trowel {

namespace {

// The server recompiles the whole buffer on every didChange, inline on its
// single thread. Coalesce keystrokes so a fast typist doesn't queue a compile
// per character.
constexpr int kDidChangeDebounceMs = 250;

// Analysis can take a while on a large file; completion and hover are
// interactive and should give up fast rather than pop up over stale text.
constexpr int kInteractiveTimeoutMs = 1500;
constexpr int kInitializeTimeoutMs = 5000;

constexpr const char* kLanguageId = "turmeric";

} // namespace

const char* LspManager::kSkipUnsavedReason =
    "unsaved buffers get no language support — save the file to enable it";

LspManager* LspManager::instance() {
    static LspManager* self = new LspManager(qApp);
    return self;
}

LspManager::LspManager(QObject* parent)
    : QObject(parent)
{
    if (!enabledInSettings()) state_ = State::Disabled;
}

bool LspManager::enabledInSettings() {
    return QSettings().value("lsp/enabled", true).toBool();
}

QString LspManager::serverPath() const {
    const QString override = QSettings().value("lsp/serverPath").toString();
    if (!override.isEmpty()) {
        const QFileInfo fi(override);
        if (fi.exists() && fi.isFile() && fi.isExecutable()) return fi.absoluteFilePath();
    }
    // Same binary the REPL uses — settings override, then the copy bundled in
    // Trowel.app, then PATH.
    return ResolveTurBinary();
}

QString LspManager::UriForPath(const QString& path) {
    if (path.isEmpty()) return {};
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString();
}

QString LspManager::PathForUri(const QString& uri) {
    return QUrl(uri).toLocalFile();
}

QString LspManager::UriFor(EditorView* view) {
    if (!view) return {};
    if (view->language() != Language::Turmeric) return {};
    // No path means no stable URI, and the server resolves diagnostics against
    // a real file path. Untitled buffers are out of scope for v1.
    return UriForPath(view->filePath());
}

LspManager::DocState* LspManager::docFor(EditorView* view) {
    const QString uri = UriFor(view);
    if (uri.isEmpty()) return nullptr;
    const auto it = docs_.find(uri);
    return it == docs_.end() ? nullptr : &it.value();
}

EditorView* LspManager::primaryView(const QString& uri) {
    const auto it = docs_.find(uri);
    if (it == docs_.end()) return nullptr;
    for (const QPointer<EditorView>& v : it->views) {
        if (v) return v.data();
    }
    return nullptr;
}

void LspManager::setState(State s, const QString& error) {
    if (!error.isEmpty()) lastError_ = error;
    if (state_ == s) return;
    state_ = s;
    emit stateChanged();
}

bool LspManager::ensureStarted() {
    if (state_ == State::Disabled || state_ == State::Failed) return false;
    if (state_ == State::Starting || state_ == State::Ready) return true;

    const QString binary = serverPath();
    if (binary.isEmpty()) {
        setState(State::Failed, QStringLiteral("could not locate the `tur` binary"));
        return false;
    }

    if (!client_) {
        client_ = new LspClient(this);
        connect(client_, &LspClient::notificationReceived, this, &LspManager::onNotification);
        connect(client_, &LspClient::finished, this, &LspManager::onServerFinished);
        connect(client_, &LspClient::startFailed, this, [this](const QString& why) {
            setState(State::Failed, why);
        });
    }

    // Pin the stdlib next to the resolved binary, exactly as ReplSession does,
    // so an ambient TUR_STDLIB_DIR can't pair a new `tur` with an old stdlib.
    QStringList extraEnv;
    const QString siblingStdlib =
        QFileInfo(binary).absolutePath() + QStringLiteral("/stdlib");
    if (QDir(siblingStdlib).exists()) {
        extraEnv << QStringLiteral("TUR_STDLIB_DIR=") + siblingStdlib;
    }

    if (!client_->start(binary, {"lsp"}, QString(), extraEnv)) {
        setState(State::Failed, QStringLiteral("failed to start `%1 lsp`").arg(binary));
        return false;
    }
    setState(State::Starting);

    QJsonObject params{
        {"processId", QCoreApplication::applicationPid()},
        {"rootUri", QJsonValue::Null},
        {"clientInfo", QJsonObject{{"name", "Trowel"}}},
        {"capabilities", QJsonObject{
            {"textDocument", QJsonObject{
                {"synchronization", QJsonObject{{"dynamicRegistration", false}}},
                {"hover", QJsonObject{{"contentFormat", QJsonArray{"markdown", "plaintext"}}}},
                {"completion", QJsonObject{{"dynamicRegistration", false}}},
                {"publishDiagnostics", QJsonObject{{"relatedInformation", false}}},
            }},
        }},
    };
    client_->request("initialize", params,
                     [this](const QJsonValue&, const LspError* err) {
                         if (err) {
                             setState(State::Failed, err->message);
                             return;
                         }
                         onInitializeReply();
                     },
                     kInitializeTimeoutMs);
    return true;
}

void LspManager::onInitializeReply() {
    client_->notify("initialized", QJsonObject{});
    setState(State::Ready);
    flushPendingOpens();
}

void LspManager::flushPendingOpens() {
    for (auto it = docs_.begin(); it != docs_.end(); ++it) {
        if (!it->openOnServer) sendDidOpen(it.key());
    }
}

void LspManager::onServerFinished(int exitCode) {
    for (auto it = docs_.begin(); it != docs_.end(); ++it) it->openOnServer = false;

    if (state_ == State::Disabled) return;
    // A restart we asked for isn't a crash. QProcess can deliver `finished`
    // synchronously from inside terminate(), so this reentrancy is the norm,
    // not an edge case.
    if (restarting_) return;

    if (restartsRemaining_ <= 0) {
        setState(State::Failed,
                 QStringLiteral("language server keeps exiting (last code %1)").arg(exitCode));
        return;
    }
    restartsRemaining_--;
    setState(State::Stopped, QStringLiteral("language server exited (%1), restarting")
                                 .arg(exitCode));
    // Re-establish only if something is still open to serve.
    if (!docs_.isEmpty() && ensureStarted()) return;
    setState(State::Stopped);
}

void LspManager::openDocument(EditorView* view) {
    const QString uri = UriFor(view);
    if (uri.isEmpty()) return;
    if (state_ == State::Disabled || state_ == State::Failed) return;

    DocState& doc = docs_[uri];
    if (!doc.views.contains(view)) doc.views.append(view);

    if (!doc.debounce) {
        doc.debounce = new QTimer(this);
        doc.debounce->setSingleShot(true);
        doc.debounce->setInterval(kDidChangeDebounceMs);
        connect(doc.debounce, &QTimer::timeout, this, [this, uri] { sendDidChange(uri); });
    }

    if (!ensureStarted()) return;
    if (state_ == State::Ready && !doc.openOnServer) sendDidOpen(uri);
}

void LspManager::closeDocument(EditorView* view) {
    // The view's path may already have changed, so search by identity rather
    // than trusting UriFor() to still point at the right entry.
    for (auto it = docs_.begin(); it != docs_.end();) {
        it->views.removeAll(QPointer<EditorView>(view));
        it->views.removeAll(QPointer<EditorView>(nullptr));
        if (!it->views.isEmpty()) {
            ++it;
            continue;
        }
        const QString uri = it.key();
        if (it->openOnServer && client_ && state_ == State::Ready) {
            client_->notify("textDocument/didClose",
                            QJsonObject{{"textDocument", QJsonObject{{"uri", uri}}}});
        }
        if (it->debounce) it->debounce->deleteLater();
        it = docs_.erase(it);
        diagnostics_.remove(uri);
        emit diagnosticsUpdated(uri);
    }
}

void LspManager::documentChanged(EditorView* view) {
    DocState* doc = docFor(view);
    if (!doc) return;
    doc->version++;
    doc->generation++;
    if (doc->debounce) doc->debounce->start();  // restarts, coalescing bursts
}

void LspManager::sendDidOpen(const QString& uri) {
    if (!client_ || state_ != State::Ready) return;
    EditorView* view = primaryView(uri);
    if (!view) return;

    DocState& doc = docs_[uri];
    doc.openOnServer = true;
    client_->notify("textDocument/didOpen", QJsonObject{
        {"textDocument", QJsonObject{
            {"uri", uri},
            {"languageId", kLanguageId},
            {"version", doc.version},
            {"text", QString::fromUtf8(view->text())},
        }},
    });
}

void LspManager::sendDidChange(const QString& uri) {
    if (!client_ || state_ != State::Ready) return;
    const auto it = docs_.find(uri);
    if (it == docs_.end()) return;
    if (!it->openOnServer) {
        sendDidOpen(uri);
        return;
    }
    EditorView* view = primaryView(uri);
    if (!view) return;

    // Full-document sync: the server advertises textDocumentSync 1 and reads
    // only contentChanges[0].text, so a range change would be silently
    // mishandled.
    client_->notify("textDocument/didChange", QJsonObject{
        {"textDocument", QJsonObject{{"uri", uri}, {"version", it->version}}},
        {"contentChanges", QJsonArray{
            QJsonObject{{"text", QString::fromUtf8(view->text())}},
        }},
    });
}

void LspManager::onNotification(const QString& method, const QJsonObject& params) {
    if (method != QLatin1String("textDocument/publishDiagnostics")) return;

    const QString uri = params.value("uri").toString();
    if (uri.isEmpty()) return;

    QVector<LspDiagnostic> out;
    const QJsonArray items = params.value("diagnostics").toArray();
    out.reserve(items.size());
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        const QJsonObject range = o.value("range").toObject();
        const QJsonObject start = range.value("start").toObject();
        const QJsonObject end = range.value("end").toObject();

        LspDiagnostic d;
        d.severity = o.value("severity").toInt(1);
        if (d.severity < 1 || d.severity > 4) d.severity = 1;
        d.startLine = start.value("line").toInt();
        d.startChar = start.value("character").toInt();
        d.endLine = end.value("line").toInt();
        d.endChar = end.value("character").toInt();
        d.message = o.value("message").toString();
        d.source = o.value("source").toString();
        out.append(d);
    }

    diagnostics_.insert(uri, out);
    emit diagnosticsUpdated(uri);
}

QVector<LspDiagnostic> LspManager::diagnosticsFor(const QString& uri) const {
    return diagnostics_.value(uri);
}

bool LspManager::hasPublishedFor(const QString& uri) const {
    return diagnostics_.contains(uri);
}

void LspManager::requestCompletion(EditorView* view, int pos, CompletionCallback cb) {
    DocState* doc = docFor(view);
    if (!doc || !client_ || state_ != State::Ready) return;

    const QString uri = UriFor(view);
    const int generation = doc->generation;

    // Send any buffered edit first — completing against text the server hasn't
    // seen yet returns symbols from the wrong buffer.
    if (doc->debounce && doc->debounce->isActive()) {
        doc->debounce->stop();
        sendDidChange(uri);
    }

    client_->request("textDocument/completion", QJsonObject{
        {"textDocument", QJsonObject{{"uri", uri}}},
        {"position", LspPositionToJson(LspPositionFromPos(view->sciWidget(), pos))},
    },
    [this, cb = std::move(cb), uri, generation](const QJsonValue& result, const LspError* err) {
        if (err || !cb) return;
        const auto it = docs_.constFind(uri);
        if (it == docs_.constEnd() || it->generation != generation) return;  // stale

        // The server returns a bare CompletionItem[]; the spec also allows a
        // CompletionList. Accept both.
        QJsonArray items = result.isArray() ? result.toArray()
                                            : result.toObject().value("items").toArray();
        QStringList labels;
        labels.reserve(items.size());
        for (const QJsonValue& v : items) {
            const QString label = v.toObject().value("label").toString();
            if (!label.isEmpty()) labels.append(label);
        }
        cb(labels);
    },
    kInteractiveTimeoutMs);
}

void LspManager::requestHover(EditorView* view, int pos, HoverCallback cb) {
    DocState* doc = docFor(view);
    if (!doc || !client_ || state_ != State::Ready) return;

    const QString uri = UriFor(view);
    const int generation = doc->generation;

    client_->request("textDocument/hover", QJsonObject{
        {"textDocument", QJsonObject{{"uri", uri}}},
        {"position", LspPositionToJson(LspPositionFromPos(view->sciWidget(), pos))},
    },
    [this, cb = std::move(cb), uri, generation](const QJsonValue& result, const LspError* err) {
        if (err || !cb) return;
        const auto it = docs_.constFind(uri);
        if (it == docs_.constEnd() || it->generation != generation) return;  // stale

        // `contents` is a MarkedString, a MarkedString[], or a MarkupContent.
        const QJsonValue contents = result.toObject().value("contents");
        QString text;
        if (contents.isString()) {
            text = contents.toString();
        } else if (contents.isObject()) {
            text = contents.toObject().value("value").toString();
        } else if (contents.isArray()) {
            QStringList parts;
            for (const QJsonValue& v : contents.toArray()) {
                parts << (v.isString() ? v.toString() : v.toObject().value("value").toString());
            }
            text = parts.join('\n');
        }
        if (!text.trimmed().isEmpty()) cb(text);
    },
    kInteractiveTimeoutMs);
}

void LspManager::restart() {
    if (state_ == State::Disabled) return;
    restartsRemaining_ = 3;
    lastError_.clear();
    for (auto it = docs_.begin(); it != docs_.end(); ++it) it->openOnServer = false;
    // Diagnostics from the old process describe a buffer the new one hasn't
    // seen; drop them so nothing stale is left painted.
    const QList<QString> staleUris = diagnostics_.keys();
    diagnostics_.clear();

    restarting_ = true;
    if (client_) client_->stop();
    restarting_ = false;

    state_ = State::Stopped;
    ensureStarted();
    emit stateChanged();
    for (const QString& uri : staleUris) emit diagnosticsUpdated(uri);
}

void LspManager::shutdown() {
    if (!client_) return;
    if (state_ == State::Ready) {
        client_->request("shutdown", QJsonObject{}, nullptr, 200);
        client_->notify("exit", QJsonObject{});
    }
    client_->stop();
    setState(State::Stopped);
}

}
