#include "gt4r/sim/exact_bridge.h"

#include "sim/exact/program_tables.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gt4r::sim {

namespace ex = gt3r::sim::exact;

namespace {

// Surfaces 0 .. 11 keep their rows; the wet variants 20 / 21 / 22 are the core's 12 / 13 / 14;
// surface 12 takes row 15. The surfaces no course of the disc uses (13 .. 19, 23) share rows.
constexpr int kCoreRow[24] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15, 15, 15, 15, 0, 0, 0, 0, 12, 13, 14, 14};
constexpr int kRowSurface[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 20, 21, 22, 12};

} // namespace

int core_surface(int surface) { return surface >= 0 && surface < 24 ? kCoreRow[surface] : 0; }
int surface_of_core_row(int row) { return row >= 0 && row < 16 ? kRowSurface[row] : 0; }

ex::ProgramData core_program(const Program& program) {
    using T = ex::ProgramData::Table;
    ex::ProgramData d = ex::ProgramData::synthetic(gt3r::sim::Program::NtscU);
    for (int t = 0; t < ex::ProgramData::kTables; ++t) {
        const ByteSpan bytes = ex::gt3_table(T(t));
        for (u32 i = 0; i < bytes.size() && i < ex::ProgramData::size(T(t)); ++i) d.poke(T(t), i, bytes[i]);
    }
    if (!program.known()) return d;
    // The static physics block in the core's layout: the head as it is, each per-surface array by row,
    // what follows the arrays 0x30 bytes back.
    const u32 g = Program::kStatic;
    for (u32 i = 0; i < 0x1C; ++i) d.poke(T::Static, i, program.b(g + i));
    for (u32 row = 0; row < 16; ++row) {
        const u32 s = u32(kRowSurface[row]);
        d.poke(T::Static, 0x1C + row, program.b(g + 0x1C + s));         // bump amplitude
        d.poke(T::Static, 0x2C + 2 * row, program.b(g + 0x34 + 2 * s)); // rolling resistance (s16)
        d.poke(T::Static, 0x2D + 2 * row, program.b(g + 0x35 + 2 * s));
        d.poke(T::Static, 0x4C + row, program.b(g + 0x64 + s));         // bump wavelength
        d.poke(T::Static, 0x5C + row, program.b(g + 0x7C + s));         // bump scaled by speed
        d.poke(T::Static, 0x6C + row, program.b(g + 0x94 + s));         // bump mode
    }
    for (u32 i = 0x7C; i < ex::ProgramData::size(T::Static); ++i) d.poke(T::Static, i, program.b(g + 0x30 + i));
    // The soft materials, by row.
    for (u32 row = 0; row < 16; ++row) {
        const u32 w = program.w(Program::kSoft + 4 * u32(kRowSurface[row]));
        for (u32 k = 0; k < 4; ++k) d.poke(T::Soft, 4 * row + k, u8(w >> (8 * k)));
    }
    return d;
}

