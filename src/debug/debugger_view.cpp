#include "debug/debugger_view.h"

#include <QAbstractItemView>
#include <QAction>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace trowel {

namespace {
// Frame id carried on the stack list's rows. Read back on selection rather
// than inferred from the row index: the two are the same today (constraint 11)
// and this does not break the day they stop being.
constexpr int kFrameIdRole = Qt::UserRole + 1;

// Breakpoint identity on a panel row. A row is keyed by path + line, not by
// its index — the panel is rebuilt from the model on every change, and an
// index would be stale the moment a breakpoint above it is removed.
constexpr int kBpPathRole = Qt::UserRole + 2;
constexpr int kBpLineRole = Qt::UserRole + 3;
// Marks the basename-collision warning row so the edit handlers skip it.
constexpr int kBpWarningRole = Qt::UserRole + 4;

constexpr int kBpConditionColumn = 1;

// `Qt::ItemIsEditable` is a property of the whole item, not of one column, so
// making the condition editable would make the location editable too — and a
// location is a fact about the model, not a text field. Refusing to build an
// editor for column 0 is the narrowest way to say that.
class ConditionOnlyDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        if (index.column() != kBpConditionColumn) return nullptr;
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
};
}  // namespace

DebuggerView::DebuggerView(QWidget* parent)
    : QWidget(parent)
    , toolbar_(new QToolBar(this))
    , panes_(new QSplitter(Qt::Horizontal, this))
    , stack_(new QListWidget(this))
    , variables_(new QTreeWidget(this))
    , breakpoints_(new QTreeWidget(this))
    , console_(new QPlainTextEdit(this))
    , evalInput_(new QLineEdit(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar: Continue (F5), Step Over (F10), Step In (F11), Step Out
    // (Shift+F11), Stop (Shift+F5). No Pause button — the adapter cannot
    // honour it (constraint 6), so it would be a button that does nothing
    // while running and is redundant while paused.
    // No shortcut of its own: F5 lives on MainWindow's Debug Buffer action,
    // which continues when a session is paused and launches otherwise. Two
    // window-scoped actions claiming F5 is an ambiguous shortcut, and Qt
    // resolves that by firing neither.
    auto* cont = toolbar_->addAction("Continue");
    cont->setToolTip("Continue (F5)");
    connect(cont, &QAction::triggered, this, &DebuggerView::continueRequested);
    stepActions_.append(cont);

    auto* over = toolbar_->addAction("Step Over");
    over->setShortcut(QKeySequence("F10"));
    over->setToolTip("Step Over (F10)");
    connect(over, &QAction::triggered, this, &DebuggerView::stepOverRequested);
    stepActions_.append(over);

    auto* into = toolbar_->addAction("Step In");
    into->setShortcut(QKeySequence("F11"));
    into->setToolTip("Step In (F11)");
    connect(into, &QAction::triggered, this, &DebuggerView::stepInRequested);
    stepActions_.append(into);

    auto* out = toolbar_->addAction("Step Out");
    out->setShortcut(QKeySequence("Shift+F11"));
    out->setToolTip("Step Out (Shift+F11)");
    connect(out, &QAction::triggered, this, &DebuggerView::stepOutRequested);
    stepActions_.append(out);

    // Reverse execution. Only meaningful over a recording, so these are hidden
    // until a replay session says otherwise. Shortcuts mirror the forward ones
    // with Shift, which is what VS Code and nvim-dap both use.
    auto* backSep = toolbar_->addSeparator();
    auto* revCont = toolbar_->addAction("Reverse");
    revCont->setToolTip("Reverse Continue — run backwards to the previous breakpoint");
    connect(revCont, &QAction::triggered, this, &DebuggerView::reverseContinueRequested);

    auto* revOver = toolbar_->addAction("Back Over");
    revOver->setShortcut(QKeySequence("Shift+F10"));
    revOver->setToolTip("Reverse Step Over (Shift+F10)");
    connect(revOver, &QAction::triggered, this, &DebuggerView::reverseStepOverRequested);

    auto* back = toolbar_->addAction("Step Back");
    back->setShortcut(QKeySequence("Shift+F12"));
    back->setToolTip("Step Back (Shift+F12)");
    connect(back, &QAction::triggered, this, &DebuggerView::stepBackRequested);

    reverseActions_ = {backSep, revCont, revOver, back};
    stepActions_.append({revCont, revOver, back});

    toolbar_->addSeparator();

    stopAction_ = toolbar_->addAction("Stop");
    stopAction_->setShortcut(QKeySequence("Shift+F5"));
    stopAction_->setToolTip("Stop (Shift+F5)");
    connect(stopAction_, &QAction::triggered, this, &DebuggerView::stopRequested);

    layout->addWidget(toolbar_);

    // Call stack: a flat list. DAP hands us a flat array and `tur dap` reports
    // exactly one thread, so there is no tree to build.
    stack_->setAlternatingRowColors(false);
    stack_->setFrameShape(QFrame::NoFrame);
    connect(stack_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* item, QListWidgetItem*) {
                if (!item) return;
                emit frameSelected(item->data(kFrameIdRole).toInt());
            });

    // Variables: a two-column tree, flat for now. `variablesReference` is
    // always 0 upstream (constraint 7), so there is no expansion machinery —
    // a tree only so the widget is ready if Turmeric grows structured values.
    // Nothing here ever requests children.
    variables_->setColumnCount(2);
    variables_->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Value")});
    variables_->setRootIsDecorated(false);
    variables_->setFrameShape(QFrame::NoFrame);
    variables_->header()->setStretchLastSection(true);

    // Breakpoints: checkable rows over the model. The condition column is
    // editable in place; both syntaxes the adapter accepts are documented in
    // the placeholder, because `i > 3` and `(> i 3)` both work and neither is
    // guessable from the other.
    breakpoints_->setColumnCount(2);
    breakpoints_->setHeaderLabels({QStringLiteral("Breakpoint"),
                                   QStringLiteral("Condition")});
    breakpoints_->setRootIsDecorated(false);
    breakpoints_->setFrameShape(QFrame::NoFrame);
    breakpoints_->header()->setStretchLastSection(true);
    breakpoints_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                  QAbstractItemView::SelectedClicked);
    breakpoints_->setItemDelegate(new ConditionOnlyDelegate(breakpoints_));
    connect(breakpoints_, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
                if (rebuildingBreakpoints_ || !item) return;
                if (item->data(0, kBpWarningRole).toBool()) return;
                const QString path = item->data(0, kBpPathRole).toString();
                const int line = item->data(0, kBpLineRole).toInt();
                if (path.isEmpty() || line < 1) return;
                if (column == kBpConditionColumn) {
                    emit breakpointConditionEdited(path, line,
                                                   item->text(kBpConditionColumn).trimmed());
                } else {
                    emit breakpointEnableToggled(path, line,
                                                 item->checkState(0) == Qt::Checked);
                }
            });
    connect(breakpoints_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int column) {
                // Double-clicking the condition column starts an edit; only
                // the location column means "take me there".
                if (!item || column == kBpConditionColumn) return;
                if (item->data(0, kBpWarningRole).toBool()) return;
                const QString path = item->data(0, kBpPathRole).toString();
                const int line = item->data(0, kBpLineRole).toInt();
                if (!path.isEmpty() && line >= 1) emit breakpointActivated(path, line);
            });
    breakpoints_->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* removeBp = new QAction(QStringLiteral("Remove Breakpoint"), breakpoints_);
    connect(removeBp, &QAction::triggered, this, [this] {
        QTreeWidgetItem* item = breakpoints_->currentItem();
        if (!item || item->data(0, kBpWarningRole).toBool()) return;
        const QString path = item->data(0, kBpPathRole).toString();
        const int line = item->data(0, kBpLineRole).toInt();
        if (!path.isEmpty() && line >= 1) emit breakpointRemoved(path, line);
    });
    breakpoints_->addAction(removeBp);

    panes_->addWidget(stack_);
    panes_->addWidget(variables_);
    panes_->addWidget(breakpoints_);
    panes_->setStretchFactor(0, 2);
    panes_->setStretchFactor(1, 3);
    panes_->setStretchFactor(2, 2);

    // Read-only: there is no debuggee stdin to write back to (constraint 9),
    // and DAP `output` events are plain text, not an ANSI stream — so this is
    // a log, not a terminal.
    console_->setReadOnly(true);
    console_->setFrameShape(QFrame::NoFrame);
    QFont f = console_->font();
    f.setFamily("Menlo");
    f.setStyleHint(QFont::Monospace);
    console_->setFont(f);
    evalInput_->setFont(f);
    evalInput_->setPlaceholderText(QStringLiteral("Evaluate in the selected frame…"));
    connect(evalInput_, &QLineEdit::returnPressed, this, [this] {
        const QString expr = evalInput_->text().trimmed();
        if (expr.isEmpty()) return;
        evalInput_->clear();
        emit evaluateRequested(expr);
    });

    auto* consoleBox = new QWidget(this);
    auto* consoleLayout = new QVBoxLayout(consoleBox);
    consoleLayout->setContentsMargins(0, 0, 0, 0);
    consoleLayout->setSpacing(0);
    consoleLayout->addWidget(console_, 1);
    consoleLayout->addWidget(evalInput_);

    // Panes above, console below, both resizable: how much of the pane the
    // stack deserves depends entirely on how deep the recursion is, and only
    // the person looking at it knows.
    auto* vsplit = new QSplitter(Qt::Vertical, this);
    vsplit->addWidget(panes_);
    vsplit->addWidget(consoleBox);
    vsplit->setStretchFactor(0, 1);
    vsplit->setStretchFactor(1, 1);
    layout->addWidget(vsplit, 1);

    // Start disabled: nothing to step until a session is paused.
    setReverseAvailable(false);
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

