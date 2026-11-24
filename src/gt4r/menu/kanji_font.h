#pragma once

// The double-byte fonts (font/*.kf, "KNJI"): fixed cells of 4-bit pixels for the characters
// the Latin fonts (font.h) do not hold: kana, kanji, hangul, full-width forms. A file has one
// or two faces (a 16 x 16 "medium" and a 22 x 22 "bold", or one of 22 x 22) and pairs of
// 16-color palettes. A face maps a range of Unicode code points to its cells.
// docs/formats/menu.md "Double-byte fonts".

#include "gt4r/base/types.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::menu {

struct KanjiFace {
    std::string name; // "medium", "bold", or the typeface's ("ShinGoM")
    u16 first = 0, last = 0; // the code points covered
    int cell_width = 0, cell_height = 0;
    u32 stride = 0;           // bytes from one cell to the next
    std::vector<u16> index;   // the cell of each code point first..last
    u16 none = 0xFFFF;        // the index of a code point without a cell
    size_t cells = 0;
    std::shared_ptr<const Bytes> data; // the whole file
    u32 cells_offset = 0;

    // The cell of a code point; -1 when the face has none.
    int cell(u32 code) const;
    // A cell's pixel, 0..15 (0 outside the cell).
    u8 pixel(int cell, int x, int y) const;
};

struct KanjiFont {
    std::vector<KanjiFace> faces;
    // 16 colors each, RGBA8 in memory order with alpha 0..255. Even ones are the outlined
    // look (light on a dark edge), odd ones white on a ramp of alpha.
    std::vector<std::array<u32, 16>> palettes;

    static bool is_font(ByteSpan d) { return d.size() >= 0x20 && gt3r::has_magic(d, "KNJI"); }
    // Throws FormatError.
    static KanjiFont parse(ByteSpan file);
    // The face by name, or the one whose cells are nearest `height`; null for an empty font.
    const KanjiFace* face(std::string_view name) const;
    const KanjiFace* face_for_height(int height) const;
};

// The double-byte font a language's menus load, the first of these the disc has: Gran Turismo
// 4 picks `jp_menu.kf`, `kr_menu.kf` or `tw_menu.kf` by its region and none for the US and
// Europe (0x108D80); Prologue `unicode.kf` for Japanese, `big5.kf` for Chinese (0x4439D8).
// `language` is a project's language folder ("JP", "TW", "US").
std::vector<std::string> kanji_font_paths(std::string_view language);

// A cell's pixels in a palette: RGBA8 in memory order, row by row (cell_width x cell_height).
std::vector<u32> cell_rgba(const KanjiFont& font, const KanjiFace& face, int cell, size_t palette);

// A picture: RGBA8 in memory order, row by row.
struct KanjiPicture {
    int width = 0, height = 0;
    std::vector<u32> rgba;
};
// Draws UTF-8 text in a face over `background`, cell after cell; a new line starts a row. A
// character the face lacks leaves a gap of half a cell (the Latin fonts draw those).
KanjiPicture draw_text(const KanjiFont& font, const KanjiFace& face, std::string_view utf8, size_t palette = 0, u32 background = 0xFF000000);

// UTF-8 to code points (a bad byte becomes U+FFFD).
std::u32string decode_utf8(std::string_view s);

} // namespace gt4r::menu
