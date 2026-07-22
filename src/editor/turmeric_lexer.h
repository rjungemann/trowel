#pragma once

#include <ILexer.h>

namespace trowel {

// Style IDs applied by TurmericLexer. Names mirror the theme keys in
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

// Number of distinct colors the rainbow-bracket cycle uses.
inline constexpr int kRainbowLevels = 7;
// Highest style id the lexer can emit (inclusive).
inline constexpr int kHighestStyle = static_cast<int>(TurStyle::BracketError);

// Construct a fresh lexer instance. Ownership passes to Scintilla — release
// happens via ILexer5::Release(). When `rainbow` is true, brackets are styled
// by nesting depth (Rainbow0..); otherwise they use the flat Delim/CurlyInfix
// styles.
Scintilla::ILexer5* CreateTurmericLexer(bool sweetExp = false, bool rainbow = true);

}
