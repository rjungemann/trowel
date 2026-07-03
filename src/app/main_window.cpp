#include "app/main_window.h"

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
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>

namespace trowel {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , editor_(nullptr)
    , terminal_(nullptr)
    , repl_(nullptr)
    , splitter_(nullptr)
{
    setupUi();
    setupMenus();
    restoreState();
    updateWindowTitle();

    repl_ = new ReplSession(terminal_, this);
    repl_->start(replWorkingDir());
}

void MainWindow::setupUi() {
    splitter_ = new QSplitter(Qt::Horizontal, this);

    editor_ = new EditorView(splitter_);
    terminal_ = new TerminalView(splitter_);

    splitter_->addWidget(editor_);
    splitter_->addWidget(terminal_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({700, 500});

    ApplyThemeToTerminal(terminal_, LoadBuiltinDarkTheme());

    setCentralWidget(splitter_);
    resize(1200, 800);

    connect(editor_, &EditorView::modifiedChanged, this, [this](bool) {
        updateWindowTitle();
    });
    connect(editor_, &EditorView::filePathChanged, this, [this](const QString&) {
        updateWindowTitle();
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

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]{ save(); });

    auto* saveAsAction = fileMenu->addAction("Save &As…");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]{ saveAs(); });

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    menuBar()->addMenu("&Edit");
    menuBar()->addMenu("&View");

    auto* runMenu = menuBar()->addMenu("&Run");

    auto* runBufferAction = runMenu->addAction("&Run Buffer");
    runBufferAction->setShortcut(QKeySequence("Ctrl+R"));
    connect(runBufferAction, &QAction::triggered, this, &MainWindow::runBuffer);

    auto* runSelectionAction = runMenu->addAction("Run &Selection");
    runSelectionAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(runSelectionAction, &QAction::triggered, this, &MainWindow::runSelection);

    runMenu->addSeparator();

    auto* restartAction = runMenu->addAction("Res&tart REPL");
    restartAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(restartAction, &QAction::triggered, this, &MainWindow::restartRepl);

    runMenu->addSeparator();

    auto* focusEditorAction = runMenu->addAction("Focus &Editor");
    focusEditorAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(focusEditorAction, &QAction::triggered, this, &MainWindow::focusEditor);

    auto* focusReplAction = runMenu->addAction("Focus RE&PL");
    focusReplAction->setShortcut(QKeySequence("Ctrl+T"));
    connect(focusReplAction, &QAction::triggered, this, &MainWindow::focusRepl);
}

bool MainWindow::openPath(const QString& path) {
    if (!maybeSave()) return false;
    if (!editor_->loadFile(path)) {
        QMessageBox::warning(this, "Trowel", QString("Could not open %1").arg(path));
        return false;
    }
    return true;
}

void MainWindow::newFile() {
    if (!maybeSave()) return;
    editor_->loadFile("/dev/null");
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

bool MainWindow::save() {
    if (editor_->filePath().isEmpty()) return saveAs();
    if (!editor_->saveCurrent()) {
        QMessageBox::warning(this, "Trowel", "Could not save file.");
        return false;
    }
    return true;
}

bool MainWindow::saveAs() {
    QSettings settings;
    const QString last = settings.value("lastOpenDir", QDir::homePath()).toString();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save As", last,
        "Turmeric (*.tur *.tur.sweet);;All files (*)");
    if (path.isEmpty()) return false;
    if (!editor_->saveFile(path)) {
        QMessageBox::warning(this, "Trowel", QString("Could not save %1").arg(path));
        return false;
    }
    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    return true;
}

void MainWindow::restartRepl() {
    repl_->restart(replWorkingDir());
}

void MainWindow::runBuffer() {
    const RunResult r = RunBuffer(editor_, repl_);
    if (!r.ok) {
        statusBar()->showMessage(r.message, 4000);
    }
}

void MainWindow::runSelection() {
    const auto [start, end] = editor_->selectionRange();
    const RunResult r = RunRange(editor_, repl_, start, end);
    if (!r.ok) {
        statusBar()->showMessage(r.message, 4000);
    }
}

void MainWindow::focusEditor() {
    if (editor_) editor_->setFocus();
}

void MainWindow::focusRepl() {
    if (terminal_) terminal_->setFocus();
}

QString MainWindow::replWorkingDir() const {
    const QString path = editor_->filePath();
    if (!path.isEmpty()) return QFileInfo(path).absolutePath();
    return QDir::homePath();
}

bool MainWindow::maybeSave() {
    if (!editor_->isModified()) return true;
    const auto choice = QMessageBox::question(
        this, "Trowel", "Save changes?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (choice) {
        case QMessageBox::Save: return save();
        case QMessageBox::Discard: return true;
        default: return false;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    if (repl_) repl_->stop();
    persistState();
    event->accept();
}

void MainWindow::updateWindowTitle() {
    const QString path = editor_->filePath();
    const QString name = path.isEmpty() ? "Untitled" : QFileInfo(path).fileName();
    const QString marker = editor_->isModified() ? " •" : "";
    setWindowTitle(QString("%1%2 — Trowel").arg(name, marker));
    setWindowFilePath(path);
    setWindowModified(editor_->isModified());
}

void MainWindow::restoreState() {
    QSettings settings;
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("splitterState")) {
        splitter_->restoreState(settings.value("splitterState").toByteArray());
    }
    const QString preferred = QFontDatabase::hasFamily("Iosevka") ? "Iosevka" : "Menlo";
    QFont font = settings.value("editorFont", QFont(preferred, 12)).value<QFont>();
    editor_->setFont(font);
}

void MainWindow::persistState() {
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("splitterState", splitter_->saveState());
}

}
