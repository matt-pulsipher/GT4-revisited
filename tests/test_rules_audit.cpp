// The scripts' natives checked against the program's routines (docs/RULES_AUDIT.md "The scripts' natives"): each test
// names the routine whose answers it holds ours to.

#include "menu_run_flow.h"
#include "natives_check.h"

#include "gt4r/menu/game.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include <cstring>

using script::Value;

namespace {

Value call(script::Vm& vm, const Value& fn, std::vector<Value> args) { return vm.call(fn, args); }

size_t stream_starts(const menu::Runtime& rt) {
    size_t n = 0;
    for (const menu::SoundCall& c : rt.sound_calls()) n += c.object == "sound" && c.method == "startStreamEx";
    return n;
}

} // namespace

// The slide show's music player (projects/slide's music_player_tick) goes on to the next track when the stream it
// started has played and ended: MSound.isPlayingStream (0x23EA18) and sound.stream.isPlaying (0x2C3700) answer the
// stream's state, which the app gives the menus (Runtime::stream_playing). Answering 0 always held it on its first track.
TEST(disc_menu_stream_playing) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    bool playing = false;
    f.rt->stream_playing = [&playing] { return playing; };
    script::Vm& vm = f.rt->vm();
    CHECK(call(vm, vm.lookup("main::sound::isPlayingStream"), {}).truthy() == false);
    playing = true;
    CHECK(call(vm, vm.lookup("main::sound::isPlayingStream"), {}).truthy());
    CHECK(call(vm, vm.attribute(vm.attribute(vm.lookup("main::sound"), "stream"), "isPlaying"), {}).truthy());
    CHECK(!call(vm, vm.attribute(vm.attribute(vm.lookup("main::sound"), "midi"), "isPlaying"), {}).truthy());
    playing = false;
    f.rt->start_project("slide");
    for (int i = 0; i < 30; ++i) f.rt->update();
    const std::string at = "SlideProject::SlideRoot::";
    const Value entry = vm.lookup(at + "music_player_entry_list"), play = vm.lookup(at + "music_player_do_play"), tick = vm.lookup(at + "music_player_tick");
    CHECK(!entry.is_nil() && !play.is_nil() && !tick.is_nil());
    if (entry.is_nil() || play.is_nil() || tick.is_nil()) return;
    call(vm, entry, {Value::array({Value::integer(3), Value::integer(5)}), Value::array({Value::integer(0), Value::integer(1)}), Value::integer(0)});
    call(vm, play, {});
    const size_t before = stream_starts(*f.rt);
    call(vm, tick, {Value{}}); // the first track started
    CHECK(stream_starts(*f.rt) == before + 1);
    playing = true;
    call(vm, tick, {Value{}}); // it plays
    call(vm, tick, {Value{}});
    CHECK(stream_starts(*f.rt) == before + 1);
    playing = false;
    for (int i = 0; i < 3; ++i) call(vm, tick, {Value{}}); // it ended: the next one
    CHECK(stream_starts(*f.rt) == before + 2);
}

// The garage car's condition natives (CarGarage 0x14E9C0 .. 0x14EDD8) on the program's limits: the engine condition's table
// (0x350678 from the static block +0xD5, +0xD7: fresh oil -300,000 m, old oil 6,000,000 m) and the body's (0x366100 from
// 0x620AF4 / 0x620AF6: 500,000 to 3,000,000 m), the dirt at most 0.8 (0x3438A0).
TEST(disc_menu_car_condition_natives) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    script::Vm& vm = f.rt->vm();
    const Value car = vm.attribute(vm.attribute(vm.lookup("main::game"), "garage"), "riding_car");
    CHECK(!car.is_nil());
    if (car.is_nil()) return;
    auto m = [&](const char* name) { return call(vm, vm.attribute(car, name), {}); };
    auto set = [&](const char* name, float v) { vm.member(car, name)->set(vm, Value::number(v)); };
    auto get = [&](const char* name) { return vm.attribute(car, name).as_float(); };
    m("replaceOil");
    CHECK(get("oil") == -300000.0f && m("getOilLifeRate").as_float() == 0.0f);
    set("oil", -150000.0f);
    CHECK(m("getOilLifeRate").as_float() == 0.025f); // (150,000 / 6,000,000)
    set("oil", 3000000.0f);
    CHECK(m("getOilLifeRate").as_float() == 0.5f);
    set("oil", 7000000.0f);
    CHECK(get("oil") == 6000000.0f && m("getOilLifeRate").as_float() == 1.0f);
    m("refreshBody");
    CHECK(get("bodyLife") == 0.0f && m("getBodyLifeRate").as_float() == 0.0f);
    set("bodyLife", 1750000.0f);
    // (2 x 1,250,000 / 2,500,000 in the EE's division: one step under 1, so 0.49999997)
    CHECK(m("getBodyLifeRate").as_float() == 0.49999997f);
    set("bodyLife", 4000000.0f);
    CHECK(get("bodyLife") == 3000000.0f && m("getBodyLifeRate").as_float() == 1.0f);
    set("bodyLife", -10.0f); // (no least: the program keeps it)
    CHECK(get("bodyLife") == -10.0f && m("getBodyLifeRate").as_float() == 0.0f);
    set("dirtiness", 1.0f);
    u32 bits = 0;
    const float dirt = get("dirtiness");
    std::memcpy(&bits, &dirt, 4);
    CHECK(bits == 0x3F4CCCCCu);
}

