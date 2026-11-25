#pragma once

// The game's own menus run from a disc as a player goes through them (tests of the menus
// running): power on, the title, a new game, Gran Turismo mode with a car.

#include "test_framework.h"

#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/runtime.h"

#include <algorithm>
#include <functional>
#include <string>

using namespace gt4r;

namespace {

struct Flow {
    Disc& disc;
    std::unique_ptr<menu::Runtime> rt;

    explicit Flow(Disc& d, const std::string& language = "US", const std::string& data_dir = {}) : disc(d) {
        menu::Runtime::Config config;
        config.language = language;
        config.data_dir = data_dir;
        config.list = [&d](const std::string& folder) {
            std::vector<std::string> out;
            for (const gt3r::Volume::Entry& e : d.volume().entries()) {
                if (e.path.compare(0, folder.size(), folder) != 0) continue;
                const size_t slash = e.path.find('/', folder.size());
                std::string name = e.path.substr(folder.size(), slash == std::string::npos ? slash : slash + 1 - folder.size());
                if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
            }
            return out;
        };
        config.program = [&d] { return d.read_program(); };
        rt = std::make_unique<menu::Runtime>(
            [&d](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = d.volume().find(path);
                if (!e) return std::nullopt;
                return d.volume().read(*e);
            },
            config);
    }
    void press(std::initializer_list<menu::Key> keys) {
        for (menu::Key k : keys) {
            rt->key(k);
            rt->settle();
        }
    }
    std::string page() const { return rt->current_page() ? rt->current_page()->name : std::string(); }
    std::string focus() const { return rt->focused() ? rt->focused()->name : std::string(); }
    bool clean() const {
        for (const std::string& line : rt->log())
            if (line.find("script error") != std::string::npos) {
                std::printf("  %s\n", line.c_str());
                return false;
            }
        return true;
    }
    // From power on to the title page, with a memory card that has no saved game: the game
    // offers to make one (no), says what cannot be saved (continue), plays its opening movie
    // (start skips it) and ends the boot project asking for arcade.
    bool boot_to_title() {
        rt->boot();
        rt->start_project(rt->first_project());
        rt->settle();
        if (page() != "ConfirmDialog") return false;
        press({menu::Key::Left, menu::Key::Accept}); // "create game data?": no
        press({menu::Key::Right, menu::Key::Accept}); // "you cannot play GT mode": continue
        if (page() != "MovieRoot") return false;
        press({menu::Key::Start});
        if (!rt->finished() || rt->wanted_project() != "arcade") return false;
        if (!rt->advance()) return false;
        rt->settle();
        return page() == "TopRoot";
    }
    // From the title: GT mode, "make the game's data", a name of two letters, yes to every
    // question until the title is back; then GT mode's map. The name typed, "" when it failed.
    std::string new_game() {
        press({menu::Key::Down});
        if (focus() != "GTmode") return {};
        press({menu::Key::Accept, menu::Key::Accept});
        if (!rt->finished() || !rt->advance()) return {};
        rt->settle();
        press({menu::Key::Left, menu::Key::Accept, menu::Key::Left, menu::Key::Accept});
        const std::string typed = rt->current_page()->child("Keyboard")->child("input")->data.text("value");
        press({menu::Key::Right, menu::Key::Right});
        for (int i = 0; i < 6 && focus() != "key_enter"; ++i) press({menu::Key::Right});
        press({menu::Key::Accept});
        for (int i = 0; i < 24 && !rt->finished(); ++i) press({menu::Key::Accept});
        if (!rt->finished() || !rt->advance()) return {};
        rt->settle();
        for (int i = 0; i < 6 && focus() != "GTmode"; ++i) press({menu::Key::Down});
        press({menu::Key::Accept});
        if (!rt->finished() || rt->wanted_project() != "gtmode" || !rt->advance()) return {};
        rt->settle();
        return page() == "TopRoot" ? typed : std::string();
    }
    // GT mode's map with a game standing in for a saved one: credits, a car bought at Alfa
    // Romeo's and driven. True on the map.
    bool gt_mode_with_a_car(s64 credits = 500000) {
        rt->game().card.has_game_data = true, rt->game().username = "Player", rt->game().money = credits;
        rt->boot();
        rt->start_project(rt->first_project());
        rt->settle();
        if (page() == "MovieRoot") press({menu::Key::Start}); // the opening after a game is loaded
        if (!rt->finished() || !rt->advance()) return false;
        rt->settle();
        press({menu::Key::Down, menu::Key::Accept});
        if (!rt->finished() || !rt->advance()) return false;
        rt->settle();
        map_focus("DealerItaly");
        press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Right,
               menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        for (int i = 0; i < 6 && page() != "TopRoot"; ++i) press({menu::Key::Cancel});
        return page() == "TopRoot" && rt->game().garage_count() == 1 && rt->game().riding_car == 0;
    }
    // `key` pressed with every license passed (each record a license test's gold for that one press).
    void press_licensed(menu::Key key) {
        const auto before = rt->game().race_units;
        for (u32& unit : rt->game().race_units) unit = (unit & ~0xFFu) | 0x91u;
        press({key});
        rt->game().race_units = before;
    }
    // A widget of the page shown by its name (depth first), focused.
    bool focus_on(const std::string& name) {
        std::function<std::shared_ptr<menu::LiveWidget>(const std::shared_ptr<menu::LiveWidget>&)> find = [&](const std::shared_ptr<menu::LiveWidget>& w) {
            if (w->name == name) return w;
            for (const auto& c : w->children)
                if (auto f = find(c)) return f;
            return std::shared_ptr<menu::LiveWidget>();
        };
        const auto w = rt->current_page() ? find(rt->current_page()->self()) : nullptr;
        if (w) rt->set_focus(*rt->current_page(), w);
        rt->settle();
        return w != nullptr;
    }
    std::string text_of(const std::string& name) {
        std::function<std::string(const menu::LiveWidget&)> find = [&](const menu::LiveWidget& w) -> std::string {
            if (w.name == name && w.is("TextFace")) return w.data.text("text");
            for (const auto& c : w.children)
                if (std::string t = find(*c); !t.empty()) return t;
            return {};
        };
        return rt->current_page() ? find(*rt->current_page()) : std::string();
    }
    // The map's place by its name, focused.
    void map_focus(const std::string& place) {
        rt->set_focus(*rt->current_page(), rt->current_page()->child("ScrollWindow")->child("clip")->child("space")->child(place));
    }
};

// The race sequence's part around a race the menus asked for, as the program's sequence
// routines run it (docs/formats/race_flow.md): the race's menus (the sequence's quick project)
// before the run, the run itself stood in for by a made-up result, the menus after it with what
// the result routines wrote, until the scripts leave; then the menus they named.
struct Sequence {
    Flow& f;
    menu::RaceRequest request;
    std::string project;

