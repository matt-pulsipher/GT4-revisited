// Gran Turismo 4 Prologue's menus and race display (docs/formats/prologue.md "The menus", "The race
// display"): its older layouts' conditions, the title's ring, the arcade pages into a race, a
// license test into its race menus, the switch buttons' colours from its program, its display's
// layout rows; on made-up layouts, then on the Prologue disc of GT4R_DISC.

#include "menu_run_flow.h"

#include "gt4r/menu/faces_movie.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/model_stream_prologue.h"
#include "gt4r/menu/native.h"
#include "gt4r/menu/prologue_license.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/race/strobe.h"
#include "race/hud_layout.h"

#include <cmath>

namespace {

Disc* prologue_menu_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::Prologue) return d.get();
    return nullptr;
}

std::optional<Bytes> disc_file(Disc& d, const char* path) {
    const gt3r::Volume::Entry* e = d.volume().find(path);
    if (!e) return std::nullopt;
    return d.volume().read(*e);
}

} // namespace

// cond_active / cond_focus / cond_prelight / cond_insensitive (0 either, 2 off, 3 on) as the
// visible condition and its mask; the select bar of a scroll box clipped in such a file.
TEST(prologue_layout_conditions) {
    const char* text = R"(RootWindow {
        name string {"Page"}
        children [2] {
            ImageFace {
                name string {"grey"}
                cond_active digit {2}
                cond_focus digit {0}
                cond_prelight digit {0}
                cond_insensitive digit {0}
            }
            ScrollBox {
                name string {"Box"}
                children [1] {
                    SelectBar {
                        name string {"Bar"}
                        children [1] {
                            ImageFace {
                                name string {"lit"}
                                cond_active digit {3}
                                cond_focus digit {3}
                                cond_prelight digit {0}
                                cond_insensitive digit {2}
                            }
                        }
                    }
                }
            }
        }
    })";
    const menu::Widget w = menu::parse_layout(std::string_view(text));
    const menu::Widget* grey = w.child("grey");
    const menu::Widget* lit = w.child("Box/Bar/lit");
    CHECK(grey && lit);
    if (!grey || !lit) return;
    CHECK(grey->number("visible_condition") == 0 && grey->number("visible_compare") == 1);
    CHECK(lit->number("visible_condition") == 3 && lit->number("visible_compare") == 11);
    CHECK(w.child("Box/Bar")->number("clip") == 1);
    CHECK(!w.child("Box")->has("clip"));
    // A file with visible conditions of its own (Gran Turismo 4's) is left as it is.
    const menu::Widget g = menu::parse_layout(std::string_view(R"(ScrollBox { name string {"B"} children [1] { SelectBar { name string {"S"} } } })"));
    CHECK(!g.child("S")->has("clip"));
}

