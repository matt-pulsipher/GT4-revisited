// gt4tool's cross-platform check:
//
//   gt4tool state-hash <disc> <course> [cars] [seconds=60] [every=10]
//       a field of computer drivers run headless on <course> (as `gt4tool ai`): every <every> simulated seconds and at the
//       end, a hash of every car's state (position, axes, velocity, speed, engine speed, gear, course distance, lap, each
//       wheel's center, spin and travel, bit for bit). Two builds that print the same lines ran the race identically;
//       the first line that differs says when they parted. [cars]: labels joined by commas (default: six small cars).

#include "gt4r/disc/disc.h"
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

} // namespace gt4r::tool