    Sequence(Flow& flow, const menu::RaceRequest& r) : f(flow), request(r), project(menu::quick_project(r.sequence)) {}
    menu::QuickWork& work() { return menu::quick_work(f.rt->game()); }
    // The work before a race (the event's fill, 0x114250): the grid's rows, the race and course,
    // the player may drive, no prize, nothing run yet.
    void fill(int cars = 6) {
        menu::QuickWork& w = work();
        w.rows.assign(size_t(cars), menu::QuickRow{});
        for (menu::QuickRow& row : w.rows) row.car = request.car, row.paint = request.paint;
        w.set("numberOfEntries", cars);
        w.set("playerGridNumber", cars - 1);
        w.race_label = request.race, w.course_label = request.course, w.player_car = request.car;
        w.set("IsASpec", 1), w.set("prize", 0), w.set("canReplay", 0), w.set("IsSessionFinished", 0);
        w.set("Laps", std::max(1, request.laps));
    }
    // The race's menus run to their end: the command chosen ("" when they did not end).
    std::string menus(std::initializer_list<menu::Key> keys = {}) {
        f.rt->game().during_quick_menu = true;
        f.rt->start_project(project);
        f.rt->settle();
        f.press(keys);
        return f.rt->finished() ? work().take_command() : std::string();
    }
    // A made-up race: the player in `place` of six (or not finished), the others in order.
    menu::RaceResult result(int place, bool finished = true) const {
        menu::RaceResult r;
        r.race = request.race, r.course = request.course, r.finished = finished, r.place = place;
        r.time_ms = 120'000 + u32(place) * 1'500, r.distance_m = 6'000, r.garage_slot = request.garage_slot, r.score = 20;
        int other = 1;
        for (int i = 0; i < 6; ++i) {
            menu::RaceResult::Finisher c;
            c.entry = (finished && i == place) ? 0 : other++;
            if (!finished && i == 5) c.entry = 0;
            c.car = request.car, c.paint = request.paint, c.time_ms = 120'000 + u32(i) * 1'500;
            c.finished = finished || c.entry != 0;
            if (!c.finished) c.time_ms = menu::kNoTime;
            r.order.push_back(c);
        }
        return r;
    }
    // After a run: the result written, the session finished.
    s64 ran(const menu::RaceResult& r) {
        const s64 prize = f.rt->race_finished(r);
        work().set("IsSessionFinished", 1), work().set("canReplay", 1);
        return prize;
    }
    // The sequence is over: the menus go on at the project the scripts named (race_flow.md).
    bool leave() {
        menu::GameState& g = f.rt->game();
        g.during_quick_menu = false;
        g.next_sequence.clear();
        g.last_sequence = request.sequence;
        if (g.next_menu_project.empty()) g.next_menu_project = g.major_menu_project;
        if (!f.rt->advance()) return false;
        f.rt->settle();
        return true;
    }
};

Disc* gt4_disc() {
    for (const auto& disc : test::discs())
        if (disc->info().game == Game::GT4) return disc.get();
    return nullptr;
}

} // namespace
