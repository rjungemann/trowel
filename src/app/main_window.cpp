#include "app/main_window.h"

#include "app/icon_font.h"
#include "app/tab_bar.h"
#include "editor/editor_view.h"
#include "editor/theme_loader.h"
#include "repl/repl_session.h"
#include "repl/run_buffer.h"
#include "repl/terminal_view.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace trowel {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    const QString preferred = QFontDatabase::hasFamily("Iosevka") ? "Iosevka" : "Menlo";
    editorFont_ = QSettings().value("editorFont", QFont(preferred, 12)).value<QFont>();

    setupUi();
    setupMenus();
    setupToolBar();
    restoreState();
    ensureAtLeastOneBuffer();
    updateWindowTitle();

    repl_ = new ReplSession(terminal_, this);
    repl_->start(replWorkingDir());
}

MainWindow::~MainWindow() = default;

EditorView* MainWindow::editorView() const {
    if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(buffers_.size())) return nullptr;
    return buffers_[activeIndex_]->view;
}

void MainWindow::setupUi() {
    splitter_ = new QSplitter(Qt::Horizontal, this);

    editorStack_ = new QStackedWidget(splitter_);
    terminal_ = new TerminalView(splitter_);

    splitter_->addWidget(editorStack_);
    splitter_->addWidget(terminal_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({700, 500});

    ApplyThemeToTerminal(terminal_, LoadBuiltinDarkTheme());

    // Central widget: vertical container holding tab bar + splitter.
    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    tabBar_ = new TabBar(central);
    const Theme theme = LoadBuiltinDarkTheme();
    tabBar_->setColors(theme.editorBg, theme.editorFg, theme.lineNumberFg);

    vbox->addWidget(tabBar_);
    vbox->addWidget(splitter_, 1);

    setCentralWidget(central);
    resize(1200, 800);

    connect(tabBar_, &TabBar::activateRequested, this, [this](int idx) {
        activateBuffer(idx);
    });
    connect(tabBar_, &TabBar::closeRequested, this, [this](int idx) {
        closeBuffer(idx);
    });
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);

    auto* openAction = fileMenu->addAction("&Open…");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    recentMenu_ = fileMenu->addMenu("Open &Recent");
    rebuildRecentMenu();

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]{ save(); });

    auto* saveAsAction = fileMenu->addAction("Save &As…");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]{ saveAs(); });

    fileMenu->addSeparator();

    auto* closeTabAction = fileMenu->addAction("&Close Tab");
    closeTabAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(closeTabAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    menuBar()->addMenu("&Edit");

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* fontAction = viewMenu->addAction("&Font…");
    fontAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(fontAction, &QAction::triggered, this, &MainWindow::pickFont);

    viewMenu->addSeparator();
    auto* nextTabAction = viewMenu->addAction("Ne&xt Tab");
    nextTabAction->setShortcut(QKeySequence("Ctrl+Tab"));
    connect(nextTabAction, &QAction::triggered, this, &MainWindow::nextTab);
    auto* prevTabAction = viewMenu->addAction("&Previous Tab");
    prevTabAction->setShortcut(QKeySequence("Ctrl+Shift+Tab"));
    connect(prevTabAction, &QAction::triggered, this, &MainWindow::prevTab);

    auto* runMenu = menuBar()->addMenu("&Run");

    runBufferAction_ = new QAction("&Run Buffer", this);
    runBufferAction_->setShortcut(QKeySequence("Ctrl+R"));
    runBufferAction_->setToolTip("Evaluate File");
    connect(runBufferAction_, &QAction::triggered, this, &MainWindow::runBuffer);
    runMenu->addAction(runBufferAction_);

    runSelectionAction_ = new QAction("Run &Selection", this);
    runSelectionAction_->setShortcut(QKeySequence("Ctrl+Shift+E"));
    runSelectionAction_->setToolTip("Evaluate Selection");
    connect(runSelectionAction_, &QAction::triggered, this, &MainWindow::runSelection);
    runMenu->addAction(runSelectionAction_);

    runMenu->addSeparator();

    restartReplAction_ = new QAction("Res&tart REPL", this);
    restartReplAction_->setShortcut(QKeySequence("Ctrl+Shift+R"));
    restartReplAction_->setToolTip("Restart REPL");
    connect(restartReplAction_, &QAction::triggered, this, &MainWindow::restartRepl);
    runMenu->addAction(restartReplAction_);

    runMenu->addSeparator();

    auto* focusEditorAction = runMenu->addAction("Focus &Editor");
    focusEditorAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(focusEditorAction, &QAction::triggered, this, &MainWindow::focusEditor);

    auto* focusReplAction = runMenu->addAction("Focus RE&PL");
    focusReplAction->setShortcut(QKeySequence("Ctrl+T"));
    connect(focusReplAction, &QAction::triggered, this, &MainWindow::focusRepl);
}

