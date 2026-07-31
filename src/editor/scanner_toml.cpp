#include "editor/scanner.h"

#include <cctype>

namespace trowel {

namespace {

constexpr int kNoMultiline = 0;
constexpr int kMultilineLiteral = 1;  // '''
constexpr int kMultilineBasic = 2;    // """

constexpr bool IsBareKeyChar(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
}

char MultilineQuote(int mode) {
    return mode == kMultilineLiteral ? '\'' : '"';
}

// Consume as much of an open multi-line string as this line contains. Sets
// `st.tomlStringMode` back to none when the closing delimiter is found.
Sci_Position ContinueMultiline(const char* text, Sci_Position i, Sci_Position end,
                               LexState& st, Emitter& out) {
    const char q = MultilineQuote(st.tomlStringMode);
    const bool basic = (st.tomlStringMode == kMultilineBasic);
    Sci_Position seg = i;
    while (i < end) {
        if (basic && text[i] == '\\' && i + 1 < end) {
            out.Emit(seg, i - seg, static_cast<int>(TomlStyle::String));
            out.Emit(i, 2, static_cast<int>(TomlStyle::StringEscape));
            i += 2;
            seg = i;
            continue;
        }
        if (text[i] == q && i + 2 < end && text[i + 1] == q && text[i + 2] == q) {
            i += 3;
            st.tomlStringMode = kNoMultiline;
            break;
        }
        ++i;
    }
    out.Emit(seg, i - seg, static_cast<int>(TomlStyle::String));
    return i;
}

// A single-line string, or the opening of a multi-line one.
Sci_Position ScanString(const char* text, Sci_Position i, Sci_Position end,
                        LexState& st, Emitter& out) {
    const char q = text[i];
    const bool basic = (q == '"');

    if (i + 2 < end && text[i + 1] == q && text[i + 2] == q) {
        out.Emit(i, 3, static_cast<int>(TomlStyle::String));
        st.tomlStringMode = basic ? kMultilineBasic : kMultilineLiteral;
        return ContinueMultiline(text, i + 3, end, st, out);
    }
    if (i + 2 == end && text[i + 1] == q) {
        // `""` / `''` — an empty string, not an opener.
        out.Emit(i, 2, static_cast<int>(TomlStyle::String));
        return end;
    }

    Sci_Position seg = i;
    Sci_Position j = i + 1;
    while (j < end && text[j] != q) {
        // Literal (single-quoted) strings have no escapes at all in TOML.
        if (basic && text[j] == '\\' && j + 1 < end) {
            out.Emit(seg, j - seg, static_cast<int>(TomlStyle::String));
            out.Emit(j, 2, static_cast<int>(TomlStyle::StringEscape));
            j += 2;
            seg = j;
            continue;
        }
        ++j;
    }
    if (j < end && text[j] == q) ++j;
    out.Emit(seg, j - seg, static_cast<int>(TomlStyle::String));
    return j;
}

bool MatchWord(const char* text, Sci_Position i, Sci_Position end, const char* word) {
    Sci_Position k = 0;
    for (; word[k] != '\0'; ++k) {
        if (i + k >= end || text[i + k] != word[k]) return false;
    }
    return i + k >= end || !IsBareKeyChar(static_cast<unsigned char>(text[i + k]));
}

// An offset date-time / local date / local time: digits mixed with `-`, `:`,
// `T`, `Z`, `+`, and `.`. Distinguished from a plain number by containing at
// least one `-` or `:` after the leading digits.
Sci_Position ScanNumberOrDate(const char* text, Sci_Position i, Sci_Position end,
                              Emitter& out) {
    Sci_Position j = i;
    bool dateish = false;
    if (text[j] == '+' || text[j] == '-') ++j;
    while (j < end) {
        const char c = text[j];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            ++j;
            continue;
        }
        if ((c == '-' || c == ':') && j > i) {
            dateish = true;
            ++j;
            continue;
        }
        if ((c == '+') && j > i && dateish) { ++j; continue; }
        break;
    }
    out.Emit(i, j - i,
             static_cast<int>(dateish ? TomlStyle::DateTime : TomlStyle::Number));
    return j;
}

// The key half of a `key = value` line: a dotted path of bare or quoted keys,
// ending at the `=`. Returns the position of the `=`, or -1 when this line is
// not an assignment.
Sci_Position FindAssign(const char* text, Sci_Position i, Sci_Position end) {
    bool inQuote = false;
    char quote = 0;
    for (Sci_Position j = i; j < end; ++j) {
        const char c = text[j];
        if (inQuote) {
            if (c == '\\' && quote == '"') { ++j; continue; }
            if (c == quote) inQuote = false;
            continue;
        }
        if (c == '"' || c == '\'') { inQuote = true; quote = c; continue; }
        if (c == '#') return -1;
        if (c == '=') return j;
    }
    return -1;
}

}

