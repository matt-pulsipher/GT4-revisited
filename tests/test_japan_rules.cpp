// Japan's disc (SCPS-17001 v1.05) races on its own program's rules (docs/specs/physics.md "Japan's rules"): license
// demonstrations recorded on the console, played by our own race, end on their recorded times. Each file is one a rule of
// Japan's own changes: the TCS's follower below 100 km/h (A-3), the contacts and the closing's side check (the pace car's
// tests A-10, B-5), the steep ground normals and the rest (A-9, A-13, IA-1, S-3).

#include "test_framework.h"

#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/demonstration_run.h"
#include "gt4r/sim/program.h"

#include <cstdio>

using namespace gt4r;

TEST(japan_demonstrations_on_japan_rules_disc) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().serial.rfind("SCPS_170.01", 0) == 0) disc = d.get();
    if (!disc) SKIP("needs the Japanese Gran Turismo 4 disc (SCPS-17001) in GT4R_DISC");
    CHECK(sim::Program::load(*disc).release() == sim::Release::Japan);
    gt3r::Volume* vol = &disc->volume();
    db::SpecDb sdb(
        [vol](const std::string& p) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(p);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    for (const char* label : {"jpl0a0003", "jpl0a0009", "jpl0a0013", "jpl0a0010", "jpl0b0005", "jplia0001", "jpl0s0003"}) {
        const race::DemonstrationRun r = race::run_demonstration(*disc, sdb, label);
        std::printf("  %s: recorded %d ms, ours %d ms (failed %d) %s\n", label, r.recorded_ms, r.ended_ms, r.fail, r.error.c_str());
        CHECK(r.ran && r.fail == 0 && r.exact());
    }
}
