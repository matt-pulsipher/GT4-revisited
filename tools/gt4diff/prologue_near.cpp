// gt4r_diff: Gran Turismo 4 Prologue's phase 14 (0x3DC1C8: which cars are near which and the slipstream) in a race of
// several cars, the program's own computer drivers driving all but the first, against the engine's pairs pass (Gran
// Turismo 3's 0x1EC098, which Prologue's is in another layout) on the cars' records as the program has them at its entry.
// Routines are named by address; nothing of the program is kept.

#include "orig.h"
#include "prologue_race.h"
#include "prologue_taps.h"

#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_prologue.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/tick.h"

#include <cstdio>
#include <sstream>

namespace gt4r::diff::prologue {

namespace ex = gt3r::sim::exact;

NearStats near_check(Disc& disc, const std::string& course, const std::string& cars, int steps, bool print) {
    NearStats out;
    if (!is_prologue(disc)) return out.err = "not Prologue's disc", out;
    std::vector<std::string> labels;
    {
        std::stringstream s(cars);
        std::string l;
        while (std::getline(s, l, ',')) labels.push_back(l);
    }
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc);
    Race r;
    std::string err;
    if (!setup_race(ee, disc, sdb, course, labels, 1, r, err, true)) {
        return out.err = err, out;
    }
    std::vector<ex::Car> before(size_t(r.cars)), mine(size_t(r.cars));
    ex::Proximity k{};
    PTapStat st, st_rest;
    u64 nonzero = 0;
    bool pending = false;
    u32 exit_at = 0;
    ee.probe(0x3DC1C8, [&](Ee& e) {
        for (int i = 0; i < r.cars; ++i) {
            const std::vector<u8> im = gt3_image(e, r.car(i));
            before[size_t(i)] = {};
            before[size_t(i)].behaviour = sim::rules::behaviour();
            ex::load_image(before[size_t(i)], im.data());
        }
        mine = before;
        ex::proximity(mine, k);
        sim::prologue::near_drivers(mine);
        pending = true;
        const u32 ra = e.g32(31);
        if (exit_at) return;
        exit_at = ra;
        e.add_probe(ra, [&](Ee& x) {
            if (!pending) return;
            pending = false;
            for (int i = 0; i < r.cars; ++i) {
                const std::vector<u8> im = gt3_image(x, r.car(i));
                ex::Car after;
                after.behaviour = sim::rules::behaviour();
                ex::load_image(after, im.data());
                compare(after, mine[size_t(i)], st);
                ex::Car rest = mine[size_t(i)]; // (the drivers' zone cells and contact aside)
                rest.ai.contact = after.ai.contact, rest.ai.zone = after.ai.zone;
                compare(after, rest, st_rest);
                nonzero += !(after.slip_target == gt3r::ee::F());
            }
        });
    });
    for (int i = 0; i < steps; ++i) {
        r.pad[0] = test_drive(i);
        step(ee, r);
    }
    out.records = st.calls, out.clean = st.clean, out.with_slip = nonzero;
    if (print) {
        std::printf("prologue-near: %s, %d cars, %d steps: %llu car records, %llu with every field equal, %llu but for the drivers' zones and "
                    "contact (%llu with a slipstream)\n",
                    course.c_str(), r.cars, steps, (unsigned long long)st.calls, (unsigned long long)st.clean, (unsigned long long)st_rest.clean,
                    (unsigned long long)nonzero);
        for (const auto& [name, f] : st.fields)
            std::printf("    %-24s differs in %llu, most by %.3g; %s\n", name.c_str(), (unsigned long long)f.off, f.worst, f.example.c_str());
    }
    return out;
}

int run_near(Disc& disc, const std::string& course, const std::string& cars, int steps) {
    const NearStats s = near_check(disc, course, cars, steps, true);
    if (!s.err.empty()) std::printf("prologue-near: %s\n", s.err.c_str());
    return s.err.empty() && s.clean == s.records ? 0 : 1;
}

} // namespace gt4r::diff::prologue
