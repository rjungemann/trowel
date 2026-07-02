#pragma once

#include <QString>
#include <QWidget>

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
