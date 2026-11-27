// GT4's race display: where its texts land on the frame (race::frame_place / text_start; docs/formats/race_display.md "Where a
// text lands").
#include "test_framework.h"

#include "base/ee_float.h"
#include "gt4r/race/race_display.h"

#include <cmath>

using namespace gt4r;

// The projection as the original's race holds it: VU0's vf24 .. vf27 read from a saved state of a Tsukuba race (03-race, the
// first corner) are 1/320 (0x3B4CCCCD) across, -1/240 (0xBB888889) down, translation (-1, 1): the one-view display's own
// matrix, nothing moved by an element. 1/320 a hair over, so a whole place often lands a hair under its pixel.
TEST(gt4_display_places_through_the_projection) {
    const race::DisplayFrame f = race::single_frame(0);
    // (the left edges of the original's texts in a GS dump of that race: the lap time "0'16.450" centered on 335, 87 wide: its
    // left 291.5 -> 291; the best lap's "-'--.---" on 550, 86 wide: 507 -> 506; the lap count "1/2" (80 %) on 135: 109.8 -> 109)
    CHECK(int(race::frame_place(f, 291.5f, 107).x) == 291);
    CHECK(int(race::frame_place(f, 507, 58).x) == 506);
    CHECK(int(race::frame_place(f, 109.8f, 74).x) == 109);
    CHECK(race::frame_place(f, 550, 58).x == 550.0f && race::frame_place(f, 335, 107).x == 335.0f && race::frame_place(f, 0, 0).x == 0.0f);
    // Lines: y x 448 / 480, whole lines down (the time's baseline 107 -> 99, the best's 58 -> 54, the place digit's middle 67 -> 62).
    CHECK(int(race::frame_place(f, 335, 107).y) == 99 && int(race::frame_place(f, 550, 58).y) == 54 && int(race::frame_place(f, 55, 67).y) == 62);
    int under = 0;
    for (int x = 0; x <= 640; ++x) {
        const float got = race::frame_place(f, float(x), 0).x;
        CHECK(got == float(x) || (got < float(x) && got > float(x) - 0.001f));
        under += got != float(x);
    }
    CHECK(under > 300 && under < 500); // (473 of the 641 with the EE's multiplier on the FPU steps)
}

// A line's start (0x44E1E0, 0x491220): centered (pen flags 15) the place's pixel less half the width, its line's top the
// place's line less half the line height; on the baseline (0) less the ascent. The original's place digit "6" (FC_26, 23
// wide) at (55, 67): its glyph at x 43.5, its line's top 45 (its glyph's top 46, as the GS dump has it); the lap time on its
// baseline 107 (hn_20R, ascent 15): top 84.
TEST(gt4_display_text_starts) {
    const race::DisplayFrame f = race::single_frame(0);
    const race::TextStart digit = race::text_start(f, 55, 67, 15, 23, 27, 35, 1);
    CHECK(digit.x == 43.5f && digit.top == 45);
    const race::TextStart time = race::text_start(f, 291.5f, 107, 0, 87, 15, 22, 1);
    CHECK(time.x == 291.0f && time.top == 84);
    const race::TextStart right = race::text_start(f, 507, 58, 2, 36, 15, 22, 1);
    CHECK(right.x == 470.0f && right.top == 39); // (the box 0 .. 506, the line at its right)
    // Wide: the canvas -106.667 .. 746.667 on the same 640 pixels; the frame back to the canvas.
    const race::DisplayFrame w = race::single_frame(320.0f / 3);
    CHECK(std::fabs(race::frame_to_canvas_x(w, race::frame_place(w, 320, 0).x) - 320) < 0.01f);
    CHECK(std::fabs(race::frame_to_canvas_x(w, 0) + 106.667f) < 0.01f && std::fabs(race::frame_to_canvas_x(w, 640) - 746.667f) < 0.01f);
    // A stacked half's: 640 x 224 for 240 of the canvas, the upper half's from -10.
    const race::DisplayFrame up = race::stacked_frame(0, 0);
    CHECK(std::fabs(race::frame_place(up, 0, -10).y) < 0.01f && std::fabs(race::frame_place(up, 0, 230).y - 224) < 0.01f);
    const race::DisplayFrame side = race::side_by_side_frame(320.0f / 3);
    CHECK(std::fabs(race::frame_place(side, 320, 0).x - 320) < 0.01f && std::fabs(race::frame_place(side, -320.0f / 3, 0).x) < 0.01f);
}

