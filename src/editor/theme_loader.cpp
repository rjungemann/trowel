#include "editor/theme_loader.h"

#include "editor/turmeric_lexer.h"
#include "repl/terminal_view.h"

#include <ScintillaEdit.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPalette>
#include <QWidget>

namespace trowel {

namespace {

QColor parseColor(const QJsonValue& v, const QColor& fallback = {}) {
    if (!v.isString()) return fallback;
    QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

StyleSpec parseStyle(const QJsonValue& v) {
    StyleSpec s;
    if (!v.isObject()) return s;
    const QJsonObject o = v.toObject();
    s.fg = parseColor(o.value("fg"));
    s.bg = parseColor(o.value("bg"));
    s.bold = o.value("bold").toBool(false);
    s.italic = o.value("italic").toBool(false);
    return s;
}

int bgra(const QColor& c) {
    // Scintilla accepts 0x00BBGGRR for opaque colors.
    return (c.blue() << 16) | (c.green() << 8) | c.red();
}

int scStyleFor(TurStyle s) {
    return static_cast<int>(s);
}

void setStyle(ScintillaEdit* sci, TurStyle style, const StyleSpec& spec,
              const QColor& defaultBg) {
    const int id = scStyleFor(style);
    if (spec.fg.isValid()) sci->styleSetFore(id, bgra(spec.fg));
    sci->styleSetBack(id, bgra(spec.bg.isValid() ? spec.bg : defaultBg));
    sci->styleSetBold(id, spec.bold);
    sci->styleSetItalic(id, spec.italic);
}

const QHash<QString, TurStyle>& StyleKeyMap() {
    static const QHash<QString, TurStyle> m = {
        {"default",      TurStyle::Default},
        {"lineComment",  TurStyle::LineComment},
        {"docComment",   TurStyle::DocComment},
        {"blockComment", TurStyle::BlockComment},
        {"string",       TurStyle::String},
        {"stringEscape", TurStyle::StringEscape},
        {"number",       TurStyle::Number},
        {"boolean",      TurStyle::Boolean},
        {"nil",          TurStyle::Nil},
        {"keywordLit",   TurStyle::KeywordLit},
        {"charLit",      TurStyle::CharLit},
        {"metadata",     TurStyle::Metadata},
        {"quote",        TurStyle::Quote},
        {"operator",     TurStyle::Operator},
        {"define",       TurStyle::Define},
        {"control",      TurStyle::Control},
        {"type",         TurStyle::Type},
        {"effect",       TurStyle::Effect},
        {"except",       TurStyle::Except},
        {"special",      TurStyle::Special},
        {"builtin",      TurStyle::Builtin},
        {"cblock",       TurStyle::CBlock},
        {"langDir",      TurStyle::LangDir},
        {"delim",        TurStyle::Delim},
        {"curlyInfix",   TurStyle::CurlyInfix},
        {"neotericCall", TurStyle::NeotericCall},
        {"identifier",   TurStyle::Identifier},
        {"invalid",      TurStyle::Invalid},
    };
    return m;
}

}

Theme LoadBuiltinDarkTheme() {
    Theme t;
    QFile f(":/themes/turmeric-dark.theme.json");
    if (!f.open(QIODevice::ReadOnly)) return t;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonObject root = doc.object();

    t.name = root.value("name").toString("Turmeric Dark");

    const QJsonObject ed = root.value("editor").toObject();
    t.editorBg            = parseColor(ed.value("background"),           QColor("#0C0A08"));
    t.editorFg            = parseColor(ed.value("foreground"),           QColor("#EAE0D2"));
    t.caret               = parseColor(ed.value("caret"),                QColor("#D48B1C"));
    t.selectionBg         = parseColor(ed.value("selectionBackground"),  QColor("#D48B1C2E"));
    t.currentLineBg       = parseColor(ed.value("currentLineBackground"),QColor("#111009"));
    t.lineNumberFg        = parseColor(ed.value("lineNumberForeground"), QColor("#453F39"));
    t.lineNumberBg        = parseColor(ed.value("lineNumberBackground"), QColor("#0C0A08"));
    t.activeLineNumberFg  = parseColor(ed.value("activeLineNumberForeground"), QColor("#88796C"));
    t.matchedBraceFg      = parseColor(ed.value("matchedBraceForeground"), QColor("#EFA030"));
    t.matchedBraceBg      = parseColor(ed.value("matchedBraceBackground"), QColor("#D48B1C1F"));
    t.indentGuide         = parseColor(ed.value("indentGuide"),          QColor("#252119"));

    const QJsonObject term = root.value("terminal").toObject();
    t.terminalBg     = parseColor(term.value("background"), t.editorBg);
    t.terminalFg     = parseColor(term.value("foreground"), t.editorFg);
    t.terminalCaret  = parseColor(term.value("caret"),      t.caret);

    const QJsonObject styles = root.value("styles").toObject();
    for (auto it = styles.begin(); it != styles.end(); ++it) {
        t.styles.insert(it.key(), parseStyle(it.value()));
    }
    return t;
}

void ApplyThemeToEditor(ScintillaEdit* sci, const Theme& theme) {
    // Force the widget palette (Active + Inactive + Disabled) to match the
    // theme background. Without this, macOS swaps a system Base color under
    // the viewport when focus changes and any transparent Scintilla styling
    // lets it bleed through — that's what caused "black on black when focused,
    // fine when unfocused".
    QPalette p = sci->palette();
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::Base,       theme.editorBg);
        p.setColor(group, QPalette::Window,     theme.editorBg);
        p.setColor(group, QPalette::Text,       theme.editorFg);
        p.setColor(group, QPalette::WindowText, theme.editorFg);
    }
    sci->setPalette(p);
    if (auto* vp = sci->viewport()) vp->setPalette(p);

