#include "app/main_window.h"
#include "app/single_instance.h"
#include "app/trowel_application.h"
#include "app/window_manager.h"
#include "control/control_server.h"
#include "lsp/lsp_manager.h"

#include "trowel/build_info.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

int main(int argc, char** argv) {
    // Resources are compiled into trowel_lib (a static library); force the
    // linker to pull in qrc_resources.o so `:/themes/...` is registered.
    Q_INIT_RESOURCE(resources);

    trowel::TrowelApplication app(argc, argv);
    QApplication::setApplicationName("Trowel");
    QApplication::setOrganizationName("turmeric");
    // From the VERSION file via CMake, not a hand-edited literal — this had
    // drifted to "0.0.1" while the project was at 0.0.10.
    QApplication::setApplicationVersion(TROWEL_VERSION_STRING);
    // Match the installed trowel.desktop so the compositor associates the
    // window with its icon (StartupWMClass=trowel) on Linux desktops.
    QApplication::setDesktopFileName("trowel");

#ifdef Q_OS_MACOS
    // macOS apps outlive their windows: closing the last one leaves the app in
    // the dock, and reopening it (dock click, or a document from Finder) makes
    // a fresh window. This is what makes the "no window open → open in a new
    // window" rule reachable at all. Other platforms keep quitting on last
    // close, which preserves the single-instance socket's assumption that a
    // live process means an available window.
    QApplication::setQuitOnLastWindowClosed(false);
#endif

    // Test-only settings isolation. On macOS QSettings resolves through
    // cfprefsd, which keys off the real uid and ignores $HOME — so a test
    // harness cannot sandbox settings with environment variables alone, and
    // would otherwise read (and overwrite) the developer's real preferences.
    // When TROWEL_SETTINGS_DIR is set we store settings as a plain INI file
    // rooted there instead. Unset in normal use, so each platform keeps its
    // native settings backend.
    const QString settingsDir = qEnvironmentVariable("TROWEL_SETTINGS_DIR");
    if (!settingsDir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Trowel — a native editor for turmeric");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "Turmeric source file to open", "[file]");
    QCommandLineOption ctlFlag(
        "control-socket",
        "Enable the control socket (uses a default per-pid path unless --control-socket-path is given).");
    parser.addOption(ctlFlag);
    QCommandLineOption ctlPath(
        "control-socket-path",
        "Path for the control socket (implies --control-socket).",
        "path");
    parser.addOption(ctlPath);
    parser.process(app);

    // Resolve positional file arguments to absolute paths up front — both the
    // single-instance forwarder and local opening want them absolute so the
    // running (or new) instance doesn't resolve them against its own cwd.
    QStringList files;
    for (const QString& a : parser.positionalArguments()) {
        files << QFileInfo(a).absoluteFilePath();
    }

    const bool explicitCtl = parser.isSet(ctlFlag)
                          || parser.isSet(ctlPath)
                          || !qEnvironmentVariableIsEmpty("TROWEL_CONTROL_SOCKET");

    // Single-instance routing: on platforms without an OS-level "open in the
    // running app" path (i.e. not macOS), a second launch forwards its files to
    // the first instance and exits. Disabled on macOS (LaunchServices delivers
    // QEvent::FileOpen instead) and whenever an explicit control socket is
    // requested — the smoke suite drives its own per-test socket and must stay
    // isolated from any real user instance.
#ifdef Q_OS_MACOS
    const bool useSingleInstance = false;
#else
    const bool useSingleInstance =
        !explicitCtl && qEnvironmentVariableIsEmpty("TROWEL_NO_SINGLE_INSTANCE");
#endif

    if (useSingleInstance && trowel::single_instance::ForwardToRunningInstance(files)) {
        return 0;  // handed off to the already-running instance
    }

    auto* windows = new trowel::WindowManager(&app);

    // Restore the previous session — every window it had — unless this launch
    // is explicitly about opening documents.
    trowel::MainWindow* window = nullptr;
    if (files.isEmpty() && !app.hadPendingOpens()) {
        window = windows->restoreAll();
    }

    // Nothing restored (fresh install, or a launch carrying files): one window,
    // built in two phases so any command-line files are in place before
    // startSession() starts the REPL, which then roots at what the window
    // actually shows.
    if (!window) {
        window = windows->createWindow();
        for (const QString& f : files) window->openPath(f);
        window->startSession();
        window->show();
    } else {
        for (const QString& f : files) window->openPath(f);
    }

    // Attach the registry so macOS "open document" requests (QEvent::FileOpen,
    // e.g. from the `trowel` CLI shim or Finder) are routed to a window. This
    // also flushes any requests that arrived during startup, before the
    // registry existed.
    app.setWindowManager(windows);

    trowel::control::ControlServer* ctl = nullptr;
    if (explicitCtl) {
        ctl = new trowel::control::ControlServer(windows, &app);
        QString requested = parser.value(ctlPath);
        if (requested.isEmpty()) requested = qEnvironmentVariable("TROWEL_CONTROL_SOCKET");
        if (requested == "1" || requested == "true") requested.clear();
        const QString path = ctl->start(requested);
        if (!path.isEmpty()) {
            QTextStream(stdout) << "trowel-control-socket: " << path << '\n';
            QTextStream(stdout).flush();
        }
    }

    // As the primary instance, listen on the well-known single-instance socket
    // so later launches can forward their files here. Best-effort: if listen
    // loses a startup race we simply run as a standalone window.
    if (useSingleInstance) {
        auto* si = new trowel::control::ControlServer(windows, &app);
        if (si->start(trowel::single_instance::WellKnownSocketPath()).isEmpty()) {
            QTextStream(stderr)
                << "[trowel] single-instance socket unavailable; "
                   "running as a standalone window\n";
        }
    }

    // Shut the language server down cleanly on quit. Without this the child is
    // only reaped when the transport is destroyed, which on an abrupt exit can
    // leave a stray `tur lsp` behind.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [] { trowel::LspManager::instance()->shutdown(); });

    return QApplication::exec();
}
