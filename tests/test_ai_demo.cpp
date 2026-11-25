// The license tests' demonstration runs (docs/formats/replay.md): the disc's files read and deciphered, and the B-1
// recording played through the program's own race step (gt4r_diff demo) and through our session, the same run.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/save/codec.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

// A demonstration's name as the program builds it: the event's label, four digits at the end from 0001 (`jpl0m0000` is
// of the older layout, as the three-digit names).
bool four_digits(const std::string& n) {
    if (n.size() < 9) return false;
    for (size_t k = n.size() - 4; k < n.size(); ++k)
        if (n[k] < '0' || n[k] > '9') return false;
    return (n[n.size() - 5] < '0' || n[n.size() - 5] > '9') && n.compare(n.size() - 4, 4, "0000") != 0;
}

} // namespace

TEST(ai_demo_files_read) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    size_t files = 0, read = 0;
    for (const std::string& n : disc->volume().list_dir("replay/license")) {
        if (!four_digits(n)) continue;
        ++files;
        const gt3r::Volume::Entry* e = disc->volume().find(replay::demonstration_path(n));
        std::string err;
        const auto d = e ? replay::Demonstration::parse(disc->volume().read(*e), program, &err) : std::nullopt;
        if (d && !d->entries.empty() && d->streams.size() == 1 && d->streams[0].frames.size() > 600) ++read;
        else std::printf("  %s: %s\n", n.c_str(), err.c_str());
    }
    std::printf("  %zu of %zu demonstrations read\n", read, files);
    CHECK(files >= 400);
    CHECK((read) == (files));

    std::string err;
    const auto b1 = sim::ai::DemoRun::load(*disc, program, "usl0b0001", &err);
    CHECK(b1.has_value());
    if (!b1) return;
    CHECK((b1->demo.race_class) == (std::string("RaceLicense")));
    CHECK((b1->recorded_ms()) == (10083));
    CHECK((b1->steps()) == (size_t(1470)));
    CHECK((b1->demo.entries.size()) == (size_t(1)));
    // A steering wheel's records; the run's full throttle from step 479 to the braking at 969.
    CHECK((int(b1->record(500).kind())) == (6));
    CHECK((int(b1->record(479).throttle())) == (4096));
    CHECK((int(b1->record(968).brake())) == (0));
    CHECK(b1->record(969).brake() > 0);
    CHECK(b1->over(1470) && !b1->over(1469));
    const std::vector<sim::Entry> entries = b1->entries();
    CHECK(entries[0].automatic && entries[0].asm_on && entries[0].tcs_on);
}

TEST(ai_demo_played_as_the_program) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own race step with the recording.
    const diff::DemoRunResult theirs = diff::run_demo(*disc, false, "usl0b0001");
    CHECK(theirs.ran);
    if (!theirs.ran) return;
    CHECK((theirs.rest_step) == (1083));
    CHECK(theirs.samples.size() > 1100 && std::fabs(theirs.samples[1100].distance - 102.27f) < 0.05f);

    // Ours: the file's car on the event's course, the same records from the first step (no hold: as the interpreter's race).
    const sim::Program program = sim::Program::load(*disc);
    const auto run = sim::ai::DemoRun::load(*disc, program, "usl0b0001");
    CHECK(run.has_value());
    if (!run) return;
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    gt3r::Volume* vol = &disc->volume();
    const gt3r::Volume::Entry* file = vol->find("crs/" + theirs.course);
    CHECK(file != nullptr);
    if (!file) return;
    const Bytes data = vol->read(*file);
    const course::CourseFile parsed = course::CourseFile::parse(data);
    std::optional<course::Runway> road = course::course_road(parsed, data);
    CHECK(road.has_value());
    if (!road) return;
    sim::Session session(sdb, program, std::make_shared<const course::Runway>(std::move(*road)), run->entries());
    std::vector<race::CarInput> in(1);
    const race::CarState& c = session.cars()[0];
    const course::Vec3 start = c.position;
    float worst = 0, worst_speed = 0;
    int rest = -1;
    bool braked = false;
    for (int i = 0; i < 1200; ++i) {
        in[0] = run->input(size_t(i));
        session.step_once(in);
        const float d = std::hypot(c.position.x - start.x, c.position.z - start.z);
        worst = std::max(worst, std::fabs(d - theirs.samples[size_t(i)].distance));
        worst_speed = std::max(worst_speed, std::fabs(c.speed - theirs.samples[size_t(i)].speed));
        if (run->record(size_t(i)).brake() > 0) braked = true;
        if (braked && rest < 0 && std::fabs(c.speed) < 0.01f) rest = i;
    }
    std::printf("  rest at step %d (the program's %d); the farthest apart %.4f m, %.4f m/s\n", rest, theirs.rest_step, double(worst), double(worst_speed));
    CHECK((rest) == (theirs.rest_step));
    CHECK(worst < 0.02f);
    CHECK(worst_speed < 0.01f);
}

// The writer: every recording of the disc read and written again is the file's own bytes (the stream's bits, the header, the cipher
// and its key), the license files and the Replay Theater's (unscrambled). A few streams end inside a last record (the part byte is
// not in a file), and the 40 files of the Japanese missions' names are one older recording, not written by this recorder: those are
// the same frames when written and read again.
TEST(ai_disc_recordings_written_again) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    size_t files = 0, same = 0, frames = 0, odd = 0;
    auto one = [&](const std::string& name, ByteSpan file) {
        const auto d = replay::Demonstration::parse(file, program);
        if (!d) return;
        ++files;
        const Bytes again = d->encode(program);
        // (a file's bytes past the Serialize header's length are padding)
        if (again.size() <= file.size() && std::equal(again.begin(), again.end(), file.begin())) {
            ++same;
            return;
        }
        const auto e = replay::Demonstration::parse(again, program);
        bool alike = e && e->streams.size() == d->streams.size() && e->entries.size() == d->entries.size();
        for (size_t i = 0; alike && i < d->streams.size(); ++i) {
            alike = e->streams[i].frames.size() == d->streams[i].frames.size() && e->streams[i].state == d->streams[i].state;
            for (size_t f = 0; alike && f + 1 < d->streams[i].frames.size(); ++f) alike = e->streams[i].frames[f].bytes == d->streams[i].frames[f].bytes;
        }
        frames += alike;
        if (name.rfind("jpl0m", 0) != 0 && d->streams[0].last) ++odd, std::printf("  %s is not the file's bytes\n", name.c_str());
    };
    for (const std::string& n : disc->volume().list_dir("replay/license"))
        if (const gt3r::Volume::Entry* e = disc->volume().find("replay/license/" + n)) one(n, disc->volume().read(*e));
    for (const std::string& n : disc->volume().list_dir("replay"))
        if (n.rfind("Rdemo", 0) == 0) {
            const std::string name = n.substr(0, n.find('/'));
            if (const gt3r::Volume::Entry* e = disc->volume().find("replay/" + name + "/" + name))
                if (const std::optional<Bytes> plain = save::unscramble(disc->volume().read(*e), 0, save::FileKind::Replay)) one(name, *plain);
        }
    std::printf("  %zu of %zu recordings written byte for byte, %zu the same frames\n", same, files, frames);
    CHECK(same > 440);
    CHECK(odd == 0);
    CHECK(same + frames == files);
}