    // Base default style — set fg/bg, then styleClearAll so unset styles inherit.
    sci->styleSetFore(STYLE_DEFAULT, bgra(theme.editorFg));
    sci->styleSetBack(STYLE_DEFAULT, bgra(theme.editorBg));
    sci->styleClearAll();

    for (auto it = theme.styles.begin(); it != theme.styles.end(); ++it) {
        const auto styleIt = StyleKeyMap().find(it.key());
        if (styleIt == StyleKeyMap().end()) continue;
        setStyle(sci, styleIt.value(), it.value(), theme.editorBg);
    }

    // Line-number margin.
    sci->styleSetFore(STYLE_LINENUMBER, bgra(theme.lineNumberFg));
    sci->styleSetBack(STYLE_LINENUMBER, bgra(theme.lineNumberBg));

    // Caret + selection + caret-line highlight.
    sci->setCaretFore(bgra(theme.caret));
    sci->setSelBack(true, bgra(theme.selectionBg));
    sci->setSelAlpha(theme.selectionBg.alpha());
    sci->setCaretLineBack(bgra(theme.currentLineBg));
    sci->setCaretLineBackAlpha(48);  // translucent so text on the line stays readable

    // Matched brace.
    sci->styleSetFore(STYLE_BRACELIGHT, bgra(theme.matchedBraceFg));
    sci->styleSetBack(STYLE_BRACELIGHT, bgra(theme.matchedBraceBg));
    sci->styleSetFore(STYLE_BRACEBAD, bgra(theme.matchedBraceFg));
}

void ApplyThemeToTerminal(TerminalView* terminal, const Theme& theme) {
    if (!terminal) return;
    QPalette p = terminal->palette();
    p.setColor(QPalette::Base, theme.terminalBg);
    p.setColor(QPalette::Text, theme.terminalFg);
    terminal->setPalette(p);
    terminal->setStyleSheet(QString(
        "QPlainTextEdit {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: none;"
        "  padding: 4px;"
        "}"
    ).arg(theme.terminalBg.name(), theme.terminalFg.name()));
    terminal->setCursorWidth(2);
}

}
