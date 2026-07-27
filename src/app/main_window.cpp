#include "app/main_window.h"

#include "app/directory_view.h"
#include "app/icon_font.h"
#include "app/preferences_view.h"
#include "app/tab_bar.h"
#include "app/tab_content.h"
#include "app/window_manager.h"
#include "editor/editor_view.h"
#include "editor/theme_loader.h"
#include "lsp/lsp_manager.h"
#include "repl/repl_session.h"

#include <ScintillaEdit.h>
#include "repl/run_buffer.h"
#include "repl/terminal_view.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
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
    loadRecentFiles();
    updateEditorActionsEnabled();
    updateWindowTitle();
    // No buffers and no REPL yet — see startSession().
}

MainWindow::~MainWindow() = default;

EditorView* MainWindow::editorView() const {
    if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(buffers_.size())) return nullptr;
    TabContent* v = buffers_[activeIndex_]->view;
    if (!v || v->kind() != TabContent::Kind::Editor) return nullptr;
    return static_cast<EditorView*>(v);
}

QStringList MainWindow::tabPaths() const {
    QStringList paths;
    for (const auto& b : buffers_) {
        paths << (b->view ? b->view->filePath() : QString());
    }
    return paths;
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
    setAcceptDrops(true);

    statusBar()->hide();

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

    auto* newWindowAction = fileMenu->addAction("New &Window");
    newWindowAction->setShortcut(QKeySequence("Ctrl+Shift+N"));
    connect(newWindowAction, &QAction::triggered, this, &MainWindow::newWindow);

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

    auto* closeWindowAction = fileMenu->addAction("Close Win&dow");
    closeWindowAction->setShortcut(QKeySequence("Ctrl+Shift+W"));
    connect(closeWindowAction, &QAction::triggered, this, &QMainWindow::close);

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitApp);

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

    windowMenu_ = menuBar()->addMenu("&Window");
    // Populated from the registry; also refreshed just before it opens so
    // window titles are current even if nothing opened or closed.
    connect(windowMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildWindowMenu);
    rebuildWindowMenu();

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
    restartReplAction_->setToolTip("Restart the REPL in the current file's directory");
    connect(restartReplAction_, &QAction::triggered, this, &MainWindow::restartRepl);
    runMenu->addAction(restartReplAction_);

    auto* restartReplInAction = runMenu->addAction("Restart REPL &In…");
    restartReplInAction->setToolTip("Pick a directory and restart the REPL there");
    connect(restartReplInAction, &QAction::triggered,
            this, &MainWindow::restartReplInDirectory);

    clearReplAction_ = new QAction("&Clear REPL", this);
    clearReplAction_->setShortcut(QKeySequence("Ctrl+Shift+K"));
    clearReplAction_->setToolTip("Clear REPL Output");
    connect(clearReplAction_, &QAction::triggered, this, &MainWindow::clearRepl);
    runMenu->addAction(clearReplAction_);

    formatFileAction_ = new QAction("&Format File", this);
    formatFileAction_->setShortcut(QKeySequence("Ctrl+Shift+F"));
    formatFileAction_->setToolTip("Format file with `tur format`");
    connect(formatFileAction_, &QAction::triggered, this, &MainWindow::formatFile);
    runMenu->addAction(formatFileAction_);

    runMenu->addSeparator();

    completeAction_ = new QAction("Complete S&ymbol", this);
    completeAction_->setShortcut(QKeySequence("Ctrl+Space"));
    completeAction_->setToolTip("Suggest completions at the caret");
    connect(completeAction_, &QAction::triggered, this, &MainWindow::requestCompletion);
    runMenu->addAction(completeAction_);

    showDocAction_ = new QAction("Show &Documentation", this);
    showDocAction_->setShortcut(QKeySequence("Ctrl+Shift+D"));
    showDocAction_->setToolTip("Show the type and docstring of the symbol at the caret");
    connect(showDocAction_, &QAction::triggered, this, &MainWindow::showDocumentation);
    runMenu->addAction(showDocAction_);

    restartLspAction_ = new QAction("Restart &Language Server", this);
    restartLspAction_->setToolTip("Restart `tur lsp`");
    connect(restartLspAction_, &QAction::triggered, this, &MainWindow::restartLanguageServer);
    runMenu->addAction(restartLspAction_);

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
    if (formatFileAction_) {
        formatFileAction_->setIcon(NerdIcon(NF::AutoFix, glyphSize, iconColor));
        toolBar_->addAction(formatFileAction_);
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
            &MainWindow::openPreferences);
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

