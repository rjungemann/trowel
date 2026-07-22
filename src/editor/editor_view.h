#pragma once

#include "app/tab_content.h"

#include <QFont>
#include <QString>

#include <utility>

class ScintillaEdit;

namespace trowel {

class EditorView : public TabContent {
    Q_OBJECT
public:
    explicit EditorView(QWidget* parent = nullptr);

    bool loadFile(const QString& path);
    bool saveFile(const QString& path);
    bool saveCurrent();

    Kind kind() const override { return Kind::Editor; }
    QString displayName() const override;
    QString filePath() const override { return path_; }
    bool isModified() const override;
    bool isEmpty() const override;
    QByteArray text() const;
    QByteArray textInRange(int startPos, int endPos) const;
    std::pair<int, int> selectionRange() const;

    void setFont(const QFont& font);
    QFont currentFont() const { return currentFont_; }

    // Toggle rainbow (depth-colored) brackets and re-lex the whole document.
    void setRainbowBrackets(bool enabled);
    bool rainbowBrackets() const { return rainbow_; }

    // Default rainbow-bracket preference, read from QSettings.
    static bool rainbowBracketsDefault();

    // Control API surface.
    ScintillaEdit* sciWidget() const { return sci_; }
    void setText(const QByteArray& text);
    int cursorPos() const;
    int anchorPos() const;
    void setCursorPos(int pos);
    void setSelection(int anchor, int caret);
    std::pair<int, int> lineColFromPos(int pos) const;
    int posFromLineCol(int line, int col) const;
    int lineCount() const;
    int styleAt(int pos) const;

private:
    void applyDefaultStyling();
    void setPath(const QString& path);

    ScintillaEdit* sci_;
    QString path_;
    QFont currentFont_;
    bool rainbow_ = true;
};

}
