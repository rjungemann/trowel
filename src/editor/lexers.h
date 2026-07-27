#pragma once

#include <ILexer.h>

// Forward-declared rather than including <QString>: pulling in a Qt header
// here would also drag in Qt's `emit` keyword macro, which collides with the
// natural name for a style-emitting helper throughout the scanners.
class QString;

namespace trowel {

// Languages Trowel can highlight. A document is lexed by one root language;
// Markdown additionally delegates fenced-code bodies to a guest language, and
// Turmeric delegates its inline ``` blocks to C.
enum class Language : int {
    Turmeric = 0,
    C,
    Markdown,
    Json,
    Just,
};

// Pick a language from a file path. Unrecognized (and empty, i.e. untitled)
// paths fall back to Turmeric, matching Trowel's pre-existing behavior.
Language LanguageForPath(const QString& path);

// Style IDs applied by the Turmeric scanner. Names mirror the theme keys in
// resources/turmeric-dark.theme.json.
enum class TurStyle : int {
    Default = 0,
    LineComment,
    DocComment,
    BlockComment,
    String,
    StringEscape,
    Number,
    Boolean,
    Nil,
    KeywordLit,
    CharLit,
    Metadata,
    Quote,
    Operator,
    Define,
    Control,
    Type,
    Effect,
    Except,
    Special,
    Builtin,
    CBlock,
    LangDir,
    Delim,
    CurlyInfix,
    NeotericCall,
    Identifier,
    Invalid,

    Count,

    // Rainbow bracket styles. These deliberately start above Scintilla's
    // predefined style range (STYLE_DEFAULT=32 .. STYLE_FOLDDISPLAYTEXT=39) so
    // they never collide with those reserved slots. Brackets are colored by
    // their nesting depth, cycling through Rainbow0..Rainbow6; an unmatched
    // closing bracket is painted with BracketError.
    Rainbow0 = 40,
    Rainbow1,
    Rainbow2,
    Rainbow3,
    Rainbow4,
    Rainbow5,
    Rainbow6,
    BracketError,
};

// Each additional language gets its own contiguous block of style IDs, clear
// of both the Turmeric range above and Scintilla's reserved 32..39.
enum class CStyle : int {
    Default = 48,
    Comment,
    DocComment,
    Preproc,
    Keyword,
    Type,
    String,
    StringEscape,
    Char,
    Number,
    Operator,
    Identifier,
};

enum class MdStyle : int {
    Default = 64,
    Heading,
    Emphasis,
    Strong,
    CodeSpan,
    Fence,
    CodeBlock,
    LinkText,
    LinkUrl,
    Blockquote,
    ListMarker,
    Rule,
    Html,
    Escape,
};

enum class JsonStyle : int {
    Default = 80,
    Key,
    String,
    StringEscape,
    Number,
    Literal,
    Operator,
    Error,
};

enum class JustStyle : int {
    Default = 91,
    Comment,
    RecipeName,
    Dependency,
    Parameter,
    Assign,
    Interpolation,
    Backtick,
    Keyword,
    String,
    Number,
    Body,
    Attribute,
    Operator,
};

// Number of distinct colors the rainbow-bracket cycle uses.
inline constexpr int kRainbowLevels = 7;
// Highest style id any scanner can emit (inclusive). EditorView applies the
// editor font across 0..kMaxStyleId.
inline constexpr int kMaxStyleId = static_cast<int>(JustStyle::Operator);

// Construct a fresh lexer for `lang`. Ownership passes to Scintilla — release
// happens via ILexer5::Release(). When `rainbow` is true, brackets are styled
// by nesting depth (Rainbow0..); otherwise they use flat per-language styles.
Scintilla::ILexer5* CreateLexerForLanguage(Language lang, bool rainbow = true);

}