int MainWindow::indexOfPath(const QString& absPath) const {
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        TabContent* v = buffers_[i]->view;
        if (!v || v->kind() != TabContent::Kind::Editor) continue;
        if (v->filePath().isEmpty()) continue;
        if (QFileInfo(v->filePath()).absoluteFilePath() == absPath) return i;
    }
    return -1;
}

bool MainWindow::openAny(const QString& path) {
    return QFileInfo(path).isDir() ? openDirectory(path) : openPath(path);
}

bool MainWindow::replaceBufferWithPath(int index, const QString& path) {
    return QFileInfo(path).isDir() ? replaceBufferWithDirectory(index, path)
                                   : replaceBufferWithFile(index, path);
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
    if (buf.view && buf.view->kind() == TabContent::Kind::Preferences) {
        return buf.view->displayName();
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
        return;
    }

    auto* editor = static_cast<EditorView*>(view);
    // Caret movement re-reads the diagnostic under the cursor. updateUi also
    // fires for selection and scroll changes, which is harmless — the readout
    // is cheap and idempotent.
    connect(editor->sciWidget(), &ScintillaEditBase::updateUi, this,
            [this, editor](Scintilla::Update) {
        if (editor == editorView()) updateDiagnosticStatus();
    });
    // EditorView connects to this signal in its own constructor, so by the time
    // this runs the view has already repainted its indicators.
    connect(LspManager::instance(), &LspManager::diagnosticsUpdated, this,
            [this, editor](const QString&) {
        if (editor == editorView()) updateDiagnosticStatus();
    });
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
    updateDiagnosticStatus();
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
    // Already open in this window? Focus that tab instead of making a second
    // one. Deliberately scoped to this window — a file open in *another*
    // window is left alone rather than yanking the user across windows.
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (const int existing = indexOfPath(abs); existing >= 0) {
        activateBuffer(existing);
        rememberRecentFile(abs);
        return true;
    }

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
                return replaceBufferWithDirectory(activeIndex_, abs);
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

bool MainWindow::replaceBufferWithDirectory(int index, const QString& path) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return false;
    QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, "Trowel", QString("Not a directory: %1").arg(path));
        return false;
    }
    Buffer* buf = buffers_[index].get();
    TabContent* old = buf->view;

    auto* dv = new DirectoryView(editorStack_);
    editorStack_->addWidget(dv);
    editorStack_->removeWidget(old);
    if (old) old->deleteLater();

    buf->view = dv;
    buf->untitledIndex = 0;
    connectBufferSignals(index);
    dv->setRoot(info.absoluteFilePath());

    if (index == activeIndex_) {
        editorStack_->setCurrentWidget(dv);
    }
    updateBufferDisplayName(index);
    updateWindowTitle();
    updateEditorActionsEnabled();
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
    if (formatFileAction_) formatFileAction_->setEnabled(hasEditor);
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

void MainWindow::setWindowManager(WindowManager* windows) {
    windows_ = windows;
    if (windows_) {
        connect(windows_, &WindowManager::windowsChanged,
                this, &MainWindow::rebuildWindowMenu);
    }
    rebuildWindowMenu();
}

void MainWindow::rebuildWindowMenu() {
    if (!windowMenu_) return;
    windowMenu_->clear();
    if (!windows_) return;

    for (MainWindow* w : windows_->windows()) {
        QAction* action = windowMenu_->addAction(w->windowTitle());
        action->setCheckable(true);
        action->setChecked(w == this);
        // `w` as the context object: if that window goes away, so does the
        // connection, and the menu is rebuilt anyway.
        connect(action, &QAction::triggered, w, [w]() {
            w->show();
            w->raise();
            w->activateWindow();
        });
    }
}

void MainWindow::newWindow() {
    if (!windows_) return;
    windows_->newWindow();
}

