#include "repl/terminal_view.h"

#include "repl/pty_session.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>

namespace trowel {

TerminalView::TerminalView(QWidget* parent)
    : QPlainTextEdit(parent)
{
    applyDefaultStyling();
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setContextMenuPolicy(Qt::NoContextMenu);
    setCursorWidth(2);
}

void TerminalView::applyDefaultStyling() {
    const QString family = QFontDatabase::hasFamily("Iosevka") ? "Iosevka" : "Menlo";
    QFont font(family, 12);
    font.setFixedPitch(true);
    setFont(font);
    setStyleSheet(
        "QPlainTextEdit {"
        "  background-color: #1e1e1e;"
        "  color: #d4d4d4;"
        "  border: none;"
        "  padding: 4px;"
        "}"
    );
}

void TerminalView::attach(PtySession* pty) {
    detach();
    pty_ = pty;
    if (!pty_) return;
    connect(pty_, &PtySession::dataReceived, this, &TerminalView::appendOutput);
    emitResize();
}

void TerminalView::detach() {
    if (pty_) {
        disconnect(pty_, nullptr, this, nullptr);
        pty_ = nullptr;
    }
}

void TerminalView::showBanner(const QString& text) {
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    cursor.insertText("\n");
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

// Consume ANSI CSI / OSC escapes; emit the remaining printable + control bytes
// (LF, CR, BS, TAB) to the QTextCursor. Colors ignored for now — this is enough
// to keep the REPL display clean without pulling in a full terminal emulator.
void TerminalView::insertProcessedText(const QByteArray& bytes) {
    QByteArray input = pending_ + bytes;
    pending_.clear();

    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);

    int i = 0;
    const int n = input.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c == 0x1b) {  // ESC
            if (i + 1 >= n) { pending_ = input.mid(i); break; }
            unsigned char kind = static_cast<unsigned char>(input[i + 1]);
            if (kind == '[') {
                // CSI: ESC [ <params> <final in @-~>
                int j = i + 2;
                while (j < n) {
                    unsigned char b = static_cast<unsigned char>(input[j]);
                    if (b >= 0x40 && b <= 0x7e) { ++j; break; }
                    ++j;
                }
                if (j > n) { pending_ = input.mid(i); break; }
                i = j;
                continue;
            } else if (kind == ']') {
                // OSC: ESC ] ... BEL or ST(ESC \)
                int j = i + 2;
                bool done = false;
                while (j < n) {
                    unsigned char b = static_cast<unsigned char>(input[j]);
                    if (b == 0x07) { ++j; done = true; break; }
                    if (b == 0x1b && j + 1 < n && input[j + 1] == '\\') { j += 2; done = true; break; }
                    ++j;
                }
                if (!done) { pending_ = input.mid(i); break; }
                i = j;
                continue;
            } else {
                // Two-byte ESC sequence — skip both.
                i += 2;
                continue;
            }
        }

        if (c == '\r') {
            // Coalesce CRLF into a single newline; lone CR moves to column 0.
            if (i + 1 < n && input[i + 1] == '\n') {
                cursor.movePosition(QTextCursor::End);
                cursor.insertBlock();
                i += 2;
                continue;
            }
            cursor.movePosition(QTextCursor::StartOfLine);
            ++i;
            continue;
        }
        if (c == '\n') {
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock();
            ++i;
            continue;
        }
        if (c == '\b') {
            cursor.deletePreviousChar();
            ++i;
            continue;
        }
        if (c == 0x07) { // BEL
            ++i;
            continue;
        }
        if (c == '\t' || c >= 0x20) {
            // Collect a run of plain bytes for fewer cursor ops.
            int j = i + 1;
            while (j < n) {
                unsigned char d = static_cast<unsigned char>(input[j]);
                if (d == 0x1b || d == '\r' || d == '\n' || d == '\b' || d == 0x07) break;
                if (d < 0x20 && d != '\t') break;
                ++j;
            }
            cursor.insertText(QString::fromUtf8(input.mid(i, j - i)));
            i = j;
            continue;
        }
        // Unknown control byte — drop.
        ++i;
    }

    setTextCursor(cursor);
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalView::appendOutput(const QByteArray& bytes) {
    insertProcessedText(bytes);
}

void TerminalView::keyPressEvent(QKeyEvent* event) {
    if (!pty_ || !pty_->isRunning()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }

    // Copy shortcut — let the widget handle it.
    if (event->matches(QKeySequence::Copy)) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    // Paste — forward text to PTY.
    if (event->matches(QKeySequence::Paste)) {
        // Let default paste flow do nothing; write clipboard through PTY.
        // For M2, defer proper bracketed-paste — just forward directly.
        // Fall through to write() with the event text below.
    }

    QByteArray toWrite;
    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            toWrite = "\r";
            break;
        case Qt::Key_Backspace:
            toWrite = "\x7f";
            break;
        case Qt::Key_Tab:
            toWrite = "\t";
            break;
        case Qt::Key_Escape:
            toWrite = "\x1b";
            break;
        case Qt::Key_Up:    toWrite = "\x1b[A"; break;
        case Qt::Key_Down:  toWrite = "\x1b[B"; break;
        case Qt::Key_Right: toWrite = "\x1b[C"; break;
        case Qt::Key_Left:  toWrite = "\x1b[D"; break;
        case Qt::Key_Home:  toWrite = "\x1b[H"; break;
        case Qt::Key_End:   toWrite = "\x1b[F"; break;
        case Qt::Key_Delete: toWrite = "\x1b[3~"; break;
        default:
            if (event->modifiers() & Qt::ControlModifier) {
                const int k = event->key();
                if (k >= Qt::Key_A && k <= Qt::Key_Z) {
                    toWrite.append(char(k - Qt::Key_A + 1));
                }
            }
            if (toWrite.isEmpty()) {
                toWrite = event->text().toUtf8();
            }
            break;
    }

    if (!toWrite.isEmpty()) {
        pty_->write(toWrite);
    }
    // Do not chain to QPlainTextEdit::keyPressEvent — the PTY will echo back.
}

void TerminalView::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    emitResize();
}

void TerminalView::emitResize() {
    if (!pty_) return;
    QFontMetrics fm(font());
    const int colWidth = fm.horizontalAdvance('M');
    const int rowHeight = fm.lineSpacing();
    if (colWidth <= 0 || rowHeight <= 0) return;
    const int cols = qMax(20, viewport()->width() / colWidth);
    const int rows = qMax(4, viewport()->height() / rowHeight);
    pty_->resize(rows, cols);
}

}
