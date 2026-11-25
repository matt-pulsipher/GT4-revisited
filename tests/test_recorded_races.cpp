// The Replay Theater's demo replays (replay/Rdemo<n>, docs/formats/replay.md), recorded on the console: our own race set up from each
// recording alone (race/recorded_race.*: the start, the drivers, the pit stops, the flow) ends on the recorded time and best lap.

#include "test_framework.h"

#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/recorded_race.h"
#include "gt4r/sim/program.h"

#include <cstdio>

using namespace gt4r;

TEST(recorded_races_end_on_their_recorded_times) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Volume* vol = &disc->volume();
    const sim::Program program = sim::Program::load(*disc);
    db::SpecDb sdb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    int exact = 0, ran = 0;
    for (int n = 0; n <= 12; ++n) {
        const std::string name = "Rdemo" + std::to_string(n);
        const gt3r::Volume::Entry* e = vol->find("replay/" + name + "/" + name);
        if (!e) continue;
        std::string error;
        std::optional<sim::ai::DemoRun> run = sim::ai::DemoRun::load_replay(vol->read(*e), program, name, &error);
        CHECK(run.has_value());
        if (!run) continue;
        race::RecordedRace race(*disc, sdb, std::move(*run));
        for (size_t k = race.run().steps() + 60; k > 0; --k) race.step();
        ++ran;
        if (race.exact()) ++exact;
        else std::printf("  %s: %lld ms, recorded %d\n", name.c_str(), race.laps().empty() ? -1LL : (long long)race.laps().back(), race.run().recorded_ms());
    }
    std::printf("  %d of %d demo replays on their recorded times\n", exact, ran);
    CHECK(ran == 13);
    CHECK(exact == ran);
}
