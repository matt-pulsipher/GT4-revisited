// The console's floats (docs/specs/physics.md "Floats"): the license demonstrations, recorded on the console, through the
// program's own race step in the interpreter on the engine's EE float model end on their recorded times, and through our
// session from the program's set-up they give the same car place bit for bit; the Replay Theater's demo replays decipher
// (the cipher is the console's multiplier and divider, and its key check is exact).

#include "test_framework.h"

#include "floats.h"
#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/sim/program.h"

#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

// A standing start and a stop in the box, a moving start, a dirt and an ice course, a training test behind the pace car,
// a Super License lap.
TEST(floats_demonstrations_end_on_their_recorded_times) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    for (const char* label : {"usl0b0001", "usl0b0004", "usl0a0003", "usl0a0011", "uslib0012", "usl0b0005", "usl0s0005"}) {
        diff::FloatDemoOptions o;
        o.ours = true;
        const diff::DemoScore s = diff::run_float_demo(*disc, label, o);
        if (!s.ran) std::printf("  %s: %s\n", label, s.error.c_str());
        CHECK(s.ran);
        if (!s.ran) continue;
        std::printf("  %-10s recorded %d ms, the program's %d ms; ours: first bit apart at step %d\n", label, s.recorded_ms, s.ended_ms, s.ours_first_bits);
        CHECK(s.recorded_ms > 0);
        CHECK(s.ended_ms == s.recorded_ms);
        CHECK(s.ours_ran && s.ours_first_bits < 0);
    }
}

TEST(floats_demo_replays_decipher) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    size_t read = 0;
    const std::vector<std::string> names = diff::theater_replays(*disc);
    for (const std::string& n : names) {
        std::string err;
        const auto d = diff::theater_replay(*disc, program, n, &err);
        if (!d) std::printf("  %s: %s\n", n.c_str(), err.c_str());
        else if (!d->streams.empty() && !d->entries.empty()) ++read;
    }
    std::printf("  %zu of %zu replays read\n", read, names.size());
    CHECK(names.size() >= 13);
    CHECK(read == names.size());
}

// The race step runs no VU0 arithmetic (a demonstration with its car, a pace car and the computer driver).
TEST(floats_race_step_runs_no_vu0) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    diff::FloatCensus c;
    diff::FloatDemoOptions o;
    o.on_setup = [&c](gt3r::diff::Ee& ee) { c.attach(ee); };
    const diff::DemoScore s = diff::run_float_demo(*disc, "usl0b0005", o);
    CHECK(s.ran);
    if (!s.ran) return;
    gt3r::u64 vu = 0, mul = 0, div = 0;
    for (const auto& [k, n] : c.by_kind) {
        if (k.rfind("vu.", 0) == 0) vu += n;
        if (k == "fpu.mul") mul = n;
        if (k == "fpu.div") div = n;
    }
    std::printf("  %d steps: %llu FPU multiplies, %llu divides, %llu VU0 operations\n", s.ended_step + 1, (unsigned long long)mul, (unsigned long long)div,
                (unsigned long long)vu);
    CHECK(vu == 0);
    CHECK(mul > 0 && div > 0);
}
