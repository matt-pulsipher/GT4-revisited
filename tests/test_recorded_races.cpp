// The Replay Theater's demo replays (replay/Rdemo<n>, docs/formats/replay.md), recorded on the console: our own race set up from each
// recording alone (race/recorded_race.*: the start, the drivers, the pit stops, the flow) ends on the recorded time and best lap.

#include "test_framework.h"

#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/race_recording.h"
#include "gt4r/race/recorded_race.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/save/codec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record_image.h"

#include <cstdio>
#include <cstring>

using namespace gt4r;

TEST(recorded_races_end_on_their_recorded_times) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
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

// A race of ours kept as the program keeps one (race/race_recording.h): made from a disc recording's race, its file is the disc's,
// byte for byte (the header's every field, the entries, the stream, the cipher and its key), and its scrambled card file reads back.
TEST(a_race_kept_as_the_program_keeps_it) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    int files = 0, equal = 0, read = 0, disc_bytes = 0;
    for (int n = 0; n <= 12; ++n) {
        const std::string name = "Rdemo" + std::to_string(n);
        const gt3r::Volume::Entry* e = disc->volume().find("replay/" + name + "/" + name);
        const std::optional<Bytes> plain = e ? save::unscramble(disc->volume().read(*e), 0, save::FileKind::Replay) : std::nullopt;
        std::optional<replay::Demonstration> d = plain ? replay::Demonstration::parse(*plain, program) : std::nullopt;
        if (!d || d->race_class != "RaceArcadeSingle") continue;
        ++files;
        sim::ai::DemoRun run;
        run.demo = *d;
        u32 w[3];
        std::memcpy(w, d->information.data(), sizeof w);
        race::RecordingSetup s;
        s.event = d->code_b0, s.course = d->code_b8, s.bspec = d->race_mode == 1;
        s.laps = int(w[0]), s.penalty = int(w[1]), s.tire_damage = int(w[2]);
        std::memcpy(s.driver.data(), d->information.data() + 0x20, s.driver.size());
        s.playlist.assign(d->header.begin() + 0x148, d->header.begin() + 0x958);
        s.time_ms = d->time_ms, s.best_ms = d->time2_ms;
        replay::Demonstration made = race::make_recording(s, run.entries(), d->streams[0].frames);
        made.streams[0].last = d->streams[0].last;
        const Bytes again = made.encode(program);
        if (again == d->encode(program)) ++equal; // (the disc's bytes but for a stream cut inside its last record: ai_disc_recordings_written_again)
        else std::printf("  %s: not the recording's bytes\n", name.c_str());
        disc_bytes += again.size() <= plain->size() && std::equal(again.begin(), again.end(), plain->begin());
        const std::optional<sim::ai::DemoRun> back = sim::ai::DemoRun::load_replay(race::recording_file(made, program, 12345u + u32(n)), program, name);
        if (back && back->demo.streams[0].frames == d->streams[0].frames && back->demo.entries.size() == d->entries.size()) ++read;
        // In the app's saved replay, beside its states.
        race::ReplayFile kept;
        kept.course = "tsukuba", kept.cars = {{"a", 1}}, kept.steps.resize(2, std::vector<race::CarState>(1));
        kept.other.emplace_back(race::ReplayFile::kRecording, again);
        const race::ReplayFile opened = race::ReplayFile::decode(kept.encode());
        CHECK(opened.recording() && *opened.recording() == again);
        CHECK(opened.steps.size() == 2);
    }
    std::printf("  %d of %d arcade recordings made again, %d of them the disc's bytes, %d read back\n", equal, files, disc_bytes, read);
    CHECK(files == 12);
    CHECK(disc_bytes >= 9);
    CHECK(equal == files);
    CHECK(read == files);
}

// A saved replay's details beside its states (the brakes' heat, the clutch): back as they were; another size leaves the states.
TEST(a_saved_replays_details) {
    race::ReplayFile f;
    f.course = "tsukuba", f.cars = {{"a", 1}, {"b", 2}};
    for (int i = 0; i < 5; ++i) {
        std::vector<race::CarState> s(2);
        s[0].wheels[2].brake_heat = 0.25f * float(i), s[1].clutch_closed = i & 1, s[0].rolling = i > 2;
        f.steps.push_back(s);
    }
    f.other.emplace_back(race::kReplayDetails, race::replay_details(f.steps));
    race::ReplayFile g = race::ReplayFile::decode(f.encode());
    CHECK(g.other.size() == 1);
    CHECK(g.steps[3][0].wheels[2].brake_heat == 0.0f);
    race::apply_details(g.steps, ByteSpan(g.other[0].second).first(20));
    CHECK(g.steps[3][0].wheels[2].brake_heat == 0.0f);
    race::apply_details(g.steps, g.other[0].second);
    for (size_t i = 0; i < 5; ++i) {
        CHECK(g.steps[i][0].wheels[2].brake_heat == f.steps[i][0].wheels[2].brake_heat);
        CHECK(g.steps[i][1].clutch_closed == f.steps[i][1].clutch_closed);
        CHECK(g.steps[i][0].rolling == f.steps[i][0].rolling);
    }
}

// A best lap's recorder header keeps the car's record (0x10A0 bytes at +0x54): what our record takes from it (sim::load_car_record)
// written back (sim::store_car_record) over the same bytes leaves them as they were; on the disc's free run (Rdemo9, a lap's recorder).
TEST(a_recorders_car_record_written_back) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    const gt3r::Volume::Entry* e = disc->volume().find("replay/Rdemo9/Rdemo9");
    const std::optional<Bytes> plain = e ? save::unscramble(disc->volume().read(*e), 0, save::FileKind::Replay) : std::nullopt;
    const std::optional<replay::Demonstration> d = plain ? replay::Demonstration::parse(*plain, program) : std::nullopt;
    CHECK(d && d->streams.size() == 1);
    if (!d || d->streams.empty()) return;
    const std::optional<replay::RecorderState> st = replay::recorder_state(d->streams[0]);
    CHECK(st.has_value());
    if (!st) return;
    sim::Record r;
    sim::load_car_record(r, st->car);
    Bytes again(st->car.begin(), st->car.end());
    sim::store_car_record(r, again);
    Bytes blank(0x10A0, 0);
    sim::store_car_record(r, blank);
    size_t written = 0;
    for (u8 b : blank) written += b != 0;
    std::printf("  %zu of 0x10A0 bytes the record writes non-zero\n", written);
    CHECK(std::equal(again.begin(), again.end(), st->car.begin()));
    CHECK(written > 0x100);
    // The whole file again with that header: its recorder's header, its records.
    replay::Demonstration made = *d;
    made.streams[0].state.assign(d->streams[0].state.begin(), d->streams[0].state.end());
    std::copy(blank.begin(), blank.end(), made.streams[0].state.begin() + 0x54);
    const std::optional<replay::Demonstration> back = replay::Demonstration::parse(made.encode(program), program);
    CHECK(back && back->streams.size() == 1 && back->streams[0].frames == d->streams[0].frames && back->streams[0].state.size() == 0x14F4);
}
