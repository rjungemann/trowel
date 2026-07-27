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

    // Drop a window from the registry. Called from MainWindow::closeEvent once
    // the close is accepted, rather than waiting for the object to be
    // destroyed: WA_DeleteOnClose defers deletion to the event loop, so a
    // count() taken immediately after closeAllWindows() would otherwise still
    // see windows that have already agreed to close.
    void forget(MainWindow* w);

    // Recreate the previous session: one window per persisted entry, each
    // populated, started and shown. Returns the window that had focus, or
    // nullptr when there was nothing to restore (fresh install, or the user
    // quit with no windows open).
    MainWindow* restoreAll();

    // Write the current window set to settings, replacing what was there.
    void persistAll();

    // True between the start of a quit and its completion (or cancellation).
    // Window closes skip their usual "rewrite the surviving set" step while
    // this holds, so a quit preserves the set it snapshotted up front.
    bool isQuitting() const { return quitting_; }
    void setQuitting(bool quitting) { quitting_ = quitting; }

    // Announce that the window set — or a window's title — changed, so every
    // Window menu relists. Called by MainWindow when its title changes.
    void notifyWindowsChanged() { emit windowsChanged(); }

signals:
    // The set of open windows, or one of their titles, changed. Each window's
    // Window menu listens so it can relist its siblings.
    void windowsChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void prune();

    QVector<QPointer<MainWindow>> windows_;
    QPointer<MainWindow> lastActive_;
    bool quitting_ = false;
};

}
