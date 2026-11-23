#pragma once

// Gran Turismo 4's layout of a menu text (the program's 0x491028 a line at a time, 0x4940E8 the
// marks, 0x492978 a character; docs/formats/menu.md "Text"): its fonts' glyphs placed as the
// program places them, which differs from Gran Turismo 3's in the spacing of a pair, the space
// and the lines' alignment.

#include "formats/text/fnt.h"
#include "formats/text/text_layout.h"
#include "gt4r/base/types.h"
#include "gt4r/menu/kanji_font.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::menu {

// What a font record of the game's FT01 files has past Gran Turismo 3's (+0x14, +0x15).
struct FontSpacing {
    int wide_advance = 0; // the pen's step after a double-byte character
    int pair = 0;         // added between two kerned glyphs
};
// By font name; empty when the file does not read.
std::map<std::string, FontSpacing> read_font_spacing(ByteSpan fnt_file);

struct TextColor {
    u8 r = 255, g = 255, b = 255, a = 255;
};

struct TextStyle {
    const gt3r::text::LatinFont* font = nullptr;
    FontSpacing spacing;
    const gt3r::text::LatinFont* gaiji = nullptr; // the pad's buttons ("%Bb")
    const KanjiFace* kanji = nullptr;             // characters from U+0300 on
    float scale_x = 1, scale_y = 1;
    gt3r::text::HAlign align = gt3r::text::HAlign::Left;
    gt3r::text::VAlign valign = gt3r::text::VAlign::Top;
    TextColor color;
    // `equalized`: each line broken at a soft break ('\v') is spread to this width at its spaces.
    float justify_width = 0;
    // "%Bc" to "%Bf" (the four shape buttons) are drawn in these; other buttons in gray.
    std::array<TextColor, 4> button_colors{};
};

struct PlacedGlyph {
    const gt3r::text::LatinFont* font = nullptr; // null: a double-byte cell of the style's kanji face
    u32 code = 0;                                // the font's code, or the code point of a cell
    float x = 0, y = 0, w = 0, h = 0;            // on the page
    u16 sx = 0, sy = 0, sw = 0, sh = 0;          // in the font's picture
    TextColor color;
};

struct TextLayout {
    std::vector<PlacedGlyph> glyphs;
    float x = 0, y = 0, width = 0, height = 0; // the block (top = first baseline - ascent)
    int lines = 1;
};

// The colours of the shape buttons in a text ("%Bc" triangle to "%Bf" square), found in the
// unpacked program (the four words before the name "Gaiji" the text code switches to); none when
// the program has no such table.
std::optional<std::array<TextColor, 4>> find_button_colors(ByteSpan program_image);

// Lays out UTF-8 text from (x, y) as the alignment says: each line on its own. The text's marks:
// "%%" a percent sign, "%Crgb" a colour (hex digits), "%Bx" the button x of the Gaiji font,
// "%F<name> " (a font) is skipped; "\r" is left out; "\v" is a line broken to fit (justified with
// `justify_width`).
TextLayout layout_menu_text(std::string_view utf8, const TextStyle& style, float x, float y);

} // namespace gt4r::menu
