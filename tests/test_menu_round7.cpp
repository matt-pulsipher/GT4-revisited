// The menus, round seven: frames and corner colors, the program's clip in whole lines, a list's rows, the event's license,
// Arcade's best A-spec score.

#include "menu_run_flow.h"

#include "gt4r/menu/faces_frame.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/menu/transform.h"

#include <cmath>
#include <cstdio>

using script::Value;

// A frame's parts are the layout's frame_width x frame_height, 16 x 16 when it gives none (0x2999C0, 0x298288), whatever its
// pictures' sizes.
TEST(menu_frame_parts) {
    const menu::Widget w = menu::parse_layout(std::string_view(R"(FrameImageFace { name string {"f"} frame_width digit {21} frame_height digit {12} })"));
    const auto p = menu::frame_parts(w, 100, 24);
    CHECK(std::string(p[0].picture) == "image_path" && p[0].at.x == 21 && p[0].at.y == 12 && p[0].at.w == 58 && p[0].at.h == 0);
    CHECK(std::string(p[8].picture) == "br_image_path" && p[8].at.x == 79 && p[8].at.y == 12 && p[8].at.w == 21 && p[8].at.h == 12);
    const auto d = menu::frame_parts(menu::parse_layout(std::string_view(R"(FrameImageFace { name string {"f"} })")), 137, 32);
    CHECK(d[5].at.w == 16 && d[5].at.h == 16 && d[1].at.w == 105 && d[3].at.h == 0);
}

// Four colors: a part's corners lie between the face's (0x2979A8), and a picture's corner color counts twice (0x49A150: the
// Replay Theater's scroll thumb, {60, 60, 62} to {20, 20, 22}, shows about 120).
TEST(menu_frame_corner_colors) {
    const std::vector<menu::Rgba> face = {{60, 60, 62, 255}, {20, 20, 22, 255}, {20, 20, 22, 255}, {60, 60, 62, 255}};
    const std::vector<menu::Rgba> left = menu::frame_part_colors(face, {0, 4, 4, 100}, 8, 108);
    CHECK(left.size() == 4 && left[0].r == 60 && left[1].r == 40 && left[2].r == 40 && left[3].r == 60);
    const auto c = menu::corner_color(face[0], 1, true);
    CHECK(std::fabs(c[0] * 255 - 119.5f) < 0.1f && c[3] == 1);
    CHECK(std::fabs(menu::corner_color(face[0], 0.5f, false)[0] * 255 - 60) < 0.01f);
    // One color stays one.
    CHECK(menu::frame_part_colors({{1, 2, 3, 4}}, {0, 0, 1, 1}, 8, 8).size() == 1);
}

// The program's scissor: place and size in whole lines of a field's 224 (0x4A2DF8).
TEST(menu_clip_in_whole_lines) {
    // A regulations row, 55 high: 25.66 lines, 25 kept: the next row starts 1.4 to 3.6 below its end.
    const menu::Rect a = menu::scissor({100, 77, 300, 55}), b = menu::scissor({100, 132, 300, 55});
    CHECK(std::fabs(a.h - 25 * 480.0f / 224.0f) < 1e-3f && std::fabs(a.y - 35 * 480.0f / 224.0f) < 1e-3f);
    const float gap = b.y - (a.y + a.h);
    CHECK(gap > 2.1f && gap < 2.2f);
    // An exact number of lines comes out one less (448 / 480 rounded toward zero), except the page's own.
    CHECK(std::fabs(menu::scissor({0, 0, 640, 30}).h - 13 * 480.0f / 224.0f) < 1e-3f);
    CHECK(menu::scissor({0, 0, 640, 480}).h == 480);
    CHECK(menu::cut({0, 0, 10, 10}, {5, 5, 10, 10}).w == 5 && menu::cut({0, 0, 10, 10}, {20, 0, 5, 5}).w == 0);
}

