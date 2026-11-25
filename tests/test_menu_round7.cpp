// The menus, round seven: frames and corner colors, the program's clip in whole lines, a list's rows, the event's license,
// Arcade's best A-spec score.

#include "menu_run_flow.h"

#include "gt4r/menu/faces_frame.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/program_sort.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/menu/transform.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/replay_particulars.h"

#include <cmath>
#include <cstdio>
#include <cstring>

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

// A child appended beside one of its name gets a free one: the name's digits dropped, the first free number (0x25ADF8), so
// each row of a list built from one template is found by its own name.
TEST(menu_unique_child_names) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				Composite { name string {"Box"} geometry rectangle {0 0 200 200} }
				Composite { name string {"Row7"} geometry rectangle {0 0 200 20} }
			}
		}
	}
})")))->child("Top");
    rt.open_page(top, true);
    script::Vm& vm = rt.vm();
    const std::shared_ptr<menu::LiveWidget> box = top->child("Box"), row = top->child("Row7");
    for (int i = 0; i < 3; ++i) {
        const Value copy = vm.call(vm.attribute(menu::widget_value(row), "doCopy"), {});
        const Value a[2] = {rt.context(), copy};
        vm.call(vm.attribute(menu::widget_value(box), "appendChild"), a);
    }
    rt.settle();
    CHECK(box->children.size() == 3);
    if (box->children.size() != 3) return;
    std::printf("  %s %s %s\n", box->children[0]->name.c_str(), box->children[1]->name.c_str(), box->children[2]->name.c_str());
    CHECK(box->children[0]->name == "Row7" && box->children[1]->name == "Row0" && box->children[2]->name == "Row1");
}

// A scroll box's arrows follow the list in it (mListBox's slots 106 and 107): rows past the box's end, not only rows after
// the chosen one.
TEST(menu_scroll_box_arrows) {
    for (const int count : {3, 9}) {
        menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
        const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				ScrollBox { name string {"Scroll"} geometry rectangle {100 100 200 100} children [3] {
					ListBox { name string {"List"} can_focus digit {1} geometry rectangle {0 0 200 100} direction digit {1} }
					Composite { name string {"inc_arrow"} geometry rectangle {0 90 20 10} }
					Composite { name string {"dec_arrow"} geometry rectangle {0 0 20 10} }
				} }
				Composite { name string {"Row"} geometry rectangle {0 0 200 20} }
			}
		}
	}
})")))->child("Top");
        rt.open_page(top, true);
        const std::shared_ptr<menu::LiveWidget> box = top->child("Scroll"), list = box->child("List");
        script::Vm& vm = rt.vm();
        const Value lv = menu::widget_value(list);
        const Value t[2] = {rt.context(), menu::widget_value(top->child("Row"))}, n[1] = {Value::integer(count)};
        vm.call(vm.attribute(lv, "setItemTemplate"), t);
        vm.call(vm.attribute(lv, "setItemCount"), n);
        rt.set_focus(*top, list);
        for (int i = 0; i < 40; ++i) rt.update();
        auto lit = [&](const char* name) { return (box->child(name)->state & menu::kActive) != 0; };
        CHECK(lit("inc_arrow") == (count == 9) && !lit("dec_arrow"));
        for (int k = 0; k < count - 1; ++k) rt.key(menu::Key::Down);
        for (int i = 0; i < 120; ++i) rt.update();
        CHECK(!lit("inc_arrow") && lit("dec_arrow") == (count == 9));
    }
}

// The program's sort (0x2F03C8) is not stable past 16: equal items change places, and the order follows the rule all the same.
TEST(menu_program_sort) {
    std::vector<std::pair<int, int>> few, many;
    for (int i = 0; i < 10; ++i) few.emplace_back(i % 2, i);
    for (int i = 0; i < 40; ++i) many.emplace_back((i * 7) % 3, i);
    auto less = [](const auto& a, const auto& b) { return a.first < b.first; };
    menu::program_sort(few, less), menu::program_sort(many, less);
    bool stable = true;
    for (size_t i = 1; i < few.size(); ++i) CHECK(few[i - 1].first < few[i].first || few[i - 1].second < few[i].second); // (an insertion sort)
    for (size_t i = 1; i < many.size(); ++i) {
        CHECK(many[i - 1].first <= many[i].first);
        if (many[i - 1].first == many[i].first && many[i - 1].second > many[i].second) stable = false;
    }
    CHECK(!stable);
}

// A saved replay's particulars (the 256-byte file beside it): its fields, and a new title with the rest kept.
TEST(menu_replay_particulars) {
    Bytes p(0xE8, 0);
    auto put = [&](size_t at, u32 v) { std::memcpy(p.data() + at, &v, 4); };
    put(0, 444), put(8, 169), put(12, 34), put(0xD0, 11166), put(0xE0, 1), put(0xE4, 1);
    std::memcpy(p.data() + 0x10, "A run", 5), std::memcpy(p.data() + 0x50, "somewhere", 9), std::memcpy(p.data() + 0x90, "A car", 5);
    p[0xD8] = 4, p[0xD9] = 1;
    const Bytes file = save::scramble(p, 0, 0x12345678, save::FileKind::Particulars);
    CHECK(file.size() == 256 && !save::unscramble(file, 0, save::FileKind::Replay) && !save::decode_particulars(ByteSpan(file).first(192)));
    const std::optional<save::ReplayParticulars> a = save::decode_particulars(file);
    CHECK(a && a->car == 444 && a->course == (u64(34) << 32 | 169) && a->title == "A run" && a->course_label == "somewhere" && a->time_ms == 11166);
    CHECK(a && std::string(save::kReplayModes[a->mode]) == "GTMODE_LICENSE_B" && a->submode == 1 && a->spectator && a->license);
    const std::optional<Bytes> renamed = save::rename_particulars(file, "Another, with a longer name");
    CHECK(renamed && renamed->size() == 256);
    const std::optional<save::ReplayParticulars> b = renamed ? save::decode_particulars(*renamed) : std::nullopt;
    CHECK(b && b->title == "Another, with a longer name" && b->course_label == "somewhere" && b->time_ms == 11166 && b->car == 444);
}

