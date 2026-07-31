#include "editor/scanner.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

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

TurStyle SymbolStyle(std::string_view sym) {
    if (sym.empty()) return TurStyle::Identifier;
    if (NilKeywords().count(sym))     return TurStyle::Nil;
    if (DefineKeywords().count(sym))  return TurStyle::Define;
    if (ControlKeywords().count(sym)) return TurStyle::Control;
    if (TypeKeywords().count(sym))    return TurStyle::Type;
    if (EffectKeywords().count(sym))  return TurStyle::Effect;
    if (ExceptKeywords().count(sym))  return TurStyle::Except;
    if (SpecialKeywords().count(sym)) return TurStyle::Special;
    if (BuiltinKeywords().count(sym)) return TurStyle::Builtin;
    return TurStyle::Identifier;
}

bool IsFenceAt(const char* text, Sci_Position i, Sci_Position end) {
    return i + 3 <= end && text[i] == '`' && text[i + 1] == '`' && text[i + 2] == '`';
}

// Consume datum-comment (`#;`) content on this line, tracking nesting in
// st.turDcDepth. Strings and line comments inside the skipped datum are
// stepped over so their brackets don't disturb the count.
Sci_Position ConsumeDatumBody(const char* text, Sci_Position i, Sci_Position end,
                              LexState& st) {
    while (i < end && st.turDcDepth > 0) {
        const char ch = text[i];
        if (ch == '"') {
            ++i;
            while (i < end && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < end) i += 2;
                else ++i;
            }
            if (i < end) ++i;
            continue;
        }
        if (ch == ';') return end;
        if (ch == '(' || ch == '[' || ch == '{') { ++st.turDcDepth; ++i; continue; }
        if (ch == ')' || ch == ']' || ch == '}') { --st.turDcDepth; ++i; continue; }
        ++i;
    }
    return i;
}

// Consume the part of this line belonging to an open inline-C block, handing
// the body to the C scanner so embedded C is highlighted as C rather than as
// one flat color. Returns the position just past what was consumed.
Sci_Position ConsumeCBlock(const ScanInput& in, LexState& st, Emitter& out,
                           Sci_Position i) {
    Sci_Position fence = i;
    while (fence < in.end && !IsFenceAt(in.text, fence, in.end)) ++fence;

    if (fence > i) {
        const ScanInput body{in.text, i, fence, in.rainbow};
        ScanCLine(body, st, out);
    }
    out.SetGapStyle(static_cast<int>(TurStyle::Default));

    if (fence < in.end) {
        out.Emit(fence, 3, static_cast<int>(TurStyle::CBlock));
        st.turInCBlock = false;
        st.cInComment = false;
        return fence + 3;
    }
    return in.end;
}

// True when `$` at `i` stands alone as the sweet reader's GROUP/SPLIT marker
// rather than opening a symbol like `$foo` (IsSymbolStart accepts '$').
bool IsSweetGroupAt(const char* text, Sci_Position i, Sci_Position end) {
    return text[i] == '$'
        && (i + 1 >= end || !IsSymbolCont(static_cast<unsigned char>(text[i + 1])));
}

// True when `\` at `i` is the sweet reader's SPLIT marker: a lone backslash,
// not the `#\` character-literal prefix (which the main loop consumes first).
bool IsSweetSplitAt(const char* text, Sci_Position i, Sci_Position end) {
    return text[i] == '\\'
        && (i + 1 >= end || text[i + 1] == ' ' || text[i + 1] == '\t');
}

