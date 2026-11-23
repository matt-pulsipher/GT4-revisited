// gt4r_diff's checks of the race step (diff.h).

#include "diff.h"
#include "orig.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::diff {

bool run_step(Disc& disc, bool print) {
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    std::string err;
    if (!setup_race(ee, disc, sdb, "tsukuba", {"nsx_r_02"}, 1, o, err)) {
        if (print) std::printf("step: %s\n", err.c_str());
        return false;
    }
    if (print) std::printf("step: the race is set up\n");
    if (std::getenv("GT4R_DUMP")) {
        const u32 car = o.car(0);
        for (u32 off = 0; off < kCarSize; off += 4) {
            const u32 v = ee.r32(car + off);
            if (!v) continue;
            const float f = ee.rf(car + off);
            if (std::fabs(f) > 1e-6f && std::fabs(f) < 1e7f) std::printf("  +%04X %12.6f\n", off, double(f));
            else std::printf("  +%04X %08X\n", off, v);
        }
    }
    try {
        for (int i = 0; i < 600; ++i) {
            o.pad[0].throttle = i > 120 ? 1.0f : 0.0f;
            orig_step(ee, o);
            if (i % 60 == 59) {
                const u32 car = o.car(0);
                std::printf("  %3d: pos %.3f %.3f %.3f  speed %.3f  F+53C %.3f omega %.2f gear %d\n", i + 1, double(ee.rf(car + 0x104)), double(ee.rf(car + 0x108)),
                            double(ee.rf(car + 0x10C)), double(ee.rf(car + kF + 0x53C)), double(ee.rf(car + kF + 0x544)), double(ee.rf(car + kF + 0x4FC)), ee.r8(car + kF + 0x520));
            }
        }
    } catch (const std::exception& e) {
        std::printf("step: stopped: %s (pc %08X ra %08X)\n", e.what(), ee.cur_pc, ee.g32(31));
        return false;
    }
    return true;
}

} // namespace gt4r::diff
