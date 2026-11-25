// A census of the float operations the program runs (floats.h FloatCensus).

#include "floats.h"
#include "orig.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace gt4r::diff {

namespace {

// The kind of a COP1 instruction (nullptr: not a float operation counted here).
const char* cop1_kind(u32 op) {
    const u32 fmt = (op >> 21) & 31, funct = op & 63;
    if (fmt == 0x14) return funct == 0x20 ? "fpu.cvt" : nullptr; // cvt.s.w
    if (fmt != 0x10) return nullptr;
    switch (funct) {
    case 0x00: case 0x01: case 0x18: case 0x19: return "fpu.add";
    case 0x02: case 0x1A: return "fpu.mul";
    case 0x1C: case 0x1D: case 0x1E: case 0x1F: return "fpu.madd";
    case 0x03: return "fpu.div";
    case 0x04: return "fpu.sqrt";
    case 0x16: return "fpu.rsqrt";
    case 0x24: return "fpu.cvt";
    case 0x05: case 0x06: case 0x07: case 0x28: case 0x29: case 0x30: case 0x32: case 0x34: case 0x36: return "fpu.cmp";
    default: return nullptr;
    }
}

// The kind of a COP2 macro instruction (VU0), and the lanes it writes (its dest mask; 1 for the Q unit's).
const char* cop2_kind(u32 op, int* lanes) {
    if (!((op >> 25) & 1)) return nullptr; // qmfc2 / cfc2 / bc2 ...: not arithmetic
    const u32 funct = op & 63;
    const u32 dest = (op >> 21) & 15;
    *lanes = __builtin_popcount(dest);
    if (funct < 0x3C) {
        if (funct < 0x08) return "vu.add";            // vadd/vsub bc
        if (funct < 0x10) return "vu.madd";           // vmadd/vmsub bc
        if (funct < 0x18) return "vu.other";          // vmax/vmini bc
        if (funct < 0x1C) return "vu.mul";            // vmul bc
        switch (funct) {
        case 0x1C: case 0x1E: case 0x2A: return "vu.mul";                 // vmulq, vmuli, vmul
        case 0x20: case 0x22: case 0x24: case 0x26: case 0x28: case 0x2C: return "vu.add";
        case 0x21: case 0x23: case 0x25: case 0x27: case 0x29: case 0x2D: return "vu.madd";
        case 0x2E: return "vu.opm";                                         // vopmsub
        case 0x1D: case 0x1F: case 0x2B: case 0x2F: return "vu.other";      // max/mini
        default: return nullptr;                                            // integer ops, vcallms
        }
    }
    const u32 idx = (((op >> 6) & 31) << 2) | (op & 3);
    if (idx < 0x08) return "vu.add";                  // vadda/vsuba bc
    if (idx < 0x10) return "vu.madd";                 // vmadda/vmsuba bc
    if (idx < 0x18) return "vu.cvt";                  // vitof / vftoi
    if (idx < 0x1C) return "vu.mul";                  // vmula bc
    switch (idx) {
    case 0x1C: case 0x1E: case 0x2A: return "vu.mul";                       // vmulaq, vmulai, vmula
    case 0x20: case 0x22: case 0x24: case 0x26: case 0x28: case 0x2C: return "vu.add";
    case 0x21: case 0x23: case 0x25: case 0x27: case 0x29: case 0x2D: return "vu.madd";
    case 0x2E: return "vu.opm";                                             // vopmula
    case 0x1D: case 0x1F: case 0x30: case 0x31: return "vu.other";          // vabs, vclipw, vmove, vmr32
    case 0x38: *lanes = 1; return "vu.div";
    case 0x39: *lanes = 1; return "vu.sqrt";
    case 0x3A: *lanes = 1; return "vu.rsqrt";
    default: return nullptr;
    }
}

} // namespace

std::map<u32, std::string> FloatCensus::scan(Ee& ee, u32 begin, u32 end) {
    std::map<u32, std::string> out;
    for (u32 a = begin; a < end; a += 4) {
        const u32 op = ee.r32(a);
        const u32 major = op >> 26;
        const char* k = nullptr;
        int lanes = 1;
        if (major == 0x11) k = cop1_kind(op);
        else if (major == 0x12) k = cop2_kind(op, &lanes);
        if (k) out[a] = k;
    }
    return out;
}

void FloatCensus::attach(Ee& ee) {
    kind_at = scan(ee, 0x100000, kTextEnd);
    for (const auto& [a, k] : kind_at) {
        const u32 op = ee.r32(a);
        int lanes = 1;
        if ((op >> 26) == 0x12) cop2_kind(op, &lanes);
        const std::string kind = k;
        u64* here = &at[a];
        u64* total = &by_kind[kind];
        ee.add_probe(a, [here, total, lanes](Ee&) { ++*here, *total += u64(lanes); });
    }
    if (const char* path = std::getenv("GT4R_FLOATS_FUNCTIONS")) {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const u32 a = u32(std::strtoul(line.c_str(), nullptr, 16));
            if (a < 0x100000 || a >= kTextEnd) continue;
            u64* here = &calls[a];
            ee.add_probe(a, [here](Ee&) { ++*here; });
        }
    }
}

} // namespace gt4r::diff
