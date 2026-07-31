#include "editor/scanner.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace trowel {

namespace {

constexpr int kNoString = 0;
constexpr int kSingleQuote = 1;
constexpr int kDoubleQuote = 2;

const std::unordered_set<std::string_view>& ShKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "if", "then", "elif", "else", "fi",
        "for", "while", "until", "do", "done",
        "case", "esac", "in", "select", "function",
        "time", "coproc", "break", "continue", "return",
    };
    return s;
}

// Builtins and the handful of external commands that are effectively part of
// the language's vocabulary in scripts.
const std::unordered_set<std::string_view>& ShBuiltins() {
    static const std::unordered_set<std::string_view> s = {
        "alias", "bg", "bind", "builtin", "cd", "command", "declare", "dirs",
        "echo", "enable", "eval", "exec", "exit", "export", "false", "fg",
        "getopts", "hash", "help", "history", "jobs", "kill", "let", "local",
        "logout", "mapfile", "popd", "printf", "pushd", "pwd", "read",
        "readarray", "readonly", "set", "shift", "shopt", "source", "suspend",
        "test", "times", "trap", "true", "type", "typeset", "ulimit", "umask",
        "unalias", "unset", "wait",
    };
    return s;
}

constexpr bool IsNameStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
constexpr bool IsNameCont(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

// `$name`, `${...}`, `$(...)`, `$((...))`, and the special parameters
// (`$?`, `$@`, `$#`, `$$`, `$!`, `$*`, `$-`, `$0`..`$9`).
Sci_Position ScanVariable(const char* text, Sci_Position i, Sci_Position end,
                          Emitter& out) {
    Sci_Position j = i + 1;
    if (j >= end) return i;

    if (text[j] == '{' || text[j] == '(') {
        const char open = text[j];
        const char close = (open == '{') ? '}' : ')';
        int depth = 0;
        while (j < end) {
            if (text[j] == open) ++depth;
            else if (text[j] == close) {
                --depth;
                if (depth == 0) { ++j; break; }
            }
            ++j;
        }
        out.Emit(i, j - i, static_cast<int>(ShStyle::Variable));
        return j;
    }

    if (IsNameStart(static_cast<unsigned char>(text[j]))) {
        while (j < end && IsNameCont(static_cast<unsigned char>(text[j]))) ++j;
        out.Emit(i, j - i, static_cast<int>(ShStyle::Variable));
        return j;
    }

    const char c = text[j];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '?' || c == '@'
        || c == '#' || c == '$' || c == '!' || c == '*' || c == '-') {
        out.Emit(i, 2, static_cast<int>(ShStyle::Variable));
        return j + 1;
    }
    return i;
}

// The body of a double-quoted string: escapes and `$` expansions are picked
// out, everything else is string.
Sci_Position ScanDoubleQuoteBody(const char* text, Sci_Position i, Sci_Position end,
                                 LexState& st, Emitter& out) {
    Sci_Position seg = i;
    while (i < end) {
        if (text[i] == '\\' && i + 1 < end) {
            out.Emit(seg, i - seg, static_cast<int>(ShStyle::String));
            out.Emit(i, 2, static_cast<int>(ShStyle::StringEscape));
            i += 2;
            seg = i;
            continue;
        }
        if (text[i] == '$') {
            out.Emit(seg, i - seg, static_cast<int>(ShStyle::String));
            const Sci_Position next = ScanVariable(text, i, end, out);
            if (next > i) { i = next; seg = i; continue; }
            ++i;
            continue;
        }
        if (text[i] == '"') {
            ++i;
            st.shStringMode = kNoString;
            break;
        }
        ++i;
    }
    out.Emit(seg, i - seg, static_cast<int>(ShStyle::String));
    return i;
}

// Single-quoted strings are fully literal — not even a backslash escapes.
Sci_Position ScanSingleQuoteBody(const char* text, Sci_Position i, Sci_Position end,
                                 LexState& st, Emitter& out) {
    const Sci_Position start = i;
    while (i < end && text[i] != '\'') ++i;
    if (i < end) { ++i; st.shStringMode = kNoString; }
    out.Emit(start, i - start, static_cast<int>(ShStyle::String));
    return i;
}

