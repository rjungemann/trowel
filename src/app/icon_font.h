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

// Debugger transport, from the **Codicon** set — the glyphs VS Code draws for
// exactly this toolbar, so they read as stepping rather than as generic arrows.
//
// Every codepoint below was resolved from the bundled font *by glyph name*, not
// guessed. That distinction is load-bearing: an earlier version used the
// plausible-looking `0xF0DA9` for step-over, which is present in the font and
// therefore passed a contains-the-codepoint check — it is
// `md-badge_account_alert_outline`, and the debug toolbar shipped with three
// account badges on it. Presence is not identity. To add one, look the name up
// in the font's cmap; do not extrapolate from a neighbouring codepoint.
constexpr char32_t DebugContinue        = 0x0EACF; // cod-debug_continue
constexpr char32_t DebugStepOver        = 0x0EAD6; // cod-debug_step_over
constexpr char32_t DebugStepInto        = 0x0EAD4; // cod-debug_step_into
constexpr char32_t DebugStepOut         = 0x0EAD5; // cod-debug_step_out
constexpr char32_t DebugStepBack        = 0x0EB8F; // cod-debug_step_back
constexpr char32_t DebugReverseContinue = 0x0EB8E; // cod-debug_reverse_continue
constexpr char32_t DebugRestart         = 0x0EAD2; // cod-debug_restart
constexpr char32_t Stop                 = 0x0EAD7; // cod-debug_stop

// Timeline transport (the scrubber), from Material Design's media set: a row
// that reads left-to-right as one direction of travel.
constexpr char32_t SkipPrevious         = 0xF04AE; // md-skip_previous
constexpr char32_t SkipNext             = 0xF04AD; // md-skip_next
constexpr char32_t StepBackward         = 0xF04D5; // md-step_backward
constexpr char32_t StepForward          = 0xF04D7; // md-step_forward
constexpr char32_t Rewind               = 0xF045F; // md-rewind
}

// Rasterize a Nerd Font glyph into a QIcon. Cached by (codepoint, size, color).
// pixelSize is the glyph's target height in device-independent pixels.
QIcon NerdIcon(char32_t codepoint, int pixelSize, const QColor& color);

}
