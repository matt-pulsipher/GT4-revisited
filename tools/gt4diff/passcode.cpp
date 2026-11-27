// gt4r_diff: Gran Turismo 4's pass codes (docs/formats/menu.md "After a race"): the program's 0x44B258 in the interpreter on
// fields made up here against ours (menu/prologue_passcode.h, PassLayout::gt4). Routines are named by address; nothing of the
// program is kept.

#include "orig.h"

#include "gt4r/menu/prologue_passcode.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include <cstring>
#include <random>

namespace gt4r::diff {

bool run_passcode(Disc& disc, int count) {
    if (!sim::Program::load(disc).known()) {
        std::printf("not the mapped Gran Turismo 4 program\n");
        return false;
    }
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    std::array<u8, 8> a, b;
    std::memcpy(a.data(), ee.host_ptr(0x6A7C88, 8), 8);
    std::memcpy(b.data(), ee.host_ptr(0x6A7C90, 8), 8);
    const menu::PassCoder ours(a, b, menu::PassLayout::gt4());
    const u32 fields = ee.alloc(0x30, 16), out = ee.alloc(0x40, 16);
    std::mt19937 rng(4321);
    const std::vector<u32> widths = menu::PassLayout::gt4().widths;
    int equal = 0;
    for (int n = 0; n < count; ++n) {
        std::vector<u32> v(widths.size());
        for (size_t k = 0; k < v.size(); ++k) v[k] = n == 0 ? u32(k * 3 + 1) : u32(rng()) & ((1u << widths[k]) - 1);
        for (size_t k = 0; k < v.size(); ++k) ee.w32(fields + 4 * u32(k), v[k]);
        std::memset(ee.host_ptr(out, 0x40), 0, 0x40);
        ee.call(0x44B258, {Arg::ptr(fields), Arg::ptr(out), Arg::integer(0x20)});
        const std::string program(reinterpret_cast<const char*>(ee.host_ptr(out, 0x40)), strnlen(reinterpret_cast<const char*>(ee.host_ptr(out, 0x40)), 0x40));
        const std::string mine = ours.encode(v, 0);
        equal += program == mine;
        if (n < 4 || program != mine) std::printf("  %2d %s (%zu) %s %s\n", n, program.c_str(), program.size(), mine.c_str(), program == mine ? "equal" : "DIFFER");
    }
    std::printf("pass codes: %d of %d equal\n", equal, count);
    return equal == count;
}

} // namespace gt4r::diff