void ScanTomlLine(const ScanInput& in, LexState& st, Emitter& out) {
    const char* text = in.text;
    const Sci_Position end = in.end;
    Sci_Position i = in.begin;

    out.SetGapStyle(static_cast<int>(TomlStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, TomlStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    // --- Continuation of a multi-line string -------------------------------
    if (st.tomlStringMode != kNoMultiline) {
        i = ContinueMultiline(text, i, end, st, out);
        if (st.tomlStringMode != kNoMultiline) return;
    }

    while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= end) return;

    // --- Comment -----------------------------------------------------------
    if (text[i] == '#') {
        emit(i, end - i, TomlStyle::Comment);
        return;
    }

    // --- Table header ------------------------------------------------------
    // `[table]`, `[a.b.c]`, `[[array-of-tables]]`. Trailing comments still get
    // their own colour.
    if (text[i] == '[') {
        Sci_Position j = i;
        while (j < end && text[j] != ']') ++j;
        while (j < end && text[j] == ']') ++j;
        emit(i, j - i, TomlStyle::Table);
        i = j;
        while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i < end && text[i] == '#') emit(i, end - i, TomlStyle::Comment);
        return;
    }

    // --- key = value -------------------------------------------------------
    const Sci_Position assign = FindAssign(text, i, end);
    if (assign >= 0) {
        emit(i, assign - i, TomlStyle::Key);
        emit(assign, 1, TomlStyle::Operator);
        i = assign + 1;
    }

    while (i < end) {
        const char c = text[i];
        if (c == ' ' || c == '\t') { ++i; continue; }

        if (c == '#') {
            emit(i, end - i, TomlStyle::Comment);
            return;
        }
        if (c == '"' || c == '\'') {
            i = ScanString(text, i, end, st, out);
            if (st.tomlStringMode != kNoMultiline) return;
            continue;
        }
        if (c == '[' || c == ']' || c == '{' || c == '}' || c == ',' || c == '=') {
            emit(i, 1, TomlStyle::Operator);
            ++i;
            continue;
        }
        if (MatchWord(text, i, end, "true"))  { emit(i, 4, TomlStyle::Boolean); i += 4; continue; }
        if (MatchWord(text, i, end, "false")) { emit(i, 5, TomlStyle::Boolean); i += 5; continue; }
        if (MatchWord(text, i, end, "inf"))   { emit(i, 3, TomlStyle::Number);  i += 3; continue; }
        if (MatchWord(text, i, end, "nan"))   { emit(i, 3, TomlStyle::Number);  i += 3; continue; }

        if (std::isdigit(static_cast<unsigned char>(c))
            || ((c == '+' || c == '-') && i + 1 < end
                && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            i = ScanNumberOrDate(text, i, end, out);
            continue;
        }

        // Bare words are not valid on the value side of a TOML assignment —
        // flag them the way the JSON scanner flags stray text.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            Sci_Position j = i;
            while (j < end && IsBareKeyChar(static_cast<unsigned char>(text[j]))) ++j;
            emit(i, j - i, TomlStyle::Error);
            i = j;
            continue;
        }

        emit(i, 1, TomlStyle::Error);
        ++i;
    }
}

}
