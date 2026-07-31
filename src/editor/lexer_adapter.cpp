#include "editor/scanner.h"

#include <ILexer.h>
#include <Scintilla.h>

#include <QFileInfo>

#include <algorithm>
#include <vector>

using Scintilla::IDocument;
using Scintilla::ILexer5;

namespace trowel {

// ---------------------------------------------------------------------------
// Emitter
// ---------------------------------------------------------------------------

void Emitter::Paint(Sci_Position from, Sci_Position len, int style) {
    if (len <= 0) return;
    doc_->StartStyling(base_ + from);
    doc_->SetStyleFor(len, static_cast<char>(style));
}

void Emitter::Emit(Sci_Position from, Sci_Position len, int style) {
    if (len <= 0) return;
    if (from > next_) Paint(next_, from - next_, gapStyle_);
    Paint(from, len, style);
    next_ = std::max(next_, from + len);
}

void Emitter::FillTo(Sci_Position to) {
    if (to > next_) {
        Paint(next_, to - next_, gapStyle_);
        next_ = to;
    }
}

// ---------------------------------------------------------------------------
// Line state packing
// ---------------------------------------------------------------------------

namespace {

// Turmeric: bits 0..15. Bracket depth gets the widest field of the three
// counters because Lisp genuinely nests deeply, while 7 levels of nested block
// or datum comments is already far past anything real.
constexpr int kTurBlockShift = 0;    // 3 bits
constexpr int kTurDcShift = 3;       // 3 bits
constexpr int kTurBracketShift = 6;  // 7 bits
constexpr int kTurInStringBit = 1 << 13;
constexpr int kTurInCBlockBit = 1 << 14;
constexpr int kTurInDatumBit = 1 << 15;

// Leaf languages: bits 16..20. JSON, Just, Python, TOML, and sh are mutually
// exclusive (see the LexState comment), so they overlay the same five bits.
// Each one's encoding is a mask of the low bits, and JSON's depth spans all
// five, so an arbitrary field value still round-trips through the OR in
// PackLexState: unpacking hands every leaf a slice of the same value, and only
// the live scanner's slice is ever meaningful.
constexpr int kLeafShift = 16;  // 5 bits
constexpr int kLeafMask = 0x1F;
constexpr int kJustInRecipeFlag = 0x1;  // bit 0 of the leaf field
constexpr int kStringModeMask = 0x3;    // bits 0..1 of the leaf field

// Turmeric shebang: bit 21. C: bit 22.
constexpr int kTurAfterShebangBit = 1 << 21;
constexpr int kCInCommentBit = 1 << 22;

// Markdown: bits 23..30. The guest field uses kMdGuestPlain as a sentinel for
// "fence with no recognized language tag", which keeps everything inside 31
// bits — bit 31 stays clear so the packed state is never negative.
constexpr int kMdInFenceBit = 1 << 23;
constexpr int kMdFenceTildeBit = 1 << 24;
constexpr int kMdFenceExtraShift = 25;  // 2 bits
constexpr int kMdGuestShift = 27;       // 4 bits
constexpr int kMdGuestPlain = 0xF;

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}

int PackLexState(const LexState& st) {
    int v = 0;
    v |= Clamp(st.turBlockDepth, 0, kMaxTurBlockDepth) << kTurBlockShift;
    v |= Clamp(st.turDcDepth, 0, kMaxTurDcDepth) << kTurDcShift;
    v |= Clamp(st.turBracketDepth, 0, kMaxTurBracketDepth) << kTurBracketShift;
    if (st.turInString) v |= kTurInStringBit;
    if (st.turInCBlock) v |= kTurInCBlockBit;
    if (st.turInDatumComment) v |= kTurInDatumBit;
    if (st.turAfterShebang) v |= kTurAfterShebangBit;

    int leaf = Clamp(st.jsonDepth, 0, kMaxJsonDepth);
    if (st.justInRecipe) leaf |= kJustInRecipeFlag;
    leaf |= st.pyStringMode & kStringModeMask;
    leaf |= st.tomlStringMode & kStringModeMask;
    leaf |= st.shStringMode & kStringModeMask;
    v |= leaf << kLeafShift;

    if (st.cInComment) v |= kCInCommentBit;
    if (st.mdInFence) v |= kMdInFenceBit;
    if (st.mdFenceTilde) v |= kMdFenceTildeBit;
    v |= Clamp(st.mdFenceExtra, 0, kMaxMdFenceExtra) << kMdFenceExtraShift;
    const int guest = st.mdGuestPlain ? kMdGuestPlain : (static_cast<int>(st.mdGuest) & 0x7);
    v |= guest << kMdGuestShift;
    return v;
}

LexState UnpackLexState(int p) {
    LexState st;
    st.turBlockDepth = (p >> kTurBlockShift) & 0x7;
    st.turDcDepth = (p >> kTurDcShift) & 0x7;
    st.turBracketDepth = (p >> kTurBracketShift) & 0x7F;
    st.turInString = (p & kTurInStringBit) != 0;
    st.turInCBlock = (p & kTurInCBlockBit) != 0;
    st.turInDatumComment = (p & kTurInDatumBit) != 0;
    st.turAfterShebang = (p & kTurAfterShebangBit) != 0;

    const int leaf = (p >> kLeafShift) & kLeafMask;
    st.jsonDepth = leaf;
    st.justInRecipe = (leaf & kJustInRecipeFlag) != 0;
    st.pyStringMode = leaf & kStringModeMask;
    st.tomlStringMode = leaf & kStringModeMask;
    st.shStringMode = leaf & kStringModeMask;

    st.cInComment = (p & kCInCommentBit) != 0;
    st.mdInFence = (p & kMdInFenceBit) != 0;
    st.mdFenceTilde = (p & kMdFenceTildeBit) != 0;
    st.mdFenceExtra = (p >> kMdFenceExtraShift) & 0x3;
    const int guest = (p >> kMdGuestShift) & 0xF;
    st.mdGuestPlain = (guest == kMdGuestPlain);
    st.mdGuest = st.mdGuestPlain ? Language::Turmeric : static_cast<Language>(guest);
    return st;
}

void ResetGuestState(LexState& st) {
    st.turBlockDepth = 0;
    st.turDcDepth = 0;
    st.turBracketDepth = 0;
    st.turInString = false;
    st.turInCBlock = false;
    st.turInDatumComment = false;
    st.turAfterShebang = false;
    st.jsonDepth = 0;
    st.justInRecipe = false;
    st.pyStringMode = 0;
    st.tomlStringMode = 0;
    st.shStringMode = 0;
    st.cInComment = false;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void ScanLine(Language lang, const ScanInput& in, LexState& st, Emitter& out) {
    switch (lang) {
    case Language::C:        ScanCLine(in, st, out); break;
    case Language::Markdown: ScanMarkdownLine(in, st, out); break;
    case Language::Json:     ScanJsonLine(in, st, out); break;
    case Language::Just:     ScanJustLine(in, st, out); break;
    case Language::CMake:    ScanCMakeLine(in, st, out); break;
    case Language::Toml:     ScanTomlLine(in, st, out); break;
    case Language::Sh:       ScanShLine(in, st, out); break;
    case Language::Python:   ScanPythonLine(in, st, out); break;
    case Language::TurmericSweet: ScanTurmericSweetLine(in, st, out); break;
    case Language::Turmeric: ScanTurmericLine(in, st, out); break;
    }
}

int DefaultStyleFor(Language lang) {
    switch (lang) {
    case Language::C:        return static_cast<int>(CStyle::Default);
    case Language::Markdown: return static_cast<int>(MdStyle::Default);
    case Language::Json:     return static_cast<int>(JsonStyle::Default);
    case Language::Just:     return static_cast<int>(JustStyle::Default);
    case Language::CMake:    return static_cast<int>(CMakeStyle::Default);
    case Language::Toml:     return static_cast<int>(TomlStyle::Default);
    case Language::Sh:       return static_cast<int>(ShStyle::Default);
    case Language::Python:   return static_cast<int>(PyStyle::Default);
    case Language::TurmericSweet:
    case Language::Turmeric: break;
    }
    return static_cast<int>(TurStyle::Default);
}

namespace {

// The path half of language dispatch, reporting whether the name actually
// matched a rule. LanguageForPath() collapses "no match" to Turmeric for
// backwards compatibility, but LanguageForBuffer() needs to tell the two apart:
// a `#!` shebang may name the language of an extensionless file, and it must
// not override an extension that already did.
bool LanguageForFileName(const QString& path, Language& out) {
    if (path.isEmpty()) return false;

    const QString name = QFileInfo(path).fileName();
    const QString lower = name.toLower();

    // Justfiles are identified by name, not suffix.
    if (lower == "justfile" || lower == ".justfile" || lower.endsWith(".just")) {
        out = Language::Just;
        return true;
    }

    // Likewise CMake's driver file; `.cmake` covers modules and toolchains.
    if (lower == "cmakelists.txt" || lower.endsWith(".cmake")) {
        out = Language::CMake;
        return true;
    }

    // `.toml` covers Cargo.toml, pyproject.toml, and friends. Cargo's lockfile
    // is TOML too but does not carry the suffix.
    if (lower.endsWith(".toml") || lower == "cargo.lock") {
        out = Language::Toml;
        return true;
    }

    // Shell: suffixes plus the dotfiles that are shell scripts by convention
    // and never carry one.
    if (lower.endsWith(".sh") || lower.endsWith(".bash") || lower.endsWith(".zsh")
        || lower.endsWith(".ksh") || lower.endsWith(".ash")
        || lower == ".bashrc" || lower == ".bash_profile" || lower == ".bash_aliases"
        || lower == ".bash_logout" || lower == ".zshrc" || lower == ".zshenv"
        || lower == ".zprofile" || lower == ".zlogin" || lower == ".zlogout"
        || lower == ".profile" || lower == ".inputrc") {
        out = Language::Sh;
        return true;
    }

    if (lower.endsWith(".py") || lower.endsWith(".pyi") || lower.endsWith(".pyw")) {
        out = Language::Python;
        return true;
    }

    // Mirrors reader_type_from_extension() in Turmeric's reader.c, which
    // recognizes exactly one suffix: `.tur.sweet`. A bare `.sweet` is read as
    // ordinary Turmeric there, so treating it as sweet here would make
    // highlighting disagree with what actually runs. Such a file gets the
    // sweet reader only by carrying a `#lang` line, same as the toolchain.
    if (lower.endsWith(".tur.sweet")) { out = Language::TurmericSweet; return true; }
    if (lower.endsWith(".tur") || lower.endsWith(".sweet")) {
        out = Language::Turmeric;
        return true;
    }
    if (lower.endsWith(".md") || lower.endsWith(".markdown")) {
        out = Language::Markdown;
        return true;
    }
    if (lower.endsWith(".json")) { out = Language::Json; return true; }
    if (lower.endsWith(".c") || lower.endsWith(".h") || lower.endsWith(".cc")
        || lower.endsWith(".cpp") || lower.endsWith(".cxx")
        || lower.endsWith(".hpp") || lower.endsWith(".hh")) {
        out = Language::C;
        return true;
    }
    return false;
}

}

Language LanguageForPath(const QString& path) {
    Language lang = Language::Turmeric;
    LanguageForFileName(path, lang);
    return lang;
}

namespace {

// Map a `#lang` base name to the scanner that should highlight the file.
//
// Turmeric has four reader types; Trowel has two scanners for them. The
// curly-infix and neoteric readers only *enable* syntax that the Turmeric
// scanner already paints unconditionally (`{a + b}` as CurlyInfix, `f(` as
// NeotericCall), so they share it. Only the sweet reader adds tokens of its
// own. Names and the legacy alias track lang_base_from_name() in reader.c.
bool LanguageForLangBase(const QByteArray& base, Language& out) {
    if (base == "turmeric" || base == "turmeric/curly-infix"
        || base == "turmeric/neoteric") {
        out = Language::Turmeric;
        return true;
    }
    // `turmeric/sweet` is canonical; `sweet-exp` is the legacy alias, still
    // accepted by the toolchain and still dominant across its own docs.
    if (base == "turmeric/sweet" || base == "sweet-exp") {
        out = Language::TurmericSweet;
        return true;
    }
    return false;
}

// Read a `#lang` directive off the head of `text`, mirroring the accept rules
// of detect_lang_layered() in reader.c: an optional `#!` shebang line, then
// leading spaces/tabs (never newlines — the directive must be on line 1),
// then `#lang`, whitespace, and the base name. Trailing layer tokens
// (`stringed`, `refined`) do not affect the reader, so they are ignored.
bool LangDirectiveIn(const QByteArray& text, Language& out) {
    int i = 0;
    const int n = text.size();

    if (n >= 2 && text[0] == '#' && text[1] == '!'
        && (n < 3 || text[2] == '/' || text[2] == ' ' || text[2] == '\t'
            || text[2] == '\n' || text[2] == '\r')) {
        const int nl = text.indexOf('\n');
        if (nl < 0) return false;  // shebang-only file
        i = nl + 1;
    }

    while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (text.mid(i, 5) != "#lang") return false;
    i += 5;

    const int wsStart = i;
    while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i == wsStart) return false;  // `#langfoo` is not a directive

    const int baseStart = i;
    while (i < n && text[i] != ' ' && text[i] != '\t'
           && text[i] != '\n' && text[i] != '\r') {
        ++i;
    }
    return LanguageForLangBase(text.mid(baseStart, i - baseStart), out);
}

// Language named by a `#!` interpreter line, for files whose name says
// nothing. Uses the same accept rule as the reader (`#!` at byte 0 followed by
// `/`, blank, or EOL) so `#!fold-case`-style directives are not mistaken for
// one. `/usr/bin/env foo` and `/usr/bin/env -S foo --flag` both resolve to
// `foo`; anything after the interpreter is ignored.
bool ShebangLanguage(const QByteArray& text, Language& out) {
    const int n = text.size();
    if (n < 3 || text[0] != '#' || text[1] != '!') return false;
    if (text[2] != '/' && text[2] != ' ' && text[2] != '\t') return false;

    int lineEnd = text.indexOf('\n');
    if (lineEnd < 0) lineEnd = n;
    if (lineEnd > 0 && text[lineEnd - 1] == '\r') --lineEnd;

    // Split the line into words, dropping `env` and any of its options so the
    // real interpreter is what gets classified.
    QByteArray interp;
    int i = 2;
    bool sawEnv = false;
    while (i < lineEnd) {
        while (i < lineEnd && (text[i] == ' ' || text[i] == '\t')) ++i;
        const int wordStart = i;
        while (i < lineEnd && text[i] != ' ' && text[i] != '\t') ++i;
        if (i == wordStart) break;
        QByteArray word = text.mid(wordStart, i - wordStart);
        const int slash = word.lastIndexOf('/');
        if (slash >= 0) word = word.mid(slash + 1);
        if (word.isEmpty()) continue;
        if (word.startsWith('-')) continue;  // `env -S`, `env -i`, ...
        if (word.contains('=')) continue;    // `env VAR=value cmd`
        if (!sawEnv && word == "env") { sawEnv = true; continue; }
        interp = word;
        break;
    }
    if (interp.isEmpty()) return false;

    if (interp == "sh" || interp == "bash" || interp == "zsh" || interp == "dash"
        || interp == "ksh" || interp == "ash") {
        out = Language::Sh;
        return true;
    }
    if (interp == "python" || interp.startsWith("python3") || interp == "python2") {
        out = Language::Python;
        return true;
    }
    if (interp == "cmake") { out = Language::CMake; return true; }
    if (interp == "just") { out = Language::Just; return true; }
    if (interp == "tur" || interp == "turmeric" || interp == "turi") {
        out = Language::Turmeric;
        return true;
    }
    return false;
}

// Offsets of the `#lang` line within `text`, or false if there isn't one.
bool LangDirectiveSpan(const QByteArray& text, int& start, int& end) {
    int i = 0;
    const int n = text.size();

    if (n >= 2 && text[0] == '#' && text[1] == '!'
        && (n < 3 || text[2] == '/' || text[2] == ' ' || text[2] == '\t'
            || text[2] == '\n' || text[2] == '\r')) {
        const int nl = text.indexOf('\n');
        if (nl < 0) return false;
        i = nl + 1;
    }

    int j = i;
    while (j < n && (text[j] == ' ' || text[j] == '\t')) ++j;
    if (text.mid(j, 5) != "#lang") return false;

    start = i;
    const int nl = text.indexOf('\n', j);
    end = (nl < 0) ? n : nl + 1;
    return true;
}

}

Language LanguageForBuffer(const QString& path, const QByteArray& text) {
    Language ext = Language::Turmeric;
    const bool named = LanguageForFileName(path, ext);

    // Turmeric's own precedence, from the `load` path in elab_toplevel.c:
    //   chosen = (ext_type != READER_TURMERIC) ? ext_type : lang_type;
    // An extension that already names a non-default reader wins and the
    // directive is a redundant hint; otherwise the directive decides. Applied
    // only within the Turmeric family — a `#lang` line in a .md or .c file is
    // just text, and those extensions are not readers at all upstream.
    if (named && ext != Language::Turmeric) return ext;

    Language fromDirective = Language::Turmeric;
    if (LangDirectiveIn(text, fromDirective)) return fromDirective;

    // Nothing in the name to go on (an extensionless script, or an untitled
    // buffer). A `#!` line is then the only signal there is, and it is the same
    // signal the kernel would use to run the file.
    if (!named) {
        Language fromShebang = Language::Turmeric;
        if (ShebangLanguage(text, fromShebang)) return fromShebang;
    }
    return ext;
}

QByteArray LangDirectiveLine(const QByteArray& text) {
    int start = 0;
    int end = 0;
    if (!LangDirectiveSpan(text, start, end)) return {};
    QByteArray line = text.mid(start, end - start);
    if (!line.endsWith('\n')) line.append('\n');
    return line;
}

// ---------------------------------------------------------------------------
// ILexer5 adapter
// ---------------------------------------------------------------------------

namespace {

class ScannerLexer final : public ILexer5 {
public:
    ScannerLexer(Language lang, bool rainbow) : lang_(lang), rainbow_(rainbow) {}

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

