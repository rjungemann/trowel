#pragma once

#include <QFont>
#include <QString>
#include <QWidget>

#include <utility>

class ScintillaEdit;

namespace trowel {

class EditorView : public QWidget {
    Q_OBJECT
public:
    explicit EditorView(QWidget* parent = nullptr);

    bool loadFile(const QString& path);
    bool saveFile(const QString& path);
    bool saveCurrent();

    QString filePath() const { return path_; }
    bool isModified() const;
    bool isEmpty() const;
    QByteArray text() const;
    QByteArray textInRange(int startPos, int endPos) const;
    std::pair<int, int> selectionRange() const;

    void setFont(const QFont& font);
    QFont currentFont() const { return currentFont_; }

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

signals:
    void modifiedChanged(bool modified);
    void filePathChanged(const QString& path);

private:
    void applyDefaultStyling();
    void setPath(const QString& path);

    ScintillaEdit* sci_;
    QString path_;
    QFont currentFont_;
};

}
