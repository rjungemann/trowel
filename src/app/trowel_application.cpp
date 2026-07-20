#include "app/trowel_application.h"

#include "app/main_window.h"

#include <QFileOpenEvent>

namespace trowel {

TrowelApplication::TrowelApplication(int& argc, char** argv)
    : QApplication(argc, argv) {}

void TrowelApplication::setMainWindow(MainWindow* window) {
    window_ = window;
    const QStringList pending = pending_;
    pending_.clear();
    for (const QString& path : pending) openFile(path);
}

bool TrowelApplication::event(QEvent* e) {
    if (e->type() == QEvent::FileOpen) {
        const QString path = static_cast<QFileOpenEvent*>(e)->file();
        if (!path.isEmpty()) {
            if (window_) {
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
    if (!window_) {
        pending_ << path;
        return;
    }
    // openPath() opens the file in a new tab (reusing only an empty, unmodified
    // Untitled buffer), so multiple FileOpen events become multiple tabs.
    window_->openPath(path);
    window_->show();
    window_->raise();
    window_->activateWindow();
}

}