void MainWindow::setupToolBar() {
    toolBar_ = addToolBar("Main");
    toolBar_->setObjectName("MainToolBar");
    toolBar_->setMovable(false);
    toolBar_->setFloatable(false);
    toolBar_->setIconSize(QSize(18, 18));
    toolBar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const Theme theme = LoadBuiltinDarkTheme();
    toolBar_->setStyleSheet(QString(
        "QToolBar { background: %1; border: none; border-bottom: 1px solid %2;"
        "           padding: 4px 6px; spacing: 6px; }"
        "QToolBar::separator { background: %2; width: 1px; margin: 4px 2px; }"
    ).arg(theme.editorBg.name(), theme.lineNumberFg.name()));

    const QColor iconColor = theme.editorFg;
    const int glyphSize = 18;

    if (runBufferAction_) {
        runBufferAction_->setIcon(NerdIcon(NF::Play, glyphSize, iconColor));
        toolBar_->addAction(runBufferAction_);
    }
    if (runSelectionAction_) {
        runSelectionAction_->setIcon(NerdIcon(NF::PlaylistPlay, glyphSize, iconColor));
        toolBar_->addAction(runSelectionAction_);
    }
    if (restartReplAction_) {
        restartReplAction_->setIcon(NerdIcon(NF::Restart, glyphSize, iconColor));
        toolBar_->addAction(restartReplAction_);
    }

    toolBar_->addSeparator();

    toggleSplitAction_ = new QAction("Toggle Split Orientation", this);
    toggleSplitAction_->setToolTip("Toggle REPL position (right / below)");
    toggleSplitAction_->setIcon(NerdIcon(NF::ViewSplitHorizontal, glyphSize, iconColor));
    connect(toggleSplitAction_, &QAction::triggered, this, &MainWindow::toggleSplitOrientation);
    toolBar_->addAction(toggleSplitAction_);
}

void MainWindow::toggleSplitOrientation() {
    if (!splitter_) return;
    const bool wasHorizontal = splitter_->orientation() == Qt::Horizontal;
    splitter_->setOrientation(wasHorizontal ? Qt::Vertical : Qt::Horizontal);
    if (toggleSplitAction_) {
        const QColor iconColor = LoadBuiltinDarkTheme().editorFg;
        const char32_t glyph = wasHorizontal ? NF::ViewSplitVertical : NF::ViewSplitHorizontal;
        toggleSplitAction_->setIcon(NerdIcon(glyph, 18, iconColor));
    }
    const int total = wasHorizontal ? splitter_->height() : splitter_->width();
    if (total > 0) {
        const int first = static_cast<int>(total * 0.58);
        splitter_->setSizes({first, total - first});
    }
}

int MainWindow::nextUntitledIndex() const {
    int max = 0;
    for (const auto& b : buffers_) {
        if (b->untitledIndex > max) max = b->untitledIndex;
    }
    return max + 1;
}

int MainWindow::indexOfView(EditorView* v) const {
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        if (buffers_[i]->view == v) return i;
    }
    return -1;
}

QString MainWindow::computeDisplayName(const Buffer& buf) const {
    if (!buf.view->filePath().isEmpty()) {
        return QFileInfo(buf.view->filePath()).fileName();
    }
    if (buf.untitledIndex <= 1) return "Untitled";
    return QString("Untitled %1").arg(buf.untitledIndex);
}

void MainWindow::updateBufferDisplayName(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return;
    buffers_[index]->displayName = computeDisplayName(*buffers_[index]);
    refreshTabBar();
}

