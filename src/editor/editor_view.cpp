#include "editor/editor_view.h"

#include <ScintillaEdit.h>

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QTextStream>
#include <QVBoxLayout>

namespace trowel {

namespace {
constexpr int kLineNumberMargin = 0;
constexpr int kSymbolMargin = 1;
constexpr int kFoldMargin = 2;
}

EditorView::EditorView(QWidget* parent)
    : QWidget(parent)
    , sci_(new ScintillaEdit(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(sci_);

    applyDefaultStyling();

    connect(sci_, &ScintillaEditBase::savePointChanged, this, [this](bool dirty) {
        emit modifiedChanged(dirty);
    });
}

void EditorView::applyDefaultStyling() {
    sci_->styleSetFont(STYLE_DEFAULT, "Menlo");
    sci_->styleSetSize(STYLE_DEFAULT, 12);
    sci_->styleClearAll();

    sci_->setMarginTypeN(kLineNumberMargin, SC_MARGIN_NUMBER);
    sci_->setMarginWidthN(kLineNumberMargin, 44);
    sci_->setMarginWidthN(kSymbolMargin, 0);
    sci_->setMarginWidthN(kFoldMargin, 0);

    sci_->setCaretLineVisible(true);
    sci_->setCaretLineBack(0xEEEEEE);
    sci_->setCaretLineBackAlpha(64);

    sci_->setUseTabs(false);
    sci_->setTabWidth(2);
    sci_->setIndent(2);
    sci_->setBackSpaceUnIndents(true);
    sci_->setEOLMode(SC_EOL_LF);

    sci_->setViewWS(SCWS_INVISIBLE);
    sci_->setScrollWidthTracking(true);
    sci_->setEndAtLastLine(false);
}

void EditorView::setFont(const QFont& font) {
    const QByteArray family = font.family().toUtf8();
    sci_->styleSetFont(STYLE_DEFAULT, family.constData());
    sci_->styleSetSize(STYLE_DEFAULT, font.pointSize() > 0 ? font.pointSize() : 12);
    sci_->styleClearAll();
}

bool EditorView::loadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray contents = file.readAll();
    sci_->setText(contents.constData());
    sci_->emptyUndoBuffer();
    sci_->setSavePoint();
    setPath(path);
    emit modifiedChanged(false);
    return true;
}

bool EditorView::saveFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray contents = sci_->getText(sci_->textLength() + 1);
    // getText appends a NUL byte; strip it before writing.
    file.write(contents.constData(), contents.size() > 0 && contents.endsWith('\0')
                                         ? contents.size() - 1
                                         : contents.size());
    file.close();
    sci_->setSavePoint();
    setPath(path);
    emit modifiedChanged(false);
    return true;
}

bool EditorView::saveCurrent() {
    if (path_.isEmpty()) return false;
    return saveFile(path_);
}

bool EditorView::isModified() const {
    return sci_->modify();
}

bool EditorView::isEmpty() const {
    return sci_->textLength() == 0;
}

void EditorView::setPath(const QString& path) {
    if (path_ == path) return;
    path_ = path;
    emit filePathChanged(path_);
}

}