// A list box's chosen row has the cursor's bit, not the focus (0x2B3518: the focused widget is the list).
TEST(menu_list_row_states) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				ListBox { name string {"List"} can_focus digit {1} geometry rectangle {100 100 200 100} direction digit {1} }
				Composite { name string {"Row"} geometry rectangle {0 0 200 20} }
			}
		}
	}
})")))->child("Top");
    rt.open_page(top, true);
    const std::shared_ptr<menu::LiveWidget> list = top->child("List");
    script::Vm& vm = rt.vm();
    const Value lv = menu::widget_value(list);
    const Value t[2] = {rt.context(), menu::widget_value(top->child("Row"))}, n[1] = {Value::integer(3)};
    vm.call(vm.attribute(lv, "setItemTemplate"), t);
    vm.call(vm.attribute(lv, "setItemCount"), n);
    rt.set_focus(*top, list);
    rt.update();
    rt.settle();
    rt.update();
    CHECK((list->state & menu::kFocus) != 0 && list->children.size() >= 2);
    if (list->children.size() < 2) return;
    CHECK((list->children[0]->state & (menu::kFocus | menu::kPrelight)) == menu::kPrelight && (list->children[0]->state & menu::kActive));
    CHECK((list->children[1]->state & (menu::kFocus | menu::kPrelight | menu::kActive)) == 0);
}

// Whether the player may start an event himself (MQuickWork's IsASpec, 0x110498): the license its row asks for.
TEST(disc_menu_event_asks_for_license) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->boot();
    menu::GameState& g = f.rt->game();
    menu::Database& d = menu::natives_of(*f.rt)->db();
    int asked = -1;
    std::string event, free;
    const db::Labels* labels = d.db->labels("RACE");
    CHECK(labels != nullptr);
    if (!labels) return;
    for (const auto& [label, id] : labels->entries) {
        const ByteSpan row = d.row("RACE", label);
        if (row.size() <= 0x45) continue;
        if (row[0x45] == 0 && free.empty() && label.rfind("am_", 0) == 0) free = label;
        if (row[0x45] == 1 && event.empty()) event = label, asked = row[0x45];
    }
    std::printf("  asks none: %s; asks B: %s\n", free.c_str(), event.c_str());
    CHECK(!event.empty() && asked == 1);
    auto aspec = [&](const std::string& race) {
        menu::RaceRequest q;
        q.sequence = "solo_event", q.race = race;
        menu::start_quick_work(*f.rt, q);
        return menu::quick_work(g).get("IsASpec");
    };
    CHECK(aspec(event) == 0);
    for (int i = 1; i <= 16; ++i) {
        char label[16];
        std::snprintf(label, sizeof label, "l0b%04d", i);
        const int unit = d.race_unit(label);
        CHECK(unit >= 0);
        if (unit >= 0) g.race_units[size_t(unit)] = 1 | 11 << 4 | 11 << 8; // a bronze
    }
    CHECK(aspec(event) == 1);
    if (!free.empty()) CHECK(aspec(free) == 1);
}

// An Arcade single race the player drove and finished keeps its A-spec points as the course's best (0x111E98).
TEST(disc_menu_arcade_best_score) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->boot();
    menu::GameState& g = f.rt->game();
    g.race_session.sequence = "single_race";
    auto best = [&] {
        const Value label[1] = {Value::string("tsukuba")};
        const Value record = f.rt->vm().attribute(f.rt->vm().lookup("game"), "course_record");
        const Value unit = f.rt->vm().call(f.rt->vm().attribute(record, "getUnit"), label);
        return f.rt->vm().attribute(unit, "best_score").i;
    };
    menu::RaceResult r;
    r.kind = menu::RaceResult::Kind::Course, r.course_kind = 7, r.course = "tsukuba", r.finished = true, r.score = 120, r.time_ms = 185'000;
    const u32 today = g.today;
    f.rt->race_finished(r);
    CHECK(best() == 120);
    r.score = 60;
    f.rt->race_finished(r);
    CHECK(best() == 120);
    r.score = 200, r.watched = true; // the B-spec driver's
    f.rt->race_finished(r);
    CHECK(best() == 120);
    r.watched = false, r.finished = false;
    f.rt->race_finished(r);
    CHECK(best() == 120);
    r.finished = true;
    f.rt->race_finished(r);
    CHECK(best() == 200 && g.today == today);
}
