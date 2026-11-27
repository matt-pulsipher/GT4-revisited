// The save editor on the disc's own game (menu/save_editor*.h): every list read by the scripts, the changes made by the game's
// natives, the save written and read back by our game, by the menus from power on and by the disc's own program
// (tools/gt4diff/save_check).

#include "menu_run_flow.h"

#include "orig.h"
#include "save_check.h"

#include "gt4r/formats/db/specdb.h"
#include "gt4r/menu/save_editor.h"
#include "gt4r/menu/save_editor_prologue.h"
#include "gt4r/save/codec.h"
#include "gt4r/save/memory_card.h"
#include "gt4r/save/prologue_data.h"
#include "gt4r/save/save_edit.h"

#include <cstdio>
#include <filesystem>
#include <random>

using menu::SaveEditor;
namespace fs = std::filesystem;

namespace {

Disc* prologue_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::Prologue) return d.get();
    return nullptr;
}

menu::FileReader reader(Disc& d) {
    return [&d](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = d.volume().find(path);
        if (!e) return std::nullopt;
        return d.volume().read(*e);
    };
}
menu::RuntimeConfig config(Disc& d) {
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
    return c;
}

// The disc's database, a new game on a card folder, and the editor over it.
struct Edit {
    Disc& disc;
    fs::path dir;
    db::SpecDb sdb;
    std::optional<save::SaveDocument> doc;
    std::unique_ptr<SaveEditor> editor;

    Edit(Disc& d, const char* name) : disc(d), dir(fs::temp_directory_path() / name), sdb(reader(d), config(d).list, "american") {
        fs::remove_all(dir);
        fs::create_directories(dir);
        save::GameSave card((dir / "memorycard").string(), save::database_codes(sdb), reader(d));
        menu::GameState s;
        s.option_defaults("us");
        s.username = "Driver", s.money = 10'000;
        s.monitor_params.assign(0x2C, 1); // (a game's are its screen's)
        if (card.save_new(s, "Game Data") != save::Result::Ok) return;
        editor = std::make_unique<SaveEditor>(reader(d), config(d));
        open();
    }
    bool open() {
        std::string why;
        const auto place = save::edit_place((dir / "memorycard").string(), "BASCUS-97328", &why);
        if (place) doc = save::SaveDocument::open(*place, save::database_codes(sdb), "BASCUS-97328", &why);
        if (!doc) {
            std::printf("  %s\n", why.c_str());
            return false;
        }
        editor->start(doc->state);
        return true;
    }
    bool changed() {
        doc->state = editor->state();
        return doc->changed();
    }
    bool write() {
        doc->state = editor->state();
        const save::SaveDocument::Written w = doc->write((dir / "backups").string());
        if (!w.ok) std::printf("  %s\n", w.message.c_str());
        return w.ok;
    }
    // As the disc's program reads the files on disk.
    bool program_reads() {
        const auto files = save::read_files(doc->place(), "BASCUS-97328");
        if (!files) return false;
        std::vector<int> slots;
        const menu::GameState& s = doc->state;
        for (size_t i = 0; i < s.garage.size(); ++i)
            if (s.garage[i].used && s.garage[i].record && int(i) != s.riding_car) slots.push_back(int(i));
        const diff::SaveCheck r = diff::check_save(disc, files->game, files->garage, slots);
        if (!r.ok())
            std::printf("  the program: ran %d, deciphered %d, valid %d, %zu bytes written differently (first 0x%zX), garage %d, %zu slots bad\n", r.ran, r.decoded, r.valid, r.differ, r.first,
                        r.garage, r.slots_bad.size());
        return r.ok() && r.money == s.money;
    }
};

} // namespace

