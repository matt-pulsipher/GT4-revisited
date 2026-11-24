#include "gt4r/menu/kanji_font.h"

#include <algorithm>
#include <cstdlib>

namespace gt4r::menu {

namespace {

std::string text_at(ByteSpan d, size_t o) {
    size_t end = o;
    while (end < d.size() && d[end]) ++end;
    return o < d.size() ? std::string(reinterpret_cast<const char*>(d.data()) + o, end - o) : std::string();
}

} // namespace

int KanjiFace::cell(u32 code) const {
    if (code < first || code > last) return -1;
    const u16 c = index[code - first];
    return c == none || c >= cells ? -1 : int(c);
}

u8 KanjiFace::pixel(int cell, int x, int y) const {
    if (cell < 0 || size_t(cell) >= cells || x < 0 || y < 0 || x >= cell_width || y >= cell_height) return 0;
    const size_t row = size_t(cell_width + 1) / 2;
    const u8 b = (*data)[cells_offset + size_t(cell) * stride + size_t(y) * row + size_t(x) / 2];
    return x & 1 ? b >> 4 : b & 0x0F;
}

KanjiFont KanjiFont::parse(ByteSpan d) {
    if (!is_font(d)) throw FormatError("KNJI: wrong magic");
    const u16 faces = rd16(d, 0x10), palettes = rd16(d, 0x12);
    const u32 face_table = rd32(d, 0x14), palette_offset = rd32(d, 0x18);
    if (faces > 16 || face_table > d.size() || d.size() - face_table < size_t(faces) * 4) throw FormatError("KNJI: bad face table");
    if (palettes > 16 || palette_offset > d.size() || d.size() - palette_offset < size_t(palettes) * 64) throw FormatError("KNJI: bad palettes");
    KanjiFont font;
    const auto data = std::make_shared<const Bytes>(d.begin(), d.end());
    // The files with two faces mark a code point without a cell with cell 0 (a blank), the
    // others with 0xFFFF: the header's word at 0x0C tells them apart.
    const bool blank_is_none = rd32(d, 0x0C) != 0;
    for (u16 i = 0; i < faces; ++i) {
        const u32 o = rd32(d, face_table + i * 4);
        if (o > d.size() || d.size() - o < 0x18) throw FormatError("KNJI: a face starts past the end");
        KanjiFace f;
        f.first = rd16(d, o);
        f.last = rd16(d, o + 2);
        f.cells_offset = rd32(d, o + 4);
        f.stride = rd32(d, o + 8);
        f.cell_width = rd16(d, o + 12);
        f.cell_height = rd16(d, o + 14);
        const u32 index = rd32(d, o + 16);
        f.name = text_at(d, rd32(d, o + 20));
        f.none = blank_is_none ? 0 : 0xFFFF;
        f.data = data;
        if (f.last < f.first) throw FormatError("KNJI: a face's codes run backwards");
        const size_t codes = size_t(f.last - f.first) + 1;
        if (index > d.size() || d.size() - index < codes * 2) throw FormatError("KNJI: an index runs past the end");
        const size_t row = size_t(f.cell_width + 1) / 2;
        if (!f.cell_width || !f.cell_height || f.stride < row * size_t(f.cell_height)) throw FormatError("KNJI: bad cell size");
        f.index.resize(codes);
        u16 top = 0;
        bool any = false;
        for (size_t c = 0; c < codes; ++c) {
            f.index[c] = rd16(d, index + c * 2);
            if (f.index[c] != 0xFFFF) {
                top = std::max(top, f.index[c]);
                any = true;
            }
        }
        f.cells = any ? size_t(top) + 1 : 0;
        if (f.cells_offset > d.size() || (d.size() - f.cells_offset) / f.stride < f.cells) throw FormatError("KNJI: the cells run past the end");
        font.faces.push_back(std::move(f));
    }
    for (u16 p = 0; p < palettes; ++p) {
        std::array<u32, 16> colors{};
        for (size_t i = 0; i < 16; ++i) {
            const size_t o = palette_offset + p * 64 + i * 4;
            const u32 alpha = std::min<u32>(u32(d[o + 3]) * 255 / 0x80, 255); // the GS's 0x80 is opaque
            colors[i] = u32(d[o]) | u32(d[o + 1]) << 8 | u32(d[o + 2]) << 16 | alpha << 24;
        }
        font.palettes.push_back(colors);
    }
    return font;
}

const KanjiFace* KanjiFont::face(std::string_view name) const {
    for (const KanjiFace& f : faces)
        if (f.name == name) return &f;
    return nullptr;
}

const KanjiFace* KanjiFont::face_for_height(int height) const {
    const KanjiFace* best = nullptr;
    for (const KanjiFace& f : faces)
        if (!best || std::abs(f.cell_height - height) < std::abs(best->cell_height - height)) best = &f;
    return best;
}

std::vector<std::string> kanji_font_paths(std::string_view language) {
    if (language == "JP") return {"font/jp_menu.kf", "font/unicode.kf"};
    if (language == "TW") return {"font/tw_menu.kf", "font/big5.kf"};
    if (language == "KR") return {"font/kr_menu.kf"};
    return {};
}

std::vector<u32> cell_rgba(const KanjiFont& font, const KanjiFace& face, int cell, size_t palette) {
    std::vector<u32> out(size_t(face.cell_width) * size_t(face.cell_height), 0);
    if (cell < 0 || palette >= font.palettes.size()) return out;
    const std::array<u32, 16>& colors = font.palettes[palette];
    for (int y = 0; y < face.cell_height; ++y)
        for (int x = 0; x < face.cell_width; ++x) out[size_t(y) * size_t(face.cell_width) + size_t(x)] = colors[face.pixel(cell, x, y)];
    return out;
}

std::u32string decode_utf8(std::string_view s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        const u8 b = u8(s[i]);
        const int extra = b < 0x80 ? 0 : (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0 || i + size_t(extra) >= s.size() + (extra == 0 ? 1 : 0)) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t c = extra == 0 ? b : b & (0x3F >> extra);
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const u8 t = u8(s[i + size_t(k)]);
            if ((t & 0xC0) != 0x80) ok = false;
            c = c << 6 | (t & 0x3F);
        }
        out.push_back(ok ? c : char32_t(0xFFFD));
        i += ok ? size_t(extra) + 1 : 1;
    }
    return out;
}

