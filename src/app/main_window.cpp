#include "app/main_window.h"

#include "app/directory_view.h"
#include "app/icon_font.h"
#include "app/tab_bar.h"
#include "app/tab_content.h"
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
#include <QDir>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QToolButton>
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
    TabContent* v = buffers_[activeIndex_]->view;
    if (!v || v->kind() != TabContent::Kind::Editor) return nullptr;
    return static_cast<EditorView*>(v);
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
    tabBar_->setActiveFg(theme.matchedBraceFg);

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

    auto* openDirAction = fileMenu->addAction("Open &Directory…");
    openDirAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openDirAction, &QAction::triggered, this, &MainWindow::openDirectoryDialog);

    recentMenu_ = fileMenu->addMenu("Open &Recent");
    rebuildRecentMenu();

    fileMenu->addSeparator();

    saveAction_ = fileMenu->addAction("&Save");
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this]{ save(); });

    saveAsAction_ = fileMenu->addAction("Save &As…");
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this]{ saveAs(); });

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
    pickFontAction_ = viewMenu->addAction("&Font…");
    pickFontAction_->setShortcut(QKeySequence("Ctrl+,"));
    connect(pickFontAction_, &QAction::triggered, this, &MainWindow::pickFont);

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

    clearReplAction_ = new QAction("&Clear REPL", this);
    clearReplAction_->setShortcut(QKeySequence("Ctrl+Shift+K"));
    clearReplAction_->setToolTip("Clear REPL Output");
    connect(clearReplAction_, &QAction::triggered, this, &MainWindow::clearRepl);
    runMenu->addAction(clearReplAction_);

    runMenu->addSeparator();

    auto* focusEditorAction = runMenu->addAction("Focus &Editor");
    focusEditorAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(focusEditorAction, &QAction::triggered, this, &MainWindow::focusEditor);

    auto* focusReplAction = runMenu->addAction("Focus RE&PL");
    focusReplAction->setShortcut(QKeySequence("Ctrl+T"));
    connect(focusReplAction, &QAction::triggered, this, &MainWindow::focusRepl);

    auto* toggleFocusAction = runMenu->addAction("Toggle REPL/Editor &Focus");
    toggleFocusAction->setShortcut(QKeySequence("Ctrl+`"));
    connect(toggleFocusAction, &QAction::triggered, this, &MainWindow::toggleReplEditorFocus);
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
    if (clearReplAction_) {
        clearReplAction_->setIcon(NerdIcon(NF::Broom, glyphSize, iconColor));
        toolBar_->addAction(clearReplAction_);
    }

    toolBar_->addSeparator();

    toggleSplitAction_ = new QAction("Toggle Split Orientation", this);
    toggleSplitAction_->setToolTip("Toggle REPL position (right / below)");
    toggleSplitAction_->setIcon(NerdIcon(NF::ViewSplitHorizontal, glyphSize, iconColor));
    connect(toggleSplitAction_, &QAction::triggered, this, &MainWindow::toggleSplitOrientation);
    toolBar_->addAction(toggleSplitAction_);

    toggleReplAction_ = new QAction("Show/Hide REPL", this);
    toggleReplAction_->setCheckable(true);
    toggleReplAction_->setChecked(true);
    toggleReplAction_->setToolTip("Show/Hide REPL");
    toggleReplAction_->setIcon(NerdIcon(NF::Console, glyphSize, iconColor));
    connect(toggleReplAction_, &QAction::toggled, this, &MainWindow::toggleReplVisible);
    toolBar_->addAction(toggleReplAction_);

    toolBar_->addSeparator();

    auto* settingsMenu = new QMenu(this);
    auto* trowelSettingsAction = settingsMenu->addAction("Trowel Settings");
    connect(trowelSettingsAction, &QAction::triggered, this,
            [this]{ openSettingsDirectory(".config/trowel"); });
    auto* turmericSettingsAction = settingsMenu->addAction("Turmeric Settings");
    connect(turmericSettingsAction, &QAction::triggered, this,
            [this]{ openSettingsDirectory(".config/turmeric"); });

    auto* settingsButton = new QToolButton(toolBar_);
    settingsButton->setToolTip("Settings");
    settingsButton->setIcon(NerdIcon(NF::Cog, glyphSize, iconColor));
    settingsButton->setMenu(settingsMenu);
    settingsButton->setPopupMode(QToolButton::InstantPopup);
    toolBar_->addWidget(settingsButton);
}

