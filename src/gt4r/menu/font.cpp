#include "gt4r/menu/font.h"

#include "formats/gs_memory.h"

#include <algorithm>
#include <map>

namespace gt4r::menu {

using gt3r::text::FontGlyph;
using gt3r::text::LatinFont;

// 0x10 u16 fonts, u16 layout (0: Gran Turismo 3's, which Prologue's files still have; 1),
// 0x14 u32 font offsets, 0x18 u32 texture set offsets (a set per font).
// A font: u32 name, u32 glyphs, u16 first code, u16 last code, u8 size, u8 line height,
// s8 ascent, u8 profile rows, u8 cell width, u8 cell height, s8 cell x, s8 cell y (the double-
// byte font's cell beside this font, as in Gran Turismo 3).
// A glyph of layout 1 (20 bytes): s8 left, s8 top, u8 width, u8 height, u8 advance, 3 unused,
// u16 x and u16 y in the picture, u32 left profile, u32 right profile.
gt3r::text::FontFile read_fonts(ByteSpan d) {
    if (!gt3r::text::FontFile::is_ft01(d) || d.size() < 0x20) throw FormatError("not an FT01 font file");
    const u32 count = rd16(d, 0x10), layout = rd16(d, 0x12), font_table = rd32(d, 0x14), set_table = rd32(d, 0x18);
    if (layout == 0) return gt3r::text::FontFile::parse(d);
    if (layout != 1 || count > 256 || size_t(font_table) + count * 4 > d.size() || size_t(set_table) + count * 4 > d.size())
        throw FormatError("FT01: bad header");

    // A font's glyph picture: one texture per palette (0 outlined, 1 plain).
    auto read_atlas = [&](u32 i) {
        const u32 set_at = rd32(d, set_table + i * 4);
        if (set_at >= d.size()) throw FormatError("FT01: texture set outside the file");
        const ByteSpan sub = d.subspan(set_at);
        if (!gt3r::Tex1::is_tex1(sub)) throw FormatError("FT01: no texture set");
        const gt3r::Tex1 set = gt3r::Tex1::parse(sub);
        gt3r::GsMemory gs;
        set.upload(sub, gs);
        std::array<gt3r::Tex1::Image, gt3r::text::kFontVariants> atlas;
        for (int v = 0; v < gt3r::text::kFontVariants && v < int(set.textures.size()); ++v) atlas[size_t(v)] = set.decode(gs, size_t(v));
        if (set.textures.size() == 1) atlas[1] = atlas[0];
        return atlas;
    };

    gt3r::text::FontFile file;
    for (u32 i = 0; i < count; ++i) {
        const size_t o = rd32(d, font_table + i * 4);
        if (o + 0x14 > d.size()) throw FormatError("FT01: font outside the file");
        LatinFont font;
        const u32 name = rd32(d, o);
        if (name >= d.size()) throw FormatError("FT01: name outside the file");
        font.name.assign(d.begin() + name, std::find(d.begin() + name, d.end(), u8(0)));
        const u32 glyphs = rd32(d, o + 4);
        font.first = rd16(d, o + 8), font.last = rd16(d, o + 10);
        if (font.last < font.first) throw FormatError("FT01: empty code range");
        font.size = d[o + 0xC], font.line_height = d[o + 0xD], font.ascent = gt3r::text::i8(d[o + 0xE]), font.profile_rows = d[o + 0xF];
        font.kanji_width = d[o + 0x10], font.kanji_height = d[o + 0x11];
        font.kanji_dx = gt3r::text::i8(d[o + 0x12]), font.kanji_dy = gt3r::text::i8(d[o + 0x13]);
        std::map<u32, u32> seen; // a profile's file offset -> its place in font.profiles
        auto profile = [&](u32 at) -> u32 {
            if (at == 0) return FontGlyph::kNoProfile;
            if (auto it = seen.find(at); it != seen.end()) return it->second;
            if (at > d.size() || d.size() - at < font.profile_rows) throw FormatError("FT01: profile outside the file");
            const u32 pos = u32(font.profiles.size());
            for (u32 r = 0; r < font.profile_rows; ++r) font.profiles.push_back(gt3r::text::i8(d[at + r]));
            seen.emplace(at, pos);
            return pos;
        };
        for (u32 c = font.first; c <= font.last; ++c) {
            const size_t g = glyphs + size_t(c - font.first) * 20;
            if (g + 20 > d.size()) throw FormatError("FT01: glyph outside the file");
            FontGlyph glyph;
            glyph.left = gt3r::text::i8(d[g]), glyph.top = gt3r::text::i8(d[g + 1]);
            glyph.width = d[g + 2], glyph.height = d[g + 3], glyph.advance = d[g + 4];
            glyph.atlas_x = rd16(d, g + 8), glyph.atlas_y = rd16(d, g + 10);
            // Width and height 0xFF: no picture (the space, which still advances).
            if (glyph.width == 0xFF && glyph.height == 0xFF) glyph.width = glyph.height = 0;
            glyph.left_profile = profile(rd32(d, g + 12)), glyph.right_profile = profile(rd32(d, g + 16));
            font.glyphs.push_back(glyph);
        }
        font.atlas = read_atlas(i);
        file.fonts.push_back(std::move(font));
    }
    return file;
}

} // namespace gt4r::menu