void MainWindow::refreshTabBar() {
    if (!tabBar_) return;
    QStringList names;
    for (const auto& b : buffers_) names.append(b->displayName);
    tabBar_->setTabs(names, activeIndex_);
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        tabBar_->setModified(i, buffers_[i]->view->isModified());
        tabBar_->setTooltip(i, buffers_[i]->view->filePath());
    }
}

void MainWindow::connectBufferSignals(int index) {
    EditorView* view = buffers_[index]->view;
    connect(view, &EditorView::modifiedChanged, this, [this, view](bool modified) {
        const int i = indexOfView(view);
        if (i < 0) return;
        if (tabBar_) tabBar_->setModified(i, modified);
        if (i == activeIndex_) updateWindowTitle();
    });
    connect(view, &EditorView::filePathChanged, this, [this, view](const QString&) {
        const int i = indexOfView(view);
        if (i < 0) return;
        updateBufferDisplayName(i);
        if (i == activeIndex_) updateWindowTitle();
    });
}

MainWindow::Buffer* MainWindow::addBuffer(const QString& path, bool untitledIfEmpty) {
    auto buf = std::make_unique<Buffer>();
    buf->view = new EditorView(editorStack_);
    buf->view->setFont(editorFont_);
    if (!path.isEmpty()) {
        if (!buf->view->loadFile(path)) {
            delete buf->view;
            return nullptr;
        }
    } else if (untitledIfEmpty) {
        buf->untitledIndex = nextUntitledIndex();
    }
    editorStack_->addWidget(buf->view);
    buf->displayName = computeDisplayName(*buf);
    buffers_.push_back(std::move(buf));
    const int newIndex = static_cast<int>(buffers_.size()) - 1;
    connectBufferSignals(newIndex);
    return buffers_.back().get();
}

void MainWindow::activateBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return;
    activeIndex_ = index;
    editorStack_->setCurrentWidget(buffers_[index]->view);
    if (tabBar_) tabBar_->setActive(index);
    updateWindowTitle();
}

bool MainWindow::maybeSaveBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return true;
    EditorView* v = buffers_[index]->view;
    if (!v->isModified()) return true;
    activateBuffer(index);
    const auto choice = QMessageBox::question(
        this, "Trowel",
        QString("Save changes to %1?").arg(buffers_[index]->displayName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (choice) {
        case QMessageBox::Save: return saveBuffer(index);
        case QMessageBox::Discard: return true;
        default: return false;
    }
}

bool MainWindow::maybeSaveAll() {
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        if (!maybeSaveBuffer(i)) return false;
    }
    return true;
}

void MainWindow::closeBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return;
    if (!maybeSaveBuffer(index)) return;

    EditorView* view = buffers_[index]->view;
    editorStack_->removeWidget(view);
    view->deleteLater();
    buffers_.erase(buffers_.begin() + index);

    if (buffers_.empty()) {
        addBuffer(QString(), /*untitledIfEmpty=*/true);
        activeIndex_ = 0;
    } else if (activeIndex_ >= static_cast<int>(buffers_.size())) {
        activeIndex_ = static_cast<int>(buffers_.size()) - 1;
    } else if (activeIndex_ > index) {
        --activeIndex_;
    } else if (activeIndex_ == index) {
        // Keep same index (now points to what used to be index+1).
        if (activeIndex_ >= static_cast<int>(buffers_.size())) {
            activeIndex_ = static_cast<int>(buffers_.size()) - 1;
        }
    }
    editorStack_->setCurrentWidget(buffers_[activeIndex_]->view);
    refreshTabBar();
    if (tabBar_) tabBar_->setActive(activeIndex_);
    updateWindowTitle();
}

void MainWindow::ensureAtLeastOneBuffer() {
    if (!buffers_.empty()) return;
    addBuffer(QString(), /*untitledIfEmpty=*/true);
    activeIndex_ = 0;
    editorStack_->setCurrentWidget(buffers_[0]->view);
    refreshTabBar();
}

