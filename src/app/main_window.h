#pragma once

#include <QFont>
#include <QMainWindow>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <vector>

class QAction;
class QDragEnterEvent;
class QDropEvent;
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
class WindowManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openPath(const QString& path);
    bool openDirectory(const QString& path);

    // Two-phase startup. The constructor builds UI only and leaves the window
    // with no buffers and no REPL; callers may then restoreSession() and/or
    // openPath() before startSession() finalizes things. Splitting it this way
    // is what lets a *new* window come up blank while the launch path still
    // restores, and it lets the REPL root at whatever the window actually ends
    // up holding rather than at whatever it held mid-construction.
    //
    // Every window must get a startSession(); WindowManager::newWindow() does
    // it for you.
    void startSession();

    // This window's slice of the persisted session: geometry, splitter, REPL
    // visibility, open buffers and which was active. Written and read by
    // WindowManager, which owns the array of them.
    QVariantMap sessionState() const;
    void applySessionState(const QVariantMap& state);

    // Handle a set of dropped paths. The first replaces the current tab (or
    // simply opens, when there is no tab to replace or the file is already
    // open here); any others open as additional tabs. Returns false if the
    // user cancelled an unsaved-changes prompt.
    bool openDropped(const QStringList& paths);

    EditorView* editorView() const;
    // One entry per open tab, in tab order; empty string for an Untitled or
    // otherwise pathless buffer. Exposed for the control socket so callers can
    // see the tab set, not just the active buffer.
    QStringList tabPaths() const;
    TerminalView* terminalView() const { return terminal_; }
    ReplSession* replSession() const { return repl_; }
    QSplitter* splitter() const { return splitter_; }

    // The registry this window belongs to, set by WindowManager::createWindow().
    // Windows need it to spawn siblings (File ▸ New Window); it is null only for
    // a window built outside the registry.
    void setWindowManager(WindowManager* windows);
    WindowManager* windowManager() const { return windows_; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void newFile();
    void newWindow();
    void quitApp();
    void openFile();
    void openDirectoryDialog();
    bool save();
    bool saveAs();
    void restartRepl();
    void restartReplInDirectory();
    void clearRepl();
    void focusEditor();
    void focusRepl();
    void toggleReplEditorFocus();
    void runBuffer();
    void runSelection();
    void formatFile();
    void pickFont();
    void openRecentFromAction();
    void toggleSplitOrientation();
    void toggleReplVisible(bool visible);
    void nextTab();
    void prevTab();
    void closeCurrentTab();
    void openPreferences();
    void applyRainbowBrackets(bool enabled);
    void rebuildWindowMenu();

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
    // App-wide settings (recent files, editor font) as opposed to the
    // per-window state in sessionState().
    void persistGlobals();
    QString replWorkingDir() const;
    void openSettingsDirectory(const QString& relPath);

    void loadRecentFiles();
    void rebuildRecentMenu();
    void rememberRecentFile(const QString& path);

    int indexOfView(TabContent* v) const;
    // Index of the editor tab already showing `absPath`, or -1.
    int indexOfPath(const QString& absPath) const;
    // Open a path as a file or a directory, whichever it is.
    bool openAny(const QString& path);
    // Replace tab `index` with `path`, swapping the view kind if needed.
    bool replaceBufferWithPath(int index, const QString& path);
    bool replaceBufferWithFile(int index, const QString& path);
    bool replaceBufferWithDirectory(int index, const QString& path);
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

    WindowManager* windows_ = nullptr;
    int activeIndex_ = -1;
    std::vector<std::unique_ptr<Buffer>> buffers_;

    QStackedWidget* editorStack_ = nullptr;
    TabBar* tabBar_ = nullptr;
    TerminalView* terminal_ = nullptr;
    ReplSession* repl_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QMenu* windowMenu_ = nullptr;
    QToolBar* toolBar_ = nullptr;
    QAction* runBufferAction_ = nullptr;
    QAction* runSelectionAction_ = nullptr;
    QAction* restartReplAction_ = nullptr;
    QAction* clearReplAction_ = nullptr;
    QAction* formatFileAction_ = nullptr;
    QAction* toggleSplitAction_ = nullptr;
    QAction* toggleReplAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* pickFontAction_ = nullptr;
    QStringList recentFiles_;
    QFont editorFont_;
};

}
