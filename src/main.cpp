#include "app/main_window.h"
#include "app/trowel_application.h"
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

    trowel::MainWindow window;

    // Attach the window so macOS "open document" requests (QEvent::FileOpen,
    // e.g. from the `trowel` CLI shim or Finder) open as tabs. This also
    // flushes any requests that arrived during startup, before the window
    // existed.
    app.setMainWindow(&window);

    const auto positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openPath(positional.first());
    } else if (!app.hadPendingOpens()) {
        const QString lastFile = QSettings().value("lastFile").toString();
        if (!lastFile.isEmpty() && QFileInfo::exists(lastFile)) {
            window.openPath(lastFile);
        }
    }
    window.show();

    trowel::control::ControlServer* ctl = nullptr;
    const bool wantCtl = parser.isSet(ctlFlag)
                      || parser.isSet(ctlPath)
                      || !qEnvironmentVariableIsEmpty("TROWEL_CONTROL_SOCKET");
    if (wantCtl) {
        ctl = new trowel::control::ControlServer(&window, &app);
        QString requested = parser.value(ctlPath);
        if (requested.isEmpty()) requested = qEnvironmentVariable("TROWEL_CONTROL_SOCKET");
        if (requested == "1" || requested == "true") requested.clear();
        const QString path = ctl->start(requested);
        if (!path.isEmpty()) {
            QTextStream(stdout) << "trowel-control-socket: " << path << '\n';
            QTextStream(stdout).flush();
        }
    }

    return QApplication::exec();
}
