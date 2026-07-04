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
}

// Rasterize a Nerd Font glyph into a QIcon. Cached by (codepoint, size, color).
// pixelSize is the glyph's target height in device-independent pixels.
QIcon NerdIcon(char32_t codepoint, int pixelSize, const QColor& color);

}
