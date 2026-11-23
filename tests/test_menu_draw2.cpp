// The menus' drawing as the program has it: the widgets' transforms, the cursor's aim, the
// actors and transitions, the text's layout and the car stage's motions; on made-up data, then
// on the discs of GT4R_DISC.

#include "test_framework.h"

#include "disc/core_image.h"
#include "gt4r/menu/faces_flash.h"
#include "gt4r/menu/faces_graph.h"
#include "gt4r/menu/font.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/menu/stage_motion.h"
#include "gt4r/menu/text_layout.h"
#include "gt4r/menu/transform.h"
#include "vfs/vfs.h"

#include <cmath>

using namespace gt4r;

namespace menu_draw2_test {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

std::shared_ptr<menu::LiveWidget> face_in(menu::Runtime& rt, const char* layout) {
    const std::shared_ptr<menu::LiveWidget> project = rt.start_layout(menu::parse_layout(std::string_view(layout)));
    return project->child("Top")->child("Face");
}

script::Value call(script::Vm& vm, const script::Value& self, const std::string& name, std::vector<script::Value> args = {}) {
    return vm.call(vm.attribute(self, name), args);
}
void assign(script::Vm& vm, const script::Value& self, const std::string& name, const script::Value& v) { vm.member(self, name)->set(vm, v); }

constexpr const char* kLayout = R"(Project {
    name string {"TestProject"}
    children [1] {
        RootWindow {
            name string {"Top"}
            children [1] {
                ColorFace { name string {"Face"} geometry rectangle {100 100 80 40} color [1] { RGBA {255 255 255 255} } }
            }
        }
    }
})";

} // namespace menu_draw2_test

using namespace menu_draw2_test;

// A widget's skew, scale and turn about the middle of its rectangle, then its translate.
TEST(menu_widget_transform) {
    const menu::Widget plain = menu::parse_layout(std::string_view(R"(TextFace { name string {"t"} })"));
    CHECK(menu::widget_transform(plain, {0, 0, 100, 20}).identity());
    // The title's italics: skew x -16 degrees, the top edge to the right, the middle unmoved.
    const menu::Widget italic = menu::parse_layout(std::string_view(R"(TextFace { name string {"t"} skew vector {-16 0} })"));
    const menu::Affine m = menu::widget_transform(italic, {100, 200, 200, 40});
    const menu::Point mid = m.apply(200, 220), top = m.apply(200, 200), bottom = m.apply(200, 240);
    const float shift = 20 * std::tan(16 * 0.017453292f);
    CHECK(near(mid.x, 200) && near(mid.y, 220));
    CHECK(near(top.x, 200 + shift) && near(top.y, 200));
    CHECK(near(bottom.x, 200 - shift));
    // A turn of 90 degrees is clockwise on the screen: the right middle goes to the bottom.
    const menu::Widget turned = menu::parse_layout(std::string_view(R"(ImageFace { name string {"i"} rotate digit {90} })"));
    const menu::Point right = menu::widget_transform(turned, {0, 0, 40, 40}).apply(40, 20);
    CHECK(near(right.x, 20) && near(right.y, 40));
    // A mirror and a move.
    const menu::Widget flipped = menu::parse_layout(std::string_view(R"(GraphFace { name string {"g"} scale vector {1 -1} translate vector {5 0} })"));
    const menu::Point p = menu::widget_transform(flipped, {0, 0, 10, 10}).apply(0, 0);
    CHECK(near(p.x, 5) && near(p.y, 10));
    // Composition: `first` applied, then the outer.
    menu::Affine outer;
    outer.e = 7;
    const menu::Point q = outer.after(menu::widget_transform(flipped, {0, 0, 10, 10})).apply(0, 0);
    CHECK(near(q.x, 12) && near(q.y, 10));
}