void MainWindow::quitApp() {
    // Save the whole window set *before* closing anything: quitting should
    // restore every window that was open, whereas closing a window one at a
    // time deliberately drops it from the session.
    if (windows_) {
        windows_->persistAll();
        windows_->setQuitting(true);
    }

    // Quit means the whole app, not just this window. closeAllWindows() runs
    // every window's closeEvent, so an unsaved-changes prompt can still cancel
    // — windows that already closed stay closed, and any that refused keep the
    // app alive.
    QApplication::closeAllWindows();

    if (!windows_ || windows_->count() == 0) {
        // On macOS quitOnLastWindowClosed is disabled (the app is meant to
        // outlive its windows), so closing them all is not enough to end the
        // process — ask explicitly.
        QApplication::quit();
        return;
    }

    // Somebody refused to close, so the quit is off. Resync the saved session
    // to what is actually still open.
    windows_->setQuitting(false);
    windows_->persistAll();
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
        "All files (*);;Turmeric (*.tur *.tur.sweet);;Markdown (*.md *.markdown);;"
        "JSON (*.json);;C (*.c *.h);;Justfile (Justfile justfile *.just)");
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
        "Turmeric (*.tur *.tur.sweet);;Markdown (*.md *.markdown);;JSON (*.json);;"
        "C (*.c *.h);;Justfile (Justfile justfile *.just);;All files (*)");
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
    // If the REPL is idle at its prompt, ask it to redraw so the user sees the
    // prompt after the wipe. When busy, leave the screen blank — a running
    // command's output will fill it back in.
    if (repl_) repl_->redrawPrompt();
}

void MainWindow::restartRepl() {
    repl_->restart(replWorkingDir());
}

