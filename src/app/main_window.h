#pragma once

#include <QMainWindow>
#include <QStringList>

class QMenu;
class QSplitter;

namespace trowel {

class EditorView;
class ReplSession;
class TerminalView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openPath(const QString& path);

    EditorView* editorView() const { return editor_; }
    TerminalView* terminalView() const { return terminal_; }
    ReplSession* replSession() const { return repl_; }
    QSplitter* splitter() const { return splitter_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newFile();
    void openFile();
    bool save();
    bool saveAs();
    void restartRepl();
    void focusEditor();
    void focusRepl();
    void runBuffer();
    void runSelection();
    void pickFont();
    void openRecentFromAction();

private:
    void setupUi();
    void setupMenus();
    void updateWindowTitle();
    bool maybeSave();
    void restoreState();
    void persistState();
    QString replWorkingDir() const;

    void loadRecentFiles();
    void rebuildRecentMenu();
    void rememberRecentFile(const QString& path);

    EditorView* editor_;
    TerminalView* terminal_;
    ReplSession* repl_;
    QSplitter* splitter_;
    QMenu* recentMenu_ = nullptr;
    QStringList recentFiles_;
};

}