TEST(disc_save_edit_lists) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Edit e(*disc, "gt4r-test-saveedit-lists");
    CHECK(e.doc && e.editor);
    if (!e.doc) return;
    SaveEditor& ed = *e.editor;
    CHECK(!e.changed()); // (opening and reading every list changes nothing)
    CHECK(ed.cars().size() == 722 && ed.licenses().size() == 6 && ed.missions().size() == 34 && ed.wheels().size() > 100 && ed.wings().size() > 10);
    size_t events = 0, races = 0, prizes = 0;
    for (const SaveEditor::EventList& l : ed.events())
        for (const SaveEditor::Event& ev : l.events) ++events, races += ev.races.size(), prizes += !ev.prize.empty();
    std::printf("  %zu events, %zu races, %zu with a prize car, %zu presents, %zu courses\n", events, races, prizes, ed.presents().size(), ed.courses().size());
    CHECK(events == 143 && races == 522);
    CHECK(prizes > 100 && ed.presents().size() > 20 && ed.courses().size() > 60);
    for (const SaveEditor::License& l : ed.licenses()) CHECK(l.tests.size() == (l.category == "l0c" ? 5u : 16u));
    for (const SaveEditor::CarKind& k : ed.cars()) CHECK(!k.name.empty() && !k.maker.empty());
    // Every page's reading of it changes nothing either.
    ed.completion(), ed.aspec_points(), ed.bspec(), ed.course_records(), ed.presents_waiting();
    for (const SaveEditor::License& l : ed.licenses()) ed.license_held(l);
    CHECK(!e.changed());
}

TEST(disc_save_edit_career_and_records) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Edit e(*disc, "gt4r-test-saveedit-career");
    CHECK(e.doc.has_value());
    if (!e.doc) return;
    SaveEditor& ed = *e.editor;
    ed.set_name("Someone"), ed.set_money(123'456'789);
    CHECK(ed.state().username == "Someone");
    ed.set_name("");
    CHECK(ed.state().username == "Someone");
    // Days: on and back, a diary day each.
    ed.set_days(40);
    CHECK(ed.days() == 40 && ed.state().diary.size() == 40);
    ed.set_days(30);
    CHECK(ed.days() == 30 && ed.state().diary.size() == 30);
    ed.set_days(0);
    CHECK(ed.days() == 0 && ed.state().diary.empty());
    ed.set_days(100);
    // The B-spec driver.
    const SaveEditor::BSpec none = ed.bspec();
    CHECK(none.skill == 0);
    for (int k = 0; k < SaveEditor::kCarClasses; ++k) ed.set_bspec_class_level(k, 100);
    ed.set_bspec_course_level(100);
    const SaveEditor::BSpec most = ed.bspec();
    // (The courses' sums are the marks', at most the program's own most: 58 courses of four marks.)
    std::printf("  the courses' sums %u and %u of %u\n", ed.state().bspec_book[64 + 56], ed.state().bspec_book[128 + 56], 0xE718u);
    CHECK(ed.state().bspec_book[64 + 56] == ed.state().bspec_book[128 + 56] && ed.state().bspec_book[64 + 56] % 1020 == 0);
    std::printf("  B-spec at its most: %d, %d / %d / %d; courses %d %%\n", most.skill, most.machine, most.course, most.battle, ed.bspec_course_level());
    CHECK(most.skill >= 9'900 && most.machine >= 99 && most.course >= 99 && most.battle >= 99 && ed.bspec_course_level() == 100);
    ed.set_bspec_class_level(3, 40);
    CHECK(ed.bspec_class_level(3) == 40 && ed.bspec_class_level(2) == 100);
    // Licenses: medals and times by the tests' own limits.
    const SaveEditor::License& b = ed.licenses()[0];
    for (const SaveEditor::LicenseTest& t : b.tests) ed.set_medal(t, SaveEditor::kBronze);
    CHECK(ed.license_held(b) && ed.medal(b.tests[0]) == SaveEditor::kBronze && ed.best_time(b.tests[0]) == b.tests[0].limit[SaveEditor::kBronze]);
    ed.set_medal(b.tests[0], SaveEditor::kGold);
    CHECK(ed.best_time(b.tests[0]) == b.tests[0].limit[SaveEditor::kGold]);
    ed.set_medal(b.tests[0], SaveEditor::kSilver); // (the gold time leaves the ranking)
    CHECK(ed.medal(b.tests[0]) == SaveEditor::kSilver && ed.best_time(b.tests[0]) == b.tests[0].limit[SaveEditor::kSilver]);
    CHECK(ed.set_time(b.tests[1], b.tests[1].limit[SaveEditor::kGold] - 500) && ed.medal(b.tests[1]) == SaveEditor::kGold && ed.best_time(b.tests[1]) == b.tests[1].limit[SaveEditor::kGold] - 500);
    CHECK(!ed.set_time(b.tests[1], b.tests[1].limit[SaveEditor::kBronze] + 1));
    ed.set_medal(b.tests[2], SaveEditor::kNone);
    CHECK(ed.medal(b.tests[2]) == SaveEditor::kNone && ed.best_time(b.tests[2]) == menu::kNoTime && !ed.license_held(b));
    // Arcade.
    ed.open_all_courses(true), ed.open_all_cars(true);
    CHECK(ed.cars_open() == ed.cars().size());
    CHECK(e.write() && e.program_reads());
    // Read again: what was set.
    CHECK(e.open());
    CHECK(ed.state().username == "Someone" && ed.state().money == 123'456'789 && ed.days() == 100 && ed.medal(ed.licenses()[0].tests[0]) == SaveEditor::kSilver && ed.cars_open() == ed.cars().size());
    CHECK(ed.bspec_class_level(3) == 40);
}