bool MainWindow::openPath(const QString& path) {
    // Reuse current buffer if it's a fresh, empty, unmodified Untitled.
    if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(buffers_.size())) {
        Buffer* cur = buffers_[activeIndex_].get();
        if (cur->view->filePath().isEmpty() && cur->view->isEmpty() &&
            !cur->view->isModified()) {
            if (!cur->view->loadFile(path)) {
                QMessageBox::warning(this, "Trowel", QString("Could not open %1").arg(path));
                return false;
            }
            cur->untitledIndex = 0;
            updateBufferDisplayName(activeIndex_);
            rememberRecentFile(path);
            updateWindowTitle();
            return true;
        }
    }

    Buffer* b = addBuffer(path, /*untitledIfEmpty=*/false);
    if (!b) {
        QMessageBox::warning(this, "Trowel", QString("Could not open %1").arg(path));
        return false;
    }
    activateBuffer(static_cast<int>(buffers_.size()) - 1);
    refreshTabBar();
    rememberRecentFile(path);
    return true;
}

void MainWindow::rememberRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    const QString abs = QFileInfo(path).absoluteFilePath();
    recentFiles_.removeAll(abs);
    recentFiles_.prepend(abs);
    while (recentFiles_.size() > 8) recentFiles_.removeLast();
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    if (recentFiles_.isEmpty()) {
        auto* empty = recentMenu_->addAction("(no recent files)");
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : recentFiles_) {
        auto* a = recentMenu_->addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        a->setData(path);
        connect(a, &QAction::triggered, this, &MainWindow::openRecentFromAction);
    }
    recentMenu_->addSeparator();
    auto* clear = recentMenu_->addAction("Clear Menu");
    connect(clear, &QAction::triggered, this, [this]{
        recentFiles_.clear();
        rebuildRecentMenu();
    });
}

void MainWindow::openRecentFromAction() {
    auto* a = qobject_cast<QAction*>(sender());
    if (!a) return;
    const QString path = a->data().toString();
    if (path.isEmpty()) return;
    if (!QFileInfo::exists(path)) {
        recentFiles_.removeAll(path);
        rebuildRecentMenu();
        statusBar()->showMessage(QString("File no longer exists: %1").arg(path), 4000);
        return;
    }
    openPath(path);
}

void MainWindow::loadRecentFiles() {
    QSettings settings;
    recentFiles_ = settings.value("recentFiles").toStringList();
    while (recentFiles_.size() > 8) recentFiles_.removeLast();
    rebuildRecentMenu();
}

void MainWindow::pickFont() {
    EditorView* v = editorView();
    if (!v) return;
    bool ok = false;
    const QFont chosen = QFontDialog::getFont(&ok, v->currentFont(), this, "Editor Font");
    if (!ok) return;
    editorFont_ = chosen;
    for (auto& b : buffers_) b->view->setFont(chosen);
    QSettings().setValue("editorFont", chosen);
}

void MainWindow::newFile() {
    Buffer* b = addBuffer(QString(), /*untitledIfEmpty=*/true);
    if (!b) return;
    activateBuffer(static_cast<int>(buffers_.size()) - 1);
    refreshTabBar();
}

void MainWindow::openFile() {
    QSettings settings;
    const QString last = settings.value("lastOpenDir", QDir::homePath()).toString();
    const QString path = QFileDialog::getOpenFileName(
        this, "Open", last,
        "Turmeric (*.tur *.tur.sweet);;All files (*)");
    if (path.isEmpty()) return;
    if (openPath(path)) {
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    }
}

bool MainWindow::saveBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return false;
    EditorView* v = buffers_[index]->view;
    if (v->filePath().isEmpty()) return saveBufferAs(index);
    if (!v->saveCurrent()) {
        QMessageBox::warning(this, "Trowel", "Could not save file.");
        return false;
    }
    return true;
}

bool MainWindow::saveBufferAs(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return false;
    EditorView* v = buffers_[index]->view;
    QSettings settings;
    const QString last = settings.value("lastOpenDir", QDir::homePath()).toString();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save As", last,
        "Turmeric (*.tur *.tur.sweet);;All files (*)");
    if (path.isEmpty()) return false;
    if (!v->saveFile(path)) {
        QMessageBox::warning(this, "Trowel", QString("Could not save %1").arg(path));
        return false;
    }
    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    rememberRecentFile(path);
    buffers_[index]->untitledIndex = 0;
    updateBufferDisplayName(index);
    return true;
}