// True when the previous non-blank character means the next word starts a
// command — which is what decides whether a bare word is a keyword.
bool AtCommandPosition(const char* text, Sci_Position begin, Sci_Position i) {
    for (Sci_Position j = i - 1; j >= begin; --j) {
        const char c = text[j];
        if (c == ' ' || c == '\t') continue;
        return c == ';' || c == '|' || c == '&' || c == '(' || c == '{'
            || c == ')' || c == '\n';
    }
    return true;
}

}

void ScanShLine(const ScanInput& in, LexState& st, Emitter& out) {
    const char* text = in.text;
    const Sci_Position end = in.end;
    Sci_Position i = in.begin;

    out.SetGapStyle(static_cast<int>(ShStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, ShStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    // --- Continuation of a string opened on an earlier line ----------------
    if (st.shStringMode == kSingleQuote) {
        i = ScanSingleQuoteBody(text, i, end, st, out);
    } else if (st.shStringMode == kDoubleQuote) {
        i = ScanDoubleQuoteBody(text, i, end, st, out);
    }

    // --- Shebang -----------------------------------------------------------
    // Line 1 of a script is a comment as far as the shell is concerned, and
    // reads as one.
    if (in.line == 0 && i == in.begin && i + 1 < end
        && text[i] == '#' && text[i + 1] == '!') {
        emit(i, end - i, ShStyle::Comment);
        return;
    }

    while (i < end) {
        const char c = text[i];

        if (c == ' ' || c == '\t') { ++i; continue; }

        // `#` only opens a comment at the start of a word; `foo#bar` and
        // `${#x}` are not comments.
        if (c == '#' && (i == in.begin || text[i - 1] == ' ' || text[i - 1] == '\t')) {
            emit(i, end - i, ShStyle::Comment);
            return;
        }

        if (c == '\'') {
            st.shStringMode = kSingleQuote;
            emit(i, 1, ShStyle::String);
            i = ScanSingleQuoteBody(text, i + 1, end, st, out);
            continue;
        }
        if (c == '"') {
            st.shStringMode = kDoubleQuote;
            emit(i, 1, ShStyle::String);
            i = ScanDoubleQuoteBody(text, i + 1, end, st, out);
            continue;
        }
        if (c == '`') {
            Sci_Position j = i + 1;
            while (j < end && text[j] != '`') {
                if (text[j] == '\\' && j + 1 < end) j += 2;
                else ++j;
            }
            if (j < end) ++j;
            emit(i, j - i, ShStyle::Backtick);
            i = j;
            continue;
        }

        if (c == '$') {
            const Sci_Position next = ScanVariable(text, i, end, out);
            if (next > i) { i = next; continue; }
            emit(i, 1, ShStyle::Operator);
            ++i;
            continue;
        }

        if (c == '\\' && i + 1 < end) {
            emit(i, 2, ShStyle::StringEscape);
            i += 2;
            continue;
        }

        if (IsNameStart(static_cast<unsigned char>(c))) {
            Sci_Position j = i;
            while (j < end && (IsNameCont(static_cast<unsigned char>(text[j]))
                               || text[j] == '-' || text[j] == '.')) {
                ++j;
            }
            const std::string_view word(text + i, static_cast<size_t>(j - i));

            // A function definition: `name()` or `name ()`.
            Sci_Position paren = j;
            while (paren < end && (text[paren] == ' ' || text[paren] == '\t')) ++paren;
            if (paren + 1 < end && text[paren] == '(' && text[paren + 1] == ')') {
                emit(i, j - i, ShStyle::Function);
                i = j;
                continue;
            }

            // `name=value` at the head of a command is an assignment; colour
            // the name as a variable so it reads like the `$name` uses of it.
            if (j < end && text[j] == '=' && AtCommandPosition(text, in.begin, i)) {
                emit(i, j - i, ShStyle::Variable);
                i = j;
                continue;
            }

            ShStyle style = ShStyle::Identifier;
            if (ShKeywords().count(word) && AtCommandPosition(text, in.begin, i)) {
                style = ShStyle::Keyword;
            } else if (ShBuiltins().count(word) && AtCommandPosition(text, in.begin, i)) {
                style = ShStyle::Builtin;
            }
            emit(i, j - i, style);
            i = j;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            Sci_Position j = i;
            while (j < end && (std::isalnum(static_cast<unsigned char>(text[j]))
                               || text[j] == '.')) {
                ++j;
            }
            emit(i, j - i, ShStyle::Number);
            i = j;
            continue;
        }

        emit(i, 1, ShStyle::Operator);
        ++i;
    }
}

}