TEST(disc_save_edit_events_and_prizes) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Edit e(*disc, "gt4r-test-saveedit-events");
    CHECK(e.doc.has_value());
    if (!e.doc) return;
    SaveEditor& ed = *e.editor;
    CHECK(ed.completion() == 0 && ed.presents_waiting() == 0);
    // One event won: no prize car waits until it is asked for.
    const SaveEditor::Event& sunday = ed.events()[0].events[0];
    ed.set_won(sunday, true);
    CHECK(ed.won(sunday) && !ed.prize_waiting(sunday) && ed.presents_waiting() == 0 && ed.completion() > 0);
    ed.set_prize_waiting(sunday, true);
    CHECK(ed.prize_waiting(sunday) && ed.presents_waiting() == 1);
    // A championship's own record.
    const SaveEditor::Event* cup = nullptr;
    for (const SaveEditor::Event& ev : ed.events()[1].events)
        if (ev.championship && !cup) cup = &ev;
    CHECK(cup != nullptr);
    ed.set_won(*cup, true);
    CHECK(ed.won(*cup) && !ed.prize_waiting(*cup));
    ed.set_place(*cup, 0, 2);
    CHECK(ed.place(cup->races[0].unit) == 2 && ed.won(*cup));
    CHECK(e.write() && e.program_reads());
    // The menus from power on: the save loads, Gran Turismo mode opens on the present page with that one car.
    {
        Flow f(*disc, "US", e.dir.string());
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        for (int i = 0; i < 6 && !f.rt->finished(); ++i) f.press({f.page() == "MovieRoot" ? menu::Key::Start : menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->game().username == "Driver");
        CHECK(f.rt->advance());
        f.rt->settle();
        for (int i = 0; i < 6 && f.page() == "TopRoot" && f.focus() != "GTmode"; ++i) f.press({menu::Key::Down});
        for (int i = 0; i < 4 && !f.rt->finished(); ++i) f.press({menu::Key::Accept});
        CHECK(f.rt->finished() && f.rt->wanted_project() == "gtmode" && f.rt->advance());
        f.rt->settle(3);
        std::printf("  from power on: %s\n", f.page().c_str());
        CHECK(f.page() == "PresentRoot");
        const script::Value list = f.rt->vm().lookup("GTmodeProject::PresentRoot::current_list");
        CHECK(list.items() && list.items()->size() == 1);
        for (int k = 0; k < 900; ++k) f.rt->update();
        CHECK(f.rt->game().garage_count() == 1 && f.rt->game().garage[0].car == sunday.prize);
        CHECK(f.clean());
    }
    // Received here instead: the same car, a day gone by, nothing waiting.
    const int days = ed.days();
    CHECK(ed.receive_presents() == 1 && ed.state().garage_count() == 1 && ed.state().garage[0].car == sunday.prize && ed.days() == days + 1 && ed.presents_waiting() == 0);
    CHECK(ed.state().total_prize_cars == 1 && ed.state().available_cars.count(sunday.prize) == 1);
    // Everything won: 100 %.
    ed.win_all(true);
    for (const SaveEditor::Mission& m : ed.missions()) ed.set_mission(m, 0);
    for (const SaveEditor::License& l : ed.licenses())
        for (const SaveEditor::LicenseTest& t : l.tests) ed.set_medal(t, SaveEditor::kGold);
    CHECK(ed.completion() == 1'000);
    ed.all_prizes_waiting();
    const int waiting = ed.presents_waiting(), cars = ed.state().garage_count();
    bool full = false;
    const int got = ed.receive_presents(&full);
    std::printf("  everything won: %d prize cars waiting, %d received\n", waiting, got);
    CHECK(got == waiting && !full && ed.state().garage_count() == cars + got && ed.presents_waiting() == 0 && got > 120);
    for (const SaveEditor::Present& p : ed.presents()) CHECK(p.ending || ed.present_given(p));
    ed.set_score(sunday.races[0].unit, 150);
    CHECK(ed.aspec_points() == 150);
    CHECK(e.write() && e.program_reads());
    ed.win_all(false);
    CHECK(!ed.won(sunday) && !ed.entered(sunday) && ed.completion() < 100);
}

// A garage slot's coding (save/codec.h encode_slot) as the disc's program reads it back (0x1CEEC0's steps), for 403 slots of
// random keys, places, stamps and records: the bit shuffle's draws on the EE's multiplier, which now and then gives one less
// than the chopped product (about one slot in 200 came out wrong while the shuffle took the exact product).
TEST(disc_save_slot_coding) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    using diff::Arg;
    db::SpecDb sdb = diff::open_db(*disc);
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    ee.hle(0x5B8450, [](diff::Ee& e) { e.set_v0(12345); }, "clock"); // (as check_save: the timer and the locks' kernel calls)
    for (u32 call = 0x5AD880; call < 0x5ADF70; call += 16) ee.hle(call, [](diff::Ee& e) { e.set_v0(1); }, "kernel");
    const u32 slot = ee.alloc(save::kSlotSize, 64), mt = ee.alloc(0x9D0, 64), lcg = ee.alloc(16, 64);
    std::mt19937 random(20261126);
    int bad = 0;
    // (First three that came out wrong: on the exact product, and with the multiplication's operands the other way round.)
    const u32 known[3][3] = {{0xE9E064EB, 422, 0x751C9383}, {0xBFA8DE34, 173, 0x5DE34173}, {0x0FA3A624, 201, 0x55322BF3}};
    for (int n = 0; n < 403; ++n) {
        const u32 key = n < 3 ? known[n][0] : random(), index = n < 3 ? known[n][1] : random() % 1000, stamp = n < 3 ? known[n][2] : random();
        Bytes record(0x4B8);
        for (u8& b : record) b = u8(random());
        const Bytes coded = save::encode_slot(ByteSpan(record.data(), record.size()), key, index, stamp);
        std::memcpy(ee.host_ptr(slot, save::kSlotSize), coded.data(), save::kSlotSize);
        ee.call(0x579420, {Arg::ptr(mt), Arg::integer(s32(key + index))});
        ee.call(0x1D4BA0, {Arg::ptr(slot), Arg::integer(s32(save::kSlotSize)), Arg::ptr(mt)});
        const u32 t = ee.r32(slot), crc = ee.r32(slot + 4);
        ee.call(0x579420, {Arg::ptr(mt), Arg::integer(s32(key ^ t))});
        ee.call(0x5792E8, {Arg::ptr(lcg), Arg::integer(s32(crc))});
        ee.call(0x5D2750, {Arg::ptr(slot + 8), Arg::integer(s32(save::kSlotSize - 8)), Arg::ptr(mt), Arg::ptr(lcg)});
        const bool good = t == stamp && u32(ee.call(0x572C90, {Arg::ptr(slot + 8), Arg::integer(s32(save::kSlotChecked))}).v0_s32()) == crc &&
                          std::memcmp(ee.host_ptr(slot + 8, 0x4B8), record.data(), 0x4B8) == 0;
        if (!good && bad++ < 5) std::printf("  key 0x%08X, slot %u, stamp 0x%08X: the program reads another record\n", key, index, stamp);
    }
    std::printf("  403 slots, %d read wrong\n", bad);
    CHECK(bad == 0);
}

