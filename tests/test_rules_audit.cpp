// The scripts' natives checked against the program's routines (docs/RULES_AUDIT.md "The scripts' natives"): each test
// names the routine whose answers it holds ours to.

#include "menu_run_flow.h"

#include <cstdio>

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
