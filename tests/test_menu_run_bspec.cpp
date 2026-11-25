// The B-spec driver's book (menu/game_bspec.h): its figures, the race's record and the record written back against
// the user's own program; the book kept in the save at the profile's +0x94 (payload 0xA4); the Status page's figures
// through MGame.

#include "menu_run_flow.h"

#include "gt4r/menu/game_bspec.h"
#include "gt4r/save/game_data.h"
#include "gt4r/sim/program.h"
#include "showroom_check.h"

#include <cstdio>

using script::Value;

namespace {

Disc* program_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(sim_program_bspec_book_equal) {
    Disc* disc = program_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::BookResult r = diff::run_bspec_book(*disc, false, 24);
    CHECK(r.ran && r.figures > 0 && r.records > 0 && r.backs > 0);
    CHECK(r.figures_equal == r.figures && r.records_equal == r.records && r.backs_equal == r.backs);
}

// The arithmetic on a book by hand: an empty book is 0 everywhere; a full one 100 / 10000; the courses' sums count
// for the course and battle figures; the class-0 column keeps its row 7 (the courses' sum) out of the record.
TEST(menu_bspec_book_figures) {
    const menu::bspec::Weights w = {8, 9, 10, 11, 12, 13, 14, 15};
    menu::bspec::Book b{};
    CHECK(menu::bspec::total_experience(b, w) == 0 && menu::bspec::car_experience(b, w) == 0 && menu::bspec::battle_experience(b, w) == 0);
    for (u16& v : b) v = 0xFFFF;
    CHECK(menu::bspec::car_experience(b, w) == 100 && menu::bspec::battle_experience(b, w) == 100 && menu::bspec::course_experience(b, w) == 100);
    CHECK(menu::bspec::total_experience(b, w) == 10000);
    menu::bspec::Book c{};
    c[64 + 56] = 59160 / 2; // the first set of courses' marks summed: half of 59160
    CHECK(menu::bspec::course_experience(c, w) == 10 && menu::bspec::battle_experience(c, w) == 0); // 20 % of 500 / 10
    for (size_t i = 0; i < c.size(); ++i) c[i] = u16(i);
    const std::array<u8, 0x40> rec = menu::bspec::race_record(c, 0, nullptr);
    CHECK(rec[7 * 2] == 0 && rec[7 * 2 + 1] == 0 && rec[6 * 2] == 48); // table 0 row 6 column 0 = 48
    const std::array<u8, 0x40> rec3 = menu::bspec::race_record(c, 3, nullptr);
    CHECK(rec3[16 + 7 * 2] == u8(64 + 7 * 8 + 3)); // table 1 row 7 column 3
}

// The book is the user profile's +0x94 .. +0x214 in the game data (payload 0xA4), kept through a save.
TEST(save_bspec_book_kept) {
    const save::Codes codes{[](u32, const std::string&) { return ~u64(0); }, [](u64) { return std::string(); }};
    menu::GameState s;
    for (size_t i = 0; i < s.bspec_book.size(); ++i) s.bspec_book[i] = u16(i * 37 + 5);
    const Bytes payload = save::write_game(s, codes);
    CHECK(payload[0xA4] == 5 && payload[0xA4 + 2] == 42);
    menu::GameState t;
    CHECK(save::read_game(payload, t, codes, nullptr));
    CHECK(t.bspec_book == s.bspec_book);
}

// MGame's figures on a made-up book: the scripts' Status page reads them.
TEST(disc_menu_bspec_figures) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    script::Vm& vm = f.rt->vm();
    for (u16& v : f.rt->game().bspec_book) v = 512;
    const Value game = vm.lookup("main::game");
    auto figure = [&](const char* name) { return vm.call(vm.attribute(game, name), {}).as_float(); };
    const float total = figure("getBspecTotalExperience"), car = figure("getBspecCarExperience"), course = figure("getBspecCourseExperience"),
                battle = figure("getBspecBattleExperience");
    std::printf("  a book of 512s: total %.0f, car %.0f, course %.0f, battle %.0f\n", double(total), double(car), double(course), double(battle));
    // A and B at half their cap (62 of 125 a class: 49), C over its cap (battle 70 % of 1000 and the courses' sum
    // 512 x 1000 / 59160 = 8), the total two thirds (6666) with the sums (86 each): 4692.
    CHECK(car == 49 && course == 34 && battle == 70 && total == 4692);
}
