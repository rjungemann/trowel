#include "app/main_window.h"
#include "app/single_instance.h"
#include "app/trowel_application.h"
#include "app/window_manager.h"
#include "control/control_server.h"

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
    QApplication::setApplicationVersion("0.0.1");
    // Match the installed trowel.desktop so the compositor associates the
    // window with its icon (StartupWMClass=trowel) on Linux desktops.
    QApplication::setDesktopFileName("trowel");

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

    // The launch window is built in two phases so the session restore and any
    // command-line files are both in place before startSession() starts the
    // REPL — that way the REPL roots at whatever this window actually ends up
    // showing.
    trowel::MainWindow* window = windows->createWindow();

    // Restore the previous session unless this launch is explicitly about
    // opening documents.
    if (files.isEmpty() && !app.hadPendingOpens()) {
        window->restoreSession();
    }

    for (const QString& f : files) window->openPath(f);

    window->startSession();
    window->show();

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

    return QApplication::exec();
}
