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

// JSON: bits 16..20. Just: bit 21. C: bit 22.
constexpr int kJsonDepthShift = 16;  // 5 bits
constexpr int kJustInRecipeBit = 1 << 21;
constexpr int kCInCommentBit = 1 << 22;

// Markdown: bits 23..30. The guest field uses kMdGuestPlain as a sentinel for
// "fence with no recognized language tag", which keeps everything inside 31
// bits — bit 31 stays clear so the packed state is never negative.
constexpr int kMdInFenceBit = 1 << 23;
constexpr int kMdFenceTildeBit = 1 << 24;
constexpr int kMdFenceExtraShift = 25;  // 3 bits
constexpr int kMdGuestShift = 28;       // 3 bits
constexpr int kMdGuestPlain = 0x7;

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
    v |= Clamp(st.jsonDepth, 0, kMaxJsonDepth) << kJsonDepthShift;
    if (st.justInRecipe) v |= kJustInRecipeBit;
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
    st.jsonDepth = (p >> kJsonDepthShift) & 0x1F;
    st.justInRecipe = (p & kJustInRecipeBit) != 0;
    st.cInComment = (p & kCInCommentBit) != 0;
    st.mdInFence = (p & kMdInFenceBit) != 0;
    st.mdFenceTilde = (p & kMdFenceTildeBit) != 0;
    st.mdFenceExtra = (p >> kMdFenceExtraShift) & 0x7;
    const int guest = (p >> kMdGuestShift) & 0x7;
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
    st.jsonDepth = 0;
    st.justInRecipe = false;
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
    case Language::TurmericSweet:
    case Language::Turmeric: break;
    }
    return static_cast<int>(TurStyle::Default);
}

Language LanguageForPath(const QString& path) {
    if (path.isEmpty()) return Language::Turmeric;

    const QString name = QFileInfo(path).fileName();
    const QString lower = name.toLower();

    // Justfiles are identified by name, not suffix.
    if (lower == "justfile" || lower == ".justfile" || lower.endsWith(".just")) {
        return Language::Just;
    }

    // Mirrors reader_type_from_extension() in Turmeric's reader.c, which
    // recognizes exactly one suffix: `.tur.sweet`. A bare `.sweet` is read as
    // ordinary Turmeric there, so treating it as sweet here would make
    // highlighting disagree with what actually runs. Such a file gets the
    // sweet reader only by carrying a `#lang` line, same as the toolchain.
    if (lower.endsWith(".tur.sweet")) return Language::TurmericSweet;
    if (lower.endsWith(".tur") || lower.endsWith(".sweet")) return Language::Turmeric;
    if (lower.endsWith(".md") || lower.endsWith(".markdown")) return Language::Markdown;
    if (lower.endsWith(".json")) return Language::Json;
    if (lower.endsWith(".c") || lower.endsWith(".h") || lower.endsWith(".cc")
        || lower.endsWith(".cpp") || lower.endsWith(".cxx")
        || lower.endsWith(".hpp") || lower.endsWith(".hh")) {
        return Language::C;
    }
    return Language::Turmeric;
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
    const Language ext = LanguageForPath(path);
    // Turmeric's own precedence, from the `load` path in elab_toplevel.c:
    //   chosen = (ext_type != READER_TURMERIC) ? ext_type : lang_type;
    // An extension that already names a non-default reader wins and the
    // directive is a redundant hint; otherwise the directive decides. Applied
    // only within the Turmeric family — a `#lang` line in a .md or .c file is
    // just text, and those extensions are not readers at all upstream.
    if (ext != Language::Turmeric) return ext;

    Language fromDirective = Language::Turmeric;
    if (LangDirectiveIn(text, fromDirective)) return fromDirective;
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

        ScanInput in{text, i, contentEnd, rainbow_};
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