KanjiPicture draw_text(const KanjiFont& font, const KanjiFace& face, std::string_view utf8, size_t palette, u32 background) {
    const std::u32string text = decode_utf8(utf8);
    // Measure: the widest line, the lines.
    int width = 0, lines = 1, x = 0;
    for (char32_t c : text) {
        if (c == U'\n') {
            ++lines;
            x = 0;
            continue;
        }
        x += face.cell(c) >= 0 ? face.cell_width : face.cell_width / 2;
        width = std::max(width, x);
    }
    KanjiPicture pic;
    pic.width = std::max(width, 1);
    pic.height = lines * face.cell_height;
    pic.rgba.assign(size_t(pic.width) * size_t(pic.height), background);
    static const std::array<u32, 16> kGray = [] {
        std::array<u32, 16> g{};
        for (u32 i = 0; i < 16; ++i) g[i] = (i ? 0xFF000000u : 0u) | (i * 17) * 0x010101u;
        return g;
    }();
    const std::array<u32, 16>& colors = palette < font.palettes.size() ? font.palettes[palette] : kGray;
    int px = 0, py = 0;
    for (char32_t c : text) {
        if (c == U'\n') {
            px = 0;
            py += face.cell_height;
            continue;
        }
        const int cell = face.cell(c);
        if (cell < 0) {
            px += face.cell_width / 2;
            continue;
        }
        for (int y = 0; y < face.cell_height; ++y)
            for (int cx = 0; cx < face.cell_width; ++cx) {
                const u32 color = colors[face.pixel(cell, cx, y)];
                const u32 a = color >> 24;
                if (!a) continue;
                u32& dst = pic.rgba[size_t(py + y) * size_t(pic.width) + size_t(px + cx)];
                u32 out = 0xFF000000;
                for (int ch = 0; ch < 3; ++ch) {
                    const u32 s = color >> (ch * 8) & 0xFF, d0 = dst >> (ch * 8) & 0xFF;
                    out |= ((s * a + d0 * (255 - a)) / 255) << (ch * 8);
                }
                dst = out;
            }
        px += face.cell_width;
    }
    return pic;
}

} // namespace gt4r::menu