void MainWindow::toggleReplVisible(bool visible) {
    if (terminal_) terminal_->setVisible(visible);
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

int MainWindow::indexOfView(TabContent* v) const {
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        if (buffers_[i]->view == v) return i;
    }
    return -1;
}

QString MainWindow::computeDisplayName(const Buffer& buf) const {
    if (buf.view && buf.view->kind() == TabContent::Kind::Directory) {
        const QString name = buf.view->displayName();
        return name.isEmpty() ? QStringLiteral("Directory") : name;
    }
    if (buf.view && !buf.view->filePath().isEmpty()) {
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
        TabContent* v = buffers_[i]->view;
        tabBar_->setModified(i, v && v->isModified());
        tabBar_->setTooltip(i, v ? v->filePath() : QString());
    }
}

void MainWindow::connectBufferSignals(int index) {
    TabContent* view = buffers_[index]->view;
    connect(view, &TabContent::modifiedChanged, this, [this, view](bool modified) {
        const int i = indexOfView(view);
        if (i < 0) return;
        if (tabBar_) tabBar_->setModified(i, modified);
        if (i == activeIndex_) updateWindowTitle();
    });
    connect(view, &TabContent::filePathChanged, this, [this, view](const QString&) {
        const int i = indexOfView(view);
        if (i < 0) return;
        updateBufferDisplayName(i);
        if (i == activeIndex_) updateWindowTitle();
    });
    connect(view, &TabContent::displayNameChanged, this, [this, view]() {
        const int i = indexOfView(view);
        if (i < 0) return;
        updateBufferDisplayName(i);
        if (i == activeIndex_) updateWindowTitle();
    });
    if (view->kind() == TabContent::Kind::Directory) {
        auto* dv = static_cast<DirectoryView*>(view);
        connect(dv, &DirectoryView::fileActivated, this, [this, view](const QString& path) {
            const int i = indexOfView(view);
            if (i < 0) return;
            replaceBufferWithFile(i, path);
        });
    }
}

MainWindow::Buffer* MainWindow::addBuffer(const QString& path, bool untitledIfEmpty) {
    auto buf = std::make_unique<Buffer>();
    auto* editor = new EditorView(editorStack_);
    editor->setFont(editorFont_);
    if (!path.isEmpty()) {
        if (!editor->loadFile(path)) {
            delete editor;
            return nullptr;
        }
    } else if (untitledIfEmpty) {
        buf->untitledIndex = nextUntitledIndex();
    }
    buf->view = editor;
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
    updateEditorActionsEnabled();
}

