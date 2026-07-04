#include "app/directory_view.h"

#include "editor/theme_loader.h"

#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QFileSystemModel>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QToolButton>
#include <QVBoxLayout>

namespace trowel {

namespace {

// Sort directories before files, then case-insensitive by name.
class DirsFirstProxy : public QSortFilterProxyModel {
public:
    explicit DirsFirstProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        auto* fsm = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fsm) return QSortFilterProxyModel::lessThan(left, right);
        const QFileInfo li = fsm->fileInfo(left);
        const QFileInfo ri = fsm->fileInfo(right);
        const bool ld = li.isDir();
        const bool rd = ri.isDir();
        if (ld != rd) return ld;
        return li.fileName().compare(ri.fileName(), Qt::CaseInsensitive) < 0;
    }
};

}

DirectoryView::DirectoryView(QWidget* parent)
    : TabContent(parent)
{
    const Theme theme = LoadBuiltinDarkTheme();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Header row.
    auto* header = new QWidget(this);
    auto* hbox = new QHBoxLayout(header);
    hbox->setContentsMargins(6, 4, 6, 4);
    hbox->setSpacing(6);

    backButton_ = new QToolButton(header);
    backButton_->setText("◀");
    backButton_->setToolTip("Up to parent directory");
    backButton_->setAutoRaise(true);
    connect(backButton_, &QToolButton::clicked, this, &DirectoryView::onBackClicked);
    hbox->addWidget(backButton_);

    pathLabel_ = new QLabel(header);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont pathFont = pathLabel_->font();
    const int basePt = pathFont.pointSize();
    if (basePt > 0) pathFont.setPointSize(std::max(1, basePt - 1));
    else if (pathFont.pixelSize() > 0) pathFont.setPixelSize(std::max(1, pathFont.pixelSize() - 1));
    pathFont.setBold(true);
    pathLabel_->setFont(pathFont);
    hbox->addWidget(pathLabel_, 1);

    header->setStyleSheet(QString(
        "background: %1; color: %2;"
    ).arg(theme.editorBg.name(), theme.editorFg.name()));

    root->addWidget(header);

    // File system model + proxy for dirs-first sort.
    model_ = new QFileSystemModel(this);
    model_->setFilter(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    model_->setReadOnly(true);

    proxy_ = new DirsFirstProxy(this);
    proxy_->setSourceModel(model_);
    proxy_->setDynamicSortFilter(true);

    list_ = new QListView(this);
    list_->setModel(proxy_);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    list_->setStyleSheet(QString(
        "QListView { background: %1; color: %2; border: none; padding: 4px 0; }"
        "QListView::item { padding: 3px 8px; }"
        "QListView::item:selected { background: %3; color: %2; }"
    ).arg(theme.editorBg.name(),
          theme.editorFg.name(),
          theme.selectionBg.name()));

    connect(list_, &QAbstractItemView::activated, this, &DirectoryView::onActivated);
    connect(list_, &QAbstractItemView::doubleClicked, this, &DirectoryView::onActivated);
    list_->installEventFilter(this);

    root->addWidget(list_, 1);
}

QString DirectoryView::displayName() const {
    if (root_.isEmpty()) return QStringLiteral("Directory");
    const QString name = QDir(root_).dirName();
    return name.isEmpty() ? root_ : name;
}

void DirectoryView::setRoot(const QString& absolutePath) {
    QFileInfo info(absolutePath);
    if (!info.exists() || !info.isDir()) return;
    const QString abs = info.absoluteFilePath();
    if (abs == root_) return;
    root_ = abs;

    const QModelIndex srcRoot = model_->setRootPath(root_);
    const QModelIndex proxyRoot = proxy_->mapFromSource(srcRoot);
    proxy_->sort(0);
    list_->setRootIndex(proxyRoot);

    // Select first row for keyboard nav.
    if (proxy_->rowCount(proxyRoot) > 0) {
        const QModelIndex first = proxy_->index(0, 0, proxyRoot);
        list_->setCurrentIndex(first);
    }

    updateHeader();
    emit displayNameChanged();
    emit filePathChanged(root_);
}

void DirectoryView::updateHeader() {
    pathLabel_->setText(root_);
    QDir d(root_);
    backButton_->setEnabled(d.cdUp());
}

void DirectoryView::onActivated(const QModelIndex& proxyIndex) {
    if (!proxyIndex.isValid()) return;
    const QModelIndex src = proxy_->mapToSource(proxyIndex);
    const QFileInfo info = model_->fileInfo(src);
    if (info.isDir()) {
        setRoot(info.absoluteFilePath());
    } else if (info.isFile()) {
        emit fileActivated(info.absoluteFilePath());
    }
}

bool DirectoryView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->modifiers() == Qt::NoModifier) {
            if (ke->key() == Qt::Key_Left) {
                onBackClicked();
                return true;
            }
            if (ke->key() == Qt::Key_Right) {
                onActivated(list_->currentIndex());
                return true;
            }
        }
    }
    return TabContent::eventFilter(watched, event);
}

void DirectoryView::onBackClicked() {
    QDir d(root_);
    if (d.cdUp()) setRoot(d.absolutePath());
}

}
