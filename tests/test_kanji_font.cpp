// The double-byte fonts ("KNJI") on a made-up file, then every .kf of the discs of GT4R_DISC.
// With GT4R_TEST_OUT naming a folder, the disc test also writes the picture it draws there.

#include "test_framework.h"

#include "base/file.h"
#include "formats/png_writer.h"
#include "gt4r/menu/kanji_font.h"
#include "gt4r/menu/text_table.h"

#include <cstdlib>
#include <cstring>

using namespace gt4r;

namespace {

void put32(Bytes& b, size_t at, u32 v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(b.data() + at, &v, 4);
}
void put16(Bytes& b, size_t at, u16 v) {
    if (b.size() < at + 2) b.resize(at + 2);
    std::memcpy(b.data() + at, &v, 2);
}
bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

// One face "medium" of 4 x 2 cells over U+3041..U+3044: U+3042 is cell 1, U+3044 cell 0, the
// others have none. Cell 1 is 1 2 3 4 / 5 6 7 8.
Bytes make_font(bool blank_is_none) {
    Bytes b(0x100, 0);
    std::memcpy(b.data(), "KNJI", 4);
    put32(b, 0x0C, blank_is_none ? 0x80 : 0);
    put16(b, 0x10, 1);
    put16(b, 0x12, 2);
    put32(b, 0x14, 0xA0);
    put32(b, 0x18, 0x20);
    for (u32 i = 0; i < 16; ++i) {
        put32(b, 0x20 + i * 4, 0x80000000u | (i * 16) * 0x010101u); // opaque grays
        put32(b, 0x60 + i * 4, (i * 8) << 24 | 0xFFFFFF);            // white on an alpha ramp
    }
    put32(b, 0xA0, 0xB0);
    const size_t f = 0xB0;
    put16(b, f, 0x3041);
    put16(b, f + 2, 0x3044);
    put32(b, f + 4, 0xF0);  // cells
    put32(b, f + 8, 4);     // stride
    put16(b, f + 12, 4);
    put16(b, f + 14, 2);
    put32(b, f + 16, 0xE0); // index
    put32(b, f + 20, 0xD0); // name
    std::memcpy(b.data() + 0xD0, "medium", 7);
    const u16 none = blank_is_none ? 0 : 0xFFFF;
    put16(b, 0xE0, none);
    put16(b, 0xE2, 1);
    put16(b, 0xE4, none);
    put16(b, 0xE6, blank_is_none ? 2 : 0);
    const u8 cells[12] = {0, 0, 0, 0, 0x21, 0x43, 0x65, 0x87, 0xFF, 0xFF, 0xFF, 0xFF};
    std::memcpy(b.data() + 0xF0, cells, 12);
    return b;
}

} // namespace

TEST(kanji_font_parses) {
    for (bool blank : {false, true}) {
        const menu::KanjiFont font = menu::KanjiFont::parse(make_font(blank));
        CHECK(font.faces.size() == 1 && font.palettes.size() == 2);
        const menu::KanjiFace& f = font.faces[0];
        CHECK(f.name == "medium" && f.first == 0x3041 && f.last == 0x3044 && f.cell_width == 4 && f.cell_height == 2);
        CHECK(f.cell(0x3042) == 1 && f.cell(0x3041) == -1 && f.cell(0x3043) == -1 && f.cell(0x3040) == -1 && f.cell(0x3045) == -1);
        CHECK(f.cell(0x3044) == (blank ? 2 : 0));
        CHECK(f.pixel(1, 0, 0) == 1 && f.pixel(1, 3, 0) == 4 && f.pixel(1, 0, 1) == 5 && f.pixel(1, 3, 1) == 8);
        CHECK(f.pixel(1, 4, 0) == 0 && f.pixel(9, 0, 0) == 0);
        CHECK(font.face("medium") == &f && !font.face("bold") && font.face_for_height(22) == &f);
        CHECK((font.palettes[0][15] >> 24) == 255 && (font.palettes[0][15] & 0xFF) == 240);
        CHECK((font.palettes[1][8] >> 24) == 127);
        // "あ", a character the face lacks, "あ" again, on a second line "あ".
        const menu::KanjiPicture p = menu::draw_text(font, f, "\xE3\x81\x82x\xE3\x81\x82\n\xE3\x81\x82", 0, 0xFF000000);
        CHECK(p.width == 10 && p.height == 4);
        CHECK((p.rgba[0] & 0xFF) == 16 && (p.rgba[3] & 0xFF) == 64 && (p.rgba[4] & 0xFF) == 0 && (p.rgba[6] & 0xFF) == 16);
        CHECK((p.rgba[2 * 10] & 0xFF) == 16 && (p.rgba[3 * 10 + 3] & 0xFF) == 128);
    }
    CHECK(menu::decode_utf8("a\xE3\x81\x82\xF0\x9F\x8F\x81") == std::u32string(U"aあ\U0001F3C1"));
    CHECK(menu::decode_utf8("\xE3\x81") == std::u32string(U"��"));
    Bytes bad = make_font(false);
    bad.resize(0xE4);
    CHECK(throws([&] { menu::KanjiFont::parse(bad); }));
    bad = make_font(false);
    put32(bad, 0xB0 + 8, 1); // a stride shorter than a cell
    CHECK(throws([&] { menu::KanjiFont::parse(bad); }));
    bad = make_font(false);
    bad[0] = 'X';
    CHECK(throws([&] { menu::KanjiFont::parse(bad); }));
}

