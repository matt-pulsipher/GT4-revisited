// The computer drivers against the user's own program (gt4r_diff ai): the program's race run in the interpreter with
// its own drivers and with ours, and the drivers' routines compared at every call.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d;
    return nullptr;
}

} // namespace

TEST(ai_program_routines_equal) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    diff::AiRunOptions opt;
    opt.checks = true;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"s500_63", "_1100_standard_type11_49"}, 1, opt);
    CHECK(r.ran);
    for (const auto& [name, c] : r.checks) std::printf("  %-28s %6llu compared, %4llu differ\n", name.c_str(), (unsigned long long)c.compared, (unsigned long long)c.differ);
    auto equal = [&](const char* name) {
        const auto it = r.checks.find(name);
        return it != r.checks.end() && it->second.compared > 0 && it->second.differ == 0;
    };
    // Bit for bit: every parameter, the braking routine's multiplier, status, mode and point; the lines' distances and kinds.
    for (const char* n : {"params brake", "params corner scale", "params corner offset", "params yaw gain", "params look speed", "params steer rate",
                          "params front weight", "params pit speed", "braking multiplier", "braking status", "braking mode", "braking next",
                          "line kind", "line surface", "line crest", "line points"})
        CHECK(equal(n));
    // The arcs' speeds (and the braking's next speed from them): equal but where a line point's road height differs in its last bits (the host-float road query).
    const auto cs = r.checks.find("corner speed");
    CHECK(cs != r.checks.end() && cs->second.differ * 3 < cs->second.compared);
}

TEST(ai_program_laps_ours_and_theirs) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The S500 alone at Tsukuba: the program's drivers and ours, both in the program's race, lap within 1.5 %.
    const diff::AiRunResult theirs = diff::run_ai(*disc, false, "tsukuba", {"s500_63"}, 2);
    diff::AiRunOptions opt;
    opt.ours = true;
    const diff::AiRunResult ours = diff::run_ai(*disc, false, "tsukuba", {"s500_63"}, 2, opt);
    CHECK(theirs.ran && ours.ran && theirs.cars.size() == 1 && ours.cars.size() == 1);
    if (theirs.cars.empty() || ours.cars.empty() || theirs.cars[0].laps.size() < 2 || ours.cars[0].laps.size() < 2) {
        CHECK(false);
        return;
    }
    const double t = theirs.cars[0].laps[1], o = ours.cars[0].laps[1];
    std::printf("  S500 second lap: the program's drivers %.3f s, ours %.3f s\n", t, o);
    CHECK(std::fabs(o - t) < t * 0.015);
}
