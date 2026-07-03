#include "editor/editor_view.h"

#include "editor/theme_loader.h"
#include "editor/turmeric_lexer.h"

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

    sci_->setILexer(reinterpret_cast<sptr_t>(CreateTurmericLexer(false)));
    ApplyThemeToEditor(sci_, LoadBuiltinDarkTheme());

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

    sci_->setCaretLineVisible(false);

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
    const int size = font.pointSize() > 0 ? font.pointSize() : 12;
    // Apply font to every style we know about individually. Do NOT use
    // styleClearAll — that would copy STYLE_DEFAULT to all styles and wipe the
    // per-style foreground colors installed by ApplyThemeToEditor.
    sci_->styleSetFont(STYLE_DEFAULT, family.constData());
    sci_->styleSetSize(STYLE_DEFAULT, size);
    sci_->styleSetFont(STYLE_LINENUMBER, family.constData());
    sci_->styleSetSize(STYLE_LINENUMBER, size);
    for (int s = 0; s < static_cast<int>(TurStyle::Count); ++s) {
        sci_->styleSetFont(s, family.constData());
        sci_->styleSetSize(s, size);
    }
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

QByteArray EditorView::text() const {
    QByteArray raw = sci_->getText(sci_->textLength() + 1);
    if (raw.endsWith('\0')) raw.chop(1);
    return raw;
}

QByteArray EditorView::textInRange(int startPos, int endPos) const {
    if (endPos <= startPos) return {};
    return sci_->textRange(startPos, endPos);
}

std::pair<int, int> EditorView::selectionRange() const {
    const int start = sci_->selectionStart();
    const int end = sci_->selectionEnd();
    return {start, end};
}

void EditorView::setPath(const QString& path) {
    if (path_ == path) return;
    path_ = path;
    emit filePathChanged(path_);
}

}
