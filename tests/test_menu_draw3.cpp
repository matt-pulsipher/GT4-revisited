// The menus' drawing, round four: double-byte text (the language's double-byte font, its cells
// on the text's line), Prologue's text placed by its own rule, a keyed text face's value; on
// made-up data, then on the discs of GT4R_DISC.

#include "menu_run_flow.h"

#include "gt4r/menu/faces_car.h"
#include "gt4r/menu/faces_tooltip.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/transform.h"
#include "gt4r/menu/kanji_font.h"
#include "gt4r/menu/text_layout.h"
#include "vfs/vfs.h"

#include <cmath>

namespace menu_draw3_test {

const gt3r::text::LatinFont* font_named(const gt3r::text::FontFile& f, const char* name) {
    for (const auto& font : f.fonts)
        if (font.name == name) return &font;
    return nullptr;
}

// Which double-byte font a language's menus take: none for the US and Europe.
TEST(menu_kanji_font_by_language) {
    CHECK(menu::kanji_font_paths("US").empty() && menu::kanji_font_paths("UK").empty() && menu::kanji_font_paths("FR").empty());
    const std::vector<std::string> jp = menu::kanji_font_paths("JP");
    CHECK(jp.size() == 2 && jp[0] == "font/jp_menu.kf" && jp[1] == "font/unicode.kf");
    CHECK(menu::kanji_font_paths("TW").back() == "font/big5.kf");
}

// Prologue's card dialog: its text from the StoreHouse face's key, its characters drawn from
// unicode.kf at the font's double-byte step (17 for sw_16), each glyph by its left offset (the
// gap after a kana before "148"), as the original's picture shows them.
TEST(disc_menu_prologue_double_byte_text) {
    Disc* prologue = nullptr;
    for (const auto& disc : test::discs())
        if (disc->info().game != Game::GT4) prologue = disc.get();
    if (!prologue) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    gt3r::Vfs vfs(&prologue->volume());
    const std::optional<Bytes> fnt = vfs.read("font/system.fnt");
    std::optional<Bytes> kf;
    for (const std::string& path : menu::kanji_font_paths("JP"))
        if ((kf = vfs.read(path))) break;
    CHECK(fnt && kf);
    if (!fnt || !kf) return;
    const gt3r::text::FontFile fonts = menu::read_fonts(*fnt);
    const std::map<std::string, menu::FontSpacing> spacing = menu::read_font_spacing(*fnt);
    const gt3r::text::LatinFont* sw = font_named(fonts, "sw_16");
    CHECK(sw && spacing.count("sw_16") && spacing.at("sw_16").gt3 && spacing.at("sw_16").wide_advance == 17 && spacing.at("sw_16").pair == 0);
    if (!sw || !spacing.count("sw_16")) return;
    const menu::KanjiFont kanji = menu::KanjiFont::parse(*kf);
    menu::TextStyle st;
    st.font = sw, st.spacing = spacing.at("sw_16"), st.kanji = kanji.face_for_height(sw->kanji_height);
    CHECK(st.kanji != nullptr);
    const menu::TextLayout lay = menu::layout_menu_text("ゲームをセーブするには148KB", st, 0, 0);
    int cells = 0;
    for (const menu::PlacedGlyph& g : lay.glyphs)
        if (!g.font) {
            CHECK(std::fabs(g.x - 17.0f * float(cells)) < 0.01f && g.w == float(sw->kanji_width));
            ++cells;
        }
    CHECK(cells == 11);
    const menu::PlacedGlyph* one = nullptr;
    for (const menu::PlacedGlyph& g : lay.glyphs)
        if (g.font && g.code == '1') one = &g;
    CHECK(one && one->x > 17.0f * 11 + 2); // stands off the last kana by its left offset
    // The original's last line, "このままゲームを開始しますか?", measured 245 pixels wide.
    const menu::TextLayout last = menu::layout_menu_text("このままゲームを開始しますか?", st, 0, 0);
    CHECK(std::fabs(last.width - 245) <= 4);
    // A cell's pixels in the outlined palette: some ink, some clear.
    const int cell = st.kanji->cell(U'ゲ');
    const std::vector<u32> px = menu::cell_rgba(kanji, *st.kanji, cell, 0);
    size_t ink = 0;
    for (u32 p : px) ink += (p >> 24) > 0x80;
    CHECK(cell >= 0 && ink > 20 && ink < px.size() / 2);

    // The menus from power on: the card's dialog shows the "nocard" text of its StoreHouse.
    Flow f(*prologue, "JP");
    f.rt->game().card.connected = false;
    f.rt->boot();
    f.rt->start_project(f.rt->first_project());
    for (int i = 0; i < 120; ++i) f.rt->update(); // the card is looked at over a few frames
    // The dialog's page over the warning's.
    std::string message;
    for (const auto& page : f.rt->open_pages())
        if (const auto dialog = page->child("dialog"))
            if (const auto m = dialog->child("message"); m && !m->data.text("text").empty()) message = m->data.text("text");
    if (message.find("専用メモリーカード") == std::string::npos) {
        for (const auto& page : f.rt->open_pages()) std::printf("  open %s\n", page->name.c_str());
        for (const std::string& line : f.rt->log()) std::printf("  log %s\n", line.c_str());
    }
    CHECK(message.find("専用メモリーカード") != std::string::npos);
}

} // namespace menu_draw3_test

