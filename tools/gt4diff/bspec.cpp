// The B-spec driver's skill (0x468C50) of the user's own program against ours (sim/ai/bspec_skill.h): the program's routine
// called on a race's car with made-up driver records (and the course byte both ways), its three multipliers against ours.

#include "diff.h"
#include "orig.h"

#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include <random>

namespace gt4r::diff {

BSpecSkillResult run_bspec_skill(Disc& disc, bool print, const std::vector<std::string>& cars, int records) {
    BSpecSkillResult r;
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) {
        r.error = "the disc's program is not a mapped build";
        return r;
    }
    db::SpecDb sdb = open_db(disc);
    const sim::ai::BSpecTables tables = sim::ai::BSpecTables::load(program);
    std::mt19937 rng(7);
    for (const std::string& car : cars) {
        Ee ee;
        boot(ee, disc, sdb);
        OrigRace o;
        std::string err;
        if (!setup_race(ee, disc, sdb, "tsukuba", {car}, 1, o, err)) {
            r.error = err;
            return r;
        }
        const u32 C = o.car(0), D = C + 0x14;
        const gt3r::ee::F rating = gt3r::ee::F::raw(ee.r32(ee.r32(C + 0x10) + 0xFE0));
        for (int n = 0; n < records; ++n) {
            u8 rec[0x40];
            for (u32 k = 0; k < 0x40; k += 2) {
                // values both under and over the caps
                const u32 v = rng() % 3 == 0 ? rng() % 0x10000 : rng() % 0x500;
                rec[k] = u8(v), rec[k + 1] = u8(v >> 8);
            }
            const bool flag = n % 2 == 1;
            ee.w8(o.cond + 0xCBE7, flag ? 1 : 0);
            for (u32 k = 0; k < 0x40; ++k) ee.w8(C + 0x8C + k, rec[k]);
            ee.w32(D + 0xB8, 0), ee.w32(D + 0xBC, 0), ee.w32(D + 0xC0, 0);
            ee.call(0x468C50, {Arg::ptr(C)});
            const auto mine = sim::ai::bspec_skill(tables, sim::ai::BSpecRecord::from_bytes(rec), rating, flag);
            ++r.calls;
            const bool same = mine && mine->corner.bits == ee.r32(D + 0xB8) && mine->brake.bits == ee.r32(D + 0xBC) && mine->alongside.bits == ee.r32(D + 0xC0);
            if (same) ++r.equal;
            else if (print && r.calls - r.equal <= 5)
                std::printf("  %s record %d: the program's %08X %08X %08X, ours %08X %08X %08X\n", car.c_str(), n, ee.r32(D + 0xB8), ee.r32(D + 0xBC), ee.r32(D + 0xC0),
                            mine ? mine->corner.bits : 0, mine ? mine->brake.bits : 0, mine ? mine->alongside.bits : 0);
        }
        if (print) std::printf("  %s: rating %.4f, class %d\n", car.c_str(), double(rating.host()), sim::ai::bspec_class(rating));
    }
    if (print) std::printf("bspec skill (0x468C50): %llu records, %llu equal\n", (unsigned long long)r.calls, (unsigned long long)r.equal);
    r.ran = true;
    return r;
}

} // namespace gt4r::diff