// Power on to the title's ring, School Mode's board, a license test asked for; the arcade's pages
// into its race (the event's grid and laps).
TEST(disc_prologue_menu_flow) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    {
        Flow f(*disc, "JP");
        f.rt->game().card.connected = false;
        f.rt->boot();
        f.rt->start_project("prologue");
        f.rt->settle();
        CHECK(f.page() == "SchoolRoot");
        CHECK(f.focus() == "SelectBar");
        // The license signs: the first two can be tried, the rest are grey (not active).
        const auto blocks = f.rt->current_page()->child("Blocks");
        CHECK(blocks && blocks->child("l0p0001") && blocks->child("l0p0003"));
        if (blocks) {
            CHECK((blocks->child("l0p0001")->state & menu::kActive) != 0);
            CHECK((blocks->child("l0p0003")->state & menu::kActive) == 0);
        }
        f.press({menu::Key::Accept}); // School Mode: the ring of signs takes the focus
        CHECK(f.focus() == "Status");
        f.press({menu::Key::Right});
        CHECK(f.focus() == "l0p0001");
        f.press({menu::Key::Accept});
        CHECK(f.page() == "EntryPopup");
        f.press({menu::Key::Accept});
        const std::optional<menu::RaceRequest> race = f.rt->race_request();
        CHECK(race.has_value());
        if (race) {
            CHECK(race->sequence == "license_race" && race->race == "l0p0001" && race->course == "L_20r60r2");
            CHECK(race->car == "demio_sport_03" && race->max_entry == 1);
            CHECK(menu::quick_project(race->sequence) == "quick");
        }
        CHECK(f.clean());
    }
    {
        Flow f(*disc, "JP");
        f.rt->game().card.connected = false;
        f.rt->boot();
        f.rt->start_project("prologue_arcade");
        f.rt->settle();
        CHECK(f.page() == "TopRoot" && f.focus() == "Start");
        f.press({menu::Key::Accept});
        for (int i = 0; i < 120; ++i) f.rt->update();
        CHECK(f.page() == "CrsSelectionRoot");
        // The five courses' pictures copied from the store house.
        const auto bar = f.rt->current_page()->child("CrsScrollBox") ? f.rt->current_page()->child("CrsScrollBox")->child("CrsSelectBar") : nullptr;
        CHECK(bar && bar->children.size() == 5);
        f.press({menu::Key::Accept});
        for (int i = 0; i < 150; ++i) f.rt->update();
        CHECK(f.page() == "CarSelectionRoot");
        // The car's figures through the formats its text faces keep as their keys (the units are the
        // region's: the app runs Prologue as Japan's, PS and kgf.m).
        const auto info = f.rt->current_page()->child("CarInfo");
        const auto power = info && info->child("TorqueInfo") ? info->child("TorqueInfo")->child("TextFace") : nullptr;
        CHECK(power != nullptr);
        if (power) CHECK(power->data.text("text").find("5600") != std::string::npos && power->data.text("text").find("rpm") != std::string::npos);
        f.press({menu::Key::Accept});
        for (int i = 0; i < 120; ++i) f.rt->update();
        f.press({menu::Key::Accept});
        for (int i = 0; i < 120; ++i) f.rt->update();
        CHECK(f.page() == "SettingPopup");
        f.focus_on("OK");
        f.press({menu::Key::Accept});
        for (int i = 0; i < 60; ++i) f.rt->update();
        const std::optional<menu::RaceRequest> race = f.rt->race_request();
        CHECK(race.has_value());
        if (race) {
            CHECK(race->sequence == "single_race" && race->course == "tsukuba" && race->car == "fairlady_240zg_71");
            CHECK(race->race == "gt4p_dirt" && race->max_entry == 1 && race->laps == 2);
        }
        CHECK(f.clean());
    }
}

// The switch buttons' colours read from the program's startup: the active base orange, the idle
// grey, and so on (the values the original's memory holds on its pre-race page).
TEST(disc_prologue_switch_colors) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    menu::RuntimeConfig config;
    config.console_card = true;
    config.program = [disc] { return disc->read_program(); };
    const std::optional<menu::SwitchColors> c = menu::prologue_switch_colors(config);
    CHECK(c.has_value());
    if (!c) return;
    auto near = [](const std::array<float, 4>& v, float r, float g, float b, float a) {
        return std::fabs(v[0] - r) < 0.002f && std::fabs(v[1] - g) < 0.002f && std::fabs(v[2] - b) < 0.002f && std::fabs(v[3] - a) < 0.002f;
    };
    CHECK(near(c->base_active, 0.902f, 0.525f, 0, 1));
    CHECK(near(c->base_idle, 0.5f, 0.5f, 0.5f, 1));
    CHECK(near(c->base_insensitive_active, 0.451f, 0.263f, 0, 1));
    CHECK(near(c->base_insensitive, 0.25f, 0.25f, 0.25f, 1));
    CHECK(near(c->text_active, 0, 0, 0, 1) && near(c->text_idle, 1, 1, 1, 1) && near(c->text_insensitive, 0.5f, 0.5f, 0.5f, 1));
    CHECK(near(c->ring_blink_off, 1, 1, 1, 1));
    // Another program (Gran Turismo 4's) has none.
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4) {
            menu::RuntimeConfig other;
            other.console_card = true;
            Disc* g = d;
            other.program = [g] { return g->read_program(); };
            CHECK(!menu::prologue_switch_colors(other).has_value());
        }
}

