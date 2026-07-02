#pragma once

#include <QMainWindow>

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

private:
    void setupUi();
    void setupMenus();
    void updateWindowTitle();
    bool maybeSave();
    void restoreState();
    void persistState();
    QString replWorkingDir() const;

    EditorView* editor_;
    TerminalView* terminal_;
    ReplSession* repl_;
    QSplitter* splitter_;
};

}
