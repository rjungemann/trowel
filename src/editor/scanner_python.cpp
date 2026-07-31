#include "editor/scanner.h"

#include <cctype>
#include <string_view>
#include <unordered_set>

namespace trowel {

namespace {

constexpr int kNoTriple = 0;
constexpr int kTripleSingle = 1;  // '''
constexpr int kTripleDouble = 2;  // """

const std::unordered_set<std::string_view>& PyKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "finally", "for", "from",
        "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
        "or", "pass", "raise", "return", "try", "while", "with", "yield",
        // Not reserved words, but they read as part of the language.
        "True", "False", "None", "self", "cls", "match", "case",
    };
    return s;
}

const std::unordered_set<std::string_view>& PyBuiltins() {
    static const std::unordered_set<std::string_view> s = {
        "abs", "aiter", "anext", "all", "any", "ascii", "bin", "bool",
        "breakpoint", "bytearray", "bytes", "callable", "chr", "classmethod",
        "compile", "complex", "delattr", "dict", "dir", "divmod", "enumerate",
        "eval", "exec", "filter", "float", "format", "frozenset", "getattr",
        "globals", "hasattr", "hash", "help", "hex", "id", "input", "int",
        "isinstance", "issubclass", "iter", "len", "list", "locals", "map",
        "max", "memoryview", "min", "next", "object", "oct", "open", "ord",
        "pow", "print", "property", "range", "repr", "reversed", "round",
        "set", "setattr", "slice", "sorted", "staticmethod", "str", "sum",
        "super", "tuple", "type", "vars", "zip",
        "Exception", "ValueError", "TypeError", "KeyError", "IndexError",
        "RuntimeError", "NotImplementedError", "StopIteration", "OSError",
        "AttributeError", "ImportError", "ZeroDivisionError",
    };
    return s;
}

constexpr bool IsNameStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
constexpr bool IsNameCont(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

// A string prefix (r, b, f, u, rb, br, fr, rf, ...) immediately followed by a
// quote. `raw` suppresses escape painting the way Python suppresses escape
// processing.
bool StringPrefixAt(const char* text, Sci_Position i, Sci_Position end,
                    Sci_Position& quotePos, bool& raw) {
    Sci_Position j = i;
    raw = false;
    int n = 0;
    while (j < end && n < 3) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[j])));
        if (c == 'r') raw = true;
        else if (c != 'b' && c != 'f' && c != 'u') break;
        ++j;
        ++n;
    }
    if (j < end && (text[j] == '"' || text[j] == '\'')) {
        quotePos = j;
        return true;
    }
    return false;
}

// Consume as much of an open triple-quoted string as this line holds.
Sci_Position ContinueTriple(const char* text, Sci_Position i, Sci_Position end,
                            LexState& st, Emitter& out) {
    const char q = (st.pyStringMode == kTripleSingle) ? '\'' : '"';
    const Sci_Position start = i;
    while (i < end) {
        if (text[i] == '\\' && i + 1 < end) { i += 2; continue; }
        if (text[i] == q && i + 2 < end && text[i + 1] == q && text[i + 2] == q) {
            i += 3;
            st.pyStringMode = kNoTriple;
            break;
        }
        ++i;
    }
    out.Emit(start, i - start, static_cast<int>(PyStyle::TripleString));
    return i;
}

// A string starting at `quotePos` (with `i` at the start of any prefix).
Sci_Position ScanString(const char* text, Sci_Position i, Sci_Position quotePos,
                        Sci_Position end, bool raw, LexState& st, Emitter& out) {
    const char q = text[quotePos];

    if (quotePos + 2 < end && text[quotePos + 1] == q && text[quotePos + 2] == q) {
        out.Emit(i, quotePos + 3 - i, static_cast<int>(PyStyle::TripleString));
        st.pyStringMode = (q == '\'') ? kTripleSingle : kTripleDouble;
        return ContinueTriple(text, quotePos + 3, end, st, out);
    }

    Sci_Position seg = i;
    Sci_Position j = quotePos + 1;
    while (j < end && text[j] != q) {
        if (text[j] == '\\' && j + 1 < end) {
            if (raw) { j += 2; continue; }
            out.Emit(seg, j - seg, static_cast<int>(PyStyle::String));
            out.Emit(j, 2, static_cast<int>(PyStyle::StringEscape));
            j += 2;
            seg = j;
            continue;
        }
        ++j;
    }
    if (j < end && text[j] == q) ++j;
    out.Emit(seg, j - seg, static_cast<int>(PyStyle::String));
    return j;
}

}

