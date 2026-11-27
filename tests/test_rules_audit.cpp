// The scripts' natives checked against the program's routines (docs/RULES_AUDIT.md "The scripts' natives"): each test
// names the routine whose answers it holds ours to.

#include "menu_run_flow.h"

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