    int SCI_METHOD NamedStyles() override { return kMaxStyleId + 1; }
    const char* SCI_METHOD NameOfStyle(int) override { return ""; }
    const char* SCI_METHOD TagsOfStyle(int) override { return ""; }
    const char* SCI_METHOD DescriptionOfStyle(int) override { return ""; }

    const char* SCI_METHOD GetName() override {
        switch (lang_) {
        case Language::C:        return "c";
        case Language::Markdown: return "markdown";
        case Language::Json:     return "json";
        case Language::Just:     return "just";
        case Language::CMake:    return "cmake";
        case Language::Toml:     return "toml";
        case Language::Sh:       return "sh";
        case Language::Python:   return "python";
        case Language::TurmericSweet: return "turmeric-sweet";
        case Language::Turmeric: break;
        }
        return "turmeric";
    }
    int SCI_METHOD GetIdentifier() override { return 0; }
    const char* SCI_METHOD PropertyGet(const char*) override { return ""; }

private:
    Language lang_;
    bool rainbow_;
};

void ScannerLexer::Lex(Sci_PositionU startPos, Sci_Position lengthDoc,
                       int initStyle, IDocument* doc) {
    (void)initStyle;

    // Widen the range to whole lines at BOTH ends.
    //
    // Backwards, so multi-line state is coherent. Forwards, because Scintilla
    // asks for arbitrary sub-ranges — every editor.get_style_at, and every
    // repaint of a partially-scrolled view, triggers a Colourise of [0, pos).
    // A buffer cut mid-token makes the scanners see a different token than the
    // full document contains ("de" instead of "def"), and they would then
    // paint that wrong answer over the correct one. Styling past the requested
    // end is explicitly allowed, so rounding up to the next line start keeps
    // tokenization stable no matter where the range happens to fall.
    const Sci_Position docLen = doc->Length();
    const Sci_Position startLine = doc->LineFromPosition(static_cast<Sci_Position>(startPos));
    const Sci_Position lineStartPos = doc->LineStart(startLine);

    Sci_Position endPos = static_cast<Sci_Position>(startPos) + lengthDoc;
    if (endPos > docLen) endPos = docLen;
    const Sci_Position endLine = doc->LineFromPosition(endPos);
    endPos = doc->LineStart(endLine + 1);
    if (endPos > docLen || endPos < 0) endPos = docLen;

    const Sci_Position readLen = endPos - lineStartPos;
    if (readLen <= 0) return;

    std::vector<char> buf(static_cast<size_t>(readLen) + 1, 0);
    doc->GetCharRange(buf.data(), lineStartPos, readLen);
    const char* text = buf.data();

    LexState st = (startLine > 0) ? UnpackLexState(doc->GetLineState(startLine - 1))
                                  : LexState{};

    Emitter out(doc, lineStartPos);
    out.SetGapStyle(DefaultStyleFor(lang_));

    Sci_Position i = 0;
    Sci_Position line = startLine;
    while (i < readLen) {
        // Content runs to the line terminator; the terminator itself is
        // painted with whatever gap style the scanner left behind.
        Sci_Position contentEnd = i;
        while (contentEnd < readLen && text[contentEnd] != '\n' && text[contentEnd] != '\r') {
            ++contentEnd;
        }
        Sci_Position lineEnd = contentEnd;
        if (lineEnd < readLen && text[lineEnd] == '\r') ++lineEnd;
        if (lineEnd < readLen && text[lineEnd] == '\n') ++lineEnd;

        ScanInput in{text, i, contentEnd, rainbow_, line};
        ScanLine(lang_, in, st, out);
        out.FillTo(lineEnd);

        doc->SetLineState(line, PackLexState(st));
        ++line;
        i = lineEnd;
    }
}

}

ILexer5* CreateLexerForLanguage(Language lang, bool rainbow) {
    return new ScannerLexer(lang, rainbow);
}

}