bool MainWindow::save() { return saveBuffer(activeIndex_); }
bool MainWindow::saveAs() { return saveBufferAs(activeIndex_); }

void MainWindow::restartRepl() {
    repl_->restart(replWorkingDir());
}

void MainWindow::runBuffer() {
    EditorView* v = editorView();
    if (!v) return;
    const RunResult r = RunBuffer(v, repl_);
    if (!r.ok) statusBar()->showMessage(r.message, 4000);
}

void MainWindow::runSelection() {
    EditorView* v = editorView();
    if (!v) return;
    const auto [start, end] = v->selectionRange();
    const RunResult r = RunRange(v, repl_, start, end);
    if (!r.ok) statusBar()->showMessage(r.message, 4000);
}

void MainWindow::focusEditor() {
    if (EditorView* v = editorView()) v->setFocus();
}

void MainWindow::focusRepl() {
    if (terminal_) terminal_->setFocus();
}

void MainWindow::nextTab() {
    if (buffers_.empty()) return;
    activateBuffer((activeIndex_ + 1) % static_cast<int>(buffers_.size()));
}

void MainWindow::prevTab() {
    if (buffers_.empty()) return;
    const int n = static_cast<int>(buffers_.size());
    activateBuffer((activeIndex_ - 1 + n) % n);
}

void MainWindow::closeCurrentTab() {
    closeBuffer(activeIndex_);
}

QString MainWindow::replWorkingDir() const {
    if (EditorView* v = editorView()) {
        const QString path = v->filePath();
        if (!path.isEmpty()) return QFileInfo(path).absolutePath();
    }
    return QDir::homePath();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSaveAll()) {
        event->ignore();
        return;
    }
    if (repl_) repl_->stop();
    persistState();
    event->accept();
}

void MainWindow::updateWindowTitle() {
    EditorView* v = editorView();
    if (!v) {
        setWindowTitle("Trowel");
        return;
    }
    const QString path = v->filePath();
    const QString name = path.isEmpty()
        ? (activeIndex_ >= 0 ? buffers_[activeIndex_]->displayName : QString("Untitled"))
        : QFileInfo(path).fileName();
    const QString marker = v->isModified() ? " •" : "";
    setWindowTitle(QString("%1%2 — Trowel").arg(name, marker));
    setWindowFilePath(path);
    setWindowModified(v->isModified());
}

void MainWindow::restoreState() {
    QSettings settings;
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("splitterState")) {
        splitter_->restoreState(settings.value("splitterState").toByteArray());
    }
    loadRecentFiles();

    // Restore open buffers (multi) or fall back to legacy `lastFile`.
    QStringList openPaths = settings.value("openBuffers").toStringList();
    if (openPaths.isEmpty() && settings.contains("lastFile")) {
        const QString legacy = settings.value("lastFile").toString();
        if (!legacy.isEmpty()) openPaths << legacy;
        settings.remove("lastFile");
    }
    int desiredActive = settings.value("activeBuffer", 0).toInt();

    for (const QString& path : openPaths) {
        if (!QFileInfo::exists(path)) {
            statusBar()->showMessage(QString("File no longer exists: %1").arg(path), 4000);
            continue;
        }
        addBuffer(path, /*untitledIfEmpty=*/false);
    }

    if (!buffers_.empty()) {
        if (desiredActive < 0 || desiredActive >= static_cast<int>(buffers_.size())) {
            desiredActive = 0;
        }
        activeIndex_ = desiredActive;
        editorStack_->setCurrentWidget(buffers_[activeIndex_]->view);
    }
    refreshTabBar();
}

void MainWindow::persistState() {
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("splitterState", splitter_->saveState());
    if (EditorView* v = editorView()) {
        settings.setValue("editorFont", v->currentFont());
    }
    settings.setValue("recentFiles", recentFiles_);

    QStringList openPaths;
    int activeInList = -1;
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        const QString path = buffers_[i]->view->filePath();
        if (path.isEmpty()) continue;
        if (i == activeIndex_) activeInList = openPaths.size();
        openPaths << path;
    }
    settings.setValue("openBuffers", openPaths);
    settings.setValue("activeBuffer", activeInList < 0 ? 0 : activeInList);
    settings.remove("lastFile");
}

}