// The whole line as the race display draws it: GT4's own text layout (menu::layout_menu_text, one line, the pen's whole as the
// width) placed by race::text_start, as race_hud's draw_text does. ✔ Every glyph of the original's race display in a GS dump of
// the Tsukuba race (03-race, the first corner; frame pixels, lines of 448): the lap time "0'30.900" (RaceTimeDisplay: the element
// centers it on 335 by its width, then draws it from there), the best lap's dashes on 550, the lap count "1/2" (80 %) from 135
// less half its width, the place digit "6" (flags 15 on 55), the speed "  3" (GT4meter, flags 2 on 581).
#include "gt4r/menu/font.h"
#include "gt4r/menu/text_layout.h"
#include "vfs/vfs.h"

TEST(disc_gt4_display_texts_land_as_the_original) {
    Disc* gt4 = nullptr;
    for (const auto& disc : test::discs())
        if (disc->info().game == Game::GT4) gt4 = disc.get();
    if (!gt4) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    gt3r::Vfs vfs(&gt4->volume());
    const std::optional<Bytes> file = vfs.read("font/system.fnt");
    CHECK(file.has_value());
    if (!file) return;
    const gt3r::text::FontFile fonts = menu::read_fonts(*file);
    const std::map<std::string, menu::FontSpacing> spacing = menu::read_font_spacing(*file);
    const race::DisplayFrame frame = race::single_frame(0);
    struct Line {
        const char* font;
        float scale, x, y;
        int flags; // (pen flags; 0x100: the element aligns it by its width, then draws from its left)
        std::string text;
        std::vector<float> xs; // the glyphs' left edges
        int top;               // the first glyph's top line
    };
    const std::vector<Line> lines = {
        {"hn_20R", 1, 335, 107, 0x103, "0'30.900", {291, 305, 308, 321, 335, 340, 353, 366}, 88},
        {"hn_20R", 1, 550, 58, 0x103, "\x7F'\x7F\x7F.\x7F\x7F\x7F", {506, 519, 523, 536, 549, 555, 568, 581}, 49},
        {"hn_20R", 1, 550, 110, 0x103, "\x7F'\x7F\x7F.\x7F\x7F\x7F", {506, 519, 523, 536, 549, 555, 568, 581}, 97},
        {"FC_26", 0.8f, 135, 74, 0x103, "1/2", {109, 125.8125f, 141}, 48},
        {"FC_26", 1, 55, 67, 15, "6", {43.5f}, 46},
        {"GT4meter", 1, 581, 438, 2, "  3", {568}, 395},
    };
    for (const Line& l : lines) {
        const gt3r::text::LatinFont* f = nullptr;
        for (const auto& ff : fonts.fonts)
            if (ff.name == l.font) f = &ff;
        CHECK(f != nullptr);
        if (!f) continue;
        menu::TextStyle st;
        st.font = f, st.spacing = spacing.count(l.font) ? spacing.at(l.font) : menu::FontSpacing{};
        st.scale_x = st.scale_y = l.scale, st.one_line = true, st.valign = gt3r::text::VAlign::Top;
        const menu::TextLayout lay = menu::layout_menu_text(l.text, st, 0, 0);
        float x = l.x;
        int pen = l.flags & 15;
        if (l.flags & 0x100) x -= (l.flags & 3) == 3 ? lay.width * 0.5f : (l.flags & 3) == 2 ? lay.width : 0, pen &= 12;
        const race::TextStart at = race::text_start(frame, x, l.y, pen, lay.width, f->ascent, f->line_height, l.scale);
        CHECK(lay.glyphs.size() == l.xs.size());
        for (size_t k = 0; k < lay.glyphs.size() && k < l.xs.size(); ++k) CHECK(std::fabs(at.x + lay.glyphs[k].x - l.xs[k]) < 0.07f); // (the GS's 16ths)
        if (!lay.glyphs.empty()) CHECK(std::fabs(float(at.top) + lay.glyphs[0].y - float(l.top)) < 0.9f);
    }
}