namespace {

// The engine's curve as at most 16 points.
void engine_curve(const Spec& s, ex::Spec& to) {
    struct Point {
        int rpm, torque;
    };
    std::vector<Point> p;
    const int n = std::min<int>(s.b(0xBF), 24);
    for (int i = 0; i < n; ++i) p.push_back({s.b(0xC0 + u32(i)), s.h(0xD8 + 2 * u32(i))});
    while (p.size() > 16) {
        size_t best = 1;
        double least = 1e30;
        for (size_t i = 1; i + 1 < p.size(); ++i) {
            const double span = double(p[i + 1].rpm - p[i - 1].rpm);
            const double on_line = span > 0 ? p[i - 1].torque + (p[i + 1].torque - p[i - 1].torque) * double(p[i].rpm - p[i - 1].rpm) / span : p[i - 1].torque;
            const double loss = std::fabs(on_line - p[i].torque) * span;
            if (loss < least) least = loss, best = i;
        }
        p.erase(p.begin() + std::ptrdiff_t(best));
    }
    to[0x71] = u8(p.size());
    for (size_t i = 0; i < 16; ++i) {
        const bool on = i < p.size();
        to[0x72 + i] = on ? u8(p[i].rpm) : 0xFF;
        const u16 t = on ? u16(p[i].torque) : 0;
        to[0x82 + 2 * i] = u8(t), to[0x83 + 2 * i] = u8(t >> 8);
    }
}

// A tire block (ours, 0xA8 bytes at `t`) as the core's (0x70 bytes at `o`).
void tire_block(const Spec& s, u32 t, ex::Spec& to, u32 o) {
    auto copy = [&](u32 dst, u32 src, u32 n) {
        for (u32 i = 0; i < n; ++i) to[o + dst + i] = s.b(t + src + i);
    };
    to[o + 0x00] = s.b(t + 0x00); // grip
    to[o + 0x01] = s.b(t + 0x02); // curve A's slip angle range
    to[o + 0x02] = 8, copy(0x03, 0x10, 16);
    to[o + 0x13] = 6, copy(0x14, 0x20, 6), copy(0x1A, 0x30, 6), copy(0x20, 0x40, 6);
    to[o + 0x26] = 6, copy(0x27, 0x28, 6), copy(0x2D, 0x38, 6), copy(0x33, 0x48, 6);
    to[o + 0x39] = 4, copy(0x3A, 0x5C, 8);
    for (u32 row = 0; row < 16; ++row) to[o + 0x4B + row] = s.b(t + 0x64 + u32(kRowSurface[row]));
    to[o + 0x5E] = 6, copy(0x5F, 0x50, 12);
    to[o + 0x6B] = s.b(t + 0x01); // wear
}

} // namespace