// The calendar's natives (MCalendar.dateToDay 0x135F50 -> 0x5797A8, dayToDate 0x1360A0 -> 0x579898) equal the program's on
// every day of 1950 .. 2049 and on months past either end of the year.
TEST(sim_program_calendar_natives_equal) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::NativesResult r = diff::run_natives(*disc, false);
    CHECK(r.ran && r.dates > 40000 && r.days > 36000);
    CHECK(r.dates_equal == r.dates && r.days_equal == r.days);
    if (r.dates_equal != r.dates || r.days_equal != r.days) std::printf("  dates %zu of %zu, days %zu of %zu\n", r.dates_equal, r.dates, r.days_equal, r.days);
}

// Japan's program numbers the parts' owned bits without the suspension and the turbine kit it lacks (its 0x43F270: 94 bits)
// and offers 4 suspensions and 5 turbine kits; the rest as v1.01's.
TEST(menu_parts_owned_bits_by_release) {
    const int us[] = {-1, 0, 2, 4, 9, 11, -1, -1, 13, -1, 17, 19, -1, 23, 27, 33, 35, 37, 39, 41, 44, 48, 52, 56, 58, 64, 77, 90, 92, -1, -1, 94};
    const int jp[] = {-1, 0, 2, 4, 8, 10, -1, -1, 12, -1, 16, 18, -1, 22, 26, 31, 33, 35, 37, 39, 42, 46, 50, 54, 56, 62, 75, 88, 90, -1, -1, 92};
    for (int t = 1; t < 32; ++t) CHECK(menu::parts_owned_from(t, false) == us[t] && menu::parts_owned_from(t, true) == jp[t]);
}

// On Japan's disc the garage car's parts and condition natives are Japan's program's (the same routines as v1.01's, 0x14D850 ..
// 0x14DC68; its tables): 4 suspensions, 5 turbine kits, old oil at 5,000,000 m (the static block's +0xD7, 50).
TEST(disc_menu_japan_car_natives) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().serial.rfind("SCPS_170.01", 0) == 0) disc = d.get();
    if (!disc) SKIP("needs the Japanese Gran Turismo 4 disc (SCPS-17001) in GT4R_DISC");
    Flow f(*disc);
    f.gt_mode_with_a_car(); // (the menus up; Japan's map has no Alfa Romeo dealer: the car is given)
    script::Vm& vm = f.rt->vm();
    const Value garage = vm.attribute(vm.lookup("main::game"), "garage");
    if (f.rt->game().garage_count() == 0) {
        call(vm, vm.attribute(garage, "addNewCar"), {Value::string("panda_super_ie_90"), Value::integer(0)});
        call(vm, vm.attribute(garage, "changeRidingCar"), {Value::integer(0)});
    }
    CHECK(f.rt->game().garage_count() == 1);
    const Value car = vm.attribute(garage, "riding_car");
    CHECK(!car.is_nil());
    if (car.is_nil()) return;
    auto m = [&](const char* name, std::vector<Value> a = {}) { return call(vm, vm.attribute(car, name), a); };
    CHECK(m("getPartsTypeMax", {Value::integer(3)}).as_float() == 4 && m("getPartsTypeMax", {Value::integer(14)}).as_float() == 5);
    CHECK(m("getPartsTypeMax", {Value::integer(24)}).as_float() == 6);
    m("replaceOil");
    const float fresh = vm.attribute(car, "oil").as_float();
    vm.member(car, "oil")->set(vm, Value::number(7000000.0f));
    std::printf("  fresh oil %g m, past the old oil %g m\n", double(fresh), double(vm.attribute(car, "oil").as_float()));
    CHECK(vm.attribute(car, "oil").as_float() == 5000000.0f && m("getOilLifeRate").as_float() == 1.0f);
}