TEST(kanji_fonts_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    const std::string kana = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x82\xA2\xE3\x82\xA4\xE3\x82\xA6"; // あいうアイウ
    for (const auto& disc : test::discs()) {
        size_t fonts = 0, faces = 0, cells = 0;
        for (const ImageFile& f : disc->files()) {
            if (f.path.size() < 3 || f.path.compare(f.path.size() - 3, 3, ".kf") != 0) continue;
            const Bytes d = disc->volume().read(*disc->volume().find(f.path));
            try {
                const menu::KanjiFont font = menu::KanjiFont::parse(d);
                ++fonts;
                CHECK(!font.faces.empty() && font.palettes.size() == font.faces.size() * 2);
                for (const menu::KanjiFace& face : font.faces) {
                    ++faces;
                    cells += face.cells;
                    CHECK((face.cell_width == 22 && face.cell_height == 22) || (face.cell_width == 16 && face.cell_height == 16));
                    CHECK(face.cells > 0);
                }
            } catch (const std::exception& e) {
                std::printf("  %s: %s\n", f.path.c_str(), e.what());
                CHECK(false);
            }
        }
        std::printf("  %s: %zu fonts, %zu faces, %zu cells\n", game_name(disc->info().game), fonts, faces, cells);
        CHECK(fonts == (disc->info().game == Game::GT4 ? 12u : 2u));

        // The font of the Japanese text: every kana has a cell with ink in it.
        const gt3r::Volume::Entry* e = disc->volume().find("font/unicode.kf");
        CHECK(e != nullptr);
        if (!e) continue;
        const menu::KanjiFont font = menu::KanjiFont::parse(disc->volume().read(*e));
        const menu::KanjiFace& face = font.faces.front();
        for (char32_t c : menu::decode_utf8(kana)) {
            const int cell = face.cell(c);
            CHECK(cell >= 0);
            int ink = 0;
            for (int y = 0; y < face.cell_height; ++y)
                for (int x = 0; x < face.cell_width; ++x) ink += face.pixel(cell, x, y) != 0;
            CHECK(ink > 40 && ink < face.cell_width * face.cell_height);
        }
        CHECK(face.cell('A') == -1); // (the Latin fonts' part)

        // A line of the disc's own Japanese text, drawn: Prologue's text tables.
        std::string text = kana;
        if (const gt3r::Volume::Entry* t = disc->volume().find("rtext/JP.rt2")) {
            const menu::TextTable table = menu::TextTable::parse(disc->volume().read(*t));
            size_t best = 0;
            for (const std::string& page : table.pages())
                for (const auto& [key, value] : *table.page(page)) {
                    size_t drawn = 0;
                    for (char32_t c : menu::decode_utf8(value)) drawn += face.cell(c) >= 0;
                    if (drawn > best && drawn <= 24 && value.find('\n') == std::string::npos) {
                        best = drawn;
                        text = value;
                    }
                }
            CHECK(best >= 8);
        }
        const menu::KanjiPicture white = menu::draw_text(font, face, text + "\n" + kana, 0, 0xFF202020);
        size_t lit = 0;
        for (u32 px : white.rgba) lit += (px & 0xFF) > 0x80;
        CHECK(white.height == 2 * face.cell_height && white.width >= 6 * face.cell_width);
        CHECK(lit > size_t(white.width) * 2 && lit < white.rgba.size() / 2);
        if (const char* out = std::getenv("GT4R_TEST_OUT")) {
            const std::string path = std::string(out) + "/kanji_" + (disc->info().game == Game::GT4 ? "gt4" : "prologue") + ".png";
            CHECK(gt3r::write_host_file(path, gt3r::encode_png(u32(white.width), u32(white.height), white.rgba)));
            std::printf("  wrote %s\n", path.c_str());
        }
    }
}
