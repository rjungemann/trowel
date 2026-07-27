#pragma once

#include "editor/lexers.h"

#include <ILexer.h>

namespace trowel {

// ---------------------------------------------------------------------------
// Emitter
// ---------------------------------------------------------------------------

// Paints style runs onto the document. Offsets handed to Emit() are relative
// to the start of the buffer the scanners were handed; the Emitter adds the
// document base itself.
//
// Emit() also backfills: any bytes skipped between the previous run and `from`
// are painted with the current gap style. Scanners therefore only have to emit
// the spans they care about, and a scanner bug can never leave stale styling
// behind.
class Emitter {
public:
    Emitter(Scintilla::IDocument* doc, Sci_Position base)
        : doc_(doc), base_(base) {}

    void SetGapStyle(int style) { gapStyle_ = style; }
    int gapStyle() const { return gapStyle_; }

    void Emit(Sci_Position from, Sci_Position len, int style);

    // Paint everything up to (but excluding) `to` with the gap style.
    void FillTo(Sci_Position to);

private:
    void Paint(Sci_Position from, Sci_Position len, int style);

    Scintilla::IDocument* doc_;
    Sci_Position base_;
    Sci_Position next_ = 0;
    int gapStyle_ = 0;
};

// ---------------------------------------------------------------------------
// Per-line state
// ---------------------------------------------------------------------------

// Everything a scanner needs to carry from one line to the next. Every field
// gets its own slice of the 31 usable bits of Scintilla's int line state — bit
// 31 stays clear so the packed value is never negative. See the bit layout in
// lexer_adapter.cpp; the widths below are what the caps enforce.
//
// The deepest legal nesting is Markdown hosting a Turmeric fence that itself
// hosts an inline C block, so Markdown, Turmeric, and C state must all be live
// simultaneously. JSON and Just can only ever be a root language or a Markdown
// guest, never a host, but they get their own bits anyway rather than aliasing
// Turmeric's — it all fits, and non-aliased state can't be corrupted by a
// scanner that forgets to reset.
struct LexState {
    // --- Turmeric ---
    int turBlockDepth = 0;    // nested #| |# depth, capped
    int turDcDepth = 0;       // nested #; datum-comment depth, capped
    int turBracketDepth = 0;  // bracket nesting, for rainbow coloring
    bool turInString = false;
    bool turInCBlock = false;
    bool turInDatumComment = false;

    // --- JSON ---
    int jsonDepth = 0;

    // --- Just ---
    bool justInRecipe = false;

    // --- C (coexists with Turmeric) ---
    bool cInComment = false;

    // --- Markdown (coexists with any guest) ---
    bool mdInFence = false;
    bool mdFenceTilde = false;  // fence uses ~~~ rather than ```
    int mdFenceExtra = 0;       // opening fence length minus 3, capped at 7
    Language mdGuest = Language::Turmeric;
    bool mdGuestPlain = true;   // fence has no recognized language tag
};

// Field caps implied by the bit widths above. Exceeding them saturates rather
// than wrapping, so pathological input degrades gracefully instead of
// corrupting neighbouring fields.
inline constexpr int kMaxTurBlockDepth = 7;
inline constexpr int kMaxTurDcDepth = 7;
inline constexpr int kMaxTurBracketDepth = 127;
inline constexpr int kMaxJsonDepth = 31;
inline constexpr int kMaxMdFenceExtra = 7;

int PackLexState(const LexState& st);
LexState UnpackLexState(int packed);

// Reset the fields that alias between root/guest languages. Called when the
// active language changes so packing stays lossless.
void ResetGuestState(LexState& st);

// ---------------------------------------------------------------------------
// Scanners
// ---------------------------------------------------------------------------

// One line's worth of text to scan. `begin`/`end` are buffer-relative offsets
// bounding the line's content; `end` excludes the line terminator, which the
// caller styles.
struct ScanInput {
    const char* text = nullptr;
    Sci_Position begin = 0;
    Sci_Position end = 0;
    bool rainbow = true;
};

void ScanTurmericLine(const ScanInput& in, LexState& st, Emitter& out);
void ScanCLine(const ScanInput& in, LexState& st, Emitter& out);
void ScanMarkdownLine(const ScanInput& in, LexState& st, Emitter& out);
void ScanJsonLine(const ScanInput& in, LexState& st, Emitter& out);
void ScanJustLine(const ScanInput& in, LexState& st, Emitter& out);

// Dispatch to the scanner for `lang`.
void ScanLine(Language lang, const ScanInput& in, LexState& st, Emitter& out);

// The style a language paints unstyled bytes (and line terminators) with.
int DefaultStyleFor(Language lang);

// Map a bracket nesting depth to its rainbow style, cycling through the
// palette. Shared by the Turmeric and JSON scanners.
constexpr TurStyle RainbowStyleForDepth(int depth) {
    const int level = depth % kRainbowLevels;
    return static_cast<TurStyle>(static_cast<int>(TurStyle::Rainbow0) + level);
}

}