// The race display's rows: database/racemode_e3.db and racedetail_e3.db read, the race mode with
// the place, the lap count, the map and three time lines; the pictures under race/JP/display/.
TEST(disc_prologue_race_display) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    namespace hr = gt3r::race;
    const std::optional<Bytes> modes = disc_file(*disc, "database/racemode_e3.db"), rows = disc_file(*disc, "database/racedetail_e3.db");
    CHECK(modes && rows);
    if (!modes || !rows) return;
    CHECK(hr::set_hud_tables(*modes, *rows));
    const hr::HudRow* rank = hr::hud_row(hr::HudMode::Race, hr::HudPart::Rank, -1, 1);
    const hr::HudRow* laps = hr::hud_row(hr::HudMode::Race, hr::HudPart::LapCount, -1, 1);
    const hr::HudRow* map = hr::hud_row(hr::HudMode::Race, hr::HudPart::Map, -1, 1);
    CHECK(rank && rank->x == 32 && rank->y == 69 && rank->w == 100);
    CHECK(laps && laps->x == 32 && laps->y == 104);
    CHECK(map && map->x == 16 && map->y == 127);
    CHECK(hr::hud_row(hr::HudMode::Race, hr::HudPart::TimeLine, 3, 1) && hr::hud_row(hr::HudMode::Race, hr::HudPart::TimeLine, 4, 1) &&
          hr::hud_row(hr::HudMode::Race, hr::HudPart::TimeLine, 5, 1));
    // The license mode's three target times.
    for (int k = 0; k < 3; ++k) CHECK(hr::hud_row(hr::HudMode::License, hr::HudPart::Target, k, 1) != nullptr);
    hr::clear_hud_tables();
    CHECK(disc_file(*disc, "race/JP/display/display.imgs").has_value());
    CHECK(disc_file(*disc, "menu/pause.strb").has_value() && disc_file(*disc, "menu/result.strb").has_value());
}

// A Prologue license run's points (0x19F410 with 0x19F508 / 0x19F620 / 0x19F670) from a RACE row in Gran Turismo 4's
// columns: gold 100 x (silver - time) / (silver - gold), silver 50 x (bronze - time) / (bronze - silver), bronze 10; a test
// without a bronze time gives silver 50; one of kind 4 or more (+0x3E) 100 / 50 / 10.
#include "gt4r/menu/game_internal.h"

TEST(prologue_license_points) {
    std::vector<u8> row(0x97, 0);
    auto put = [&row](size_t k, u32 min, u32 sec, u32 ms) {
        row[0x76 + 2 * k] = u8(min), row[0x77 + 2 * k] = u8(sec);
        row[0x1A + 2 * k] = u8(ms), row[0x1B + 2 * k] = u8(ms >> 8);
    };
    put(0, 0, 12, 180), put(1, 0, 12, 200), put(2, 0, 13, 0); // l0p0002's
    CHECK(menu::prologue_license_points(row, "gold", 12000) == 100 * 200 / 20);
    CHECK(menu::prologue_license_points(row, "silver", 12190) == 50 * 810 / 800);
    CHECK(menu::prologue_license_points(row, "bronze", 12500) == 10);
    CHECK(menu::prologue_license_points(row, "failed", 14000) == 0);
    put(2, 0, 0, 0); // no bronze time
    CHECK(menu::prologue_license_points(row, "silver", 12190) == 50);
    row[0x3E] = 4;
    CHECK(menu::prologue_license_points(row, "gold", 12000) == 100);
}

