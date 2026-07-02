#pragma once

#include <QMainWindow>

class QSplitter;

namespace trowel {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QSplitter* splitter_;
};

}
