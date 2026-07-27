#include "editor/scanner.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace trowel {

namespace {

const std::unordered_set<std::string_view>& CKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "alignas", "alignof", "auto", "break", "case", "const", "constexpr",
        "continue", "default", "do", "else", "enum", "extern", "for", "goto",
        "if", "inline", "register", "restrict", "return", "sizeof", "static",
        "static_assert", "struct", "switch", "thread_local", "typedef",
        "typeof", "union", "volatile", "while",
        "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
        "_Static_assert", "_Thread_local",
        // C++ spellings that show up in Trowel's own sources.
        "class", "namespace", "template", "typename", "using", "public",
        "private", "protected", "virtual", "override", "final", "new",
        "delete", "throw", "try", "catch", "operator", "explicit", "friend",
        "mutable", "noexcept", "nullptr", "this", "true", "false",
    };
    return s;
}

const std::unordered_set<std::string_view>& CTypes() {
    static const std::unordered_set<std::string_view> s = {
        "bool", "char", "double", "float", "int", "long", "short", "signed",
        "unsigned", "void", "_Bool", "_Complex", "_Imaginary",
        "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "wchar_t", "char16_t", "char32_t", "FILE", "va_list",
    };
    return s;
}

constexpr bool IsIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
constexpr bool IsIdentCont(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

// A quoted run: string or character literal, honoring backslash escapes and
// painting the escapes in their own style.
Sci_Position ScanQuoted(const char* text, Sci_Position i, Sci_Position end,
                        char quote, Emitter& out, CStyle body) {
    Sci_Position seg = i;  // start of the current unescaped segment
    ++i;
    while (i < end && text[i] != quote) {
        if (text[i] == '\\' && i + 1 < end) {
            out.Emit(seg, i - seg, static_cast<int>(body));
            out.Emit(i, 2, static_cast<int>(CStyle::StringEscape));
            i += 2;
            seg = i;
            continue;
        }
        ++i;
    }
    if (i < end && text[i] == quote) ++i;
    out.Emit(seg, i - seg, static_cast<int>(body));
    return i;
}

}

void ScanCLine(const ScanInput& in, LexState& st, Emitter& out) {
    const char* text = in.text;
    const Sci_Position end = in.end;
    Sci_Position i = in.begin;

    out.SetGapStyle(static_cast<int>(CStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, CStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    // Continuation of a /* */ comment from a previous line.
    if (st.cInComment) {
        const Sci_Position start = i;
        while (i < end) {
            if (i + 1 < end && text[i] == '*' && text[i + 1] == '/') {
                i += 2;
                st.cInComment = false;
                break;
            }
            ++i;
        }
        emit(start, i - start, CStyle::Comment);
    }

    // Preprocessor directive: '#' as the first non-blank of our content. Only
    // the '#' and the directive word are painted; the rest of the line falls
    // through to the main loop so paths, macros, and comments still scan.
    if (i == in.begin) {
        Sci_Position j = i;
        while (j < end && (text[j] == ' ' || text[j] == '\t')) ++j;
        if (j < end && text[j] == '#') {
            Sci_Position k = j + 1;
            while (k < end && (text[k] == ' ' || text[k] == '\t')) ++k;
            while (k < end && IsIdentCont(static_cast<unsigned char>(text[k]))) ++k;
            emit(j, k - j, CStyle::Preproc);
            i = k;
        }
    }

    while (i < end) {
        const char c = text[i];

        if (c == ' ' || c == '\t') { ++i; continue; }

        // Comments.
        if (c == '/' && i + 1 < end && text[i + 1] == '/') {
            emit(i, end - i, CStyle::Comment);
            i = end;
            continue;
        }
        if (c == '/' && i + 1 < end && text[i + 1] == '*') {
            const Sci_Position start = i;
            const bool isDoc = (i + 2 < end && text[i + 2] == '*');
            i += 2;
            st.cInComment = true;
            while (i < end) {
                if (i + 1 < end && text[i] == '*' && text[i + 1] == '/') {
                    i += 2;
                    st.cInComment = false;
                    break;
                }
                ++i;
            }
            emit(start, i - start, isDoc ? CStyle::DocComment : CStyle::Comment);
            continue;
        }

        // String / char literals.
        if (c == '"') {
            i = ScanQuoted(text, i, end, '"', out, CStyle::String);
            continue;
        }
        if (c == '\'') {
            i = ScanQuoted(text, i, end, '\'', out, CStyle::Char);
            continue;
        }

        // Numbers (including hex, binary, and the usual suffixes).
        if (std::isdigit(static_cast<unsigned char>(c))
            || (c == '.' && i + 1 < end
                && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            const Sci_Position start = i;
            if (i + 1 < end && text[i] == '0'
                && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                i += 2;
                while (i < end && (std::isxdigit(static_cast<unsigned char>(text[i]))
                                   || text[i] == '\'')) {
                    ++i;
                }
            } else if (i + 1 < end && text[i] == '0'
                       && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
                i += 2;
                while (i < end && (text[i] == '0' || text[i] == '1' || text[i] == '\'')) ++i;
            } else {
                while (i < end && (std::isdigit(static_cast<unsigned char>(text[i]))
                                   || text[i] == '.' || text[i] == '\'')) {
                    ++i;
                }
                if (i < end && (text[i] == 'e' || text[i] == 'E')) {
                    ++i;
                    if (i < end && (text[i] == '+' || text[i] == '-')) ++i;
                    while (i < end && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
                }
            }
            while (i < end && IsIdentCont(static_cast<unsigned char>(text[i]))) ++i;  // suffix
            emit(start, i - start, CStyle::Number);
            continue;
        }

        // Identifiers and keywords.
        if (IsIdentStart(static_cast<unsigned char>(c))) {
            const Sci_Position start = i;
            while (i < end && IsIdentCont(static_cast<unsigned char>(text[i]))) ++i;
            const std::string_view word(text + start, static_cast<size_t>(i - start));
            CStyle s = CStyle::Identifier;
            if (CKeywords().count(word)) s = CStyle::Keyword;
            else if (CTypes().count(word)) s = CStyle::Type;
            emit(start, i - start, s);
            continue;
        }

        // Operators and punctuation.
        if (std::ispunct(static_cast<unsigned char>(c))) {
            emit(i, 1, CStyle::Operator);
            ++i;
            continue;
        }

        emit(i, 1, CStyle::Default);
        ++i;
    }
}

}
