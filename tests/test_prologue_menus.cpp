// Gran Turismo 4 Prologue's menus and race display (docs/formats/prologue.md "The menus", "The race
// display"): its older layouts' conditions, the title's ring, the arcade pages into a race, a
// license test into its race menus, the switch buttons' colours from its program, its display's
// layout rows; on made-up layouts, then on the Prologue disc of GT4R_DISC.

#include "menu_run_flow.h"

#include "gt4r/menu/layout.h"
#include "gt4r/menu/runtime_internal.h"
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
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4) {
            menu::RuntimeConfig other;
            Disc* g = d.get();
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