TEST(disc_save_edit_garage) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Edit e(*disc, "gt4r-test-saveedit-garage");
    CHECK(e.doc.has_value());
    if (!e.doc) return;
    SaveEditor& ed = *e.editor;
    // Every car of the disc, as a purchase makes it.
    for (size_t i = 0; i < ed.cars().size(); ++i) CHECK(ed.add_car(ed.cars()[i].label, int(i % 3)) == int(i));
    CHECK(ed.state().garage_count() == 722);
    size_t figures = 0;
    for (size_t i = 0; i < ed.cars().size(); ++i) figures += ed.figures(int(i)).hp > 0;
    std::printf("  722 cars added, %zu with their power shown\n", figures);
    CHECK(figures > 690);
    // A full garage takes no more.
    while (ed.state().garage_count() < 1'000) CHECK(ed.add_car("panda_super_ie_90", 0) >= 0);
    CHECK(ed.add_car("panda_super_ie_90", 0) == -1);
    for (int i = 722; i < 1'000; ++i) ed.remove_car(i);
    CHECK(ed.state().garage_count() == 722);
    auto slot_of = [&](const char* label) {
        for (size_t i = 0; i < ed.state().garage.size(); ++i)
            if (ed.state().garage[i].used && ed.state().garage[i].car == label) return int(i);
        return -1;
    };
    // A car tuned: every kind of part it takes, each option fitted in turn.
    for (const char* label : {"impreza_wrx_sti_02", "panda_super_ie_90", "ford_gt_05", "r390_race_98"}) {
        const int slot = slot_of(label);
        CHECK(slot >= 0);
        const int stock = ed.figures(slot).hp;
        const std::vector<SaveEditor::PartKind> kinds = ed.parts(slot);
        CHECK(!ed.state().garage[size_t(slot)].record); // (looking makes no record)
        CHECK(kinds.size() > 3);
        for (const SaveEditor::PartKind& k : kinds)
            for (const SaveEditor::PartOption& o : k.options) {
                ed.fit_part(slot, k.type, o.category);
                bool fitted = false, owned = false;
                for (const SaveEditor::PartKind& now : ed.parts(slot))
                    if (now.type == k.type)
                        for (const SaveEditor::PartOption& n : now.options)
                            if (n.category == o.category) fitted = n.fitted, owned = n.owned;
                if (!fitted || !owned) std::printf("  %s: %s, %s: fitted %d, owned %d\n", label, k.name.c_str(), o.name.c_str(), fitted, owned);
                CHECK(fitted && owned);
            }
        ed.own_all_parts(slot);
        for (const SaveEditor::PartKind& k : ed.parts(slot))
            if (!k.permanent)
                for (const SaveEditor::PartOption& o : k.options) CHECK(o.owned);
        // Back as it was sold: what the last options left is undone by fitting the first again.
        for (const SaveEditor::PartKind& k : kinds) ed.fit_part(slot, k.type, k.options[size_t(k.fitted)].category);
        const int again = ed.figures(slot).hp;
        if (again != stock) std::printf("  %s: %d then %d HP\n", label, stock, again);
        CHECK(std::abs(again - stock) <= 1);
    }
    // Its settings, to both ends and back as they read.
    const int sti = slot_of("impreza_wrx_sti_02");
    ed.fit_part(sti, 3, 3), ed.fit_part(sti, 11, 3), ed.fit_part(sti, 24, 4), ed.fit_part(sti, 2, 1), ed.fit_part(sti, 10, 1), ed.fit_part(sti, 27, 1);
    const std::vector<SaveEditor::Setting> settings = ed.settings(sti);
    std::printf("  %zu settings\n", settings.size());
    CHECK(settings.size() >= 25);
    for (size_t i = 0; i < settings.size(); ++i)
        for (s32 v : {settings[i].lo, settings[i].hi}) {
            ed.set_setting(sti, settings[i], v);
            s32 now = -1;
            for (const SaveEditor::Setting& s : ed.settings(sti))
                if (s.name == settings[i].name && s.group == settings[i].group) now = s.value;
            if (now != v) std::printf("  %s: set %d, reads %d\n", settings[i].name.c_str(), v, now);
            CHECK(now == v);
        }
    // The sheets are apart; the paint is the car's.
    ed.set_sheet(sti, 1);
    ed.fit_part(sti, 3, 0);
    ed.set_sheet(sti, 0);
    for (const SaveEditor::PartKind& k : ed.parts(sti))
        if (k.type == 3) CHECK(k.options[size_t(k.fitted)].category == 3);
    ed.set_car_paint(sti, 2);
    for (int s = 0; s < 3; ++s) CHECK(ed.state().garage[size_t(sti)].record->sheet(s)[0x18] == 2);
    // The condition, the odometer.
    ed.set_odometer(sti, 12'345);
    CHECK(ed.odometer(sti) == 12'345 || ed.odometer(sti) == 12'344);
    ed.set_engine(sti, SaveEditor::kWorn);
    const int worn = ed.figures(sti).hp;
    ed.set_engine(sti, SaveEditor::kRunIn);
    CHECK(ed.engine(sti) == SaveEditor::kRunIn && worn < ed.figures(sti).hp);
    ed.set_oil_used(sti, 50), ed.set_body_worn(sti, 30), ed.set_dirt(sti, 80);
    CHECK(ed.oil_used(sti) == 50 && ed.body_worn(sti) == 30 && ed.dirt(sti) == 80);
    ed.set_wheel(sti, 0, ed.wheels()[3].label), ed.set_wing(sti, ed.wings()[0].label);
    CHECK(ed.wheel(sti, 0) == ed.wheels()[3].label && ed.wing(sti) == ed.wings()[0].label);
    // Driven; another driven then.
    ed.set_active(sti);
    const int panda = slot_of("panda_super_ie_90");
    ed.set_active(panda);
    CHECK(ed.state().riding_car == panda && ed.state().garage[size_t(sti)].ride_rank == 1);
    CHECK(e.write() && e.program_reads());
    CHECK(e.open());
    CHECK(ed.state().riding_car == panda && ed.wheel(sti, 0) == ed.wheels()[3].label && ed.dirt(sti) == 80 && ed.odometer(sti) >= 12'344);
    // Removing the car driven leaves none, as selling it does.
    ed.remove_car(panda);
    CHECK(ed.state().riding_car == -1 && ed.state().garage_count() == 721);
    CHECK(e.write() && e.program_reads());
}

TEST(disc_save_edit_prologue) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    menu::GameState s;
    s.username = "ABC";
    save::EditFiles files;
    files.game = save::write_prologue(s);
    std::optional<save::SaveDocument> doc = save::SaveDocument::from_files(files, {}, "BISCPS-15055");
    CHECK(doc.has_value());
    menu::RuntimeConfig c = config(*disc);
    c.region = "jp", c.language = "JP";
    menu::PrologueSaveEditor ed(reader(*disc), c);
    ed.start(doc->state);
    std::printf("  %zu tests\n", ed.tests().size());
    CHECK(ed.tests().size() >= 40);
    if (ed.tests().empty()) return;
    doc->state = ed.state();
    CHECK(!doc->changed());
    for (size_t i = 0; i < ed.tests().size(); ++i) {
        ed.set_medal(i, 3);
        CHECK(ed.tests()[i].medal == 3 && (!ed.tests()[i].limit[3] || ed.tests()[i].best == ed.tests()[i].limit[3]));
    }
    ed.set_medal(1, 2);
    CHECK(ed.tests()[1].medal == 2 && ed.tests()[1].best == ed.tests()[1].limit[2]);
    ed.set_medal(2, 0);
    CHECK(ed.tests()[2].medal == 0 && ed.tests()[2].best == menu::kNoTime);
    ed.set_name("xyz");
    doc->state = ed.state();
    const std::optional<save::EditFiles> out = doc->files();
    CHECK(out.has_value());
    if (!out) return;
    const diff::SaveCheck r = diff::check_prologue_save(*disc, out->game);
    if (!r.ok()) std::printf("  the program: ran %d, valid %d, %zu bytes written differently (first 0x%zX)\n", r.ran, r.valid, r.differ, r.first);
    CHECK(r.ok());
    const std::optional<save::SaveDocument> back = save::SaveDocument::from_files(*out, {}, "BISCPS-15055");
    CHECK(back && back->state.username == "XYZ");
}