// The cursor's aim: a point two pixels inside the rectangle by `cursor_align`, and its turn.
TEST(menu_cursor_aim) {
    const menu::CursorAim centre = menu::cursor_aim(100, 100, 200, 140, 8);
    CHECK(near(centre.x, 150) && near(centre.y, 120) && near(centre.angle, -22.5f));
    const menu::CursorAim corner = menu::cursor_aim(100, 100, 200, 140, 3);
    CHECK(near(corner.x, 198) && near(corner.y, 138) && near(corner.angle, -22.5f));
    CHECK(near(menu::cursor_aim(100, 100, 200, 140, 0).angle, 45) && near(menu::cursor_aim(100, 100, 200, 140, 0).x, 102));
    CHECK(near(menu::cursor_aim(100, 100, 200, 140, 4).angle, -45) && near(menu::cursor_aim(100, 100, 200, 140, 2).angle, 0));
    // Kept on the screen.
    CHECK(near(menu::cursor_aim(600, 460, 700, 520, 3).y, 480) && near(menu::cursor_aim(600, 460, 700, 520, 3).x, 640));
}

// MBlinkActor sets a face's own alpha; MChaseActor hops a widget; MRotateActor turns it.
TEST(menu_actors_of_faces) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    script::Vm& vm = rt.vm();
    const std::shared_ptr<menu::LiveWidget> face = face_in(rt, kLayout);
    const script::Value menu_module = vm.lookup("menu");
    const script::Value blink = call(vm, menu_module, "MBlinkActor", {menu::widget_value(face)});
    assign(vm, menu::widget_value(face), "actor", blink);
    assign(vm, blink, "period", script::Value::number(0.1f));
    rt.update();
    CHECK(near(face->data.number("alpha", 1), 5.0f / 6.0f, 1e-4f) && face->opacity() == 1);
    for (int i = 0; i < 4; ++i) rt.update();
    CHECK(near(face->data.number("alpha", 1), 1.0f / 6.0f, 1e-4f));

    // Chase: 360 / (8.5 x 60) degrees a picture with the fraction dropped: 0 until it starts.
    const script::Value chase = call(vm, menu_module, "MChaseActor", {menu::widget_value(face)});
    assign(vm, menu::widget_value(face), "actor", chase);
    rt.update();
    CHECK(near(face->data.rect("geometry")->y, 100));
    assign(vm, chase, "period", script::Value::number(0.5f)); // 12 degrees a picture
    call(vm, chase, "doStart");
    for (int i = 0; i < 3; ++i) rt.update();
    CHECK(near(face->data.rect("geometry")->y, 100 - 16 * std::sin(36 * 0.017453292f), 1e-3f));
    call(vm, chase, "doStop");
    rt.update();
    CHECK(near(face->data.rect("geometry")->y, 100));

    // Rotate: a fifth of the way a picture, clockwise.
    const script::Value turn = call(vm, menu_module, "MRotateActor", {menu::widget_value(face), script::Value::number(90), script::Value::integer(1)});
    assign(vm, menu::widget_value(face), "actor", turn);
    assign(vm, turn, "out", script::Value::integer(1));
    rt.update();
    CHECK(near(face->data.number("rotate"), 18));
    call(vm, turn, "warp");
    CHECK(near(face->data.number("rotate"), 90));
}