void core_spec(const Spec& s, ex::Spec& to) {
    to.fill(0);
    auto b = [&](u32 o, u32 v) { to[o] = u8(v); };
    auto h = [&](u32 o, u32 v) { to[o] = u8(v), to[o + 1] = u8(v >> 8); };
    auto bytes = [&](u32 dst, u32 src, u32 n) {
        for (u32 i = 0; i < n; ++i) to[dst + i] = s.b(src + i);
    };
    auto cap = [](u32 v) { return std::min<u32>(v, 255); };
    b(0x0F, 9), b(0x10, 2), b(0x12, 1);
    h(0x16, s.h(0x40)); // frontal area
    h(0x1A, s.h(0x10)), h(0x1C, s.h(0x12)), h(0x1E, s.h(0x14)), h(0x20, s.h(0x16)), h(0x22, s.h(0x18));
    h(0x24, s.h(0x1A)), h(0x26, s.h(0x1C)), h(0x28, s.h(0x1E));
    // The weight: the car's (its percentage applied) and the ballast.
    int weight = s.sh(0x20) != 0 ? s.sh(0x20) : 1200;
    if (s.b(0x08) != 0 && s.b(0x08) != 100) weight = weight * s.b(0x08) / 100;
    h(0x2A, u32(weight + s.b(0x05)));
    b(0x2C, s.b(0x22)), b(0x2D, s.b(0x23));
    // The inertia bytes: the yaw's; the pitch's and roll's as the core's own builder makes them (the yaw's,
    // and half of it). Gran Turismo 4's shares (spec +0x25 / +0x26) belong to its own attitude step.
    b(0x2E, s.b(0x24)), b(0x2F, s.b(0x24)), b(0x30, s.b(0x24) >> 1), b(0x31, s.b(0x27));
    b(0x32, s.b(0x42)), b(0x33, s.b(0x43)), b(0x34, s.b(0x44));
    b(0x35, s.b(0x28)), b(0x36, s.b(0x2A)), b(0x37, s.b(0x2B));
    b(0x38, s.b(0x98)), b(0x39, s.b(0x99));
    // The axles' spring and damper blocks: ours keep the knee gap as a word after the bump damping.
    bytes(0x3A, 0x58, 12), b(0x46, cap(s.h(0x66))), b(0x47, s.b(0x65));
    bytes(0x4A, 0x70, 12), b(0x56, cap(s.h(0x7E))), b(0x57, s.b(0x7D));
    b(0x5A, s.b(0x88)), b(0x5B, s.b(0x89));
    b(0x5C, cap(s.h(0x9A))), b(0x5D, cap(s.h(0x9C)));
    b(0x5E, s.b(0x8C)), b(0x5F, s.b(0x8D)), b(0x60, s.b(0x8E)), b(0x61, s.b(0x8F));
    b(0x62, s.b(0x90)), b(0x63, s.b(0x91));
    bytes(0x64, 0x92, 4), bytes(0x68, 0x96, 2);
    h(0x6A, s.h(0xB8)), h(0x6C, s.h(0xBA));
    b(0x6E, s.b(0xBC)), b(0x6F, s.b(0xBD)), b(0x70, s.b(0xBE));
    engine_curve(s, to);
    // The output factor with the entry's power percentage.
    u32 out = s.h(0x108);
    if (s.b(0x07) != 0 && s.b(0x07) != 100) out = (out ? out : 1000) * s.b(0x07) / 100;
    h(0xA2, out), h(0xA4, s.h(0x10A)), b(0xA6, s.b(0x10C)), b(0xA7, s.b(0x113));
    bytes(0xAA, 0x116, 6);
    b(0xB2, s.b(0x128)), b(0xB3, s.b(0x129)), b(0xB4, s.b(0x12A));
    const u32 gears = std::min<u32>(s.b(0x155), 7);
    for (u32 i = 0; i < 8; ++i) h(0xB6 + 2 * i, i <= gears ? s.h(0x138 + 2 * i) : 0xFFFF);
    h(0xC6, s.h(0x150)), b(0xC8, gears), b(0xC9, s.b(0x160)), b(0xCB, s.b(0x157)), b(0xCC, s.b(0x161));
    h(0xCE, s.h(0x164)), h(0xD0, s.h(0x166)), h(0xD2, s.h(0x174)), h(0xD4, s.h(0x176));
    b(0xD6, s.b(0x168)), b(0xD7, s.b(0x169)), bytes(0xD8, 0x16A, 6);
    bytes(0xDE, 0x178, 3);
    // The steering curves.
    b(0xE2, s.b(0x180)), bytes(0xE3, 0x184, 12);
    b(0xEF, s.b(0x181)), bytes(0xF0, 0x190, 12);
    b(0xFC, s.b(0x182)), bytes(0xFD, 0x19C, 12);
    // The aids.
    b(0x109, s.b(0x1B8)), b(0x10A, s.b(0x1A8)), b(0x10B, s.b(0x1A9));
    b(0x10C, s.b(0x1BA)), b(0x10D, s.b(0x1BB)), b(0x111, s.b(0x1BC)), b(0x114, s.b(0x1BD)), b(0x115, s.b(0x1BE)), b(0x116, s.b(0x1BF));
    b(0x10E, s.b(0x1C0)), b(0x10F, s.b(0x1C1)), b(0x110, s.b(0x1C2)), b(0x117, s.b(0x1C3));
    b(0x112, s.b(0x1AA)), b(0x113, s.b(0x1AB));
    // The tires: the rim in whole inches.
    for (u32 a = 0; a < 2; ++a) {
        const u32 rim = s.b(0x1CA + a);
        b(0x11A + a, s.b(0x1C8 + a)), b(0x11C + a, rim < 61 ? rim : (rim + 5) / 10), b(0x11E + a, s.b(0x1CC + a));
    }
    bytes(0x120, 0x1CE, 2), bytes(0x122, 0x1D0, 16);
    for (u32 row = 0; row < 16; ++row) b(0x132 + row, s.b(0x1F0 + u32(kRowSurface[row])));
    for (int k = 0; k < 6; ++k) tire_block(s, Spec::tire_block(k), to, (k < 3 ? 0x142u : 0x292u) + 0x70u * u32(k % 3));
}

} // namespace gt4r::sim
