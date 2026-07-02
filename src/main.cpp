#include "app/main_window.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char** argv) {
    // Resources are compiled into trowel_lib (a static library); force the
    // linker to pull in qrc_resources.o so `:/themes/...` is registered.
    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Trowel");
    QApplication::setOrganizationName("turmeric");
    QApplication::setApplicationVersion("0.0.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("Trowel — a native editor for turmeric");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "Turmeric source file to open", "[file]");
    parser.process(app);

    trowel::MainWindow window;
    const auto positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openPath(positional.first());
    }
    window.show();

    return QApplication::exec();
}