void MainWindow::restartReplInDirectory() {
    if (!repl_) return;
    // Offer where the REPL is now, so the dialog opens somewhere meaningful
    // rather than at the filesystem root.
    QString start = repl_->workingDir();
    if (start.isEmpty()) start = replWorkingDir();

    const QString dir = QFileDialog::getExistingDirectory(
        this, "Restart REPL In", start);
    if (dir.isEmpty()) return;  // cancelled

    // A running process cannot be moved, so "set the directory" is necessarily
    // a restart — which is why the menu entry says so.
    repl_->restart(dir);
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

void MainWindow::formatFile() {
    EditorView* v = editorView();
    if (!v) return;
    const QString binary = ResolveTurBinary();
    if (binary.isEmpty()) {
        statusBar()->showMessage("Could not locate `tur` executable.", 4000);
        return;
    }
    QProcess proc;
    proc.start(binary, {"format"});
    if (!proc.waitForStarted(3000)) {
        statusBar()->showMessage("Failed to start `tur format`.", 4000);
        return;
    }
    proc.write(v->text());
    proc.closeWriteChannel();
    if (!proc.waitForFinished(10000)) {
        proc.kill();
        statusBar()->showMessage("`tur format` timed out.", 4000);
        return;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        statusBar()->showMessage(err.isEmpty() ? QString("`tur format` failed.")
                                               : QString("Format error: %1").arg(err),
                                 6000);
        return;
    }
    const QByteArray formatted = proc.readAllStandardOutput();
    if (formatted == v->text()) {
        statusBar()->showMessage("Already formatted.", 2000);
        return;
    }
    const int caret = v->cursorPos();
    const int anchor = v->anchorPos();
    v->setText(formatted);
    const int len = formatted.size();
    v->setSelection(qMin(anchor, len), qMin(caret, len));
    statusBar()->showMessage("Formatted.", 2000);
}

void MainWindow::requestCompletion() {
    EditorView* v = editorView();
    if (!v) return;
    if (v->filePath().isEmpty()) {
        statusBar()->show();
        statusBar()->showMessage(LspManager::kSkipUnsavedReason, 4000);
        return;
    }
    emit v->completionRequested(v->cursorPos());
}

void MainWindow::showDocumentation() {
    EditorView* v = editorView();
    if (!v) return;
    if (v->filePath().isEmpty()) {
        statusBar()->show();
        statusBar()->showMessage(LspManager::kSkipUnsavedReason, 4000);
        return;
    }
    emit v->hoverRequested(v->cursorPos());
}

void MainWindow::restartLanguageServer() {
    LspManager::instance()->restart();
    statusBar()->show();
    statusBar()->showMessage("Restarting language server…", 2000);
}

void MainWindow::updateDiagnosticStatus() {
    EditorView* v = editorView();
    if (!v) return;

    const auto& diagnostics = v->diagnostics();
    if (diagnostics.isEmpty()) {
        statusBar()->clearMessage();
        statusBar()->hide();
        return;
    }

    // Prefer the diagnostic under the caret; fall back to a count so the user
    // knows something is wrong even when the caret is elsewhere.
    QString message = v->diagnosticMessageAt(v->cursorPos());
    if (message.isEmpty()) {
        int errors = 0;
        for (const LspDiagnostic& d : diagnostics) {
            if (d.severity <= LspDiagnostic::Error) errors++;
        }
        const int warnings = int(diagnostics.size()) - errors;
        QStringList parts;
        if (errors > 0) parts << QString("%1 error%2").arg(errors).arg(errors == 1 ? "" : "s");
        if (warnings > 0) parts << QString("%1 warning%2").arg(warnings).arg(warnings == 1 ? "" : "s");
        message = parts.join(", ");
    }

    statusBar()->show();
    statusBar()->showMessage(message);
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

void MainWindow::openPreferences() {
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        TabContent* v = buffers_[i]->view;
        if (v && v->kind() == TabContent::Kind::Preferences) {
            activateBuffer(i);
            return;
        }
    }

    auto* prefs = new PreferencesView(editorStack_);
    connect(prefs, &PreferencesView::rainbowBracketsChanged,
            this, &MainWindow::applyRainbowBrackets);

    // Reuse a fresh, empty, unmodified Untitled editor if available.
    if (activeIndex_ >= 0 && activeIndex_ < static_cast<int>(buffers_.size())) {
        Buffer* cur = buffers_[activeIndex_].get();
        if (cur->view && cur->view->kind() == TabContent::Kind::Editor) {
            auto* ev = static_cast<EditorView*>(cur->view);
            if (ev->filePath().isEmpty() && ev->isEmpty() && !ev->isModified()) {
                editorStack_->addWidget(prefs);
                editorStack_->removeWidget(ev);
                ev->deleteLater();
                cur->view = prefs;
                cur->untitledIndex = 0;
                connectBufferSignals(activeIndex_);
                editorStack_->setCurrentWidget(prefs);
                updateBufferDisplayName(activeIndex_);
                updateWindowTitle();
                updateEditorActionsEnabled();
                return;
            }
        }
    }

    auto buf = std::make_unique<Buffer>();
    buf->view = prefs;
    editorStack_->addWidget(prefs);
    buf->displayName = computeDisplayName(*buf);
    buffers_.push_back(std::move(buf));
    const int newIndex = static_cast<int>(buffers_.size()) - 1;
    connectBufferSignals(newIndex);
    activateBuffer(newIndex);
    refreshTabBar();
}

void MainWindow::applyRainbowBrackets(bool enabled) {
    for (auto& b : buffers_) {
        if (b->view && b->view->kind() == TabContent::Kind::Editor) {
            static_cast<EditorView*>(b->view)->setRainbowBrackets(enabled);
        }
    }
}

QString MainWindow::replWorkingDir() const {
    if (EditorView* v = editorView()) {
        const QString path = v->filePath();
        if (!path.isEmpty()) return QFileInfo(path).absolutePath();
    }
    return QDir::homePath();
}

namespace {

// Local-file paths carried by a drag, in order. Non-file URLs (http://, and
// so on) are ignored rather than opened.
QStringList DroppedPaths(const QMimeData* mime) {
    QStringList paths;
    if (!mime || !mime->hasUrls()) return paths;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QString local = url.toLocalFile();
        if (!local.isEmpty()) paths << local;
    }
    return paths;
}

}  // namespace

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    // Only claim the drag if it actually carries local files; leaving it
    // unaccepted otherwise lets the drop fall through to whatever else wants it.
    if (DroppedPaths(event->mimeData()).isEmpty()) return;
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QStringList paths = DroppedPaths(event->mimeData());
    if (paths.isEmpty()) return;
    event->acceptProposedAction();
    openDropped(paths);
    raise();
    activateWindow();
}

bool MainWindow::openDropped(const QStringList& paths) {
    if (paths.isEmpty()) return false;

    const QString first = QFileInfo(paths.first()).absoluteFilePath();
    bool ok = true;

    // A drop replaces the current tab — but not when there is no tab to
    // replace, and not when the file is already open in this window, where the
    // focus-existing-tab rule wins (replacing would leave two tabs on one file).
    if (buffers_.empty() || activeIndex_ < 0 || indexOfPath(first) >= 0) {
        ok = openAny(first);
    } else {
        // Replacing discards whatever the tab held, so offer to save first.
        if (!maybeSaveBuffer(activeIndex_)) return false;
        ok = replaceBufferWithPath(activeIndex_, first);
    }

    // Anything else in the same drop opens as an additional tab.
    for (int i = 1; i < paths.size(); ++i) {
        ok = openAny(paths[i]) && ok;
    }
    return ok;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSaveAll()) {
        event->ignore();
        return;
    }
    if (repl_) repl_->stop();
    persistGlobals();
    // Leave the registry before the deferred delete: WA_DeleteOnClose runs on
    // the next event-loop pass, so anything checking count() in between (a
    // quit, or the "was that the last window?" decision) must not still see us.
    if (windows_) {
        windows_->forget(this);
        // Closing a window drops it from the saved session, so rewrite what
        // survives. During a quit we skip this: quitApp() already snapshotted
        // the full set, and rewriting per close would erase it window by
        // window until nothing was left to restore.
        if (!windows_->isQuitting()) windows_->persistAll();
    }
    event->accept();
}

