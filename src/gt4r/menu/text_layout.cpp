#include "gt4r/menu/text_layout.h"

#include <algorithm>
#include <cmath>

namespace gt4r::menu {

std::map<std::string, FontSpacing> read_font_spacing(ByteSpan d) {
    std::map<std::string, FontSpacing> out;
    if (d.size() < 0x20 || !gt3r::has_magic(d, "FT01")) return out;
    // Prologue's files (layout 0) have the double-byte step at +0x14 too, and no pair spacing.
    const bool gt4 = rd16(d, 0x12) == 1;
    const u32 count = rd16(d, 0x10), table = rd32(d, 0x14);
    for (u32 i = 0; i < count && size_t(table) + i * 4 + 4 <= d.size(); ++i) {
        const size_t o = rd32(d, table + i * 4);
        if (o + 0x18 > d.size()) continue;
        const size_t name = rd32(d, o);
        std::string n;
        for (size_t k = name; k < d.size() && d[k]; ++k) n += char(d[k]);
        out[n] = {d[o + 0x14], gt4 ? d[o + 0x15] : 0, !gt4};
    }
    return out;
}

std::optional<std::array<TextColor, 4>> find_button_colors(ByteSpan d) {
    static const u8 name[] = {'G', 'a', 'i', 'j', 'i', 0};
    for (size_t at = 16; at + sizeof name <= d.size(); ++at) {
        if (!std::equal(name, name + sizeof name, d.begin() + std::ptrdiff_t(at))) continue;
        // Four words 0x00BBGGRR, the colour's bytes in memory order red, green, blue.
        std::array<TextColor, 4> out{};
        bool ok = (at % 8) == 0;
        for (size_t k = 0; k < 4 && ok; ++k) {
            const size_t w = at - 16 + k * 4;
            ok = d[w + 3] == 0 && (d[w] | d[w + 1] | d[w + 2]) != 0;
            out[k] = {d[w], d[w + 1], d[w + 2], 255};
        }
        if (ok) return out;
    }
    return std::nullopt;
}

namespace {

using gt3r::text::FontGlyph;
using gt3r::text::LatinFont;

// The kerning of a pair (0x492B24): the profiles' rows added with saturation, the least of them
// (even rows taken to 7 bits, odd rows as bytes), to 7 bits. Glyphs without a profile: none.
int kern(const LatinFont& font, const FontGlyph& prev, const FontGlyph& cur) {
    if (prev.right_profile == FontGlyph::kNoProfile || cur.left_profile == FontGlyph::kNoProfile) return 0;
    int k = 0xFF;
    for (u32 r = 0; r < font.profile_rows; ++r) {
        const int sum = std::clamp(int(font.profiles[cur.left_profile + r]) + int(font.profiles[prev.right_profile + r]), -128, 127);
        k = std::min(k, (r & 1) ? (sum & 0xFF) : (sum & 0x7F));
    }
    return k & 0x7F;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 15;
}

struct Line {
    std::vector<PlacedGlyph> glyphs;
    std::vector<int> spaces; // a glyph's spaces before it on its line
    int space_count = 0;
    float pen = 0;
    bool soft = false; // broken to fit, not at the text's own line end
};

} // namespace

TextLayout layout_menu_text(std::string_view s, const TextStyle& st, float x, float y) {
    TextLayout lay;
    if (!st.font) return lay;
    const LatinFont& font = *st.font;
    const float sx = st.scale_x, sy = st.scale_y;
    std::vector<Line> lines(1);
    const FontGlyph* prev = nullptr;
    TextColor color = st.color;

    // A Latin glyph (0x492AC8): after a glyph of the same word its left offset and, unless it is a
    // digit, the font's pair spacing less the pair's kerning move it; the first of a word stands
    // at the pen.
    auto latin = [&](const LatinFont& f, int pair, const FontGlyph& g, u32 code, float scale_x, float scale_y, TextColor c) {
        Line& line = lines.back();
        float d = 0;
        if (st.spacing.gt3) d = float(g.left - (prev ? kern(f, *prev, g) : 0));
        else if (prev) {
            if (!(code >= '0' && code <= '9') && code != 0x7F) d += float(pair - kern(f, *prev, g));
            d += float(g.left);
        }
        PlacedGlyph p;
        p.font = &f, p.code = code, p.color = c;
        p.x = line.pen + d * scale_x, p.y = float(g.top) * scale_y;
        p.w = float(g.width) * scale_x, p.h = float(g.height) * scale_y;
        p.sx = g.atlas_x, p.sy = g.atlas_y, p.sw = g.width, p.sh = g.height;
        if (g.width && g.height) line.glyphs.push_back(p), line.spaces.push_back(line.space_count);
        line.pen += (d + float(g.advance)) * scale_x;
        prev = &g;
    };
    // The space, and a code the font does not have: the space's step, and the next glyph is the
    // first of its word (0x492A84, 0x491AC0).
    auto space = [&] {
        const FontGlyph* g = font.glyph(' ');
        const float step = g ? float(g->advance) : float(font.size / 2);
        lines.back().pen += step * sx;
        ++lines.back().space_count;
        prev = nullptr;
    };

    size_t i = 0;
    while (i < s.size()) {
        // One code point of UTF-8.
        u32 c = u8(s[i]);
        size_t n = 1;
        if (c >= 0xF0 && i + 3 < s.size()) c = ((c & 7) << 18) | ((u8(s[i + 1]) & 0x3F) << 12) | ((u8(s[i + 2]) & 0x3F) << 6) | (u8(s[i + 3]) & 0x3F), n = 4;
        else if (c >= 0xE0 && i + 2 < s.size()) c = ((c & 0xF) << 12) | ((u8(s[i + 1]) & 0x3F) << 6) | (u8(s[i + 2]) & 0x3F), n = 3;
        else if (c >= 0xC0 && i + 1 < s.size()) c = ((c & 0x1F) << 6) | (u8(s[i + 1]) & 0x3F), n = 2;
        i += n;
        if (c == '%' && i < s.size()) {
            const char m = s[i];
            if (m == '%') ++i; // a percent sign
            else if (m == 'C') { // a colour, three hex digits (0x493BC0)
                const size_t at = i + 1;
                auto digit = [&](size_t k) { return at + k < s.size() ? hex_digit(s[at + k]) : 15; };
                color.r = u8(digit(0) * 17), color.g = u8(digit(1) * 17), color.b = u8(digit(2) * 17);
                i = std::min(s.size(), at + 3);
                continue;
            } else if (m == 'B') { // a button of the Gaiji font, at its own size (0x494228)
                if (i + 1 >= s.size()) break;
                const u32 code = u8(s[i + 1]);
                i += 2;
                if (!st.gaiji) continue;
                TextColor bc{100, 100, 100, color.a};
                if (code >= 'c' && code <= 'f') bc = st.button_colors[code - 'c'], bc.a = color.a;
                if (const FontGlyph* g = st.gaiji->glyph(code)) {
                    prev = nullptr;
                    latin(*st.gaiji, 0, *g, code, 1, 1, bc);
                    prev = nullptr;
                }
                continue;
            } else if (m == 'F') { // a font by name up to a space: ❓ kept in the style's font
                const size_t end = s.find(' ', i);
                i = end == std::string_view::npos ? s.size() : end + 1;
                continue;
            }
        }
        if (c == '\r') continue;
        if (c == '\n' || c == '\v') {
            lines.back().soft = c == '\v';
            lines.emplace_back();
            prev = nullptr;
            continue;
        }
        if (c == ' ') {
            space();
            continue;
        }
        if (c >= 0x300) {
            // A double-byte character (0x4924C0): the font's cell size at its offset (one more
            // after a glyph), then the font's wide step.
            const FontGlyph* fallback = nullptr;
            if (st.kanji && st.kanji->cell(c) >= 0) {
                Line& line = lines.back();
                PlacedGlyph p;
                p.font = nullptr, p.code = c, p.color = color;
                p.x = line.pen + float(font.kanji_dx + (prev && !st.spacing.gt3 ? 1 : 0)) * sx, p.y = float(font.kanji_dy) * sy;
                p.w = float(font.kanji_width) * sx, p.h = float(font.kanji_height) * sy;
                p.sw = u16(st.kanji->cell_width), p.sh = u16(st.kanji->cell_height);
                line.glyphs.push_back(p), line.spaces.push_back(line.space_count);
                line.pen += float(st.spacing.wide_advance) * sx;
                prev = nullptr;
                continue;
            }
            // ❓ A full-width form no face has is drawn as its Latin letter (the US diary's
            // "1～7 days" shows a tilde).
            if (c >= 0xFF01 && c <= 0xFF5E) fallback = font.glyph(c - 0xFEE0);
            if (fallback && fallback->defined()) latin(font, st.spacing.pair, *fallback, c - 0xFEE0, sx, sy, color);
            else space();
            continue;
        }
        const FontGlyph* g = font.glyph(c);
        if (!g || !g->defined()) {
            space();
            continue;
        }
        latin(font, st.spacing.pair, *g, c, sx, sy, color);
    }

    // Each line by itself (0x491028): its width is the pen's less half a space.
    const FontGlyph* sp = font.glyph(' ');
    const float half_space = float((sp ? int(sp->advance) : int(font.size / 2)) / 2) * sx;
    const float line_advance = float(font.line_height) * sy;
    lay.lines = int(lines.size());
    lay.height = float(font.line_height) * sy + float(lay.lines - 1) * line_advance;
    for (const Line& l : lines) lay.width = std::max(lay.width, l.pen - half_space);
    float baseline = y;
    const float ascent = float(font.ascent) * sy;
    switch (st.valign) {
    case gt3r::text::VAlign::Baseline: break;
    case gt3r::text::VAlign::Top: baseline = y + ascent; break;
    case gt3r::text::VAlign::Middle: baseline = y + ascent - lay.height * 0.5f; break;
    case gt3r::text::VAlign::Bottom: baseline = y + ascent - lay.height; break;
    }
    lay.x = x;
    if (st.align == gt3r::text::HAlign::Right) lay.x = x - lay.width;
    else if (st.align == gt3r::text::HAlign::Center) lay.x = x - lay.width * 0.5f;
    lay.y = baseline - ascent;
    for (size_t k = 0; k < lines.size(); ++k) {
        const Line& l = lines[k];
        float w = l.pen - half_space, gap = 0;
        // ❓ `equalized`: a line broken to fit is spread over the width at its spaces (judged
        // from the license tests' information text).
        if (st.justify_width > 0 && l.soft && l.space_count > 0 && w < st.justify_width) gap = (st.justify_width - w) / float(l.space_count), w = st.justify_width;
        float ox = x;
        if (st.align == gt3r::text::HAlign::Right) ox = x - w;
        else if (st.align == gt3r::text::HAlign::Center) ox = x - w * 0.5f;
        for (size_t g = 0; g < l.glyphs.size(); ++g) {
            PlacedGlyph p = l.glyphs[g];
            p.x += ox + gap * float(l.spaces[g]), p.y += baseline + float(k) * line_advance;
            lay.glyphs.push_back(p);
        }
    }
    return lay;
}

TextLayout layout_menu_text_in(std::string_view utf8, TextStyle st, float x, float y, float w, float h, int align) {
    const int col = std::clamp(align, 0, 8) % 3, row = std::clamp(align, 0, 8) / 3;
    st.align = col == 0 ? gt3r::text::HAlign::Left : col == 1 ? gt3r::text::HAlign::Center : gt3r::text::HAlign::Right;
    st.valign = row == 0 ? gt3r::text::VAlign::Top : row == 1 ? gt3r::text::VAlign::Middle : gt3r::text::VAlign::Bottom;
    return layout_menu_text(utf8, st, col == 0 ? x : col == 1 ? x + w / 2 : x + w, row == 0 ? y : row == 1 ? y + h / 2 : y + h);
}

} // namespace gt4r::menu