// The license record in the program's layout (prologue_license.h): a result keeps the best medal, a run with a time goes
// into the ranking at its place with its name, points and date; isPassed is a medal; the units' bytes for the saved game
// read back as they were.
TEST(disc_prologue_license_record) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    Flow f(*disc, "JP");
    f.rt->game().card.connected = false;
    f.rt->boot();
    script::Vm& vm = f.rt->vm();
    const script::Value game = vm.lookup("game");
    const script::Value lic = vm.attribute(game, "license");
    CHECK(lic.p != nullptr);
    if (!lic.p) return;
    auto call = [&vm](const script::Value& o, const char* m, std::vector<script::Value> a) { return vm.call(vm.attribute(o, m), a); };
    using script::Value;
    const Value unit = call(lic, "getUnit", {Value::string("l0p0001")});
    CHECK(unit.p != nullptr);
    if (!unit.p) return;
    CHECK(call(unit, "isPassed", {}).i == 0);
    CHECK(call(unit, "getBestTime", {}).as_string() == "\x7F'\x7F\x7F.\x7F\x7F\x7F");
    call(lic, "recordResult", {Value::string("l0p0001"), Value::string("silver"), Value::integer(107900), Value::integer(30), Value::string("ABC")});
    call(lic, "recordResult", {Value::string("l0p0001"), Value::string("bronze"), Value::integer(107500), Value::integer(10), Value::string("XYZ"),
                               Value::integer(5), Value::integer(3)});
    call(lic, "recordResult", {Value::string("l0p0001"), Value::string("failed"), Value::integer(359999999), Value::integer(0)});
    CHECK(call(unit, "isPassed", {}).i == 1 && call(unit, "isCompletePassed", {}).i == 0);
    CHECK(call(unit, "getRankNum", {}).i == 2);
    CHECK(call(unit, "getBestTime", {Value::integer(0)}).as_string() == "1'47.500");
    CHECK(call(unit, "getEntryName", {Value::integer(0)}).as_string() == "XYZ");
    CHECK(call(unit, "getEntryName", {Value::integer(1)}).as_string() == "ABC");
    CHECK(call(unit, "getPoint", {Value::integer(1)}).i == 30 && call(unit, "getMaxPoint", {}).i == 30);
    CHECK(vm.attribute(unit, "detail").as_string() == "silver");
    CHECK(call(lic, "getDetailCount", {Value::string("silver")}).i == 1);
    CHECK(call(lic, "getTotalPoint", {}).i == 30);
    CHECK(call(unit, "getDate", {Value::integer(0)}).as_string().size() == 19);
    // The coded run's pass code: 30 digits, in four parts of eight; the other's none.
    const std::string code = call(unit, "getPassCode", {Value::integer(0)}).as_string();
    CHECK(code.size() == 30 && code.find_first_not_of("0123456789") == std::string::npos);
    CHECK(call(unit, "getPassCode", {Value::integer(0), Value::integer(1)}).as_string() == code.substr(8, 8));
    CHECK(call(unit, "getPassCode", {Value::integer(1)}).as_string() == "--------");
    std::vector<u8> bytes = menu::prologue_license_units(*game.as<menu::Object>());
    CHECK(bytes.size() == 48 * 0x2E0);
    if (bytes.size() != 48 * 0x2E0) return;
    auto u32_at = [&bytes](size_t o) { return u32(bytes[o]) | u32(bytes[o + 1]) << 8 | u32(bytes[o + 2]) << 16 | u32(bytes[o + 3]) << 24; };
    CHECK(u32_at(4) == 2 && u32_at(8) == 30);                                // the medal, the most points
    CHECK(u32_at(0x20) == 107500 && u32_at(0x20 + 0x48) == 107900);          // the entries' times
    CHECK(u32_at(0x20 + 2 * 0x48) == 359999999);                             // an empty one
    CHECK(std::string(reinterpret_cast<const char*>(&bytes[0x34 + 2 * 0x48])) == "--------"); // its pass code
    CHECK(bytes[0x10] == 'X' && u32_at(0x30) == 10);
    // A unit changed and read back.
    bytes[0x2E0 + 4] = 3;
    CHECK(menu::prologue_license_load(*game.as<menu::Object>(), bytes));
    CHECK(call(call(lic, "getUnit", {Value::integer(1)}), "isCompletePassed", {}).i == 1);
    CHECK(menu::prologue_license_units(*game.as<menu::Object>()) == bytes);
}

