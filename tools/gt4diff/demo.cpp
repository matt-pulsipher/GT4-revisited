// A demonstration run played through the program's own race step in the interpreter (diff.h run_demo): the file's
// entries' equipment on the event's course, the recorded pad records fed to car 0 step by step, and the car's speed and
// place over the run; where it comes to rest after braking, against the time the file records.

#include "diff.h"
#include "orig.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/sim/program.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::diff {

namespace {
DemoRunResult demo_run(Disc& disc, bool print, const std::string& label);
}

DemoRunResult run_demo(Disc& disc, bool print, const std::string& label) {
    DemoRunResult r = demo_run(disc, print, label);
    if (print && !r.ran) std::printf("demo %s: %s\n", label.c_str(), r.error.c_str());
    return r;
}

namespace {

DemoRunResult demo_run(Disc& disc, bool print, const std::string& label) {
    DemoRunResult r;
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) {
        r.error = "the disc's program is not a mapped build";
        return r;
    }
    const gt3r::Volume::Entry* e = disc.volume().find(replay::demonstration_path(label));
    if (!e) {
        r.error = "no demonstration " + label;
        return r;
    }
    std::string err;
    const auto d = replay::Demonstration::parse(disc.volume().read(*e), program, &err);
    if (!d) {
        r.error = err;
        return r;
    }
    db::SpecDb sdb = open_db(disc);
    gt3r::Volume* vol = &disc.volume();
    const db::RaceEvents events(sdb, [vol](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* f = vol->find(p);
        return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
    });
    // (the file's own event code may be another region's row of the same test: the label names the event)
    auto ev = events.event(label);
    if (!ev) ev = events.event(u32(d->code_b0));
    if (!ev) {
        r.error = "the demonstration's event is not in the database";
        return r;
    }
    std::vector<std::string> labels;
    std::vector<std::array<u8, 0x178>> equipment;
    for (const replay::DemoEntry& en : d->entries) {
        const db::Labels* l = sdb.labels("GENERIC_CAR");
        const std::string* s = l ? l->label(u32(u64(en.equipment.part(sim::Equipment::Car)))) : nullptr;
        labels.push_back(s ? *s : "");
        std::array<u8, 0x178> b{};
        std::copy(en.equipment.bytes.begin(), en.equipment.bytes.end(), b.begin());
        equipment.push_back(b);
    }
    // (the license tests' runs: one car, the player's)
    labels.resize(1), equipment.resize(1);
    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    if (!setup_race(ee, disc, sdb, ev->course, labels, 1, o, err, false, &equipment)) {
        r.error = err;
        return r;
    }
    r.course = ev->course;
    r.recorded_ms = d->time_ms;
    const replay::ControlStream& s = d->streams.at(0);
    const int extra = std::getenv("GT4R_DEMO_EXTRA") ? std::atoi(std::getenv("GT4R_DEMO_EXTRA")) : 120;
    const int steps = int(s.frames.size()) + extra;
    if (print)
        std::printf("demo %s: %s, %s on %s, %zu frames, recorded %d ms\n", label.c_str(), d->race_class.c_str(), labels[0].c_str(), ev->course.c_str(),
                    s.frames.size(), d->time_ms);
    const u32 F = o.car(0) + kF;
    float x0 = 0, y0 = 0;
    bool braked = false;
    try {
        for (int i = 0; i < steps; ++i) {
            const replay::PadRecord rec = s.frame(size_t(i));
            o.pad[0] = Pad{};
            o.pad[0].frame = rec.bytes.data();
            orig_step(ee, o);
            const float x = ee.rf(F), z = ee.rf(F + 8), v = ee.rf(F + 0x53C);
            if (i == 0) x0 = x, y0 = ee.rf(F + 4);
            const float y = ee.rf(F + 4);
            DemoSample smp{i, std::hypot(x - x0, y - y0), v};
            r.samples.push_back(smp);
            if (rec.brake() > 0) braked = true;
            if (braked && r.rest_step < 0 && std::fabs(v) < 0.01f) r.rest_step = i;
            if (print && (i % 60 == 0 || i == steps - 1 || (std::getenv("GT4R_DEMO_FROM") && i >= std::atoi(std::getenv("GT4R_DEMO_FROM")) && i < std::atoi(std::getenv("GT4R_DEMO_FROM")) + 20)))
                std::printf("  %5d  %8.3f m  %7.2f km/h  throttle %4d brake %4d steer %d  at %.3f %.3f %.3f\n", i, double(smp.distance), double(v) * 3.6,
                            rec.throttle(), rec.brake(), rec.steer(), double(x), double(ee.rf(F + 4)), double(z));
        }
    } catch (const std::exception& ex) {
        char b[160];
        std::snprintf(b, sizeof b, "stopped: %s (pc %08X ra %08X)", ex.what(), ee.cur_pc, ee.g32(31));
        r.error = b;
        if (print) std::printf("demo: %s\n", b);
        return r;
    }
    if (print) std::printf("demo: at rest after braking at step %d (%.3f m)\n", r.rest_step, r.rest_step >= 0 ? double(r.samples[size_t(r.rest_step)].distance) : 0.0);
    r.ran = true;
    return r;
}

} // namespace

} // namespace gt4r::diff
