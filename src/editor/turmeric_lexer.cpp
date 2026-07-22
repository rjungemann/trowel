#include "editor/turmeric_lexer.h"

#include <ILexer.h>
#include <Scintilla.h>

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using Scintilla::IDocument;
using Scintilla::ILexer5;

namespace trowel {

namespace {

// Keyword sets ported from turmeric/vim-syntax/syntax/turmeric.vim.
const std::unordered_set<std::string_view>& DefineKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "def", "defn", "defmacro", "defmodule", "defdata", "defgadt",
        "defclass", "definstance", "defstruct", "deftuple",
        "defpackage", "use", "import", "export",
    };
    return s;
}
const std::unordered_set<std::string_view>& ControlKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "if", "cond", "case", "match", "loop", "while", "for",
        "break", "continue", "return", "and", "or", "not",
        "when", "unless",
    };
    return s;
}
const std::unordered_set<std::string_view>& TypeKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "type", "typeclass", "impl", "where", "forall", "generic",
        "trait", "any",
    };
    return s;
}
const std::unordered_set<std::string_view>& EffectKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "effect", "handle", "do", "perform", "with",
    };
    return s;
}
const std::unordered_set<std::string_view>& ExceptKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "try", "catch", "throw", "finally", "raise",
    };
    return s;
}
const std::unordered_set<std::string_view>& SpecialKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "let", "let*", "lambda", "fn", "quote", "unquote",
        "quasiquote", "begin",
    };
    return s;
}
const std::unordered_set<std::string_view>& BuiltinKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "vec", "push", "pop", "len", "nth", "set-nth!", "append",
        "first", "rest", "cons", "reverse", "list", "apply", "map",
        "filter", "reduce", "fold", "print", "println", "read",
        "str", "concat", "split", "coerce", "cast", "type-of",
        "is-a?",
        "string?", "vector?", "list?", "number?", "symbol?",
        "boolean?", "null?", "atom?", "pair?", "empty?", "extern-c",
    };
    return s;
}
const std::unordered_set<std::string_view>& NilKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "nil", "null", "none", "unit",
    };
    return s;
}

constexpr bool IsSymbolStart(unsigned char c) {
    return (std::isalpha(c) || c == '_' || c == '+' || c == '-' || c == '*'
            || c == '/' || c == '<' || c == '>' || c == '=' || c == '!'
            || c == '?' || c == '$' || c == '%' || c == '&' || c == '^');
}
constexpr bool IsSymbolCont(unsigned char c) {
    return (std::isalnum(c) || c == '_' || c == '-' || c == '+' || c == '*'
            || c == '/' || c == '<' || c == '>' || c == '=' || c == '!'
            || c == '?' || c == '$' || c == '%' || c == '&' || c == '.'
            || c == ':' || c == '\'');
}

// Line state bits.
constexpr int kBlockCommentDepthMask = 0xFF;
constexpr int kInStringBit = 0x100;
constexpr int kInCBlockBit = 0x200;
constexpr int kInDatumCommentBit = 0x400;
constexpr int kDcDepthShift = 16;
constexpr int kDcDepthMask = 0xFF << kDcDepthShift;
// Bracket nesting depth carried across lines so rainbow coloring stays
// coherent for multi-line s-expressions. Stored in bits 24-30 (bit 31 is left
// clear to avoid touching the sign bit of the int line state).
constexpr int kBracketDepthShift = 24;
constexpr int kBracketDepthMask = 0x7F << kBracketDepthShift;

// Map a nesting depth to its rainbow style, cycling through the palette.
constexpr TurStyle RainbowStyleForDepth(int depth) {
    const int level = depth % kRainbowLevels;
    return static_cast<TurStyle>(static_cast<int>(TurStyle::Rainbow0) + level);
}

class TurmericLexer final : public ILexer5 {
public:
    explicit TurmericLexer(bool sweet, bool rainbow)
        : sweet_(sweet), rainbow_(rainbow) {}

    int SCI_METHOD Version() const override { return Scintilla::lvRelease5; }
    void SCI_METHOD Release() override { delete this; }

