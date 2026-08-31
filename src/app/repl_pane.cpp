#include "app/repl_pane.h"

#include "app/tab_bar.h"
#include "debug/debugger_view.h"
#include "editor/theme_loader.h"
#include "repl/terminal_view.h"

#include <QStackedWidget>
#include <QVBoxLayout>

namespace trowel {

ReplPane::ReplPane(TerminalView* terminal, QWidget* parent)
    : QWidget(parent)
    , terminal_(terminal)
    , debugger_(new DebuggerView(this))
    , tabs_(new TabBar(this))
    , stack_(new QStackedWidget(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Stack first, tab bar below it: the pane's top edge stays flush with the
    // editor's, and the tab strip reads as the pane's footer.
    layout->addWidget(stack_, 1);
    layout->addWidget(tabs_);

    // Reparent the existing terminal into the stack so MainWindow's
    // `terminal_` reference keeps working — the widget survives, only its
    // parent changes.
    if (terminal_) stack_->addWidget(terminal_);
    stack_->addWidget(debugger_);

    // Two fixed, non-closable tabs. The divider rule sits on top (the bar is
    // bottom-mounted), so it reads as the pane's footer rather than the
    // content's header.
    const Theme theme = LoadBuiltinDarkTheme();
    tabs_->setTabs({QStringLiteral("REPL"), QStringLiteral("Debugger")}, 0);
    tabs_->setClosable(0, false);
    tabs_->setClosable(1, false);
    tabs_->setColors(theme.editorBg, theme.editorFg, theme.lineNumberFg);
    tabs_->setActiveFg(theme.matchedBraceFg);
    tabs_->setDividerEdge(TabBar::DividerEdge::Top);

    connect(tabs_, &TabBar::activateRequested, this, [this](int idx) {
        setActiveTab(idx);
        emit tabChanged(idx);
    });
}

void ReplPane::showTerminal() { setActiveTab(static_cast<int>(Tab::Repl)); }
void ReplPane::showDebugger() { setActiveTab(static_cast<int>(Tab::Debugger)); }

int ReplPane::activeTab() const {
    return stack_ ? stack_->currentIndex() : 0;
}

void ReplPane::setActiveTab(int index) {
    if (index < 0 || index >= stack_->count()) return;
    stack_->setCurrentIndex(index);
    tabs_->setActive(index);
}

}