void ScanPythonLine(const ScanInput& in, LexState& st, Emitter& out) {
    const char* text = in.text;
    const Sci_Position end = in.end;
    Sci_Position i = in.begin;

    out.SetGapStyle(static_cast<int>(PyStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, PyStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    // --- Continuation of a triple-quoted string ----------------------------
    if (st.pyStringMode != kNoTriple) {
        i = ContinueTriple(text, i, end, st, out);
        if (st.pyStringMode != kNoTriple) return;
    }

    // --- Shebang -----------------------------------------------------------
    if (in.line == 0 && i == in.begin && i + 1 < end
        && text[i] == '#' && text[i + 1] == '!') {
        emit(i, end - i, PyStyle::Comment);
        return;
    }

    // `def name` / `class name`: the name after the keyword gets its own
    // colour, which is what makes definitions findable when skimming.
    PyStyle pendingName = PyStyle::Default;

    while (i < end) {
        const char c = text[i];

        if (c == ' ' || c == '\t') { ++i; continue; }

        if (c == '#') {
            emit(i, end - i, PyStyle::Comment);
            return;
        }

        // Decorator: `@name` / `@pkg.name`, only at the head of a line.
        if (c == '@' && i == in.begin) {
            Sci_Position j = i + 1;
            while (j < end && (IsNameCont(static_cast<unsigned char>(text[j]))
                               || text[j] == '.')) {
                ++j;
            }
            emit(i, j - i, PyStyle::Decorator);
            i = j;
            continue;
        }

        if (c == '"' || c == '\'') {
            i = ScanString(text, i, i, end, false, st, out);
            if (st.pyStringMode != kNoTriple) return;
            continue;
        }

        if (IsNameStart(static_cast<unsigned char>(c))) {
            Sci_Position quotePos = 0;
            bool raw = false;
            if (StringPrefixAt(text, i, end, quotePos, raw)) {
                i = ScanString(text, i, quotePos, end, raw, st, out);
                if (st.pyStringMode != kNoTriple) return;
                continue;
            }

            Sci_Position j = i;
            while (j < end && IsNameCont(static_cast<unsigned char>(text[j]))) ++j;
            const std::string_view word(text + i, static_cast<size_t>(j - i));

            if (pendingName != PyStyle::Default) {
                emit(i, j - i, pendingName);
                pendingName = PyStyle::Default;
            } else if (PyKeywords().count(word)) {
                emit(i, j - i, PyStyle::Keyword);
                if (word == "def") pendingName = PyStyle::FuncName;
                else if (word == "class") pendingName = PyStyle::ClassName;
            } else if (PyBuiltins().count(word)) {
                emit(i, j - i, PyStyle::Builtin);
            } else {
                emit(i, j - i, PyStyle::Identifier);
            }
            i = j;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))
            || (c == '.' && i + 1 < end
                && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            Sci_Position j = i;
            while (j < end && (std::isalnum(static_cast<unsigned char>(text[j]))
                               || text[j] == '_' || text[j] == '.')) {
                // An exponent's sign is part of the literal.
                if ((text[j] == 'e' || text[j] == 'E') && j + 1 < end
                    && (text[j + 1] == '+' || text[j + 1] == '-')) {
                    j += 2;
                    continue;
                }
                ++j;
            }
            emit(i, j - i, PyStyle::Number);
            i = j;
            continue;
        }

        emit(i, 1, PyStyle::Operator);
        ++i;
    }
}

}
