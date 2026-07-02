#include "app/main_window.h"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Trowel");
    QApplication::setOrganizationName("turmeric");

    trowel::MainWindow window;
    window.show();

    return QApplication::exec();
}