// The cross transition: its out half takes no time; coming in, the pages from twice their size
// and invisible to their own size in 31 pictures.
TEST(menu_cross_transition) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    script::Vm& vm = rt.vm();
    face_in(rt, kLayout);
    const script::Value t = call(vm, vm.lookup("menu"), "MCrossTransition");
    assign(vm, rt.context(), "transition", t);
    call(vm, t, "panOutIn", {rt.context()});
    rt.update(); // out at once
    rt.update(); // the page: coming in
    menu::TransitionPicture p = menu::transition_picture(vm.attribute(rt.context(), "transition"));
    CHECK(near(p.page_opacity, 0) && near(p.page_scale, 2) && p.cover == 0);
    for (int i = 0; i < 15; ++i) rt.update();
    p = menu::transition_picture(vm.attribute(rt.context(), "transition"));
    CHECK(near(p.page_opacity, 0.5f) && near(p.page_scale, 1.25f));
    for (int i = 0; i < 20; ++i) rt.update();
    p = menu::transition_picture(vm.attribute(rt.context(), "transition"));
    CHECK(near(p.page_opacity, 1) && near(p.page_scale, 1));

    // The block one holds the screen in 32 x 24 blocks, the left side going first.
    const script::Value b = call(vm, vm.lookup("menu"), "MBlockTransition");
    assign(vm, rt.context(), "transition", b);
    call(vm, b, "panOutIn", {rt.context()});
    p = menu::transition_picture(vm.attribute(rt.context(), "transition"));
    CHECK(p.blocks && p.blocks->size() == 32 * 24 && p.capture);
    if (p.blocks) CHECK((*p.blocks)[0] < (*p.blocks)[31] && (*p.blocks)[0] >= 1);
}

// The text: a pair's spacing, the space, the first glyph of a word, each line aligned by itself;
// measured against the original's first dialog (US disc, its font hn_14).
TEST(disc_menu_text_layout) {
    bool any = false;
    for (const auto& disc : test::discs()) {
        gt3r::Vfs vfs(&disc->volume());
        const std::optional<Bytes> file = vfs.read("font/system.fnt");
        if (!file || disc->info().serial != "SCUS_973.28") continue;
        any = true;
        const gt3r::text::FontFile fonts = menu::read_fonts(*file);
        const std::map<std::string, menu::FontSpacing> spacing = menu::read_font_spacing(*file);
        const gt3r::text::LatinFont* hn = nullptr;
        for (const auto& f : fonts.fonts)
            if (f.name == "hn_14") hn = &f;
        CHECK(hn != nullptr && spacing.count("hn_14") && spacing.at("hn_14").pair == 1 && spacing.at("hn_14").wide_advance == 12);
        if (!hn) continue;
        menu::TextStyle st;
        st.font = hn, st.spacing = spacing.at("hn_14");
        // The words' starts on the original's screen (its pixels, measured): the second line.
        const menu::TextLayout lay = menu::layout_menu_text("1498KB of free space is required to save GT4 game data.", st, 0, 0);
        std::vector<float> starts;
        float last_end = -100;
        for (const menu::PlacedGlyph& g : lay.glyphs) {
            if (g.x - last_end > 3) starts.push_back(g.x);
            last_end = g.x + g.w;
        }
        const float measured[] = {0, 57, 75, 107, 154, 169, 229, 247, 284, 318, 362};
        CHECK(starts.size() >= 11);
        for (size_t i = 0; i < 11 && i < starts.size(); ++i) CHECK(std::fabs(starts[i] - starts[0] - measured[i]) <= 1.5f);
        // Centred lines each by itself.
        st.align = gt3r::text::HAlign::Center;
        const menu::TextLayout two = menu::layout_menu_text("Wide line here\nNo", st, 300, 0);
        float min_second = 1e9f;
        for (const menu::PlacedGlyph& g : two.glyphs)
            if (g.y > two.glyphs.front().y + 5) min_second = std::min(min_second, g.x);
        CHECK(min_second > two.glyphs.front().x + 20);
        // The program's button colours.
        const gt3r::CoreImage core = gt3r::CoreImage::decode(disc->read_program());
        const std::optional<std::array<menu::TextColor, 4>> buttons = menu::find_button_colors(core.program);
        CHECK(buttons.has_value());
        if (buttons) CHECK((*buttons)[1].r > 200 && (*buttons)[1].g < 100); // the circle is red
    }
    if (!any) SKIP("set GT4R_DISC to the US disc");
}