// The license results movie (menu/result_license.strb) of a passing run: its mask track (a clip depth) is not drawn but
// the texts above it are clipped by its box, the time shows, and the bright bar never covers the page.
TEST(disc_prologue_license_movie_mask) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    const std::optional<Bytes> file = disc_file(*disc, "menu/result_license.strb");
    CHECK(file.has_value() && gt4r::race::StrobeMovie::is_movie(*file));
    if (!file) return;
    auto movie = std::make_shared<const gt4r::race::StrobeMovie>(gt4r::race::StrobeMovie::parse(*file));
    int masks = 0;
    for (const auto& s : movie->sprites)
        for (const auto& t : s.tracks) masks += t.clip != 0;
    CHECK(masks > 0);
    gt4r::race::StrobePlayer p(movie, [](const std::string& name, const std::vector<gt4r::race::StrobePlayer::Value>&) {
        using V = gt4r::race::StrobePlayer::Value;
        std::string n = name;
        for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (n == "PRIZELEVEL") return V::number(0);
        if (n == "GETTIME") return V::string("0'07.867");
        if (n == "GETRANK") return V::number(0);
        if (n == "GETDEFAULTNAME") return V::string("AAA");
        return V{};
    });
    p.restart();
    bool time_shown = false, clipped_text = false;
    float largest_bar = 0;
    for (int i = 0; i < 360; ++i) {
        p.advance(1.0f / 30.0f);
        for (const gt4r::race::StrobePlayer::Draw& d : p.draw_list()) {
            if (d.text && d.words == "0'07.867") time_shown = true;
            if (d.text && d.words == "New Record" && d.clipped) clipped_text = true;
            if (d.group && d.group->fill < movie->fills.size() && movie->fills[d.group->fill].kind == 0 && movie->fills[d.group->fill].value == 0x80FFFFFFu) {
                float x0 = 1e9f, x1 = -1e9f;
                for (const auto& q : d.group->triangles) {
                    const float x = d.m[0] * q.x + d.m[2] * q.y + d.m[4];
                    x0 = std::min(x0, x), x1 = std::max(x1, x);
                }
                largest_bar = std::max(largest_bar, x1 - x0);
            }
        }
    }
    CHECK(time_shown && clipped_text);
    CHECK(largest_bar < 60); // (the mask's bar is 38 units wide before its scaling; drawn it was 225)
}

#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/db/specdb_prologue.h"
#include "gt4r/menu/game.h"

#include <map>
#include <set>

// School Mode from a fresh game through its 46 lessons: each chosen on its board, entered, its race asked for (the event's course
// and the pool's car), passed, its present shown, and the board again with the next ones open; the record kept through a save.
// The streams the new-car pages show their cars by: 14 s of cameras, the first one looking at the car.
TEST(disc_prologue_model_streams) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    for (const char* course : {"assisi", "fisco", "grandcanyon", "newyork", "paris", "tsukuba"}) {
        const std::string name = std::string("prize_") + course;
        const std::optional<Bytes> file = disc_file(*disc, menu::model_stream_path("prologue", name).c_str());
        CHECK(file.has_value());
        if (!file) continue;
        const std::optional<menu::ModelStream> s = menu::read_model_stream(*file);
        CHECK(s.has_value() && s->frames.size() == 840);
        if (!s || s->frames.empty()) continue;
        CHECK(s->frame_at(0) == 0 && s->frame_at(1) == 60 && s->frame_at(99) == 839 && !s->ended(13.9) && s->ended(14.0));
        menu::FaceView v;
        s->apply(0, v);
        CHECK(v.lights[0].on && v.lights[1].on && v.view_angle > 10 && v.view_angle < 60);
        // The view's axis passes within two meters of the car's middle.
        const float yaw = v.camera_rotate[1] * 3.14159265f / 180;
        const float dx = -std::sin(yaw), dz = -std::cos(yaw);
        CHECK(std::abs(v.camera_trans[0] * dz - v.camera_trans[2] * dx) < 2.0f);
        CHECK(v.camera_trans[0] * dx + v.camera_trans[2] * dz < 0);
    }
}

