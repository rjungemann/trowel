#include "app/window_manager.h"

#include "app/main_window.h"

#include <QEvent>
#include <QSettings>

namespace trowel {

namespace {

// Keys that described the single window Trowel used to have. Read once to
// migrate an older session into the windows array, then removed.
const char* const kLegacyKeys[] = {
    "geometry", "splitterState", "replVisible", "openBuffers", "activeBuffer",
};

// Build a one-window session from the pre-multi-window settings layout.
// Returns an empty map when there is nothing to migrate.
QVariantMap LegacySessionState(QSettings& s) {
    QVariantMap state;
    for (const char* key : kLegacyKeys) {
        if (s.contains(key)) state[key] = s.value(key);
    }
    // `lastFile` predates even `openBuffers`.
    if (!state.contains("openBuffers") && s.contains("lastFile")) {
        const QString legacy = s.value("lastFile").toString();
        if (!legacy.isEmpty()) state["openBuffers"] = QStringList{legacy};
    }
    return state;
}

void RemoveLegacyKeys(QSettings& s) {
    for (const char* key : kLegacyKeys) s.remove(key);
    s.remove("lastFile");
}

}  // namespace

WindowManager::WindowManager(QObject* parent) : QObject(parent) {}

void WindowManager::prune() {
    windows_.removeIf([](const QPointer<MainWindow>& w) { return w.isNull(); });
}

MainWindow* WindowManager::createWindow() {
    auto* w = new MainWindow();
    w->setWindowManager(this);
    // Windows are heap-allocated and self-owning: closing one frees it, and the
    // QPointers here go null on their own.
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->installEventFilter(this);
    windows_.append(QPointer<MainWindow>(w));
    emit windowsChanged();
    return w;
}

MainWindow* WindowManager::newWindow() {
    MainWindow* w = createWindow();
    w->startSession();
    w->show();
    w->raise();
    w->activateWindow();
    return w;
}

MainWindow* WindowManager::activeWindow() const {
    if (lastActive_) return lastActive_;
    // No activation seen yet (or the activated window has since closed): fall
    // back to the newest live window.
    for (int i = windows_.size() - 1; i >= 0; --i) {
        if (windows_[i]) return windows_[i];
    }
    return nullptr;
}

MainWindow* WindowManager::activeOrNewWindow() {
    if (MainWindow* w = activeWindow()) return w;
    return newWindow();
}

void WindowManager::forget(MainWindow* w) {
    windows_.removeIf([w](const QPointer<MainWindow>& p) { return p.isNull() || p == w; });
    if (lastActive_ == w) lastActive_ = nullptr;
    emit windowsChanged();
}

QVector<MainWindow*> WindowManager::windows() const {
    QVector<MainWindow*> live;
    for (const auto& w : windows_) {
        if (w) live.append(w.data());
    }
    return live;
}

int WindowManager::count() const { return windows().size(); }

void WindowManager::persistAll() {
    QSettings s;
    const QVector<MainWindow*> live = windows();

    // Clear first: beginWriteArray only truncates, so a shrinking window set
    // could otherwise leave a stale trailing entry behind.
    s.remove("windows");
    s.beginWriteArray("windows", live.size());
    for (int i = 0; i < live.size(); ++i) {
        s.setArrayIndex(i);
        const QVariantMap state = live[i]->sessionState();
        for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
            s.setValue(it.key(), it.value());
        }
    }
    s.endArray();

    const int focused = live.indexOf(activeWindow());
    s.setValue("focusedWindow", focused < 0 ? 0 : focused);

    // The windows array is now authoritative.
    RemoveLegacyKeys(s);
}

MainWindow* WindowManager::restoreAll() {
    QSettings s;
    QList<QVariantMap> states;

    const int count = s.beginReadArray("windows");
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QVariantMap state;
        for (const QString& key : s.childKeys()) state[key] = s.value(key);
        states << state;
    }
    s.endArray();
    int focused = s.value("focusedWindow", 0).toInt();

    if (states.isEmpty()) {
        // No array yet: migrate a pre-multi-window session, if there is one.
        const QVariantMap legacy = LegacySessionState(s);
        if (!legacy.isEmpty()) {
            states << legacy;
            focused = 0;
        }
    }
    if (states.isEmpty()) return nullptr;

    MainWindow* focusedWindow = nullptr;
    for (int i = 0; i < states.size(); ++i) {
        MainWindow* w = createWindow();
        w->applySessionState(states[i]);
        w->startSession();
        w->show();
        if (i == focused) focusedWindow = w;
    }
    if (!focusedWindow) focusedWindow = windows().value(0, nullptr);
    if (focusedWindow) {
        focusedWindow->raise();
        focusedWindow->activateWindow();
    }
    return focusedWindow;
}

bool WindowManager::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::WindowActivate) {
        if (auto* w = qobject_cast<MainWindow*>(obj)) {
            lastActive_ = w;
            prune();
        }
    }
    return QObject::eventFilter(obj, event);
}

}
