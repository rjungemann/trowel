#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

namespace trowel {

class MainWindow;

// Registry of open editor windows, and the one place windows get created.
//
// File-open requests reach Trowel from three directions — LaunchServices
// (macOS QEvent::FileOpen), the single-instance forwarder (Linux), and the
// control socket — and all of them route through activeOrNewWindow(), which
// owns the "existing window gets a new tab, no window gets a new window"
// decision in a single place.
class WindowManager : public QObject {
    Q_OBJECT
public:
    explicit WindowManager(QObject* parent = nullptr);

    // Most recently activated live window, or nullptr when none are open.
    //
    // Tracked here rather than read from QApplication::activeWindow(), which
    // reports nullptr whenever the app is backgrounded — exactly the situation
    // when a forwarded CLI open or a LaunchServices FileOpen arrives. Falls
    // back to the most recently created window if no activation has been seen
    // (the offscreen platform used by the smoke suite never delivers one).
    MainWindow* activeWindow() const;

    // Construct and register a window without showing it or starting its REPL.
    // The caller is expected to populate it (restoreSession() and/or openPath())
    // and then call MainWindow::startSession(). Use newWindow() unless you
    // specifically need that seam.
    MainWindow* createWindow();

    // A blank window, ready to use: one empty Untitled buffer, REPL started,
    // shown and activated.
    MainWindow* newWindow();

    // The routing primitive: open into the active window when there is one,
    // otherwise create a window to hold the file.
    MainWindow* activeOrNewWindow();

    QVector<MainWindow*> windows() const;
    int count() const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void prune();

    QVector<QPointer<MainWindow>> windows_;
    QPointer<MainWindow> lastActive_;
};

}
