#pragma once

#include <QIcon>
#include <QString>

namespace trowel {

// Register the bundled Symbols Nerd Font with the QFontDatabase. Safe to call
// multiple times — subsequent calls are no-ops. Returns the resolved family
// name (empty on failure).
QString RegisterNerdFont();

// Well-known Nerd Font codepoints used by the app. Values from
// https://www.nerdfonts.com/cheat-sheet — verify at bump time.
namespace NF {
constexpr char32_t Play                 = 0xF040A; // nf-md-play
constexpr char32_t PlaylistPlay         = 0xF0411; // nf-md-playlist_play
constexpr char32_t ViewSplitHorizontal  = 0xF0BCD; // nf-md-view_split_horizontal
constexpr char32_t ViewSplitVertical    = 0xF0BCE; // nf-md-view_split_vertical
constexpr char32_t Restart              = 0xF0709; // nf-md-restart
constexpr char32_t Console              = 0xF018D; // nf-md-console
constexpr char32_t Cog                  = 0xF0493; // nf-md-cog
constexpr char32_t Broom                = 0xF00E2; // nf-md-broom
constexpr char32_t AutoFix              = 0xF0068; // nf-md-auto_fix
constexpr char32_t FormatListBulleted   = 0xF0279; // nf-md-format_list_bulleted

// Debugger transport. The three `debug_step_*` glyphs are the ones Material
// Design draws for exactly this toolbar, so they read as stepping rather than
// as generic arrows. Verified present in the bundled SymbolsNerdFont — a
// missing codepoint renders as a tofu box, which reads as corruption.
constexpr char32_t DebugStepOver        = 0xF0DA9; // nf-md-debug_step_over
constexpr char32_t DebugStepInto        = 0xF0DA8; // nf-md-debug_step_into
constexpr char32_t DebugStepOut         = 0xF0DAA; // nf-md-debug_step_out
constexpr char32_t Stop                 = 0xF04DB; // nf-md-stop
constexpr char32_t Rewind               = 0xF045F; // nf-md-rewind
constexpr char32_t StepBackward         = 0xF04FC; // nf-md-step_backward
constexpr char32_t SkipPrevious         = 0xF04AE; // nf-md-skip_previous
}

// Rasterize a Nerd Font glyph into a QIcon. Cached by (codepoint, size, color).
// pixelSize is the glyph's target height in device-independent pixels.
QIcon NerdIcon(char32_t codepoint, int pixelSize, const QColor& color);

}