void MainWindow::updateWindowTitle() {
    const bool haveBuffer =
        activeIndex_ >= 0 && activeIndex_ < static_cast<int>(buffers_.size());
    TabContent* v = haveBuffer ? buffers_[activeIndex_]->view : nullptr;

    QString title = QStringLiteral("Trowel");
    if (v) {
        const QString name = buffers_[activeIndex_]->displayName;
        const QString marker = v->isModified() ? " •" : "";
        title = QString("%1%2 — Trowel").arg(name, marker);
        setWindowFilePath(v->kind() == TabContent::Kind::Editor ? v->filePath() : QString());
        setWindowModified(v->isModified());
    }
    if (windowTitle() == title) return;

    setWindowTitle(title);
    // Every window's Window menu lists these titles, so a change has to reach
    // the other windows' menus too — otherwise they keep showing the "Trowel"
    // a window was called before it had loaded anything.
    if (windows_) windows_->notifyWindowsChanged();
}

void MainWindow::startSession() {
    // Guard: a window only ever gets one session. Cheap insurance against a
    // caller that both restores and calls newWindow()-style setup.
    if (repl_) return;

    ensureAtLeastOneBuffer();
    activateBuffer(activeIndex_ < 0 ? 0 : activeIndex_);

    // Started last, so replWorkingDir() sees whatever this window actually
    // ended up holding — a restored session, a file opened from the CLI, or
    // nothing at all (a blank window roots the REPL at $HOME).
    repl_ = new ReplSession(terminal_, this);
    repl_->start(replWorkingDir());
}

void MainWindow::applySessionState(const QVariantMap& state) {
    if (state.contains("geometry")) {
        restoreGeometry(state.value("geometry").toByteArray());
    }
    if (state.contains("splitterState")) {
        splitter_->restoreState(state.value("splitterState").toByteArray());
    }
    const bool replVisible = state.value("replVisible", true).toBool();
    if (terminal_) terminal_->setVisible(replVisible);
    if (toggleReplAction_) toggleReplAction_->setChecked(replVisible);

    const QStringList openPaths = state.value("openBuffers").toStringList();
    int desiredActive = state.value("activeBuffer", 0).toInt();

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

QVariantMap MainWindow::sessionState() const {
    QVariantMap state;
    state["geometry"] = saveGeometry();
    state["splitterState"] = splitter_->saveState();
    state["replVisible"] = toggleReplAction_ ? toggleReplAction_->isChecked() : true;

    QStringList openPaths;
    int activeInList = -1;
    for (int i = 0; i < static_cast<int>(buffers_.size()); ++i) {
        TabContent* v = buffers_[i]->view;
        if (!v) continue;
        const QString path = v->filePath();
        if (path.isEmpty()) continue;  // Untitled buffers have nothing to point at
        if (i == activeIndex_) activeInList = openPaths.size();
        if (v->kind() == TabContent::Kind::Directory) {
            openPaths << (QStringLiteral("dir://") + path);
        } else {
            openPaths << path;
        }
    }
    state["openBuffers"] = openPaths;
    state["activeBuffer"] = activeInList < 0 ? 0 : activeInList;
    return state;
}

void MainWindow::persistGlobals() {
    QSettings settings;
    if (EditorView* v = editorView()) {
        settings.setValue("editorFont", v->currentFont());
    }
    // Merge rather than overwrite. Each window carries its own recent-files
    // list, so a plain write would let whichever window closed last throw away
    // everything the others had opened. This window's entries stay in front.
    QStringList merged = recentFiles_;
    for (const QString& path : settings.value("recentFiles").toStringList()) {
        if (!merged.contains(path)) merged << path;
    }
    while (merged.size() > 8) merged.removeLast();
    settings.setValue("recentFiles", merged);
}

}
