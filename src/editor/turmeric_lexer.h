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

    Count
};

// Construct a fresh lexer instance. Ownership passes to Scintilla — release
// happens via ILexer5::Release().
Scintilla::ILexer5* CreateTurmericLexer(bool sweetExp = false);

}