    const char* SCI_METHOD PropertyNames() override { return ""; }
    int SCI_METHOD PropertyType(const char*) override { return 0; }
    const char* SCI_METHOD DescribeProperty(const char*) override { return ""; }
    Sci_Position SCI_METHOD PropertySet(const char*, const char*) override { return -1; }
    const char* SCI_METHOD DescribeWordListSets() override { return ""; }
    Sci_Position SCI_METHOD WordListSet(int, const char*) override { return -1; }

    void SCI_METHOD Lex(Sci_PositionU startPos, Sci_Position lengthDoc,
                        int initStyle, IDocument* doc) override;
    void SCI_METHOD Fold(Sci_PositionU, Sci_Position, int, IDocument*) override {}

    void* SCI_METHOD PrivateCall(int, void*) override { return nullptr; }
    int SCI_METHOD LineEndTypesSupported() override { return 0; }

    int SCI_METHOD AllocateSubStyles(int, int) override { return -1; }
    int SCI_METHOD SubStylesStart(int) override { return -1; }
    int SCI_METHOD SubStylesLength(int) override { return 0; }
    int SCI_METHOD StyleFromSubStyle(int subStyle) override { return subStyle; }
    int SCI_METHOD PrimaryStyleFromStyle(int style) override { return style; }
    void SCI_METHOD FreeSubStyles() override {}
    void SCI_METHOD SetIdentifiers(int, const char*) override {}
    int SCI_METHOD DistanceToSecondaryStyles() override { return 0; }
    const char* SCI_METHOD GetSubStyleBases() override { return ""; }

    int SCI_METHOD NamedStyles() override {
        return kHighestStyle + 1;
    }
    const char* SCI_METHOD NameOfStyle(int) override { return ""; }
    const char* SCI_METHOD TagsOfStyle(int) override { return ""; }
    const char* SCI_METHOD DescriptionOfStyle(int) override { return ""; }

    const char* SCI_METHOD GetName() override {
        return sweet_ ? "turmeric-sweet" : "turmeric";
    }
    int SCI_METHOD GetIdentifier() override { return 0; }
    const char* SCI_METHOD PropertyGet(const char*) override { return ""; }

private:
    void SetStyle(IDocument* doc, Sci_PositionU pos, Sci_Position length, TurStyle style) {
        doc->StartStyling(static_cast<Sci_Position>(pos));
        doc->SetStyleFor(length, static_cast<char>(style));
    }

    TurStyle SymbolStyle(std::string_view sym) const;

    bool sweet_;
    bool rainbow_;
};

TurStyle TurmericLexer::SymbolStyle(std::string_view sym) const {
    if (sym.empty()) return TurStyle::Identifier;
    // Booleans and nil-family.
    if (NilKeywords().count(sym)) return TurStyle::Nil;
    // Definitions, control, types, etc.
    if (DefineKeywords().count(sym))  return TurStyle::Define;
    if (ControlKeywords().count(sym)) return TurStyle::Control;
    if (TypeKeywords().count(sym))    return TurStyle::Type;
    if (EffectKeywords().count(sym))  return TurStyle::Effect;
    if (ExceptKeywords().count(sym))  return TurStyle::Except;
    if (SpecialKeywords().count(sym)) return TurStyle::Special;
    if (BuiltinKeywords().count(sym)) return TurStyle::Builtin;
    return TurStyle::Identifier;
}