TEST(disc_prologue_school_lessons) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    Flow f(*disc, "JP");
    f.rt->game().card.connected = false;
    f.rt->boot();
    f.rt->start_project("prologue");
    f.rt->settle();
    script::Vm& vm = f.rt->vm();
    const script::Value lic = vm.attribute(vm.lookup("game"), "license");
    auto call = [&vm](const script::Value& o, const char* m, std::vector<script::Value> a) { return vm.call(vm.attribute(o, m), a); };
    auto passed = [&](const std::string& label) {
        const script::Value u = call(lic, "getUnit", {script::Value::string(label)});
        return u.p && call(u, "isPassed", {}).i != 0;
    };
    // The signs of a ring that can be tried (active).
    auto open = [&](const char* ring) {
        std::vector<std::string> out;
        const auto page = f.rt->current_page();
        const auto blocks = page ? page->child(ring) : nullptr;
        if (!blocks) return out;
        for (const auto& w : blocks->children)
            if (w->name.rfind("l0p", 0) == 0 && (w->state & menu::kActive)) out.push_back(w->name);
        return out;
    };
    std::set<std::string> done;
    std::vector<std::string> order;
    gt3r::Volume* vol = &disc->volume();
    const auto read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    db::SpecDb sdb(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "japanese");
    db::RaceEvents events(sdb, read);
    int streams = 0;
    std::map<std::string, int> presents; // the pages shown between a pass and the board
    for (int step = 0; step < 60 && done.size() < 46; ++step) {
        CHECK(f.page() == "SchoolRoot");
        if (f.page() != "SchoolRoot") break;
        // The next sign open and not yet passed.
        std::string next;
        const char* ring = "";
        for (const char* r : {"Blocks", "UraBlocks"})
            for (const std::string& l : open(r))
                if (next.empty() && !done.count(l)) next = l, ring = r;
        if (next.empty()) {
            std::printf("  no sign open after %zu lessons\n", done.size());
            break;
        }
        if (f.focus() == "SelectBar") f.press({menu::Key::Accept});
        f.focus_on(next);
        f.press({menu::Key::Accept});
        CHECK(f.page() == "EntryPopup");
        f.press({menu::Key::Accept});
        const std::optional<menu::RaceRequest> race = f.rt->race_request();
        CHECK(race.has_value());
        if (!race) {
            std::printf("  %s: no race (page %s, focus %s)\n", next.c_str(), f.page().c_str(), f.focus().c_str());
            break;
        }
        CHECK(race->race == next && race->sequence == "license_race");
        if (race->race != next) {
            std::printf("  %s: the race of %s\n", next.c_str(), race->race.c_str());
            break;
        }
        menu::RaceResult r;
        r.kind = menu::RaceResult::Kind::License;
        r.race = next, r.course = race->course, r.result = "bronze", r.time_ms = 109301, r.name = "ABC";
        f.rt->race_finished(r);
        done.insert(next);
        CHECK(passed(next));
        menu::GameState& g = f.rt->game();
        g.next_sequence.clear();
        g.last_sequence = "license_race";
        g.next_menu_project = "prologue";
        f.rt->finish();
        CHECK(f.rt->advance());
        std::string shown;
        for (int i = 0; i < 6000 && f.page() != "SchoolRoot"; ++i) {
            f.rt->update();
            if (f.page() == "PrizeRoot" && streams < 2) {
                // The new-car page's film and the stream its car is shown by.
                const auto page = f.rt->current_page();
                const auto back = page ? page->child("MpegBack") : nullptr, front = page ? page->child("MpegFront") : nullptr;
                const auto car = page ? page->child("CarFace") : nullptr;
                if (back && front && car && !car->data.text("model_stream").empty() && !front->data.text("movie_name").empty()) {
                    const std::string film = back->data.text("movie_name"), stream = car->data.text("model_stream");
                    CHECK(film == front->data.text("movie_name") && f.rt->movie_expand_alpha);
                    CHECK(disc_file(*disc, menu::movie_path("prologue", film).c_str()).has_value());
                    CHECK(disc_file(*disc, menu::model_stream_path("prologue", stream).c_str()).has_value());
                    ++streams;
                }
            }
            if (i % 300 == 299 && f.page() != "SchoolRoot") {
                if (shown.empty() || shown.substr(shown.rfind(' ') + 1) != f.page()) shown += " " + f.page(), ++presents[f.page()];
                f.rt->key(f.page() == "MovieRoot" ? menu::Key::Start : menu::Key::Cancel);
            }
        }
        f.rt->settle();
        for (int i = 0; i < 60; ++i) f.rt->update();
        order.push_back(next);
        // (the event's own course and its pool's first car)
        const std::optional<db::RaceEvent> e = events.event(next);
        CHECK(e.has_value());
        if (e) {
            CHECK(race->course == e->course);
            const std::vector<db::Opponent> pool = events.pool(*e);
            CHECK(!pool.empty() && pool[0].car == race->car);
        }
    }
    for (const auto& [page, n] : presents) std::printf("  %s shown after %d lessons\n", page.c_str(), n);
    std::printf("  %zu lessons passed; the order:", done.size());
    for (const std::string& l : order) std::printf(" %d", std::atoi(l.c_str() + 3));
    std::printf("\n");
    CHECK(done.size() == 46);
    CHECK(streams == 2);
    CHECK(!f.rt->movie_expand_alpha);
    // A car for every lesson, an emblem's page after lessons 6, 12, 19, 25 and the last, the ending movie after 25.
    CHECK(presents["PrizeRoot"] == 46 && presents["StatusRoot"] == 5 && presents["MovieRoot"] == 1);
    CHECK(call(lic, "getDetailCount", {script::Value::string("bronze")}).i == 46);
    // The record saved and read back into a new game: every lesson still passed and its sign open.
    const std::vector<u8> bytes = menu::prologue_license_units(*vm.lookup("game").as<menu::Object>());
    Flow again(*disc, "JP");
    again.rt->game().card.connected = false;
    again.rt->boot();
    script::Vm& vm2 = again.rt->vm();
    CHECK(menu::prologue_license_load(*vm2.lookup("game").as<menu::Object>(), bytes));
    const script::Value lic2 = vm2.attribute(vm2.lookup("game"), "license");
    for (const std::string& l : order) {
        const script::Value label[1] = {script::Value::string(l)};
        const script::Value u = vm2.call(vm2.attribute(lic2, "getUnit"), label);
        CHECK(u.p && vm2.call(vm2.attribute(u, "isPassed"), std::span<const script::Value>{}).i != 0);
    }
    again.rt->start_project("prologue");
    again.rt->settle();
    size_t signs = 0;
    for (const char* ring : {"Blocks", "UraBlocks"})
        if (const auto page = again.rt->current_page(); page && page->child(ring))
            for (const auto& w : page->child(ring)->children) signs += w->name.rfind("l0p", 0) == 0 && (w->state & menu::kActive);
    CHECK(signs >= 46); // (and one sign without a lesson)
    CHECK(f.clean() && again.clean());
}

