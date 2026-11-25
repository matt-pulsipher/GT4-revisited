// The race step against the original's saved states (tools/gt4diff states.cpp): a pair of the reference's states of the
// Grand Canyon standing start (the Ford Ka and the Volkswagen Beetle, Cross held) run through the program's own race step
// from state 0's memory reproduces state 1, and our session from state 0 stays with the program's run; and a pair at
// Tsukuba (MT, Driving Aids None) with Cross let go 62 steps in. Reads the Gran Turismo 4 disc of GT4R_DISC and the states'
// extracted main memories (eeMemory.bin, one folder per state) under GT4R_SIM_STATES (default
// ~/gt4re-out/sim-step/states/<set>/<state>/); skips without them.
//
// The states were made by an emulator whose float arithmetic is not the PlayStation 2's: the program's run in the shared
// engine's interpreter (the PlayStation 2's arithmetic) differs from them in the last bits, which the chaotic parts of the
// step (wheelspin, the dampers) amplify to tenths of millimetres over these pairs; with a float model of that emulator
// loaded as the interpreter's oracle (GT3R_FLOAT_ORACLE) every record byte is equal, and the test asks for that.

#include "test_framework.h"

#include "states.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace gt4r;

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

std::string states_dir() {
    if (const char* e = std::getenv("GT4R_SIM_STATES")) return e;
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/gt4re-out/sim-step/states" : std::string();
}

bool have(const std::string& dir, std::initializer_list<const char*> states) {
    for (const char* s : states)
        if (!std::filesystem::exists(std::filesystem::path(dir) / s / "eeMemory.bin")) return false;
    return true;
}

} // namespace

TEST(sim_states_program_step_reproduces_the_original) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::string dir = states_dir();
    const char* const a = "grand_canyon_standing_start/launch_cross_08";
    const char* const b = "grand_canyon_standing_start/launch_cross_09";
    if (dir.empty() || !have(dir, {a, b})) SKIP("needs the reference's saved states (GT4R_SIM_STATES)");
    const bool pcsx2_floats = std::getenv("GT3R_FLOAT_ORACLE") != nullptr;
    diff::StatesOptions options;
    const diff::StatesResult r = diff::run_states(*disc, dir + "/" + a, dir + "/" + b, options, false);
    CHECK(r.ran);
    if (!r.ran) {
        std::printf("  %s\n", r.error.c_str());
        return;
    }
    CHECK(r.course == "grandcanyon");
    CHECK(r.frames == 38);            // the boot counters' difference
    CHECK(r.best_frames == r.frames); // one race step a frame
    CHECK(r.frame_matches);
    CHECK(r.cars.size() == 2);
    for (const diff::StatesCar& c : r.cars) {
        // The program's run against state 1.
        CHECK(c.position_error < 1e-4);
        CHECK(c.speed_error < 1e-3);
        if (pcsx2_floats) CHECK(c.bytes_off == 0);
        // Ours from state 0 against the program's run (the computer car on the program's drivers' outputs).
        CHECK(c.end_position_error < 1e-3);
        CHECK(std::fabs(c.end_speed_ours - c.end_speed_program) < 1e-2);
    }
    CHECK(r.cars[0].label == "ka_01" && r.cars[0].player);
    std::printf("  grand canyon 08 -> 09: %d steps, the Ka %.6f m and %.6f m/s from state 1, ours %.6f m from the program's run\n", r.frames,
                r.cars[0].position_error, r.cars[0].speed_error, r.cars[0].end_position_error);
}

TEST(sim_states_throttle_release_at_a_pinned_step) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::string dir = states_dir();
    const char* const a = "tsukuba_mt_aids_off/main_straight_mt_1";
    const char* const b = "tsukuba_mt_aids_off/main_straight_mt_2";
    if (dir.empty() || !have(dir, {a, b})) SKIP("needs the reference's saved states (GT4R_SIM_STATES)");
    const bool pcsx2_floats = std::getenv("GT3R_FLOAT_ORACLE") != nullptr;
    // The keyboard pad's frames as the original's controller had them.
    std::array<u8, 0x20> cross{}, none{};
    CHECK(diff::keys_frame("cross", cross));
    CHECK(diff::keys_frame("none", none));
    CHECK(cross[0] == 2 && cross[3] == 6 && cross[8] == 0 && cross[9] == 0x10);
    CHECK(none[0] == 0 && none[3] == 6);
    // Cross held 62 steps from state 1, then let go (the reference polled the release between those boot frames).
    diff::StatesOptions options;
    options.keys = "cross:62,none";
    options.ours = false;
    const diff::StatesResult r = diff::run_states(*disc, dir + "/" + a, dir + "/" + b, options, false);
    CHECK(r.ran);
    if (!r.ran) return;
    CHECK(r.frames == 92);
    CHECK(r.frame_matches);
    CHECK(r.cars.size() == 6);
    // (the Ka within 0.1 mm; let go a step early or late it ends 9 mm off; the computer cars within a centimetre)
    for (const diff::StatesCar& c : r.cars) {
        CHECK(c.position_error < (c.player ? 1e-4 : 1e-2));
        if (pcsx2_floats) CHECK(c.bytes_off == 0);
    }
}
