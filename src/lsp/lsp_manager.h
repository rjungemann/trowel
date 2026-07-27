#pragma once

#include "lsp/lsp_diagnostic.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QTimer;

namespace trowel {

class EditorView;
class LspClient;

// Owns the single `tur lsp` child shared by the whole application.
//
// One server, not one per window. The Turmeric server indexes only the
// documents it has been sent (workspace/symbol iterates the open-doc store),
// so a per-window rootUri buys nothing — while a second process would mean a
// second blocking compiler competing for the same CPU and a duplicate copy of
// every shared document.
//
// Documents are keyed by URI, not by EditorView: the same file can be open in
// two windows, and LSP has exactly one document per URI. didOpen fires when the
// first view attaches, didClose when the last one detaches.
class LspManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disabled,  // turned off in settings — no child is ever spawned
        Stopped,   // enabled, nothing running yet (nothing has asked for it)
        Starting,  // spawned, initialize in flight
        Ready,
        Failed,    // gave up after repeated crashes; see lastError()
    };

    // Application-wide instance, parented to qApp. Created on first use so a
    // Trowel session that never opens a Turmeric file never spawns a server.
    static LspManager* instance();

    State state() const { return state_; }
    QString lastError() const { return lastError_; }
    // Resolved path of the server binary, or empty if none could be found.
    QString serverPath() const;
    static bool enabledInSettings();

    // Attach/detach a buffer. Non-Turmeric buffers and buffers with no path are
    // ignored — see kSkipUnsavedReason.
    void openDocument(EditorView* view);
    void closeDocument(EditorView* view);
    // Buffer edited; schedules a debounced didChange.
    void documentChanged(EditorView* view);

    using CompletionCallback = std::function<void(const QStringList& labels)>;
    using HoverCallback = std::function<void(const QString& text)>;

    // Both drop their reply if the document changed underneath them, so a
    // stale popup can never appear over newer text. The callback simply isn't
    // invoked in that case.
    void requestCompletion(EditorView* view, int pos, CompletionCallback cb);
    void requestHover(EditorView* view, int pos, HoverCallback cb);

    QVector<LspDiagnostic> diagnosticsFor(const QString& uri) const;
    // True once the server has published at least one batch for this URI.
    // Distinguishes "analyzed, clean" from "not analyzed yet" — diagnosticsFor
    // returns an empty vector for both.
    bool hasPublishedFor(const QString& uri) const;

    void restart();
    void shutdown();

    static QString UriForPath(const QString& path);
    static QString PathForUri(const QString& uri);

    // Why an unsaved buffer gets no language support, surfaced in the UI rather
    // than failing silently.
    static const char* kSkipUnsavedReason;

signals:
    void diagnosticsUpdated(const QString& uri);
    void stateChanged();

private:
    explicit LspManager(QObject* parent = nullptr);

    struct DocState {
        QVector<QPointer<EditorView>> views;
        int version = 0;
        // Bumped on every edit. A request captures it and discards its reply if
        // it no longer matches — the staleness guard.
        int generation = 0;
        QTimer* debounce = nullptr;
        bool openOnServer = false;
    };

    bool ensureStarted();
    void setState(State s, const QString& error = {});
    void onInitializeReply();
    void onNotification(const QString& method, const QJsonObject& params);
    void onServerFinished(int exitCode);

    void sendDidOpen(const QString& uri);
    void sendDidChange(const QString& uri);
    void flushPendingOpens();

    // The URI a view maps to, or empty when the view isn't eligible.
    static QString UriFor(EditorView* view);
    DocState* docFor(EditorView* view);
    EditorView* primaryView(const QString& uri);

    LspClient* client_ = nullptr;
    State state_ = State::Stopped;
    // Set while restart() is tearing the child down, so the resulting `finished`
    // signal isn't mistaken for a crash and counted against restartsRemaining_.
    bool restarting_ = false;
    QString lastError_;
    int restartsRemaining_ = 3;

    QHash<QString, DocState> docs_;
    QHash<QString, QVector<LspDiagnostic>> diagnostics_;
};

}