void DebuggerView::appendEvaluation(const QString& expression, const QString& result,
                                    bool ok) {
    // Prefixed, because the console interleaves these with the program's own
    // stdout and an unmarked value would read as something the program printed.
    appendOutput(QStringLiteral("\n> %1\n%2%3\n")
                     .arg(expression,
                          ok ? QString() : QStringLiteral("error: "),
                          result));
}

void DebuggerView::clearOutput() {
    console_->clear();
}

void DebuggerView::setFrames(const QVector<DebugSession::Frame>& frames, int selectedId) {
    // Blocked so rebuilding the list does not emit a selection change that
    // would be read as the user picking a frame, re-issuing scopes+variables
    // for a frame nobody asked about.
    const QSignalBlocker blocker(stack_);
    stack_->clear();
    for (const DebugSession::Frame& f : frames) {
        const QString where = f.filePath.isEmpty()
            ? QString()
            : QStringLiteral("%1:%2").arg(QFileInfo(f.filePath).fileName()).arg(f.line);
        auto* item = new QListWidgetItem(
            where.isEmpty() ? f.name : QStringLiteral("%1    %2").arg(f.name, where),
            stack_);
        item->setData(kFrameIdRole, f.id);
        if (f.id == selectedId) stack_->setCurrentItem(item);
    }
}