// The car-selection stage's motions: a camera that stands still, and the light motion's named
// values with the backdrop's weights.
TEST(disc_menu_stage_motion) {
    bool any = false;
    for (const auto& disc : test::discs()) {
        gt3r::Vfs vfs(&disc->volume());
        const std::optional<Bytes> cam = vfs.read("menu/select/camera.mot"), light = vfs.read("menu/select/light.mot");
        if (!cam || !light) continue;
        any = true;
        const menu::StageMotion c = menu::StageMotion::parse(*cam);
        CHECK(c.camera.has_value() && c.fov.has_value());
        if (c.camera) {
            const auto& v = *c.camera;
            std::printf("  %s camera: eye (%.3f %.3f %.3f) at (%.3f %.3f %.3f) up (%.3f %.3f %.3f) fov %.4f\n", disc->info().serial.c_str(), v[0].constant, v[1].constant,
                        v[2].constant, v[3].constant, v[4].constant, v[5].constant, v[6].constant, v[7].constant, v[8].constant, c.fov ? c.fov->constant : 0.0f);
            CHECK(v[2].constant < -5 && v[7].constant > 0.9f);
        }
        const menu::StageMotion l = menu::StageMotion::parse(*light);
        CHECK(l.length() > 10 && l.named.count("Alpha0") && l.named.count("WhiteLevel"));
        CHECK(near(l.named_at("WhiteLevel", 0, -1), 1));
        CHECK(l.lights.size() >= 2);
        float most = 0;
        for (float t = 0; t < l.length(); t += 0.5f) most = std::max(most, l.named_at("Alpha0", t, 0));
        CHECK(most > 0.5f);
    }
    if (!any) SKIP("set GT4R_DISC");
}

// A flash face's movie: the license center's background cross-fades its pictures; the
// dealers' banners from the disc.
TEST(disc_menu_flash_movies) {
    bool any = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial != "SCUS_973.28") continue;
        any = true;
        gt3r::Vfs vfs(&disc->volume());
        const std::optional<Bytes> banner = vfs.read("piece/tuner_screen/daihatsu.strb");
        CHECK(banner && menu::FlashMovie::is_movie(*banner));
        if (!banner) continue;
        const menu::FlashMovie m = menu::FlashMovie::parse(*banner);
        CHECK(near(m.width, 640) && m.frames > 100 && m.images.size() == 2);
        const std::vector<menu::FlashQuad> first = m.at(0);
        CHECK(!first.empty());
        if (!first.empty()) CHECK(near(first.front().x0, 0) && near(first.front().x1, 640) && first.front().u0 >= 0 && first.front().u1 <= 1.01f);
        // The upper picture fades out between frames 61 and 239.
        float early = -1, late = -1;
        for (const menu::FlashQuad& q : m.at(10.0f / 60)) early = q.a;
        for (const menu::FlashQuad& q : m.at(200.0f / 60)) late = q.a;
        CHECK(early > late);
        CHECK(m.at(float(m.frames) / m.rate + 1).empty());
    }
    if (!any) SKIP("set GT4R_DISC to the US disc");
}

// A graph face's points as the scripts give them, and the segments of each type.
TEST(menu_graph_face) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const std::shared_ptr<menu::LiveWidget> face = face_in(rt, kLayout);
    const script::Value p = script::Value::array({script::Value::array({script::Value::number(0), script::Value::number(0)}),
                                                  script::Value::array({script::Value::number(0.5f), script::Value::number(1)}),
                                                  script::Value::array({script::Value::number(1), script::Value::number(0.25f)})});
    face->set("points", p);
    menu::GraphFaceSettings s = menu::graph_face_settings(face->data);
    CHECK(s.points.size() == 3 && near(s.points[1].x, 0.5f) && near(s.points[2].y, 0.25f) && !s.strip && near(s.line_width, 2));
    CHECK(menu::graph_segments(s).size() == 1); // break_line: pairs
    face->set("type", script::Value::string("line"));
    s = menu::graph_face_settings(face->data);
    CHECK(s.strip && menu::graph_segments(s).size() == 2);
}
