#include "control/control_handlers.h"

#include "control/control_connection.h"
#include "app/main_window.h"
#include "editor/editor_view.h"
#include "repl/repl_session.h"
#include "repl/pty_session.h"
#include "repl/terminal_view.h"
#include "repl/run_buffer.h"

#include <ScintillaEdit.h>

#include <QAction>
#include <QApplication>
#include <QHash>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSplitter>
#include <QTimer>

#include <memory>

namespace trowel::control {

namespace {

// Convenience: reply with a simple {"ok": true} object when there's nothing
// meaningful to return.
inline QJsonObject Ok() { QJsonObject o; o["ok"] = true; return o; }

inline void ReplyErr(const Reply& reply, const QString& code, const QString& msg) {
    ControlError e{code, msg};
    reply({}, &e);
}

Qt::KeyboardModifiers ParseMods(const QJsonArray& mods) {
    Qt::KeyboardModifiers m = Qt::NoModifier;
    for (const QJsonValue& v : mods) {
        const QString s = v.toString().toLower();
        if (s == "ctrl" || s == "control") m |= Qt::ControlModifier;
        else if (s == "shift") m |= Qt::ShiftModifier;
        else if (s == "alt") m |= Qt::AltModifier;
        else if (s == "meta" || s == "cmd") m |= Qt::MetaModifier;
    }
    return m;
}

// Very small subset of Qt::Key names. Extend as smoke tests need.
static const QHash<QString, int>& KeyTable() {
    static const QHash<QString, int> t = {
        {"return", Qt::Key_Return}, {"enter", Qt::Key_Return},
        {"backspace", Qt::Key_Backspace}, {"tab", Qt::Key_Tab},
        {"escape", Qt::Key_Escape}, {"esc", Qt::Key_Escape},
        {"space", Qt::Key_Space},
        {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
        {"left", Qt::Key_Left}, {"right", Qt::Key_Right},
        {"home", Qt::Key_Home}, {"end", Qt::Key_End},
        {"delete", Qt::Key_Delete}, {"del", Qt::Key_Delete},
        {"pageup", Qt::Key_PageUp}, {"pagedown", Qt::Key_PageDown},
        {"insert", Qt::Key_Insert},
    };
    return t;
}

bool ParseKey(const QString& name, int& outKey, QString& outText) {
    const QString lower = name.toLower();
    auto it = KeyTable().find(lower);
    if (it != KeyTable().end()) {
        outKey = it.value();
        // Provide sensible text for keys that produce characters.
        switch (outKey) {
            case Qt::Key_Return: outText = "\r"; break;
            case Qt::Key_Tab: outText = "\t"; break;
            case Qt::Key_Space: outText = " "; break;
            default: outText.clear();
        }
        return true;
    }
    if (name.size() == 1) {
        const QChar c = name.at(0);
        outText = QString(c);
        outKey = c.toUpper().unicode();
        return true;
    }
    return false;
}

void SendKeyToWidget(QWidget* target, int key, Qt::KeyboardModifiers mods, const QString& text) {
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(target, &release);
}

QAction* FindMenuAction(QMenuBar* bar, const QStringList& path) {
    if (!bar || path.isEmpty()) return nullptr;
    auto stripAmp = [](QString s) { return s.remove(QChar('&')); };

    QList<QAction*> current = bar->actions();
    QAction* found = nullptr;
    for (int i = 0; i < path.size(); ++i) {
        const QString target = path[i];
        found = nullptr;
        for (QAction* a : current) {
            if (stripAmp(a->text()).compare(target, Qt::CaseInsensitive) == 0) {
                found = a;
                break;
            }
        }
        if (!found) return nullptr;
        if (i + 1 < path.size()) {
            if (!found->menu()) return nullptr;
            current = found->menu()->actions();
        }
    }
    return found;
}

// Handler wiring ---------------------------------------------------------

struct WaitCtx {
    bool done = false;
    QMetaObject::Connection conn;
    QMetaObject::Connection conn2;
    QTimer* timer = nullptr;
};

void ArmTimeout(std::shared_ptr<WaitCtx> ctx, QObject* parent, int ms, Reply reply) {
    ctx->timer = new QTimer(parent);
    ctx->timer->setSingleShot(true);
    QObject::connect(ctx->timer, &QTimer::timeout, parent, [ctx, reply]() {
        if (ctx->done) return;
        ctx->done = true;
        QObject::disconnect(ctx->conn);
        QObject::disconnect(ctx->conn2);
        ControlError e{"timeout", "wait timed out"};
        reply({}, &e);
    });
    ctx->timer->start(ms);
}

void HandleWindowFocus(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    const QString pane = args.value("pane").toString();
    if (pane == "editor") { w->editorView()->setFocus(); reply(Ok(), nullptr); return; }
    if (pane == "terminal" || pane == "repl") { w->terminalView()->setFocus(); reply(Ok(), nullptr); return; }
    ReplyErr(reply, "bad_pane", QString("unknown pane: %1").arg(pane));
}

void HandleWindowGeometry(MainWindow* w, const QJsonObject&, const Reply& reply) {
    QJsonObject o;
    const QRect g = w->geometry();
    o["x"] = g.x(); o["y"] = g.y(); o["w"] = g.width(); o["h"] = g.height();
    const auto sizes = w->splitter()->sizes();
    QJsonArray a; for (int s : sizes) a.append(s);
    o["splitter"] = a;
    o["title"] = w->windowTitle();
    o["file_path"] = w->editorView()->filePath();
    reply(o, nullptr);
}

void HandleWindowSetSplitter(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    if (args.contains("sizes")) {
        QList<int> sizes;
        for (const QJsonValue& v : args.value("sizes").toArray()) sizes.append(v.toInt());
        w->splitter()->setSizes(sizes);
        reply(Ok(), nullptr);
        return;
    }
    if (args.contains("pos")) {
        const int pos = args.value("pos").toInt();
        const int total = w->splitter()->orientation() == Qt::Horizontal
                          ? w->splitter()->width() : w->splitter()->height();
        w->splitter()->setSizes({pos, qMax(0, total - pos)});
        reply(Ok(), nullptr);
        return;
    }
    ReplyErr(reply, "bad_args", "expected `pos` or `sizes`");
}

void HandleMenuInvoke(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    QStringList path;
    for (const QJsonValue& v : args.value("path").toArray()) path.append(v.toString());
    QAction* a = FindMenuAction(w->menuBar(), path);
    if (!a) { ReplyErr(reply, "no_action", "menu action not found"); return; }
    if (!a->isEnabled()) { ReplyErr(reply, "action_disabled", "menu action is disabled"); return; }
    a->trigger();
    reply(Ok(), nullptr);
}

void HandleEditorOpen(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    const QString path = args.value("path").toString();
    if (path.isEmpty()) { ReplyErr(reply, "bad_args", "missing `path`"); return; }
    if (!w->openPath(path)) { ReplyErr(reply, "open_failed", QString("could not open %1").arg(path)); return; }
    reply(Ok(), nullptr);
}

void HandleEditorSave(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    EditorView* e = w->editorView();
    const QString path = args.value("path").toString();
    const bool ok = path.isEmpty() ? e->saveCurrent() : e->saveFile(path);
    if (!ok) { ReplyErr(reply, "save_failed", "save failed"); return; }
    reply(Ok(), nullptr);
}

void HandleEditorSetText(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    w->editorView()->setText(args.value("text").toString().toUtf8());
    reply(Ok(), nullptr);
}

void HandleEditorType(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    const QString text = args.value("text").toString();
    QWidget* target = w->editorView()->sciWidget();
    if (!target) { ReplyErr(reply, "no_target", "editor unavailable"); return; }
    for (QChar c : text) {
        int key;
        if (c == QChar('\n') || c == QChar('\r')) key = Qt::Key_Return;
        else if (c == QChar('\t')) key = Qt::Key_Tab;
        else key = c.toUpper().unicode();
        SendKeyToWidget(target, key, Qt::NoModifier, QString(c));
    }
    reply(Ok(), nullptr);
}

void HandleEditorPress(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    int key = 0; QString text;
    if (!ParseKey(args.value("key").toString(), key, text)) {
        ReplyErr(reply, "bad_key", "unknown key name"); return;
    }
    const Qt::KeyboardModifiers mods = ParseMods(args.value("mods").toArray());
    // Modified keys typically don't produce text.
    if (mods && mods != Qt::ShiftModifier) text.clear();
    SendKeyToWidget(w->editorView()->sciWidget(), key, mods, text);
    reply(Ok(), nullptr);
}

void HandleEditorGetText(MainWindow* w, const QJsonObject&, const Reply& reply) {
    EditorView* e = w->editorView();
    const QByteArray raw = e->text();
    QJsonObject o;
    if (raw.size() > 4 * 1024 * 1024) {
        o["text_truncated"] = true;
        o["size"] = raw.size();
    } else {
        o["text"] = QString::fromUtf8(raw);
    }
    o["modified"] = e->isModified();
    o["path"] = e->filePath();
    reply(o, nullptr);
}

void HandleEditorGetCursor(MainWindow* w, const QJsonObject&, const Reply& reply) {
    EditorView* e = w->editorView();
    const int pos = e->cursorPos();
    const auto [line, col] = e->lineColFromPos(pos);
    QJsonObject o;
    o["pos"] = pos;
    o["line"] = line;
    o["col"] = col;
    o["anchor"] = e->anchorPos();
    const auto [s, en] = e->selectionRange();
    QJsonArray sel; sel.append(s); sel.append(en);
    o["selection"] = sel;
    reply(o, nullptr);
}

void HandleEditorSetCursor(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    EditorView* e = w->editorView();
    int pos = -1;
    if (args.contains("pos")) pos = args.value("pos").toInt();
    else if (args.contains("line")) {
        const int line = args.value("line").toInt();
        const int col = args.value("col").toInt(0);
        pos = e->posFromLineCol(line, col);
    }
    if (pos < 0) { ReplyErr(reply, "bad_args", "expected `pos` or `line`"); return; }
    e->setCursorPos(pos);
    reply(Ok(), nullptr);
}

void HandleEditorGetSelection(MainWindow* w, const QJsonObject&, const Reply& reply) {
    EditorView* e = w->editorView();
    const auto [s, en] = e->selectionRange();
    QJsonObject o;
    o["start"] = s; o["end"] = en;
    o["text"] = QString::fromUtf8(e->textInRange(s, en));
    reply(o, nullptr);
}

void HandleEditorSetSelection(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    const int start = args.value("start").toInt();
    const int end = args.value("end").toInt();
    w->editorView()->setSelection(start, end);
    reply(Ok(), nullptr);
}

void HandleEditorGetStyleAt(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    const int pos = args.value("pos").toInt();
    QJsonObject o;
    o["style"] = w->editorView()->styleAt(pos);
    reply(o, nullptr);
}

// --- REPL ---------------------------------------------------------------

void HandleReplSend(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    ReplSession* r = w->replSession();
    if (!r || !r->isRunning()) { ReplyErr(reply, "no_repl", "REPL not running"); return; }
    QByteArray bytes = args.value("text").toString().toUtf8();
    const bool newline = args.value("newline").toBool(true);
    if (newline && !bytes.endsWith('\r') && !bytes.endsWith('\n')) bytes.append('\r');
    r->pty()->write(bytes);
    reply(Ok(), nullptr);
}

void HandleReplPress(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    ReplSession* r = w->replSession();
    if (!r || !r->isRunning()) { ReplyErr(reply, "no_repl", "REPL not running"); return; }
    const QString name = args.value("key").toString().toLower();
    const Qt::KeyboardModifiers mods = ParseMods(args.value("mods").toArray());

    QByteArray seq;
    if (name == "return" || name == "enter") seq = "\r";
    else if (name == "backspace") seq = "\x7f";
    else if (name == "tab") seq = "\t";
    else if (name == "escape" || name == "esc") seq = "\x1b";
    else if (name == "up") seq = "\x1b[A";
    else if (name == "down") seq = "\x1b[B";
    else if (name == "right") seq = "\x1b[C";
    else if (name == "left") seq = "\x1b[D";
    else if (name == "home") seq = "\x1b[H";
    else if (name == "end") seq = "\x1b[F";
    else if (name == "delete" || name == "del") seq = "\x1b[3~";
    else if (name.size() == 1) {
        const char c = name.at(0).toLatin1();
        if (mods & Qt::ControlModifier) {
            if (c >= 'a' && c <= 'z') seq.append(char(c - 'a' + 1));
            else { ReplyErr(reply, "bad_key", "unsupported Ctrl-<key>"); return; }
        } else {
            seq.append(c);
        }
    } else {
        ReplyErr(reply, "bad_key", "unknown key name"); return;
    }
    r->pty()->write(seq);
    reply(Ok(), nullptr);
}

void HandleReplGetScreen(MainWindow* w, const QJsonObject& args, const Reply& reply) {
    const int lines = args.contains("lines") ? args.value("lines").toInt() : 40;
    const int capped = qMin(qMax(lines, 1), 500);
    QJsonObject o;
    o["text"] = w->terminalView()->screenText(capped);
    reply(o, nullptr);
}

void HandleReplGetCursor(MainWindow* w, const QJsonObject&, const Reply& reply) {
    const auto [line, col] = w->terminalView()->screenCursor();
    QJsonObject o;
    o["line"] = line; o["col"] = col;
    reply(o, nullptr);
}

void HandleReplRestart(MainWindow* w, const QJsonObject&, const Reply& reply) {
    QMetaObject::invokeMethod(w, "restartRepl", Qt::QueuedConnection);
    reply(Ok(), nullptr);
}

void HandleReplIsRunning(MainWindow* w, const QJsonObject&, const Reply& reply) {
    ReplSession* r = w->replSession();
    QJsonObject o;
    o["running"] = r && r->isRunning();
    reply(o, nullptr);
}

void HandleRunBuffer(MainWindow* w, const QJsonObject&, const Reply& reply) {
    QMetaObject::invokeMethod(w, "runBuffer", Qt::QueuedConnection);
    reply(Ok(), nullptr);
}

void HandleRunSelection(MainWindow* w, const QJsonObject&, const Reply& reply) {
    QMetaObject::invokeMethod(w, "runSelection", Qt::QueuedConnection);
    reply(Ok(), nullptr);
}

// --- Waiters ------------------------------------------------------------

void HandleWaitReplOutput(MainWindow* w, QPointer<ControlConnection> conn,
                          const QJsonObject& args, const Reply& reply) {
    const QString pattern = args.value("pattern").toString();
    const bool regex = args.value("regex").toBool(false);
    const int timeout = args.value("timeout_ms").toInt(3000);
    if (pattern.isEmpty()) { ReplyErr(reply, "bad_args", "missing pattern"); return; }

    auto matcher = [pattern, regex](const QString& hay) -> QString {
        if (regex) {
            QRegularExpression re(pattern);
            if (!re.isValid()) return {};
            auto m = re.match(hay);
            return m.hasMatch() ? m.captured(0) : QString();
        }
        return hay.contains(pattern) ? pattern : QString();
    };

    // Check what's already on-screen first.
    const QString existing = w->terminalView()->screenText(500);
    const QString hit = matcher(existing);
    if (!hit.isEmpty()) {
        QJsonObject o; o["matched"] = hit; o["already_present"] = true;
        reply(o, nullptr);
        return;
    }

    ReplSession* r = w->replSession();
    if (!r) { ReplyErr(reply, "no_repl", "REPL not running"); return; }
    auto ctx = std::make_shared<WaitCtx>();
    auto accum = std::make_shared<QByteArray>();

    ctx->conn = QObject::connect(r, &ReplSession::dataReceived, w,
        [ctx, accum, matcher, reply](const QByteArray& bytes) {
            if (ctx->done) return;
            accum->append(bytes);
            const QString hit = matcher(QString::fromUtf8(*accum));
            if (!hit.isEmpty()) {
                ctx->done = true;
                if (ctx->timer) ctx->timer->stop();
                QObject::disconnect(ctx->conn);
                QJsonObject o; o["matched"] = hit;
                reply(o, nullptr);
            }
        });
    ArmTimeout(ctx, w, timeout, reply);
    (void)conn;
}

void HandleWaitReplIdle(MainWindow* w, QPointer<ControlConnection> conn,
                        const QJsonObject& args, const Reply& reply) {
    const int quiet = args.value("quiet_ms").toInt(200);
    const int timeout = args.value("timeout_ms").toInt(5000);
    ReplSession* r = w->replSession();
    if (!r) { ReplyErr(reply, "no_repl", "REPL not running"); return; }

    auto ctx = std::make_shared<WaitCtx>();
    auto quietTimer = std::make_shared<QPointer<QTimer>>(new QTimer(w));
    (*quietTimer)->setSingleShot(true);

    QObject::connect((*quietTimer).data(), &QTimer::timeout, w, [ctx, reply]() {
        if (ctx->done) return;
        ctx->done = true;
        if (ctx->timer) ctx->timer->stop();
        QObject::disconnect(ctx->conn);
        reply(Ok(), nullptr);
    });

    ctx->conn = QObject::connect(r, &ReplSession::dataReceived, w,
        [quietTimer, quiet](const QByteArray&) {
            if (*quietTimer) (*quietTimer)->start(quiet);
        });

    (*quietTimer)->start(quiet);
    ArmTimeout(ctx, w, timeout, reply);
    (void)conn;
}

void HandleWaitEditorSignal(MainWindow* w, QPointer<ControlConnection> conn,
                            const QJsonObject& args, const Reply& reply) {
    const QString sig = args.value("signal").toString();
    const int timeout = args.value("timeout_ms").toInt(3000);
    auto ctx = std::make_shared<WaitCtx>();

    if (sig == "modifiedChanged") {
        ctx->conn = QObject::connect(w->editorView(), &EditorView::modifiedChanged, w,
            [ctx, reply](bool modified) {
                if (ctx->done) return;
                ctx->done = true;
                if (ctx->timer) ctx->timer->stop();
                QObject::disconnect(ctx->conn);
                QJsonObject o; o["modified"] = modified;
                reply(o, nullptr);
            });
    } else if (sig == "filePathChanged") {
        ctx->conn = QObject::connect(w->editorView(), &EditorView::filePathChanged, w,
            [ctx, reply](const QString& path) {
                if (ctx->done) return;
                ctx->done = true;
                if (ctx->timer) ctx->timer->stop();
                QObject::disconnect(ctx->conn);
                QJsonObject o; o["path"] = path;
                reply(o, nullptr);
            });
    } else {
        ReplyErr(reply, "bad_signal", QString("unknown signal: %1").arg(sig));
        return;
    }
    ArmTimeout(ctx, w, timeout, reply);
    (void)conn;
}

void HandleWaitProcessExit(MainWindow* w, QPointer<ControlConnection> conn,
                           const QJsonObject& args, const Reply& reply) {
    const int timeout = args.value("timeout_ms").toInt(3000);
    ReplSession* r = w->replSession();
    if (!r) { ReplyErr(reply, "no_repl", "REPL not running"); return; }
    if (!r->isRunning()) { QJsonObject o; o["exit_code"] = -1; o["already_exited"] = true; reply(o, nullptr); return; }
    auto ctx = std::make_shared<WaitCtx>();
    ctx->conn = QObject::connect(r, &ReplSession::stopped, w, [ctx, reply](int code) {
        if (ctx->done) return;
        ctx->done = true;
        if (ctx->timer) ctx->timer->stop();
        QObject::disconnect(ctx->conn);
        QJsonObject o; o["exit_code"] = code;
        reply(o, nullptr);
    });
    ArmTimeout(ctx, w, timeout, reply);
    (void)conn;
}

}  // namespace

void Dispatch(MainWindow* w, QPointer<ControlConnection> conn,
              const QString& cmd, const QJsonObject& args, Reply reply) {
    if (!w) { ReplyErr(reply, "no_window", "main window not available"); return; }

    if (cmd == "ping") { QJsonObject o; o["pong"] = true; reply(o, nullptr); return; }

    if (cmd == "window.focus")         { HandleWindowFocus(w, args, reply); return; }
    if (cmd == "window.geometry")      { HandleWindowGeometry(w, args, reply); return; }
    if (cmd == "window.set_splitter")  { HandleWindowSetSplitter(w, args, reply); return; }
    if (cmd == "menu.invoke")          { HandleMenuInvoke(w, args, reply); return; }

    if (cmd == "editor.open")          { HandleEditorOpen(w, args, reply); return; }
    if (cmd == "editor.save")          { HandleEditorSave(w, args, reply); return; }
    if (cmd == "editor.save_as")       { HandleEditorSave(w, args, reply); return; }
    if (cmd == "editor.set_text")      { HandleEditorSetText(w, args, reply); return; }
    if (cmd == "editor.type")          { HandleEditorType(w, args, reply); return; }
    if (cmd == "editor.press")         { HandleEditorPress(w, args, reply); return; }
    if (cmd == "editor.get_text")      { HandleEditorGetText(w, args, reply); return; }
    if (cmd == "editor.get_cursor")    { HandleEditorGetCursor(w, args, reply); return; }
    if (cmd == "editor.set_cursor")    { HandleEditorSetCursor(w, args, reply); return; }
    if (cmd == "editor.get_selection") { HandleEditorGetSelection(w, args, reply); return; }
    if (cmd == "editor.set_selection") { HandleEditorSetSelection(w, args, reply); return; }
    if (cmd == "editor.get_style_at")  { HandleEditorGetStyleAt(w, args, reply); return; }

    if (cmd == "repl.send")            { HandleReplSend(w, args, reply); return; }
    if (cmd == "repl.press")           { HandleReplPress(w, args, reply); return; }
    if (cmd == "repl.get_screen")      { HandleReplGetScreen(w, args, reply); return; }
    if (cmd == "repl.get_cursor")      { HandleReplGetCursor(w, args, reply); return; }
    if (cmd == "repl.restart")         { HandleReplRestart(w, args, reply); return; }
    if (cmd == "repl.is_running")      { HandleReplIsRunning(w, args, reply); return; }
    if (cmd == "run.buffer")           { HandleRunBuffer(w, args, reply); return; }
    if (cmd == "run.selection")        { HandleRunSelection(w, args, reply); return; }

    if (cmd == "wait.repl_output")     { HandleWaitReplOutput(w, conn, args, reply); return; }
    if (cmd == "wait.repl_idle")       { HandleWaitReplIdle(w, conn, args, reply); return; }
    if (cmd == "wait.editor_signal")   { HandleWaitEditorSignal(w, conn, args, reply); return; }
    if (cmd == "wait.process_exit")    { HandleWaitProcessExit(w, conn, args, reply); return; }

    ReplyErr(reply, "unknown_cmd", QString("no such command: %1").arg(cmd));
}

}