void DebuggerView::setVariables(const QVector<DebugSession::Variable>& vars) {
    variables_->clear();
    for (const DebugSession::Variable& v : vars) {
        auto* item = new QTreeWidgetItem(variables_);
        item->setText(0, v.name);
        item->setText(1, v.value);
        if (!v.type.isEmpty()) item->setToolTip(0, v.type);
    }
}

void DebuggerView::setBreakpoints(const QVector<BreakpointModel::Breakpoint>& bps,
                                  const QStringList& collidingBasenames) {
    // itemChanged fires while the tree is being filled — setCheckState and
    // setText both emit it — and every one of those would read as the user
    // clicking a checkbox.
    rebuildingBreakpoints_ = true;
    breakpoints_->clear();

    for (const QString& name : collidingBasenames) {
        auto* warn = new QTreeWidgetItem(breakpoints_);
        warn->setText(0, QStringLiteral("⚠ two open files named %1").arg(name));
        warn->setText(kBpConditionColumn,
                      QStringLiteral("breakpoints bind by basename and apply to both"));
        warn->setData(0, kBpWarningRole, true);
        warn->setFlags(Qt::ItemIsEnabled);  // not selectable, not checkable
        warn->setToolTip(0, QStringLiteral(
            "`tur dap` reduces a breakpoint's path to its basename before "
            "binding it, so these two files share one breakpoint set inside "
            "the interpreter."));
    }

    for (const BreakpointModel::Breakpoint& b : bps) {
        auto* item = new QTreeWidgetItem(breakpoints_);
        item->setText(0, QStringLiteral("%1:%2")
                             .arg(QFileInfo(b.path).fileName()).arg(b.line));
        item->setToolTip(0, b.path);
        item->setCheckState(0, b.enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(kBpConditionColumn, b.condition);
        item->setToolTip(kBpConditionColumn, QStringLiteral(
            "Stop only when this holds. Two spellings are accepted: `i > 3` "
            "and `(> i 3)`. A condition that fails to evaluate stops anyway, "
            "so a broken one never silently swallows a breakpoint."));
        item->setData(0, kBpPathRole, b.path);
        item->setData(0, kBpLineRole, b.line);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    }
    rebuildingBreakpoints_ = false;
}

void DebuggerView::setPaused(bool paused) {
    for (QAction* a : stepActions_) a->setEnabled(paused);
    // Stop is enabled whenever a session is live (paused or running).
    if (stopAction_) stopAction_->setEnabled(true);
    // Two independent conditions, and both have to hold: you can only evaluate
    // while paused, and only in a session that has a live frame at all.
    evalInput_->setEnabled(paused && evaluateAllowed_);
}

void DebuggerView::setRunning(bool running) {
    // Stop is the only button that means anything while running.
    for (QAction* a : stepActions_) a->setEnabled(false);
    if (stopAction_) stopAction_->setEnabled(running);
    evalInput_->setEnabled(false);
    if (!running) {
        stack_->clear();
        variables_->clear();
    }
}

void DebuggerView::setReverseAvailable(bool available) {
    for (QAction* a : reverseActions_) a->setVisible(available);
}

void DebuggerView::setEvaluateEnabled(bool enabled, const QString& whyNot) {
    evaluateAllowed_ = enabled;
    evalInput_->setEnabled(enabled);
    evalInput_->setPlaceholderText(
        enabled ? QStringLiteral("Evaluate in the selected frame…") : whyNot);
}

}
