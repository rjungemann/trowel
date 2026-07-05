#pragma once

#include <QColor>
#include <QPlainTextEdit>
#include <QTextCharFormat>

#include <array>
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
    void clearScreen();

    // Set the ANSI palette + default fg/bg for SGR rendering. Colors are
    // consulted for SGR 30–37, 90–97, 38/48;5;n and 38/48;2;r;g;b. Default fg
    // and bg are applied on SGR 0 (reset) and on the initial state.
    void setTerminalPalette(const QColor& defaultFg, const QColor& defaultBg,
                            const std::array<QColor, 16>& ansi);

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
    void applySgr(const QByteArray& params);
    QColor xtermColor(int index) const;
    void resetFormat();
    void emitResize();

    PtySession* pty_ = nullptr;
    QByteArray pending_;

    // SGR state.
    std::array<QColor, 16> ansi_ = {};
    QColor defaultFg_;
    QColor defaultBg_;
    QTextCharFormat currentFormat_;
    bool boldOn_ = false;
    bool italicOn_ = false;
    bool underlineOn_ = false;
    bool reverseOn_ = false;
    // Semantic fg/bg so bold-brightening can be applied without losing the
    // originally-requested color.
    QColor fgColor_;
    QColor bgColor_;
    bool fgIsDefault_ = true;
    bool bgIsDefault_ = true;
};

}
