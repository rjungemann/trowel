#pragma once

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

signals:
    void modifiedChanged(bool modified);
    void filePathChanged(const QString& path);

private:
    void applyDefaultStyling();
    void setPath(const QString& path);

    ScintillaEdit* sci_;
    QString path_;
};

}
