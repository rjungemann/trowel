#pragma once

#include <QApplication>
#include <QStringList>

namespace trowel {

class MainWindow;

// QApplication subclass that turns macOS "open document" requests into new
// editor tabs.
//
// On macOS, LaunchServices delivers files to an app as QEvent::FileOpen — both
// when the app is cold-launched with documents and, crucially, when an
// already-running instance is re-activated with new documents (e.g. via the
// `trowel` CLI shim, which runs `open -b <bundle id> <files>`). Handling these
// events here is what gives the `code`-style "open in the existing window as
// tabs" behavior for free: the OS routes a second invocation to this process
// instead of spawning a new one.
//
// FileOpen events can arrive before the main window exists (during app
// construction), so requests received before setMainWindow() are buffered and
// flushed once the window is attached.
class TrowelApplication : public QApplication {
    Q_OBJECT
public:
    TrowelApplication(int& argc, char** argv);

    // Attach the main window. Any file-open requests that arrived before the
    // window existed are delivered immediately, in order.
    void setMainWindow(MainWindow* window);

    // True if at least one FileOpen request arrived before the window was
    // attached, i.e. the app was launched by opening documents.
    bool hadPendingOpens() const { return hadPendingOpens_; }

protected:
    bool event(QEvent* e) override;

private:
    void openFile(const QString& path);

    MainWindow* window_ = nullptr;
    QStringList pending_;
    bool hadPendingOpens_ = false;
};

}
