#pragma once

#include <QPlainTextEdit>

#include <utility>

namespace trowel {

class PtySession;

class TerminalView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TerminalView(QWidget* parent = nullptr);

    void attach(PtySession* pty);
    void detach();

    void appendOutput(const QByteArray& bytes);
    void showBanner(const QString& text);

    PtySession* pty() const { return pty_; }
    QString screenText(int lastLines = -1) const;
    std::pair<int, int> screenCursor() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override { return {600, 400}; }

private:
    void applyDefaultStyling();
    void insertProcessedText(const QByteArray& bytes);
    void emitResize();

    PtySession* pty_ = nullptr;
    QByteArray pending_;
};

}