// The race's own replay file: the row's particulars and picture in their sections, a section it does not know kept.
TEST(menu_replay_file_sections) {
    race::ReplayFile f;
    f.course = "somewhere", f.sequence = "license", f.laps = 1;
    f.cars.push_back({"a_car", 2});
    f.steps.assign(3, std::vector<race::CarState>(1));
    const race::ReplayFile bare = race::ReplayFile::decode(f.encode());
    CHECK(bare.title.empty() && bare.submode == -1 && bare.picture.empty() && bare.other.empty() && bare.steps.size() == 3);
    f.title = "A run", f.mode = "GTMODE_LICENSE_B", f.submode = 1, f.time_ms = 11166, f.bspec = true;
    f.picture.assign(64 * 64, 0xFF336699u);
    f.other.emplace_back(0x444C5052u, Bytes{1, 2, 3});
    const race::ReplayFile g = race::ReplayFile::decode(f.encode());
    CHECK(g.title == f.title && g.mode == f.mode && g.submode == 1 && g.time_ms == 11166 && g.bspec && g.picture == f.picture && g.other == f.other);
    CHECK(g.steps.size() == 3 && g.course == "somewhere");
}

// The keyboard page's text: letters go where the caret is, the arrow keys move it, backspace takes the letter before it.
TEST(menu_input_text_caret) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [1] { InputTextFace { name string {"input"} geometry rectangle {0 0 200 40} max_input_length digit {6} } }
		}
	}
})")))->child("Top");
    rt.open_page(top, true);
    const std::shared_ptr<menu::LiveWidget> input = top->child("input");
    script::Vm& vm = rt.vm();
    auto call = [&](const char* method, const char* text = nullptr) {
        const Value a[1] = {Value::string(text ? text : "")};
        vm.call(vm.attribute(menu::widget_value(input), method), text ? std::span<const Value>(a) : std::span<const Value>());
    };
    auto value = [&] { return input->data.text("value"); };
    call("putString", "ABD"), call("left"), call("putString", "C");
    CHECK(value() == "ABCD");
    call("backspace");
    CHECK(value() == "ABD");
    for (int i = 0; i < 9; ++i) call("left");
    call("backspace"), call("putString", "\xC3\x89");
    CHECK(value() == "\xC3\x89" "ABD");
    call("right"), call("backspace");
    CHECK(value() == "\xC3\x89" "BD");
    for (int i = 0; i < 9; ++i) call("right");
    call("putString", "1234");
    CHECK(value() == "\xC3\x89" "BD12");
}

// Prologue's picture faces: a palette's alpha is 0 .. 255 (255 drawn 125 of 128), the indices as they are.
TEST(menu_prologue_face_alpha) {
    gt3r::Tex1 set;
    set.textures.resize(1);
    set.textures[0].tex0.psm = 0x13, set.textures[0].tex0.tbp = 0, set.textures[0].tex0.cbp = 0x100;
    set.buffers.push_back({0, 0, 2, 0x13, 8, 1}), set.buffers.push_back({8, 0x100, 1, 0, 2, 1}), set.buffers.push_back({16, 0x200, 1, 0, 1, 1});
    const Bytes out = menu::Project::face_alpha(set, Bytes{255, 255, 255, 255, 255, 255, 255, 255, 1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 255});
    CHECK(out == (Bytes{255, 255, 255, 255, 255, 255, 255, 255, 1, 2, 3, 125, 4, 5, 6, 63, 7, 8, 9, 255}));
}

// A car face's setColorIndex is the paint it shows; a colored face's own alpha is its color's until a script sets it.
TEST(menu_face_paint_and_alpha) {
    menu::Runtime rt([](const std::string&) -> std::optional<Bytes> { return std::nullopt; });
    const std::shared_ptr<menu::LiveWidget> top = rt.start_layout(menu::parse_layout(std::string_view(R"(Project {
	name string {"TestProject"}
	children [1] {
		RootWindow {
			name string {"Top"}
			children [2] {
				CarFace { name string {"car"} }
				ColorFace { name string {"veil"} color [1] { RGBA {0 0 0 51} } }
			}
		}
	}
})")))->child("Top");
    rt.open_page(top, true);
    script::Vm& vm = rt.vm();
    const Value two[1] = {Value::integer(2)}, some[1] = {Value::number(0.5f)};
    vm.call(vm.attribute(menu::widget_value(top->child("car")), "setColorIndex"), two);
    CHECK(int(top->child("car")->data.number("car_color", -1)) == 2);
    const Value veil = menu::widget_value(top->child("veil"));
    CHECK(std::fabs(vm.call(vm.attribute(veil, "getAlpha"), {}).as_float() - 0.2f) < 1e-6f);
    vm.call(vm.attribute(veil, "setAlpha"), some);
    CHECK(vm.call(vm.attribute(veil, "getAlpha"), {}).as_float() == 0.5f && vm.attribute(veil, "alpha").as_float() == 0.5f);
}