void TurmericLexer::Lex(Sci_PositionU startPos, Sci_Position lengthDoc,
                         int initStyle, IDocument* doc) {
    (void)initStyle;

    // Widen the range back to a line start so multi-line state is coherent.
    const Sci_Position startLine = doc->LineFromPosition(static_cast<Sci_Position>(startPos));
    const Sci_Position lineStartPos = doc->LineStart(startLine);
    const Sci_Position endPos = static_cast<Sci_Position>(startPos) + lengthDoc;
    const Sci_Position readLen = endPos - lineStartPos;

    std::vector<char> buf(static_cast<size_t>(readLen) + 1, 0);
    doc->GetCharRange(buf.data(), lineStartPos, readLen);
    const char* text = buf.data();

    int lineState = (startLine > 0) ? doc->GetLineState(startLine - 1) : 0;
    int blockDepth = lineState & kBlockCommentDepthMask;
    bool inString = (lineState & kInStringBit) != 0;
    bool inCBlock = (lineState & kInCBlockBit) != 0;
    bool inDatumComment = (lineState & kInDatumCommentBit) != 0;
    int dcDepth = (lineState & kDcDepthMask) >> kDcDepthShift;
    int bracketDepth = (lineState & kBracketDepthMask) >> kBracketDepthShift;

    Sci_Position i = 0;
    Sci_Position curLine = startLine;

    auto atLineEnd = [&](Sci_Position p) -> bool {
        return p >= readLen || text[p] == '\n' || text[p] == '\r';
    };

    auto stylePosOf = [&](Sci_Position offset) -> Sci_PositionU {
        return static_cast<Sci_PositionU>(lineStartPos + offset);
    };

    auto emit = [&](Sci_Position from, Sci_Position len, TurStyle style) {
        if (len <= 0) return;
        SetStyle(doc, stylePosOf(from), len, style);
    };

    while (i < readLen) {
        // At start of a line, remember state so we can persist it.
        if (i == 0 || (i > 0 && (text[i - 1] == '\n' || text[i - 1] == '\r'))) {
            if (i > 0) {
                doc->SetLineState(curLine,
                    (blockDepth & kBlockCommentDepthMask)
                    | (inString ? kInStringBit : 0)
                    | (inCBlock ? kInCBlockBit : 0)
                    | (inDatumComment ? kInDatumCommentBit : 0)
                    | ((dcDepth << kDcDepthShift) & kDcDepthMask)
                    | ((bracketDepth << kBracketDepthShift) & kBracketDepthMask));
                ++curLine;
            }
        }

        // Continuations from prior lines.
        if (blockDepth > 0) {
            Sci_Position start = i;
            while (i < readLen && blockDepth > 0) {
                if (i + 1 < readLen && text[i] == '#' && text[i + 1] == '|') {
                    ++blockDepth; i += 2; continue;
                }
                if (i + 1 < readLen && text[i] == '|' && text[i + 1] == '#') {
                    --blockDepth; i += 2; continue;
                }
                if (text[i] == '\n' || text[i] == '\r') break;
                ++i;
            }
            emit(start, i - start, TurStyle::BlockComment);
            if (text[i] == '\n' || text[i] == '\r') {
                emit(i, 1, TurStyle::BlockComment); ++i;
            }
            continue;
        }
        if (inString) {
            Sci_Position start = i;
            while (i < readLen && text[i] != '"' && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < readLen) i += 2;
                else ++i;
            }
            if (i < readLen && text[i] == '"') { ++i; inString = false; }
            emit(start, i - start, TurStyle::String);
            continue;
        }
        if (inDatumComment) {
            Sci_Position start = i;
            while (i < readLen && dcDepth > 0 && text[i] != '\n' && text[i] != '\r') {
                char ch = text[i];
                if (ch == '"') {
                    ++i;
                    while (i < readLen && text[i] != '"' && text[i] != '\n' && text[i] != '\r') {
                        if (text[i] == '\\' && i + 1 < readLen) i += 2;
                        else ++i;
                    }
                    if (i < readLen && text[i] == '"') ++i;
                    continue;
                }
                if (ch == ';') {
                    while (i < readLen && text[i] != '\n' && text[i] != '\r') ++i;
                    continue;
                }
                if (ch == '(' || ch == '[' || ch == '{') { ++dcDepth; ++i; continue; }
                if (ch == ')' || ch == ']' || ch == '}') { --dcDepth; ++i; continue; }
                ++i;
            }
            emit(start, i - start, TurStyle::LineComment);
            if (dcDepth == 0) { inDatumComment = false; continue; }
            if (i < readLen && (text[i] == '\n' || text[i] == '\r')) {
                emit(i, 1, TurStyle::LineComment);
                ++i;
            }
            continue;
        }
        if (inCBlock) {
            Sci_Position start = i;
            while (i + 2 < readLen && !(text[i] == '`' && text[i + 1] == '`' && text[i + 2] == '`')) {
                if (text[i] == '\n' || text[i] == '\r') break;
                ++i;
            }
            if (i + 2 < readLen && text[i] == '`' && text[i + 1] == '`' && text[i + 2] == '`') {
                i += 3;
                inCBlock = false;
            }
            emit(start, i - start, TurStyle::CBlock);
            continue;
        }

        char c = text[i];

        // Whitespace / newline
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            Sci_Position start = i;
            while (i < readLen && (text[i] == '\n' || text[i] == '\r'
                                    || text[i] == ' ' || text[i] == '\t')) {
                ++i;
            }
            emit(start, i - start, TurStyle::Default);
            continue;
        }

        // Comments
        if (c == ';') {
            const bool doc3 = (i + 2 < readLen && text[i + 1] == ';' && text[i + 2] == ';');
            Sci_Position start = i;
            while (i < readLen && text[i] != '\n' && text[i] != '\r') ++i;
            emit(start, i - start, doc3 ? TurStyle::DocComment : TurStyle::LineComment);
            continue;
        }
        if (c == '#' && i + 1 < readLen && text[i + 1] == '|') {
            Sci_Position start = i;
            i += 2;
            blockDepth = 1;
            while (i < readLen && blockDepth > 0) {
                if (i + 1 < readLen && text[i] == '#' && text[i + 1] == '|') {
                    ++blockDepth; i += 2; continue;
                }
                if (i + 1 < readLen && text[i] == '|' && text[i + 1] == '#') {
                    --blockDepth; i += 2; continue;
                }
                if (text[i] == '\n' || text[i] == '\r') break;
                ++i;
            }
            emit(start, i - start, TurStyle::BlockComment);
            continue;
        }

        // Datum comment: #; skips the next s-expression.
        if (c == '#' && i + 1 < readLen && text[i + 1] == ';') {
            Sci_Position start = i;
            i += 2;
            while (i < readLen && (text[i] == ' ' || text[i] == '\t')) ++i;
            if (i < readLen) {
                char dc = text[i];
                if (dc == '(' || dc == '[' || dc == '{') {
                    ++i;
                    dcDepth = 1;
                    inDatumComment = true;
                    while (i < readLen && dcDepth > 0 && text[i] != '\n' && text[i] != '\r') {
                        char ch = text[i];
                        if (ch == '"') {
                            ++i;
                            while (i < readLen && text[i] != '"' && text[i] != '\n' && text[i] != '\r') {
                                if (text[i] == '\\' && i + 1 < readLen) i += 2;
                                else ++i;
                            }
                            if (i < readLen && text[i] == '"') ++i;
                            continue;
                        }
                        if (ch == ';') {
                            while (i < readLen && text[i] != '\n' && text[i] != '\r') ++i;
                            continue;
                        }
                        if (ch == '(' || ch == '[' || ch == '{') { ++dcDepth; ++i; continue; }
                        if (ch == ')' || ch == ']' || ch == '}') { --dcDepth; ++i; continue; }
                        ++i;
                    }
                    if (dcDepth == 0) inDatumComment = false;
                } else if (dc == '"') {
                    ++i;
                    while (i < readLen && text[i] != '"' && text[i] != '\n' && text[i] != '\r') {
                        if (text[i] == '\\' && i + 1 < readLen) i += 2;
                        else ++i;
                    }
                    if (i < readLen && text[i] == '"') ++i;
                } else if (dc != '\n' && dc != '\r') {
                    while (i < readLen && text[i] != '\n' && text[i] != '\r'
                           && text[i] != ' ' && text[i] != '\t'
                           && text[i] != '(' && text[i] != ')'
                           && text[i] != '[' && text[i] != ']'
                           && text[i] != '{' && text[i] != '}') {
                        ++i;
                    }
                }
            }
            emit(start, i - start, TurStyle::LineComment);
            continue;
        }

        // C inline block: ```c ... ``` or ``` ... ```
        if (c == '`' && i + 2 < readLen && text[i + 1] == '`' && text[i + 2] == '`') {
            Sci_Position start = i;
            i += 3;
            // Optional language tag (e.g. "c").
            while (i < readLen && (std::isalnum(static_cast<unsigned char>(text[i])))) ++i;
            inCBlock = true;
            while (i < readLen && inCBlock) {
                if (i + 2 < readLen && text[i] == '`' && text[i + 1] == '`' && text[i + 2] == '`') {
                    i += 3; inCBlock = false; break;
                }
                if (text[i] == '\n' || text[i] == '\r') break;
                ++i;
            }
            emit(start, i - start, TurStyle::CBlock);
            continue;
        }

        // String
        if (c == '"') {
            Sci_Position start = i;
            ++i;
            inString = true;
            while (i < readLen && text[i] != '"' && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < readLen) i += 2;
                else ++i;
            }
            if (i < readLen && text[i] == '"') { ++i; inString = false; }
            emit(start, i - start, TurStyle::String);
            continue;
        }

        // Character literal: #\name or #\.
        if (c == '#' && i + 1 < readLen && text[i + 1] == '\\') {
            Sci_Position start = i;
            i += 2;
            if (i < readLen) {
                // Named character
                if (std::isalpha(static_cast<unsigned char>(text[i]))) {
                    while (i < readLen && std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
                } else {
                    ++i; // single-char literal
                }
            }
            emit(start, i - start, TurStyle::CharLit);
            continue;
        }

        // Booleans: #t or #f (word boundary).
        if (c == '#' && i + 1 < readLen && (text[i + 1] == 't' || text[i + 1] == 'f')) {
            if (i + 2 >= readLen || !IsSymbolCont(static_cast<unsigned char>(text[i + 2]))) {
                emit(i, 2, TurStyle::Boolean);
                i += 2;
                continue;
            }
        }

        // #lang directive at column 0
        if (c == '#' && i + 4 < readLen
            && text[i + 1] == 'l' && text[i + 2] == 'a' && text[i + 3] == 'n' && text[i + 4] == 'g') {
            // Check we're at logical BOL.
            bool atBol = (i == 0) || text[i - 1] == '\n' || text[i - 1] == '\r';
            if (atBol) {
                Sci_Position start = i;
                i += 5;
                emit(start, i - start, TurStyle::LangDir);
                // Whitespace to language name
                Sci_Position wsStart = i;
                while (i < readLen && (text[i] == ' ' || text[i] == '\t')) ++i;
                emit(wsStart, i - wsStart, TurStyle::Default);
                Sci_Position nameStart = i;
                while (i < readLen && !atLineEnd(i) && text[i] != ' ' && text[i] != '\t') ++i;
                emit(nameStart, i - nameStart, TurStyle::Type);
                continue;
            }
        }

        // Reader conditional prefix #?
        if (c == '#' && i + 1 < readLen && text[i + 1] == '?') {
            emit(i, 2, TurStyle::LangDir);
            i += 2;
            continue;
        }

        // Metadata annotation: ^foo
        if (c == '^' && i + 1 < readLen && std::isalpha(static_cast<unsigned char>(text[i + 1]))) {
            Sci_Position start = i;
            ++i;
            while (i < readLen && (std::isalnum(static_cast<unsigned char>(text[i]))
                                    || text[i] == '_' || text[i] == '-')) ++i;
            emit(start, i - start, TurStyle::Metadata);
            continue;
        }

        // Keyword literal: :foo
        if (c == ':' && i + 1 < readLen && (std::isalpha(static_cast<unsigned char>(text[i + 1])) || text[i + 1] == '_')) {
            Sci_Position start = i;
            ++i;
            while (i < readLen && IsSymbolCont(static_cast<unsigned char>(text[i]))) ++i;
            emit(start, i - start, TurStyle::KeywordLit);
            continue;
        }

        // Special ops :: and |>
        if (c == ':' && i + 1 < readLen && text[i + 1] == ':') {
            emit(i, 2, TurStyle::Operator); i += 2; continue;
        }
        if (c == '|' && i + 1 < readLen && text[i + 1] == '>') {
            emit(i, 2, TurStyle::Operator); i += 2; continue;
        }

        // Reader macros: ~@ ~ ` '
        if (c == '~' && i + 1 < readLen && text[i + 1] == '@') {
            emit(i, 2, TurStyle::Quote); i += 2; continue;
        }
        if (c == '~' || c == '`' || c == '\'') {
            emit(i, 1, TurStyle::Quote); ++i; continue;
        }

        // Delimiters. With rainbow coloring on, every bracket type is painted
        // by its nesting depth so matching pairs share a color; an unmatched
        // closer is flagged. Without it, brackets fall back to the flat
        // Delim / CurlyInfix styles.
        if (c == '(' || c == '[' || c == '{') {
            const TurStyle flat = (c == '{') ? TurStyle::CurlyInfix : TurStyle::Delim;
            emit(i, 1, rainbow_ ? RainbowStyleForDepth(bracketDepth) : flat);
            ++bracketDepth;
            ++i; continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            const TurStyle flat = (c == '}') ? TurStyle::CurlyInfix : TurStyle::Delim;
            TurStyle s = flat;
            if (rainbow_) {
                if (bracketDepth > 0) {
                    --bracketDepth;
                    s = RainbowStyleForDepth(bracketDepth);
                } else {
                    s = TurStyle::BracketError;
                }
            }
            emit(i, 1, s);
            ++i; continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c))
            || (c == '-' && i + 1 < readLen && std::isdigit(static_cast<unsigned char>(text[i + 1])))
            || (c == '.' && i + 1 < readLen && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            Sci_Position start = i;
            if (c == '-') ++i;
            if (i + 1 < readLen && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                i += 2;
                while (i < readLen && std::isxdigit(static_cast<unsigned char>(text[i]))) ++i;
            } else if (i + 1 < readLen && text[i] == '0' && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
                i += 2;
                while (i < readLen && (text[i] == '0' || text[i] == '1')) ++i;
            } else {
                while (i < readLen && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                if (i < readLen && text[i] == '.') {
                    ++i;
                    while (i < readLen && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
                if (i < readLen && (text[i] == 'e' || text[i] == 'E')) {
                    ++i;
                    if (i < readLen && (text[i] == '+' || text[i] == '-')) ++i;
                    while (i < readLen && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
            }
            emit(start, i - start, TurStyle::Number);
            continue;
        }

        // Identifier / keyword
        if (IsSymbolStart(static_cast<unsigned char>(c))) {
            Sci_Position start = i;
            while (i < readLen && IsSymbolCont(static_cast<unsigned char>(text[i]))) ++i;
            std::string_view sym(text + start, static_cast<size_t>(i - start));
            TurStyle s = SymbolStyle(sym);
            if (s == TurStyle::Identifier) {
                // Neoteric call if immediately followed by '('
                if (i < readLen && text[i] == '(') s = TurStyle::NeotericCall;
            }
            emit(start, i - start, s);
            continue;
        }

        // Fallback: unknown byte
        emit(i, 1, TurStyle::Default);
        ++i;
    }

    // Persist final line state.
    doc->SetLineState(curLine,
        (blockDepth & kBlockCommentDepthMask)
        | (inString ? kInStringBit : 0)
        | (inCBlock ? kInCBlockBit : 0)
        | (inDatumComment ? kInDatumCommentBit : 0)
        | ((dcDepth << kDcDepthShift) & kDcDepthMask)
        | ((bracketDepth << kBracketDepthShift) & kBracketDepthMask));
}

} // namespace

ILexer5* CreateTurmericLexer(bool sweetExp, bool rainbow) {
    return new TurmericLexer(sweetExp, rainbow);
}

}
