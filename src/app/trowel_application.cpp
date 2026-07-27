#include "app/trowel_application.h"

#include "app/main_window.h"
#include "app/window_manager.h"

#include <QFileOpenEvent>

namespace trowel {

TrowelApplication::TrowelApplication(int& argc, char** argv)
    : QApplication(argc, argv) {}

void TrowelApplication::setWindowManager(WindowManager* windows) {
    windows_ = windows;
    const QStringList pending = pending_;
    pending_.clear();
    for (const QString& path : pending) openFile(path);
}

bool TrowelApplication::event(QEvent* e) {
    // Dock-icon click / app reactivation with no windows left. On macOS the app
    // outlives its windows (setQuitOnLastWindowClosed(false)), so this is the
    // path back to a usable editor. Guarded on closingDown() so a quit in
    // progress can't resurrect a window it just closed.
    if (e->type() == QEvent::ApplicationActivate) {
        if (windows_ && windows_->count() == 0 && !closingDown()) {
            windows_->newWindow();
        }
        return QApplication::event(e);
    }
    if (e->type() == QEvent::FileOpen) {
        const QString path = static_cast<QFileOpenEvent*>(e)->file();
        if (!path.isEmpty()) {
            if (windows_) {
                openFile(path);
            } else {
                hadPendingOpens_ = true;
                pending_ << path;
            }
        }
        return true;
    }
    return QApplication::event(e);
}

void TrowelApplication::openFile(const QString& path) {
    if (!windows_) {
        pending_ << path;
        return;
    }
    // Route through the registry: an open window gets a new tab, no open
    // window gets a new window to hold the file.
    MainWindow* window = windows_->activeOrNewWindow();
    // openPath() reuses only an empty, unmodified Untitled buffer, so multiple
    // FileOpen events become multiple tabs.
    window->openPath(path);
    window->show();
    window->raise();
    window->activateWindow();
}

}
