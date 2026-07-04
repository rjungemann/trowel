#pragma once

#include <QColor>
#include <QHash>
#include <QString>

class ScintillaEdit;

namespace trowel {

class TerminalView;

struct StyleSpec {
    QColor fg;
    QColor bg;
    bool bold = false;
    bool italic = false;
};

struct Theme {
    QString name;
    QColor editorBg;
    QColor editorFg;
    QColor caret;
    QColor selectionBg;
    QColor currentLineBg;
    QColor lineNumberFg;
    QColor lineNumberBg;
    QColor activeLineNumberFg;
    QColor matchedBraceFg;
    QColor matchedBraceBg;
    QColor indentGuide;

    QColor terminalBg;
    QColor terminalFg;
    QColor terminalCaret;
    // Standard 16-color ANSI palette: 0-7 normal, 8-15 bright.
    QColor ansi[16];

    QHash<QString, StyleSpec> styles;
};

// Load the built-in "Turmeric Dark" theme from the Qt resource bundle.
Theme LoadBuiltinDarkTheme();

// Apply the given theme to a Scintilla editor. Only the style slots defined
// in the theme are touched; the caller is responsible for the base font.
void ApplyThemeToEditor(ScintillaEdit* sci, const Theme& theme);

void ApplyThemeToTerminal(TerminalView* terminal, const Theme& theme);

}
