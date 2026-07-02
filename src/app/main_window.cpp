#include "app/main_window.h"

#include <QAction>
#include <QLabel>
#include <QMenuBar>
#include <QSplitter>
#include <QTextEdit>

namespace trowel {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , splitter_(new QSplitter(Qt::Horizontal, this))
{
    setWindowTitle("Trowel");
    resize(1200, 800);

    auto* editorPlaceholder = new QTextEdit(splitter_);
    editorPlaceholder->setPlaceholderText("Editor pane — Scintilla goes here (M1)");

    auto* terminalPlaceholder = new QTextEdit(splitter_);
    terminalPlaceholder->setPlaceholderText("Terminal pane — QTermWidget goes here (M2)");
    terminalPlaceholder->setReadOnly(true);

    splitter_->addWidget(editorPlaceholder);
    splitter_->addWidget(terminalPlaceholder);
    splitter_->setChildrenCollapsible(false);
    splitter_->setSizes({700, 500});

    setCentralWidget(splitter_);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    menuBar()->addMenu("&Edit");
    menuBar()->addMenu("&View");
    menuBar()->addMenu("&Run");
}

}