bool MainWindow::maybeSaveBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return true;
    TabContent* v = buffers_[index]->view;
    if (!v || v->kind() != TabContent::Kind::Editor) return true;
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

    TabContent* view = buffers_[index]->view;
    editorStack_->removeWidget(view);
    if (view) view->deleteLater();
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
    // Reuse current buffer if it's a fresh, empty, unmodified Untitled editor.
    if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(buffers_.size())) {
        Buffer* cur = buffers_[activeIndex_].get();
        if (cur->view && cur->view->kind() == TabContent::Kind::Editor) {
            auto* ev = static_cast<EditorView*>(cur->view);
            if (ev->filePath().isEmpty() && ev->isEmpty() && !ev->isModified()) {
                if (!ev->loadFile(path)) {
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

bool MainWindow::openDirectory(const QString& path) {
    QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, "Trowel", QString("Not a directory: %1").arg(path));
        return false;
    }
    const QString abs = info.absoluteFilePath();

    // Reuse current buffer if it's a fresh, empty, unmodified Untitled editor.
    if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(buffers_.size())) {
        Buffer* cur = buffers_[activeIndex_].get();
        if (cur->view && cur->view->kind() == TabContent::Kind::Editor) {
            auto* ev = static_cast<EditorView*>(cur->view);
            if (ev->filePath().isEmpty() && ev->isEmpty() && !ev->isModified()) {
                // Swap the editor out for a directory view in place.
                auto* dv = new DirectoryView(editorStack_);
                editorStack_->addWidget(dv);
                editorStack_->removeWidget(ev);
                ev->deleteLater();
                cur->view = dv;
                cur->untitledIndex = 0;
                connectBufferSignals(activeIndex_);
                dv->setRoot(abs);
                editorStack_->setCurrentWidget(dv);
                updateBufferDisplayName(activeIndex_);
                updateWindowTitle();
                updateEditorActionsEnabled();
                return true;
            }
        }
    }

    // Otherwise create a new buffer with a directory view.
    auto buf = std::make_unique<Buffer>();
    auto* dv = new DirectoryView(editorStack_);
    buf->view = dv;
    editorStack_->addWidget(dv);
    dv->setRoot(abs);
    buf->displayName = computeDisplayName(*buf);
    buffers_.push_back(std::move(buf));
    const int newIndex = static_cast<int>(buffers_.size()) - 1;
    connectBufferSignals(newIndex);
    activateBuffer(newIndex);
    refreshTabBar();
    return true;
}

bool MainWindow::replaceBufferWithFile(int index, const QString& path) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return false;
    Buffer* buf = buffers_[index].get();
    TabContent* old = buf->view;

    auto* editor = new EditorView(editorStack_);
    editor->setFont(editorFont_);
    if (!editor->loadFile(path)) {
        delete editor;
        QMessageBox::warning(this, "Trowel", QString("Could not open %1").arg(path));
        return false;
    }
    editorStack_->addWidget(editor);
    editorStack_->removeWidget(old);
    if (old) old->deleteLater();

    buf->view = editor;
    buf->untitledIndex = 0;
    connectBufferSignals(index);

    if (index == activeIndex_) {
        editorStack_->setCurrentWidget(editor);
    }
    updateBufferDisplayName(index);
    rememberRecentFile(path);
    updateWindowTitle();
    updateEditorActionsEnabled();
    return true;
}

void MainWindow::updateEditorActionsEnabled() {
    const bool hasEditor = editorView() != nullptr;
    if (saveAction_) saveAction_->setEnabled(hasEditor);
    if (saveAsAction_) saveAsAction_->setEnabled(hasEditor);
    if (runBufferAction_) runBufferAction_->setEnabled(hasEditor);
    if (runSelectionAction_) runSelectionAction_->setEnabled(hasEditor);
    if (pickFontAction_) pickFontAction_->setEnabled(hasEditor);
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
    for (auto& b : buffers_) {
        if (b->view && b->view->kind() == TabContent::Kind::Editor) {
            static_cast<EditorView*>(b->view)->setFont(chosen);
        }
    }
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
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, "Open", last,
        "All files (*);;Turmeric (*.tur *.tur.sweet)");
    if (paths.isEmpty()) return;
    for (const QString& path : paths) {
        if (QFileInfo(path).isDir()) {
            openDirectory(path);
        } else {
            openPath(path);
        }
    }
    settings.setValue("lastOpenDir", QFileInfo(paths.last()).absolutePath());
}

void MainWindow::openDirectoryDialog() {
    QSettings settings;
    const QString last = settings.value("lastOpenDir", QDir::homePath()).toString();
    const QString path = QFileDialog::getExistingDirectory(this, "Open Directory", last);
    if (path.isEmpty()) return;
    settings.setValue("lastOpenDir", path);
    openDirectory(path);
}

