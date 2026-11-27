// Quick race's game unlocks nothing: every race of Gran Turismo mode's event lists starts with A-spec offered (the race menus'
// IsASpec, which asks for the event's license), every license test and mission makes its race.

#include "menu_run_flow.h"

#include "gt4r/menu/game.h"
#include "gt4r/menu/race_natives.h"
#include "gt4r/menu/save_editor.h"

#include <cstdio>

namespace {

menu::RuntimeConfig editor_config(Disc& d) {
    menu::RuntimeConfig c;
    c.list = [&d](const std::string& folder) {
        std::vector<std::string> out;
        const std::string f = !folder.empty() && folder.back() == '/' ? folder : folder + "/";
        for (const gt3r::Volume::Entry& e : d.volume().entries()) {
            if (e.path.compare(0, f.size(), f) != 0) continue;
            const size_t slash = e.path.find('/', f.size());
            std::string name = e.path.substr(f.size(), slash == std::string::npos ? slash : slash + 1 - f.size());
            if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        }
        return out;
    };
    c.program = [&d] { return d.read_program(); };
    c.region = d.info().region, c.product = d.info().product; // (as the app's menus: the program's region names the tests)
    if (const std::vector<std::string> languages = menu::project_languages(c.list, "boot"); !languages.empty()) {
        c.language = languages[0];
        if (std::find(languages.begin(), languages.end(), d.info().language) != languages.end()) c.language = d.info().language;
    }
    return c;
}

} // namespace

TEST(disc_quick_race_every_event_offers_aspec) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    const menu::FileReader read = [disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc->volume().find(path);
        return e ? std::optional<Bytes>(disc->volume().read(*e)) : std::nullopt;
    };
    menu::SaveEditor editor(read, editor_config(*disc));
    editor.start(menu::GameState{});
    // Quick race's own menus (a new game's).
    menu::Runtime rt(read, editor_config(*disc));
    rt.boot();
    menu::GameState& g = rt.game();
    const auto run = [&](const char* sequence, const std::string& race) {
        menu::RaceRequest q;
        q.sequence = sequence, q.race = race;
        menu::start_quick_work(rt, q);
        return menu::quick_work(g).get("IsASpec");
    };
    // A new game: the events that ask for a license refuse A-spec.
    int asking = 0, races = 0;
    for (const menu::SaveEditor::EventList& l : editor.events())
        for (const menu::SaveEditor::Event& e : l.events)
            for (const menu::SaveEditor::EventRace& r : e.races) ++races, asking += run("solo_event", r.label) == 0;
    menu::hold_every_license(rt);
    int refused = 0;
    std::string first;
    for (const menu::SaveEditor::EventList& l : editor.events())
        for (const menu::SaveEditor::Event& e : l.events)
            for (const menu::SaveEditor::EventRace& r : e.races)
                if (run("solo_event", r.label) != 1 && ++refused == 1) first = r.label;
    // Every license test and mission: its request completed as the menus complete it (the test's car, course and grid).
    int tests = 0, missions = 0, incomplete = 0;
    for (const menu::SaveEditor::License& l : editor.licenses())
        for (const menu::SaveEditor::LicenseTest& t : l.tests) {
            menu::RaceRequest q;
            q.sequence = "license", q.race = t.label;
            menu::complete_race_request(rt, q);
            ++tests, incomplete += q.course.empty();
        }
    for (const menu::SaveEditor::Mission& m : editor.missions()) {
        menu::RaceRequest q;
        q.sequence = "mission", q.race = m.label;
        menu::complete_race_request(rt, q);
        ++missions, incomplete += q.course.empty();
    }
    std::printf("  %d event races (%d ask for a license on a new game), %d refused with every license held (%s); %d license tests, %d missions, %d without a course\n",
                races, asking, refused, first.c_str(), tests, missions, incomplete);
    CHECK(races > 500 && asking > 50);
    CHECK(refused == 0);
    CHECK(tests >= 80 && missions > 30 && incomplete == 0);
}
