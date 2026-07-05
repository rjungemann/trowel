#pragma once

#include <QFont>
#include <QMainWindow>
#include <QStringList>

#include <memory>
#include <vector>

class QAction;
class QMenu;
class QSplitter;
class QStackedWidget;
class QToolBar;

namespace trowel {

class DirectoryView;
class EditorView;
class ReplSession;
class TabBar;
class TabContent;
class TerminalView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openPath(const QString& path);
    bool openDirectory(const QString& path);

    EditorView* editorView() const;
    TerminalView* terminalView() const { return terminal_; }
    ReplSession* replSession() const { return repl_; }
    QSplitter* splitter() const { return splitter_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newFile();
    void openFile();
    void openDirectoryDialog();
    bool save();
    bool saveAs();
    void restartRepl();
    void focusEditor();
    void focusRepl();
    void toggleReplEditorFocus();
    void runBuffer();
    void runSelection();
    void pickFont();
    void openRecentFromAction();
    void toggleSplitOrientation();
    void toggleReplVisible(bool visible);
    void nextTab();
    void prevTab();
    void closeCurrentTab();

private:
    struct Buffer {
        TabContent* view = nullptr;
        QString displayName;
        int untitledIndex = 0;
    };

    void setupUi();
    void setupMenus();
    void setupToolBar();
    void updateWindowTitle();
    bool maybeSaveBuffer(int index);
    bool maybeSaveAll();
    void restoreState();
    void persistState();
    QString replWorkingDir() const;
    void openSettingsDirectory(const QString& relPath);

    void loadRecentFiles();
    void rebuildRecentMenu();
    void rememberRecentFile(const QString& path);

    int indexOfView(TabContent* v) const;
    bool replaceBufferWithFile(int index, const QString& path);
    void updateEditorActionsEnabled();
    int nextUntitledIndex() const;
    void refreshTabBar();
    QString computeDisplayName(const Buffer& buf) const;
    void updateBufferDisplayName(int index);
    Buffer* addBuffer(const QString& path, bool untitledIfEmpty);
    void activateBuffer(int index);
    void closeBuffer(int index);
    void ensureAtLeastOneBuffer();
    bool saveBuffer(int index);
    bool saveBufferAs(int index);
    void connectBufferSignals(int index);

    int activeIndex_ = -1;
    std::vector<std::unique_ptr<Buffer>> buffers_;

    QStackedWidget* editorStack_ = nullptr;
    TabBar* tabBar_ = nullptr;
    TerminalView* terminal_ = nullptr;
    ReplSession* repl_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QToolBar* toolBar_ = nullptr;
    QAction* runBufferAction_ = nullptr;
    QAction* runSelectionAction_ = nullptr;
    QAction* restartReplAction_ = nullptr;
    QAction* toggleSplitAction_ = nullptr;
    QAction* toggleReplAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* pickFontAction_ = nullptr;
    QStringList recentFiles_;
    QFont editorFont_;
};

}