void ScanTurmeric(const ScanInput& in, LexState& st, Emitter& out, bool sweet) {
    const char* text = in.text;
    const Sci_Position end = in.end;
    Sci_Position i = in.begin;

    out.SetGapStyle(static_cast<int>(TurStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, TurStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    // --- Continuations from previous lines ---------------------------------

    if (st.turBlockDepth > 0) {
        const Sci_Position start = i;
        while (i < end && st.turBlockDepth > 0) {
            if (i + 1 < end && text[i] == '#' && text[i + 1] == '|') {
                ++st.turBlockDepth; i += 2; continue;
            }
            if (i + 1 < end && text[i] == '|' && text[i + 1] == '#') {
                --st.turBlockDepth; i += 2; continue;
            }
            ++i;
        }
        emit(start, i - start, TurStyle::BlockComment);
    }

    if (st.turInString && i < end) {
        const Sci_Position start = i;
        while (i < end && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < end) i += 2;
            else ++i;
        }
        if (i < end && text[i] == '"') { ++i; st.turInString = false; }
        emit(start, i - start, TurStyle::String);
    }

    if (st.turInDatumComment && i < end) {
        const Sci_Position start = i;
        i = ConsumeDatumBody(text, i, end, st);
        emit(start, i - start, TurStyle::LineComment);
        if (st.turDcDepth == 0) st.turInDatumComment = false;
    }

    if (st.turInCBlock) {
        i = ConsumeCBlock(in, st, out, i);
    }

    // --- Main loop ---------------------------------------------------------

    while (i < end) {
        const char c = text[i];

        if (c == ' ' || c == '\t') { ++i; continue; }  // backfilled as Default

        // Line / doc comment.
        if (c == ';') {
            const bool doc3 = (i + 2 < end && text[i + 1] == ';' && text[i + 2] == ';');
            emit(i, end - i, doc3 ? TurStyle::DocComment : TurStyle::LineComment);
            i = end;
            continue;
        }

        // Nested block comment.
        if (c == '#' && i + 1 < end && text[i + 1] == '|') {
            const Sci_Position start = i;
            i += 2;
            st.turBlockDepth = 1;
            while (i < end && st.turBlockDepth > 0) {
                if (i + 1 < end && text[i] == '#' && text[i + 1] == '|') {
                    ++st.turBlockDepth; i += 2; continue;
                }
                if (i + 1 < end && text[i] == '|' && text[i + 1] == '#') {
                    --st.turBlockDepth; i += 2; continue;
                }
                ++i;
            }
            emit(start, i - start, TurStyle::BlockComment);
            continue;
        }

        // Datum comment: #; skips the next s-expression.
        if (c == '#' && i + 1 < end && text[i + 1] == ';') {
            const Sci_Position start = i;
            i += 2;
            while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
            if (i < end) {
                const char dc = text[i];
                if (dc == '(' || dc == '[' || dc == '{') {
                    ++i;
                    st.turDcDepth = 1;
                    st.turInDatumComment = true;
                    i = ConsumeDatumBody(text, i, end, st);
                    if (st.turDcDepth == 0) st.turInDatumComment = false;
                } else if (dc == '"') {
                    ++i;
                    while (i < end && text[i] != '"') {
                        if (text[i] == '\\' && i + 1 < end) i += 2;
                        else ++i;
                    }
                    if (i < end) ++i;
                } else {
                    while (i < end && text[i] != ' ' && text[i] != '\t'
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

        // Inline C block: ```c ... ``` or ``` ... ```. The fence markers and
        // language tag stay CBlock-colored; the body goes to the C scanner.
        if (IsFenceAt(text, i, end)) {
            const Sci_Position start = i;
            i += 3;
            while (i < end && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
            emit(start, i - start, TurStyle::CBlock);
            st.turInCBlock = true;
            st.cInComment = false;
            i = ConsumeCBlock(in, st, out, i);
            continue;
        }

        // String.
        if (c == '"') {
            const Sci_Position start = i;
            ++i;
            st.turInString = true;
            while (i < end && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < end) i += 2;
                else ++i;
            }
            if (i < end && text[i] == '"') { ++i; st.turInString = false; }
            emit(start, i - start, TurStyle::String);
            continue;
        }

        // Character literal: #\name or #\<char>.
        if (c == '#' && i + 1 < end && text[i + 1] == '\\') {
            const Sci_Position start = i;
            i += 2;
            if (i < end) {
                if (std::isalpha(static_cast<unsigned char>(text[i]))) {
                    while (i < end && std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
                } else {
                    ++i;
                }
            }
            emit(start, i - start, TurStyle::CharLit);
            continue;
        }

        // Booleans: #t or #f at a word boundary.
        if (c == '#' && i + 1 < end && (text[i + 1] == 't' || text[i + 1] == 'f')
            && (i + 2 >= end || !IsSymbolCont(static_cast<unsigned char>(text[i + 2])))) {
            emit(i, 2, TurStyle::Boolean);
            i += 2;
            continue;
        }

        // #lang directive at column 0.
        if (c == '#' && i == in.begin && i + 5 <= end
            && text[i + 1] == 'l' && text[i + 2] == 'a' && text[i + 3] == 'n'
            && text[i + 4] == 'g') {
            emit(i, 5, TurStyle::LangDir);
            i += 5;
            while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
            const Sci_Position nameStart = i;
            while (i < end && text[i] != ' ' && text[i] != '\t') ++i;
            emit(nameStart, i - nameStart, TurStyle::Type);
            continue;
        }

        // Reader conditional prefix #?
        if (c == '#' && i + 1 < end && text[i + 1] == '?') {
            emit(i, 2, TurStyle::LangDir);
            i += 2;
            continue;
        }

        // Metadata annotation: ^foo
        if (c == '^' && i + 1 < end && std::isalpha(static_cast<unsigned char>(text[i + 1]))) {
            const Sci_Position start = i;
            ++i;
            while (i < end && (std::isalnum(static_cast<unsigned char>(text[i]))
                               || text[i] == '_' || text[i] == '-')) {
                ++i;
            }
            emit(start, i - start, TurStyle::Metadata);
            continue;
        }

        // Keyword literal: :foo
        if (c == ':' && i + 1 < end
            && (std::isalpha(static_cast<unsigned char>(text[i + 1])) || text[i + 1] == '_')) {
            const Sci_Position start = i;
            ++i;
            while (i < end && IsSymbolCont(static_cast<unsigned char>(text[i]))) ++i;
            emit(start, i - start, TurStyle::KeywordLit);
            continue;
        }

        // Special ops :: and |>
        if (c == ':' && i + 1 < end && text[i + 1] == ':') {
            emit(i, 2, TurStyle::Operator); i += 2; continue;
        }
        if (c == '|' && i + 1 < end && text[i + 1] == '>') {
            emit(i, 2, TurStyle::Operator); i += 2; continue;
        }

        // Reader macros: ~@ ~ ` '
        if (c == '~' && i + 1 < end && text[i + 1] == '@') {
            emit(i, 2, TurStyle::Quote); i += 2; continue;
        }
        if (c == '~' || c == '`' || c == '\'') {
            emit(i, 1, TurStyle::Quote); ++i; continue;
        }

        // Sweet-expression reader markers. Checked before the identifier path
        // because IsSymbolStart accepts '$', which would otherwise swallow a
        // standalone GROUP marker as a one-character symbol.
        if (sweet && (IsSweetGroupAt(text, i, end) || IsSweetSplitAt(text, i, end))) {
            emit(i, 1, TurStyle::SweetMarker);
            ++i;
            continue;
        }

        // Delimiters. With rainbow coloring on, every bracket type is painted
        // by its nesting depth so matching pairs share a color; an unmatched
        // closer is flagged. Without it, brackets fall back to the flat
        // Delim / CurlyInfix styles.
        if (c == '(' || c == '[' || c == '{') {
            const TurStyle flat = (c == '{') ? TurStyle::CurlyInfix : TurStyle::Delim;
            emit(i, 1, in.rainbow ? RainbowStyleForDepth(st.turBracketDepth) : flat);
            if (st.turBracketDepth < kMaxTurBracketDepth) ++st.turBracketDepth;
            ++i;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            const TurStyle flat = (c == '}') ? TurStyle::CurlyInfix : TurStyle::Delim;
            TurStyle s = flat;
            if (in.rainbow) {
                if (st.turBracketDepth > 0) {
                    --st.turBracketDepth;
                    s = RainbowStyleForDepth(st.turBracketDepth);
                } else {
                    s = TurStyle::BracketError;
                }
            }
            emit(i, 1, s);
            ++i;
            continue;
        }

        // Numbers.
        if (std::isdigit(static_cast<unsigned char>(c))
            || ((c == '-' || c == '.') && i + 1 < end
                && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            const Sci_Position start = i;
            if (c == '-') ++i;
            if (i + 1 < end && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                i += 2;
                while (i < end && std::isxdigit(static_cast<unsigned char>(text[i]))) ++i;
            } else if (i + 1 < end && text[i] == '0'
                       && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
                i += 2;
                while (i < end && (text[i] == '0' || text[i] == '1')) ++i;
            } else {
                while (i < end && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                if (i < end && text[i] == '.') {
                    ++i;
                    while (i < end && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
                if (i < end && (text[i] == 'e' || text[i] == 'E')) {
                    ++i;
                    if (i < end && (text[i] == '+' || text[i] == '-')) ++i;
                    while (i < end && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
            }
            emit(start, i - start, TurStyle::Number);
            continue;
        }

        // Identifier / keyword.
        if (IsSymbolStart(static_cast<unsigned char>(c))) {
            const Sci_Position start = i;
            while (i < end && IsSymbolCont(static_cast<unsigned char>(text[i]))) ++i;
            const std::string_view sym(text + start, static_cast<size_t>(i - start));
            TurStyle s = SymbolStyle(sym);
            if (s == TurStyle::Identifier && i < end && text[i] == '(') {
                s = TurStyle::NeotericCall;
            }
            emit(start, i - start, s);
            continue;
        }

        // Fallback: unknown byte.
        emit(i, 1, TurStyle::Default);
        ++i;
    }
}

}

void ScanTurmericLine(const ScanInput& in, LexState& st, Emitter& out) {
    ScanTurmeric(in, st, out, /*sweet=*/false);
}

void ScanTurmericSweetLine(const ScanInput& in, LexState& st, Emitter& out) {
    ScanTurmeric(in, st, out, /*sweet=*/true);
}

}
