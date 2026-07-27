#include "editor/scanner.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace trowel {

namespace {

const std::unordered_set<std::string_view>& JustKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "alias", "export", "import", "mod", "set", "shell", "if", "else",
    };
    return s;
}

constexpr bool IsNameStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
constexpr bool IsNameCont(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
}

// `{{ ... }}` interpolation, `` `...` `` command substitution, quoted strings,
// and numbers — the pieces that look the same in a recipe body and in an
// assignment's right-hand side.
//
// Returns the position just past the construct, or `i` if nothing matched.
Sci_Position ScanValueToken(const char* text, Sci_Position i, Sci_Position end,
                            Emitter& out) {
    if (i + 1 < end && text[i] == '{' && text[i + 1] == '{') {
        Sci_Position j = i + 2;
        while (j + 1 < end && !(text[j] == '}' && text[j + 1] == '}')) ++j;
        j = (j + 1 < end) ? j + 2 : end;
        out.Emit(i, j - i, static_cast<int>(JustStyle::Interpolation));
        return j;
    }
    if (text[i] == '`') {
        Sci_Position j = i + 1;
        while (j < end && text[j] != '`') ++j;
        if (j < end) ++j;
        out.Emit(i, j - i, static_cast<int>(JustStyle::Backtick));
        return j;
    }
    if (text[i] == '"' || text[i] == '\'') {
        const char quote = text[i];
        Sci_Position j = i + 1;
        while (j < end && text[j] != quote) {
            // Single-quoted strings in just are raw; double-quoted honor \.
            if (quote == '"' && text[j] == '\\' && j + 1 < end) j += 2;
            else ++j;
        }
        if (j < end) ++j;
        out.Emit(i, j - i, static_cast<int>(JustStyle::String));
        return j;
    }
    if (std::isdigit(static_cast<unsigned char>(text[i]))) {
        Sci_Position j = i;
        while (j < end && std::isdigit(static_cast<unsigned char>(text[j]))) ++j;
        out.Emit(i, j - i, static_cast<int>(JustStyle::Number));
        return j;
    }
    return i;
}

// Scan an expression: everything after `:=`, or a recipe body line.
void ScanValue(const char* text, Sci_Position i, Sci_Position end, Emitter& out) {
    while (i < end) {
        const Sci_Position next = ScanValueToken(text, i, end, out);
        if (next > i) { i = next; continue; }
        if (IsNameStart(static_cast<unsigned char>(text[i]))) {
            Sci_Position j = i;
            while (j < end && IsNameCont(static_cast<unsigned char>(text[j]))) ++j;
            const std::string_view word(text + i, static_cast<size_t>(j - i));
            if (JustKeywords().count(word)) {
                out.Emit(i, j - i, static_cast<int>(JustStyle::Keyword));
            }
            i = j;
            continue;
        }
        ++i;
    }
}

// A recipe header looks like `name arg1 +arg2: dep1 dep2`. Requires a colon
// that is not part of `:=`.
Sci_Position FindRecipeColon(const char* text, Sci_Position i, Sci_Position end) {
    for (Sci_Position j = i; j < end; ++j) {
        if (text[j] == '#') return -1;
        if (text[j] == '"' || text[j] == '\'' || text[j] == '`') return -1;
        if (text[j] != ':') continue;
        if (j + 1 < end && text[j + 1] == '=') return -1;
        return j;
    }
    return -1;
}

}

void ScanJustLine(const ScanInput& in, LexState& st, Emitter& out) {
    const char* text = in.text;
    const Sci_Position end = in.end;
    const Sci_Position begin = in.begin;

    out.SetGapStyle(static_cast<int>(JustStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, JustStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    if (begin >= end) return;  // blank lines don't end a recipe

    const bool indented = (text[begin] == ' ' || text[begin] == '\t');

    // A non-indented, non-blank line always ends the previous recipe body.
    if (!indented) st.justInRecipe = false;

    // Recipe body: painted as shell text, with interpolations picked out.
    if (st.justInRecipe && indented) {
        out.SetGapStyle(static_cast<int>(JustStyle::Body));
        Sci_Position i = begin;
        while (i < end) {
            const Sci_Position next = ScanValueToken(text, i, end, out);
            if (next > i) { i = next; continue; }
            ++i;
        }
        out.FillTo(end);
        return;
    }

    Sci_Position i = begin;
    while (i < end && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= end) return;

    // Comment.
    if (text[i] == '#') {
        emit(i, end - i, JustStyle::Comment);
        return;
    }

    // Attribute: [private], [group('x')], [confirm]
    if (text[i] == '[') {
        emit(i, end - i, JustStyle::Attribute);
        return;
    }

    // Leading keyword (set / export / alias / import / mod).
    Sci_Position wordEnd = i;
    while (wordEnd < end && IsNameCont(static_cast<unsigned char>(text[wordEnd]))) ++wordEnd;
    const std::string_view first(text + i, static_cast<size_t>(wordEnd - i));
    const bool leadingKeyword = JustKeywords().count(first) > 0;

    // Assignment: `name := value` (optionally prefixed by export/set).
    for (Sci_Position j = i; j + 1 < end; ++j) {
        if (text[j] == '#' || text[j] == '"' || text[j] == '\'') break;
        if (text[j] != ':' || text[j + 1] != '=') continue;
        if (leadingKeyword) {
            emit(i, wordEnd - i, JustStyle::Keyword);
            Sci_Position n = wordEnd;
            while (n < end && (text[n] == ' ' || text[n] == '\t')) ++n;
            Sci_Position nEnd = n;
            while (nEnd < j && IsNameCont(static_cast<unsigned char>(text[nEnd]))) ++nEnd;
            emit(n, nEnd - n, JustStyle::Assign);
        } else {
            emit(i, wordEnd - i, JustStyle::Assign);
        }
        emit(j, 2, JustStyle::Operator);
        ScanValue(text, j + 2, end, out);
        return;
    }

    if (leadingKeyword) {
        emit(i, wordEnd - i, JustStyle::Keyword);
        ScanValue(text, wordEnd, end, out);
        return;
    }

    // Recipe header: `name params: deps`.
    if (const Sci_Position colon = FindRecipeColon(text, i, end);
        colon >= 0 && IsNameStart(static_cast<unsigned char>(text[i]))) {
        emit(i, wordEnd - i, JustStyle::RecipeName);

        // Parameters between the name and the colon.
        Sci_Position p = wordEnd;
        while (p < colon) {
            while (p < colon && (text[p] == ' ' || text[p] == '\t')) ++p;
            const Sci_Position pStart = p;
            while (p < colon && text[p] != ' ' && text[p] != '\t') ++p;
            if (p > pStart) emit(pStart, p - pStart, JustStyle::Parameter);
        }

        emit(colon, 1, JustStyle::Operator);

        // Dependencies after the colon.
        Sci_Position d = colon + 1;
        while (d < end) {
            while (d < end && (text[d] == ' ' || text[d] == '\t')) ++d;
            if (d < end && text[d] == '(') {  // parenthesized dep arguments
                Sci_Position close = d;
                while (close < end && text[close] != ')') ++close;
                if (close < end) ++close;
                ScanValue(text, d, close, out);
                d = close;
                continue;
            }
            const Sci_Position dStart = d;
            while (d < end && text[d] != ' ' && text[d] != '\t' && text[d] != '(') ++d;
            if (d > dStart) emit(dStart, d - dStart, JustStyle::Dependency);
            if (d == dStart) break;
        }

        st.justInRecipe = true;
        return;
    }

    ScanValue(text, i, end, out);
}

}
