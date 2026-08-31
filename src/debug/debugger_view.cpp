#include "debug/debugger_view.h"

#include <QAction>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

namespace trowel {

DebuggerView::DebuggerView(QWidget* parent)
    : QWidget(parent)
    , toolbar_(new QToolBar(this))
    , console_(new QPlainTextEdit(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar_);
    layout->addWidget(console_);

    // Read-only: there is no debuggee stdin to write back to (constraint 9),
    // and DAP `output` events are plain text, not an ANSI stream — so this is
    // a log, not a terminal.
    console_->setReadOnly(true);
    console_->setFrameShape(QFrame::NoFrame);
    QFont f = console_->font();
    f.setFamily("Menlo");
    f.setStyleHint(QFont::Monospace);
    console_->setFont(f);

    // Toolbar: Continue (F5), Step Over (F10), Step In (F11), Step Out
    // (Shift+F11), Stop (Shift+F5). No Pause button — the adapter cannot
    // honour it (constraint 6), so it would be a button that does nothing
    // while running and is redundant while paused.
    auto* cont = toolbar_->addAction("Continue");
    cont->setShortcut(QKeySequence("F5"));
    cont->setToolTip("Continue (F5)");
    connect(cont, &QAction::triggered, this, &DebuggerView::continueRequested);

    auto* over = toolbar_->addAction("Step Over");
    over->setShortcut(QKeySequence("F10"));
    over->setToolTip("Step Over (F10)");
    connect(over, &QAction::triggered, this, &DebuggerView::stepOverRequested);

    auto* into = toolbar_->addAction("Step In");
    into->setShortcut(QKeySequence("F11"));
    into->setToolTip("Step In (F11)");
    connect(into, &QAction::triggered, this, &DebuggerView::stepInRequested);

    auto* out = toolbar_->addAction("Step Out");
    out->setShortcut(QKeySequence("Shift+F11"));
    out->setToolTip("Step Out (Shift+F11)");
    connect(out, &QAction::triggered, this, &DebuggerView::stepOutRequested);

    toolbar_->addSeparator();

    auto* stop = toolbar_->addAction("Stop");
    stop->setShortcut(QKeySequence("Shift+F5"));
    stop->setToolTip("Stop (Shift+F5)");
    connect(stop, &QAction::triggered, this, &DebuggerView::stopRequested);

    // Start disabled: nothing to step until a session is paused.
    setPaused(false);
    setRunning(false);
}

void DebuggerView::appendOutput(const QString& text) {
    if (text.isEmpty()) return;
    // appendPlainText inserts a newline between calls; DAP `output` events
    // already carry their own newlines, so move the cursor and insert raw to
    // avoid doubling them up.
    const bool atEnd = console_->textCursor().atEnd();
    console_->moveCursor(QTextCursor::End);
    console_->insertPlainText(text);
    if (atEnd) console_->ensureCursorVisible();
}

void DebuggerView::clearOutput() {
    console_->clear();
}

void DebuggerView::setPaused(bool paused) {
    // Everything except Stop is enabled only while Paused.
    for (int i = 0; i < toolbar_->actions().size() - 1; ++i) {
        toolbar_->actions().at(i)->setEnabled(paused);
    }
    // Stop is enabled whenever a session is live (paused or running).
    toolbar_->actions().constLast()->setEnabled(true);
}

void DebuggerView::setRunning(bool running) {
    // Stop is the only button that means anything while running.
    for (int i = 0; i < toolbar_->actions().size() - 1; ++i) {
        toolbar_->actions().at(i)->setEnabled(false);
    }
    toolbar_->actions().constLast()->setEnabled(running);
}

}

