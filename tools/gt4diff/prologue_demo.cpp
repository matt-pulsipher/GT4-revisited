// gt4r_diff <prologue> prologue-demo <label> [steps]: a lesson's demonstration through the program's own step, the car and its
// parts, the row and the recorded pad frames from the disc, beside our run of it (race/demonstration_run_prologue.h): the first
// step whose record differs, and the two runs' positions at intervals.

#include "prologue_taps.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/replay/demonstration_prologue.h"
#include "gt4r/race/demonstration_run_prologue.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include "sim/exact/car_fields.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace gt4r::diff::prologue {

namespace {

// Row `id` of the plain table `name` (docs/formats/prologue.md "The car database").
std::vector<u8> table_row(Disc& disc, const std::string& name, u32 id) {
    const gt3r::Volume::Entry* e = disc.volume().find("specdb/GT4_PROLOGUE1000/" + name + ".dbt");
    if (!e) return {};
    const Bytes b = disc.volume().read(*e);
    const u32 rows = rd32(b, 8), size = rd32(b, 12);
    const size_t base = 16 + size_t(rows) * 8;
    for (u32 k = 0; k < rows; ++k)
        if (rd32(b, 16 + 8 * k) == id) {
            const size_t at = base + rd32(b, 20 + 8 * k);
            return {b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + size)};
        }
    return {};
}

} // namespace

int run_demo(Disc& disc, const std::string& label, int limit) {
    namespace ex = gt3r::sim::exact;
    const sim::Program program = sim::Program::load(disc);
    gt3r::Volume* vol = &disc.volume();
    const auto read = [vol](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* f = vol->find(p);
        return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
    };
    db::SpecDb sdb(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const std::optional<Bytes> file = read(replay::prologue::run_path(label));
    std::string err;
    const auto run = file ? replay::prologue::Run::parse(*file, replay::prologue::delta_bases(program), &err) : std::nullopt;
    const db::RaceEvents events(sdb, read);
    const auto ev = events.event(label);
    if (!run || !ev) return std::printf("%s: %s\n", label.c_str(), err.c_str()), 1;
    const auto pool = events.pool(*ev);

    // Ours.
    const size_t watch = std::getenv("GT4R_DEMO_CAR") ? size_t(std::atoi(std::getenv("GT4R_DEMO_CAR"))) : 0; // (which car is compared)
    std::vector<ex::Car> mine;
    race::prologue::Options o;
    o.judge = false;
    o.on_step = [&](size_t, const sim::Session& s) {
        if (mine.size() >= size_t(limit)) return;
        ex::Car c = s.record(watch);
        const sim::prologue::CarPart& q = s.prologue_part(watch);
        for (size_t w = 0; w < 4; ++w) c.t[w].steer_sin = q.offset_rate[w], c.t[w].steer_cos = q.tires.steer_offset[w];
        mine.push_back(c);
    };
    const race::DemonstrationRun d = race::prologue::run_demonstration(disc, sdb, label, o);
    std::printf("%s on %s: recorded %d ms, ours %d ms, %zu steps %s\n", label.c_str(), ev->course.c_str(), d.recorded_ms, d.ended_ms, d.steps, d.error.c_str());

    // The program's.
    Ee ee;
    boot(ee, disc);
    Race r;
    Lesson lesson;
    lesson.row = table_row(disc, "RACE", u32(run->event));
    if (run->entries.size() > 1) lesson.vtable = 0x539788; // (a race against others: DynamicsConductorTraining)
    lesson.entry = [&](Ee& e, u32 entry, int i) {
        const replay::prologue::Entry& n = run->entries[size_t(i)];
        for (u32 k = 0; k < n.equipment.size(); ++k) e.w8(entry + 0x20 + k, n.equipment[k]);
        for (u32 k = 0; k < n.condition.size(); ++k) e.w8(entry + 0x3FCC + k, n.condition[k]);
        e.w32(entry + 0x3FA8, u32(n.grid_place)), e.w32(entry + 0x3FAC, n.pedal_style), e.w32(entry + 0x3FB0, n.driver_kind), e.w32(entry + 0x3FB4, n.x16a);
        e.w32(entry + 0x3FB8, n.asm_on), e.w32(entry + 0x3FBC, n.tcs_on);
    };
    std::vector<std::string> labels;
    for (size_t i = 0; i < run->entries.size() && i < pool.size(); ++i) labels.push_back(pool[i].car);
    if (!setup_race(ee, disc, sdb, ev->course, labels, 1, r, err, true, &lesson)) return std::printf("%s\n", err.c_str()), 1;
    if (lesson.row.empty()) std::printf("  (no RACE row %u)\n", u32(run->event));
    const bool rolling = ee.r8(r.cond + 0x107CE) != 0; // (no start hold)
    std::printf("  rules: start %d km/h at %d m\n", ee.r8(r.cond + 0x107CE), s16(ee.r16(r.cond + 0x107D4)));
    ee.w32(r.info, std::max(1, ev->laps)), ee.w32(r.info + 4, 1);
    const auto& frames = run->streams[0].frames;
    int shown = 0;
    static const u8 kNone[16] = {};
    const int show = std::getenv("GT4R_DEMO_SHOW") ? std::atoi(std::getenv("GT4R_DEMO_SHOW")) : 12;
    const int every = std::getenv("GT4R_DEMO_EVERY") ? std::atoi(std::getenv("GT4R_DEMO_EVERY")) : 600;
    for (size_t i = 0; i < mine.size(); ++i) {
        ee.w32(r.info + 0x74, rolling ? 0 : u32(std::max(0, 239 - int(i))));
        r.pad[0].raw = i < frames.size() ? frames[i].bytes.data() : kNone;
        step(ee, r);
        const std::vector<u8> im = gt3_image(ee, r.car(int(watch)));
        ex::Car after;
        after.behaviour = mine[i].behaviour;
        ex::load_image(after, im.data());
        PTapStat st;
        compare(after, mine[i], st);
        static const std::set<std::string> kSkip = {"steer_force", "ffb", "ffb_count", "lag_pitch_diff", "lag_heading_diff", "f3e4", "prev.f3e4", "laps", "x535", "x51c", "feedback", "hit_part", "rumble", "pit_box", "wheel_type"};
        std::string diff;
        int n = 0;
        for (const auto& [name, f] : st.fields)
            if (!kSkip.count(name) && name.rfind("ai.", 0) != 0 && name.rfind("lag_", 0) != 0 && name.find("flap") == std::string::npos &&
                !(name.rfind("aids.", 0) == 0 && name.ends_with("[1]")) /* (the driver's block: kept outside the record) */ && ++n <= show) diff += "\n      " + name + ": " + f.example;
        if (i % size_t(every) == 0)
            std::printf("  step %zu: the program's (%.3f, %.3f) %.2f km/h  ours (%.3f, %.3f) %.2f km/h; %d fields differ\n", i, double(after.pos.x.host()), double(after.pos.y.host()),
                        double(after.speed.host()) * 3.6, double(mine[i].pos.x.host()), double(mine[i].pos.y.host()), double(mine[i].speed.host()) * 3.6, n);
        if (n > 0 && shown < 3) {
            ++shown;
            std::printf("  step %zu: %d fields differ%s\n", i, n, diff.c_str());
        }
    }
    return 0;
}

} // namespace gt4r::diff::prologue
