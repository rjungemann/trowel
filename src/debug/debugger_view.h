#pragma once

#include <QWidget>

class QPlainTextEdit;
class QToolBar;

namespace trowel {

// The Debugger tab's contents. Phase 2 shipped a read-only output console;
// phase 3 adds the stepping toolbar. The stack, variables, and breakpoints
// panels land in later phases. Lives in the REPL pane (not the editor stack),
// so it is a plain QWidget, not a TabContent.
class DebuggerView : public QWidget {
    Q_OBJECT
public:
    explicit DebuggerView(QWidget* parent = nullptr);

    // Append a chunk of debuggee output (from a DAP `output` event).
    void appendOutput(const QString& text);
    // Clear the output console.
    void clearOutput();

    // Enable/disable the toolbar buttons from the session state machine:
    // everything except Stop is disabled unless Paused.
    void setPaused(bool paused);
    void setRunning(bool running);

signals:
    // Toolbar actions. MainWindow connects these to the live DebugSession.
    void continueRequested();
    void stepOverRequested();
    void stepInRequested();
    void stepOutRequested();
    void stopRequested();

private:
    QToolBar* toolbar_;
    QPlainTextEdit* console_;
};

}