// The title left alone (its watcher's 30 checks, go_demo: next_sequence "autodemo"): Prologue's time-out (0x10A1F0) plays its
// own list, the disc's recordings and its opening movie by turns, five a time-out (setNextDemo, 0x3FDD18: four tries), a key
// ending it, then the project the title is in (not Gran Turismo 4's arcade, which this disc has not).
TEST(disc_prologue_title_timeout) {
    Disc* disc = prologue_menu_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    Flow f(*disc, "JP");
    f.rt->game().card.connected = false;
    f.rt->boot();
    f.rt->start_project("prologue");
    f.rt->settle();
    CHECK(f.page() == "SchoolRoot");
    script::Vm& vm = f.rt->vm();
    const script::Value go = vm.lookup("PrologueProject::SchoolRoot::go_demo");
    CHECK(go.p && go.p->callable());
    if (!go.p) return;
    const script::Value context[1] = {f.rt->context()};
    vm.call(go, context);
    f.rt->settle();
    const std::optional<menu::RaceRequest> r = f.rt->race_request();
    CHECK(f.rt->finished() && r && r->sequence == "autodemo");
    CHECK(f.rt->project() && f.rt->project()->name() == "prologue"); // (what the menus go back to)
    menu::GameState& g = f.rt->game();
    std::vector<std::string> seen;
    auto timeout = [&](bool skip) {
        seen.clear();
        for (auto step = menu::autodemo_step(*f.rt, true); step; step = menu::autodemo_step(*f.rt, false)) {
            if (!step->replay.empty()) CHECK(f.rt->reader()(step->replay).has_value());
            seen.push_back(step->replay.empty() ? step->project : step->replay);
            g.menu_result = skip ? "terminate" : "";
        }
    };
    timeout(false);
    std::string line;
    for (const std::string& s : seen) line += " " + s;
    std::printf("  a time-out:%s\n", line.c_str());
    CHECK(seen.size() == 5);
    if (seen.size() == 5) {
        CHECK(seen[0] == "replay/JP/demo_evo4_gc/demo_evo4_gc" && seen[1] == "prologue_opening");
        CHECK(seen[2] == "replay/JP/demo_gt40_as/demo_gt40_as" && seen[3] == "prologue_opening" && seen[4] == "replay/JP/demo_ny_gtr/demo_ny_gtr");
    }
    timeout(true); // the list goes on where it was; a key ends the time-out
    CHECK(seen.size() == 1 && !seen.empty() && seen[0] == "prologue_opening");
    CHECK(f.clean());
}
