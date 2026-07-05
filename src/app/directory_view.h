#pragma once

#include "app/tab_content.h"

#include <QString>

class QEvent;
class QFileSystemModel;
class QLineEdit;
class QListView;
class QModelIndex;
class QSortFilterProxyModel;
class QToolButton;

namespace trowel {

class DirectoryView : public TabContent {
    Q_OBJECT
public:
    explicit DirectoryView(QWidget* parent = nullptr);

    void setRoot(const QString& absolutePath);
    QString root() const { return root_; }

    bool eventFilter(QObject* watched, QEvent* event) override;

    Kind kind() const override { return Kind::Directory; }
    QString displayName() const override;
    QString filePath() const override { return root_; }

signals:
    void fileActivated(const QString& absolutePath);

private slots:
    void onActivated(const QModelIndex& proxyIndex);
    void onBackClicked();
    void commitPathEdit();

private:
    void updateHeader();

    QString root_;
    QFileSystemModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QListView* list_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QToolButton* backButton_ = nullptr;
};

}
