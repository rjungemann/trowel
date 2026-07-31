#include "editor/scanner.h"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>

namespace trowel {

namespace {

// Flow-control commands. CMake has no reserved words — these are ordinary
// commands — but they read as keywords and colouring them that way is what
// makes the block structure of a CMakeLists visible at a glance.
const std::unordered_set<std::string_view>& CMakeControl() {
    static const std::unordered_set<std::string_view> s = {
        "if", "elseif", "else", "endif",
        "foreach", "endforeach", "while", "endwhile",
        "function", "endfunction", "macro", "endmacro",
        "break", "continue", "return", "block", "endblock",
    };
    return s;
}

// Argument keywords that are structural rather than data: the operators of
// `if()` and the section markers of the common commands.
const std::unordered_set<std::string_view>& CMakeArgKeywords() {
    static const std::unordered_set<std::string_view> s = {
        "AND", "OR", "NOT", "MATCHES", "STREQUAL", "STRLESS", "STRGREATER",
        "EQUAL", "LESS", "GREATER", "LESS_EQUAL", "GREATER_EQUAL",
        "VERSION_EQUAL", "VERSION_LESS", "VERSION_GREATER",
        "VERSION_LESS_EQUAL", "VERSION_GREATER_EQUAL",
        "DEFINED", "EXISTS", "COMMAND", "TARGET", "TEST", "POLICY", "IN_LIST",
        "PUBLIC", "PRIVATE", "INTERFACE",
        "REQUIRED", "QUIET", "COMPONENTS", "OPTIONAL_COMPONENTS",
        "NAMES", "PATHS", "HINTS", "DESTINATION", "FILES", "DIRECTORY",
        "TARGETS", "PROPERTIES", "CACHE", "FORCE", "PARENT_SCOPE",
        "STATIC", "SHARED", "MODULE", "OBJECT", "ALIAS", "IMPORTED",
        "ON", "OFF", "TRUE", "FALSE", "YES", "NO",
    };
    return s;
}

constexpr bool IsNameStart(unsigned char c) {
    return std::isalpha(c) || c == '_';
}
constexpr bool IsNameCont(unsigned char c) {
    return std::isalnum(c) || c == '_';
}

// `${...}`, `$ENV{...}`, `$CACHE{...}`, and generator expressions `$<...>`.
// Nesting is common (`${${prefix}_NAME}`), so the closer is matched by depth.
// Returns the position just past the reference, or `i` if `$` starts nothing.
Sci_Position ScanReference(const char* text, Sci_Position i, Sci_Position end,
                           Emitter& out) {
    Sci_Position j = i + 1;
    while (j < end && IsNameCont(static_cast<unsigned char>(text[j]))) ++j;

    char open = 0;
    char close = 0;
    if (j < end && text[j] == '{') { open = '{'; close = '}'; }
    else if (j < end && text[j] == '<') { open = '<'; close = '>'; }
    else return i;

    int depth = 0;
    while (j < end) {
        if (text[j] == open) ++depth;
        else if (text[j] == close) {
            --depth;
            if (depth == 0) { ++j; break; }
        }
        ++j;
    }
    out.Emit(i, j - i, static_cast<int>(CMakeStyle::Variable));
    return j;
}

// A quoted argument. Escapes are painted separately; `${...}` inside a string
// is still a variable reference, because CMake expands it there too.
Sci_Position ScanQuoted(const char* text, Sci_Position i, Sci_Position end,
                        Emitter& out) {
    Sci_Position seg = i;
    Sci_Position j = i + 1;
    while (j < end && text[j] != '"') {
        if (text[j] == '\\' && j + 1 < end) {
            out.Emit(seg, j - seg, static_cast<int>(CMakeStyle::String));
            out.Emit(j, 2, static_cast<int>(CMakeStyle::StringEscape));
            j += 2;
            seg = j;
            continue;
        }
        if (text[j] == '$') {
            const Sci_Position after = j + 1;
            Sci_Position probe = after;
            while (probe < end && IsNameCont(static_cast<unsigned char>(text[probe]))) ++probe;
            if (probe < end && (text[probe] == '{' || text[probe] == '<')) {
                out.Emit(seg, j - seg, static_cast<int>(CMakeStyle::String));
                j = ScanReference(text, j, end, out);
                seg = j;
                continue;
            }
        }
        ++j;
    }
    if (j < end && text[j] == '"') ++j;
    out.Emit(seg, j - seg, static_cast<int>(CMakeStyle::String));
    return j;
}

}

void ScanCMakeLine(const ScanInput& in, LexState& st, Emitter& out) {
    (void)st;
    const char* text = in.text;
    const Sci_Position end = in.end;
    Sci_Position i = in.begin;

    out.SetGapStyle(static_cast<int>(CMakeStyle::Default));
    auto emit = [&](Sci_Position from, Sci_Position len, CMakeStyle s) {
        out.Emit(from, len, static_cast<int>(s));
    };

    // The command name is the first word on the line — anything after it is an
    // argument. Multi-line invocations therefore paint their continuation lines
    // as plain arguments, which is what they are.
    bool sawCommand = false;

    while (i < end) {
        const char c = text[i];

        if (c == ' ' || c == '\t') { ++i; continue; }

        // Comment: `#` to end of line. `#[[ ... ]]` bracket comments are
        // recognized only as far as this line — they are rare enough that
        // spending a line-state bit on them is not worth it.
        if (c == '#') {
            emit(i, end - i, CMakeStyle::Comment);
            return;
        }

        if (c == '"') {
            i = ScanQuoted(text, i, end, out);
            sawCommand = true;
            continue;
        }

        if (c == '$') {
            const Sci_Position next = ScanReference(text, i, end, out);
            if (next > i) { i = next; sawCommand = true; continue; }
            emit(i, 1, CMakeStyle::Operator);
            ++i;
            continue;
        }

        if (c == '(' || c == ')') {
            emit(i, 1, CMakeStyle::Operator);
            ++i;
            sawCommand = true;
            continue;
        }

        if (IsNameStart(static_cast<unsigned char>(c))) {
            Sci_Position j = i;
            while (j < end && IsNameCont(static_cast<unsigned char>(text[j]))) ++j;
            const std::string_view word(text + i, static_cast<size_t>(j - i));

            if (!sawCommand) {
                // Lower-case the name for the control lookup: CMake command
                // names are case-insensitive and `IF(...)` is still common.
                std::string lowered;
                lowered.reserve(word.size());
                for (const char ch : word) {
                    lowered.push_back(static_cast<char>(
                        std::tolower(static_cast<unsigned char>(ch))));
                }
                emit(i, j - i,
                     CMakeControl().count(lowered) ? CMakeStyle::Keyword
                                                   : CMakeStyle::Command);
                sawCommand = true;
            } else {
                emit(i, j - i,
                     CMakeArgKeywords().count(word) ? CMakeStyle::Keyword
                                                    : CMakeStyle::Identifier);
            }
            i = j;
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            Sci_Position j = i;
            while (j < end && (std::isdigit(static_cast<unsigned char>(text[j]))
                               || text[j] == '.')) {
                ++j;
            }
            emit(i, j - i, CMakeStyle::Number);
            i = j;
            sawCommand = true;
            continue;
        }

        emit(i, 1, CMakeStyle::Operator);
        ++i;
        sawCommand = true;
    }
}

}
