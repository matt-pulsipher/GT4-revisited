// gt4tool's cross-platform check:
//
//   gt4tool state-hash <disc> <course> [cars] [seconds=60] [every=10]
//       a field of computer drivers run headless on <course> (as `gt4tool ai`): every <every> simulated seconds and at the
//       end, a hash of every car's state (position, axes, velocity, speed, engine speed, gear, course distance, lap, each
//       wheel's center, spin and travel, bit for bit), then each car's record (the step's own floats: place, axes,
//       velocity, attitude, engine speed). Two builds that print the same lines ran the race identically;
//       the first line that differs says when they parted. [cars]: labels joined by commas (default: six small cars).
//   gt4tool car-digests <disc> [all]
//       every car of the disc as an online race checks it (net::CarBuilder::digest: its parameter block as the converter
//       makes it): a hash of all the digests and, with [all], each car's. Machines that race each other online must
//       print the same.

#include "gt4r/disc/disc.h"
#include "gt4r/net/car_choice.h"
#include "gt4r/race/simulation.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

struct Fnv {
    u64 h = 0xcbf29ce484222325ull;
    void byte(u8 b) { h = (h ^ b) * 0x100000001b3ull; }
    void word(u32 w) {
        for (int i = 0; i < 4; ++i) byte(u8(w >> (8 * i)));
    }
    void f(float v) { word(std::bit_cast<u32>(v)); }
    void v3(const course::Vec3& v) { f(v.x), f(v.y), f(v.z); }
};

// The car's own record (the step's single floats as the program keeps them): place, axes, velocity, attitude, engine.
u64 record_hash(const gt3r::sim::exact::Car& r) {
    Fnv h;
    for (const gt3r::sim::exact::Vec3* v : {&r.pos, &r.fwd, &r.lat, &r.up, &r.vel})
        h.word(v->x.bits), h.word(v->y.bits), h.word(v->z.bits);
    for (const gt3r::ee::F f : {r.pitch, r.roll, r.heading, r.yaw_rate, r.omega, r.speed, r.lat_speed}) h.word(f.bits);
    return h.h;
}

u64 state_hash(const race::CarState& c) {
    Fnv h;
    h.v3(c.position), h.v3(c.right), h.v3(c.up), h.v3(c.forward), h.v3(c.velocity);
    h.f(c.speed), h.f(c.engine_rpm), h.word(u32(c.gear)), h.f(c.course_v), h.word(u32(c.lap));
    for (const race::WheelState& w : c.wheels) h.v3(w.center), h.f(w.spin), h.f(w.travel);
    return h.h;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= s.size()) {
        const size_t b = s.find(',', a);
        const std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty()) out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

} // namespace

int cmd_state_hash(Disc& disc, const std::vector<std::string>& a) {
    std::vector<std::string> cars = {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01"};
    if (a.size() > 1 && !a[1].empty()) cars = split(a[1]);
    const double seconds = a.size() > 2 ? std::atof(a[2].c_str()) : 60.0;
    const double every = a.size() > 3 ? std::atof(a[3].c_str()) : 10.0;
    std::vector<sim::ai::FieldCar> entries;
    {
        db::SpecDb sdb = sim::ai::open_specdb(disc);
        const sim::Program program = sim::Program::load(disc);
        for (const std::string& c : cars) {
            sim::ai::FieldCar f;
            f.name = c;
            if (!sim::stock_car(sdb, program, c, f.entry.equipment)) {
                std::fprintf(stderr, "no car %s\n", c.c_str());
                return 1;
            }
            entries.push_back(f);
        }
    }
    const long period = std::max(1L, std::lround(every * 60.0)); // the simulation's steps are 1/60 s
    long next = period;
    u64 last = 0;
    auto on_step = [&](const sim::Session& s, const sim::ai::Field&) {
        Fnv all;
        for (const race::CarState& c : s.cars()) {
            const u64 h = state_hash(c);
            for (int i = 0; i < 8; ++i) all.byte(u8(h >> (8 * i)));
        }
        last = all.h;
        const long step = std::lround(s.time() * 60.0);
        if (step < next) return;
        next += period;
        std::printf("%8.2f s  %016llx", s.time(), static_cast<unsigned long long>(all.h));
        for (const race::CarState& c : s.cars()) std::printf("  %08x", u32(state_hash(c)));
        std::printf("  record");
        for (size_t i = 0; i < s.cars().size(); ++i) std::printf("  %08x", u32(record_hash(s.record(i))));
        std::printf("\n");
    };
    const sim::ai::FieldRun run = sim::ai::run_field(disc, a[0], entries, 99, {}, seconds, on_step);
    std::printf("end %.2f s  %016llx\n", run.seconds, static_cast<unsigned long long>(last));
    for (const sim::ai::CarRun& r : run.cars) {
        std::printf("%-28s laps", r.car.c_str());
        for (double l : r.laps) std::printf(" %.4f", l);
        std::printf("\n");
    }
    return 0;
}

int cmd_car_digests(Disc& disc, const std::vector<std::string>& a) {
    const bool all = a.size() > 0 && a[0] == "all";
    db::SpecDb sdb = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) { // (online races are Gran Turismo 4's: CarBuilder needs its program)
        std::fprintf(stderr, "not a Gran Turismo 4 program this builds cars from\n");
        return 1;
    }
    gt3r::Volume* vol = &disc.volume();
    const sim::FileReader read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    net::CarBuilder builder(sdb, program, read);
    Fnv h;
    int cars = 0;
    for (const db::CarEntry& e : sdb.cars()) {
        net::CarChoice c;
        c.label = e.label, c.paint = 1;
        const u64 d = builder.digest(c);
        if (!d) continue;
        ++cars;
        for (int i = 0; i < 8; ++i) h.byte(u8(d >> (8 * i)));
        if (all) std::printf("%-32s %016llx\n", e.label.c_str(), static_cast<unsigned long long>(d));
    }
    std::printf("%d cars, digests %016llx\n", cars, static_cast<unsigned long long>(h.h));
    return 0;
}

} // namespace gt4r::tool