bool MainWindow::saveBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return false;
    TabContent* tc = buffers_[index]->view;
    if (!tc || tc->kind() != TabContent::Kind::Editor) return false;
    auto* v = static_cast<EditorView*>(tc);
    if (v->filePath().isEmpty()) return saveBufferAs(index);
    if (!v->saveCurrent()) {
        QMessageBox::warning(this, "Trowel", "Could not save file.");
        return false;
    }
    return true;
}

bool MainWindow::saveBufferAs(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return false;
    TabContent* tc = buffers_[index]->view;
    if (!tc || tc->kind() != TabContent::Kind::Editor) return false;
    auto* v = static_cast<EditorView*>(tc);
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

void MainWindow::clearRepl() {
    if (terminal_) terminal_->clearScreen();
}

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

void MainWindow::toggleReplEditorFocus() {
    if (terminal_ && terminal_->hasFocus()) {
        focusEditor();
    } else {
        focusRepl();
    }
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

void MainWindow::openSettingsDirectory(const QString& relPath) {
    const QString path = QDir(QDir::homePath()).filePath(relPath);
    QDir().mkpath(path);
    const QString abs = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        TabContent* v = buffers_[i]->view;
        if (!v || v->kind() != TabContent::Kind::Directory) continue;
        if (QFileInfo(v->filePath()).absoluteFilePath() == abs) {
            activateBuffer(i);
            return;
        }
    }
    openDirectory(abs);
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
    if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(buffers_.size())) {
        setWindowTitle("Trowel");
        return;
    }
    TabContent* v = buffers_[activeIndex_]->view;
    if (!v) {
        setWindowTitle("Trowel");
        return;
    }
    const QString path = v->filePath();
    const QString name = buffers_[activeIndex_]->displayName;
    const QString marker = v->isModified() ? " •" : "";
    setWindowTitle(QString("%1%2 — Trowel").arg(name, marker));
    setWindowFilePath(v->kind() == TabContent::Kind::Editor ? path : QString());
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
    const bool replVisible = settings.value("replVisible", true).toBool();
    if (terminal_) terminal_->setVisible(replVisible);
    if (toggleReplAction_) toggleReplAction_->setChecked(replVisible);
    loadRecentFiles();

    // Restore open buffers (multi) or fall back to legacy `lastFile`.
    QStringList openPaths = settings.value("openBuffers").toStringList();
    if (openPaths.isEmpty() && settings.contains("lastFile")) {
        const QString legacy = settings.value("lastFile").toString();
        if (!legacy.isEmpty()) openPaths << legacy;
        settings.remove("lastFile");
    }
    int desiredActive = settings.value("activeBuffer", 0).toInt();

    static const QString kDirPrefix = QStringLiteral("dir://");
    for (const QString& entry : openPaths) {
        const bool isDir = entry.startsWith(kDirPrefix);
        const QString path = isDir ? entry.mid(kDirPrefix.size()) : entry;
        if (!QFileInfo::exists(path)) {
            statusBar()->showMessage(QString("Path no longer exists: %1").arg(path), 4000);
            continue;
        }
        if (isDir) {
            openDirectory(path);
        } else {
            addBuffer(path, /*untitledIfEmpty=*/false);
        }
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
    if (toggleReplAction_) {
        settings.setValue("replVisible", toggleReplAction_->isChecked());
    }
    if (EditorView* v = editorView()) {
        settings.setValue("editorFont", v->currentFont());
    }
    settings.setValue("recentFiles", recentFiles_);

    QStringList openPaths;
    int activeInList = -1;
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        TabContent* v = buffers_[i]->view;
        if (!v) continue;
        const QString path = v->filePath();
        if (path.isEmpty()) continue;
        if (i == activeIndex_) activeInList = openPaths.size();
        if (v->kind() == TabContent::Kind::Directory) {
            openPaths << (QStringLiteral("dir://") + path);
        } else {
            openPaths << path;
        }
    }
    settings.setValue("openBuffers", openPaths);
    settings.setValue("activeBuffer", activeInList < 0 ? 0 : activeInList);
    settings.remove("lastFile");
}

}
