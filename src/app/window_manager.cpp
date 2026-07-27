#include "app/window_manager.h"

#include "app/main_window.h"

#include <QEvent>

namespace trowel {

WindowManager::WindowManager(QObject* parent) : QObject(parent) {}

void WindowManager::prune() {
    windows_.removeIf([](const QPointer<MainWindow>& w) { return w.isNull(); });
}

MainWindow* WindowManager::createWindow() {
    auto* w = new MainWindow();
    // Windows are heap-allocated and self-owning: closing one frees it, and the
    // QPointers here go null on their own.
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->installEventFilter(this);
    windows_.append(QPointer<MainWindow>(w));
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

QVector<MainWindow*> WindowManager::windows() const {
    QVector<MainWindow*> live;
    for (const auto& w : windows_) {
        if (w) live.append(w.data());
    }
    return live;
}

int WindowManager::count() const { return windows().size(); }

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
