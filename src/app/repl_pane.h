#pragma once

#include <QWidget>

class QStackedWidget;

namespace trowel {

class DebuggerView;
class TabBar;
class TerminalView;

// The right-hand pane: a stack of the TerminalView (REPL) and the DebuggerView,
// with a bottom-mounted tab bar to switch between them. Owns neither the
// terminal (MainWindow keeps its `terminal_` reference for the control socket)
// nor the debugger view's session — it is a layout wrapper.
//
// Keeping `MainWindow::terminal_` and `terminalView()` meaning exactly what
// they mean today is what makes this refactor cheap: the control socket and
// every `repl.*` handler continue to work untouched.
class ReplPane : public QWidget {
    Q_OBJECT
public:
    // `terminal` is the existing TerminalView, reparented into the stack.
    explicit ReplPane(TerminalView* terminal, QWidget* parent = nullptr);

    TerminalView* terminal() const { return terminal_; }
    DebuggerView* debugger() const { return debugger_; }

    enum class Tab { Repl = 0, Debugger = 1 };
    void showTerminal();
    void showDebugger();
    int activeTab() const;
    void setActiveTab(int index);

signals:
    // Fired when the user clicks a pane tab. MainWindow persists this and
    // auto-switches to the Debugger tab when a debug session starts.
    void tabChanged(int index);

private:
    TerminalView* terminal_;
    DebuggerView* debugger_;
    TabBar* tabs_;
    QStackedWidget* stack_;
};

}