namespace menu_draw3_test {

// The boxes cut their children to their rectangles unless their layout says not (mBox's
// constructor); other composites do not.
TEST(menu_boxes_clip_by_default) {
    for (const char* box : {"Box", "HBox", "VBox", "FBox", "MBox", "RootWindow", "SelectBox", "ListBox"}) CHECK(menu::clips_by_default(box));
    for (const char* other : {"Composite", "ScrollBox", "ScrollWindow", "SelectBar", "TextFace", "Button", "OptionMenu", "SliderBar", "ScaleBar"})
        CHECK(!menu::clips_by_default(other));
}

// A car face keeps what its page's scripts load for it: the camera motion and the model sets.
TEST(menu_car_face_loaded_motion_and_sets) {
    menu::Widget w = menu::parse_layout(std::string_view(R"(CarFace {
        name string {"CarFace"}
        action_type string {"normal"}
        option_motion string {"image/gtmode/gtauto/car_wash_cam.mot"}
        option_motion_frame digit {120}
        model_sets string {"image/a.mdl|image/b.mdl"}
    })"));
    const menu::CarFaceSettings s = menu::car_face_settings(w);
    CHECK(!s.advanced && s.option_motion == "image/gtmode/gtauto/car_wash_cam.mot" && s.option_motion_frame == 120);
    CHECK(s.model_sets.size() == 2 && s.model_sets[1] == "image/b.mdl");
}

} // namespace menu_draw3_test

namespace menu_draw3_test {

// A tool tip: one that fits rises into the middle and stays; a longer one starts 5 % in, waits
// the face's width x 0.4 / 80 seconds, scrolls left at 80 units a second until gone, and comes
// back from below at the start.
TEST(menu_tooltip_scrolls_as_the_program) {
    menu::ToolTipScroll fit;
    fit.start(200, 640, 34);
    CHECK(fit.fits() && std::fabs(fit.x() - 220) < 0.01f && std::fabs(fit.y() - 34) < 0.01f);
    for (int i = 0; i < 60; ++i) fit.step();
    CHECK(std::fabs(fit.y()) < 0.01f && std::fabs(fit.x() - 220) < 0.01f);
    menu::ToolTipScroll tip;
    tip.start(1000, 640, 34);
    CHECK(!tip.fits() && std::fabs(tip.x() - 32) < 0.01f);
    for (int i = 0; i < 3 + 192; ++i) tip.step(); // rise, then the wait of 640 x 0.4 / 80 s
    CHECK(std::fabs(tip.x() - 32) < 0.01f);
    for (int i = 0; i < 120; ++i) tip.step(); // two seconds across: 160 units, the drawing eased behind
    CHECK(tip.x() < 32 - 140 && tip.x() > 32 - 160);
    for (int i = 0; i < 60 * 20; ++i) tip.step(); // gone, a second, back below, a second, risen
    CHECK(tip.x() > -1000);
}

} // namespace menu_draw3_test
