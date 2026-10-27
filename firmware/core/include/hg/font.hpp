#pragma once

#include <cstddef>
#include <string_view>

namespace hg::font {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
// Advance and line pitch at scale 1 (glyph plus one pixel of spacing).
constexpr int kCellWidth = 6;
constexpr int kCellHeight = 9;

// CJK 12x12 glyphs
constexpr int kCjkGlyphWidth = 12;
constexpr int kCjkGlyphHeight = 12;
constexpr int kCjkCellWidth = 12;

// Text is UTF-8. The font draws printable ASCII, Latin-1 (U+00A0..U+00FF) and CJK;
// any other code point draws as '?'. Widths and columns count code points.

// Row-major glyph pixels: kGlyphWidth * kGlyphHeight chars, '#' = on.
const char* glyph(char32_t code_point);
// 12x12 1-bit bitmap (18 bytes row-major) for CJK glyphs, or nullptr if absent.
const uint8_t* cjk_glyph(char32_t code_point);
bool is_cjk(char32_t code_point);
// True for the code points the font has its own glyph for.
bool drawable(char32_t code_point);
// The code point at byte `i`, advancing `i` past it. A malformed or truncated
// sequence reads as U+FFFD and advances one byte, so any input terminates.
char32_t next(std::string_view s, size_t& i);
// Code points in `s`.
size_t length(std::string_view s);
// Visual columns taken by a code point: 2 for CJK, 1 for ASCII/Latin-1, 0 for control/newline.
int char_columns(char32_t code_point);
// Total visual columns in `s`.
int columns(std::string_view s);
// Bytes taken by the first `chars` code points of `s` (all of it if shorter).
size_t prefix(std::string_view s, size_t chars);
// Bytes taken by the first `max_cols` visual columns of `s`.
size_t prefix_cols(std::string_view s, int max_cols);

}  // namespace hg::font
