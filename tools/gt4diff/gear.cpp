// The driver's gear change against the program's own, call by call, on a saved state's memory:
//
//   gt4r_diff <disc> gear <eeMemory.bin>
//
// The lever's place for a gear (0x411EB0), the shift reach (0x40CA58) and the hand along the gate (0x411628) on drawn
// inputs, every output to the last bit. (The whole step with them: gt4r_diff <disc> posture, GT4R_POSTURE_GEAR=1.)

#include "orig.h"
#include "gt4r/race/driver_gear.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

namespace gt4r::diff {

namespace rp = gt4r::race::posture;

namespace {

constexpr u32 kGatePlace = 0x411EB0, kShiftReach = 0x40CA58, kGateHand = 0x411628, kUpRpm = 0x36BEE0, kDownRpm = 0x36BF28;
constexpr u32 kScratch = 0x1F00000; // (below the interpreter's stack)
constexpr u32 kRaceCarVtable = 0x0067FB50, kHuman = 0x5E0, kHumanVtable = 0x7DC;

rp::F rf(Ee& ee, u32 a) { return rp::F::raw(ee.r32(a)); }

} // namespace

int run_gear(Disc& disc, const std::vector<std::string>& paths) {
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    int bad = 0, calls = 0;
    std::mt19937 rng(12345);
    const auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    const auto pick = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    for (const std::string& path : paths) {
        std::ifstream in(path, std::ios::binary);
        const Bytes mem((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (mem.size() != Ee::kRamSize) {
            std::printf("%s: not a 32 MB main memory\n", path.c_str());
            continue;
        }
        const auto fresh = [&] {
            ee.reset();
            ee.protect_text = false;
            ee.write_bytes(0, mem);
            ee.clear_probes();
        };
        fresh();
        // The state's layouts, and drawn ones.
        std::vector<rp::GearLayout> layouts;
        std::vector<u32> cars;
        for (u32 a = 0x100000; a + 4 <= mem.size(); a += 4) {
            u32 w;
            std::memcpy(&w, mem.data() + a, 4);
            if (w != kRaceCarVtable || a < kHumanVtable + kHuman) continue;
            const u32 car = ee.r32(a - kHumanVtable - kHuman + 4);
            if (car < 0x100000 || car >= Ee::kRamSize - 0x1100) continue;
            cars.push_back(car);
            rp::GearLayout g;
            g.kind = int(ee.r32(car + 0x1080)), g.offset = int(ee.r32(car + 0x1084)), g.reverse = int(ee.r32(car + 0x1088)), g.count = int(ee.r32(car + 0x108C));
            layouts.push_back(g);
        }
        for (int k = 0; k < 40; ++k) layouts.push_back({pick(0, 5), pick(-2, 3), pick(-3, 9), pick(1, 7)});
        // 0x411EB0.
        int places = 0, places_bad = 0;
        for (const rp::GearLayout& g : layouts)
            for (int gear = -2; gear <= 10; ++gear) {
                ee.w32(kScratch, 0x12345678), ee.w32(kScratch + 4, 0x12345678);
                ee.call(kGatePlace, {Arg::integer(g.kind), Arg::integer(g.offset), Arg::integer(g.reverse), Arg::integer(g.count), Arg::integer(gear),
                                     Arg::ptr(kScratch), Arg::ptr(kScratch + 4)});
                rp::F x, y;
                rp::gate_place(g, gear, x, y);
                ++places;
                if (x.bits != ee.r32(kScratch) || y.bits != ee.r32(kScratch + 4)) {
                    if (places_bad++ < 10)
                        std::printf("  place kind %d %d %d %d gear %d: ours %08X %08X program %08X %08X\n", g.kind, g.offset, g.reverse, g.count, gear, x.bits, y.bits,
                                    ee.r32(kScratch), ee.r32(kScratch + 4));
                }
            }
        std::printf("%s: the lever's place, %d calls, %d differ\n", path.c_str(), places, places_bad);
        bad += places_bad, calls += places;
        // 0x40CA58 on each car of the state, its gear, engine speed, pedals and target speeds drawn.
        int reaches = 0, reaches_bad = 0, hits[3] = {0, 0, 0};
        for (u32 car : cars) {
            const u32 p = ee.r32(car + 0x10);
            const int top = ee.r8(p + 0x236);
            for (int k = 0; k < 400; ++k) {
                fresh();
                const int gear = pick(0, top);
                ee.w8(car + 0x624, u8(gear));
                const s32 up = s32(ee.call(kUpRpm, {Arg::ptr(car), Arg::integer(gear)}).v0_s32());
                const s32 down = gear >= 2 ? ee.call(kDownRpm, {Arg::ptr(car), Arg::integer(gear)}).v0_s32() : 0;
                const s32 centre = pick(0, 1) ? up : down;
                ee.w32(car + 0x738, rp::F(float(centre) + uni(-1600, 1600)).bits);
                const float pedal = uni(0, 1);
                ee.w32(car + 0x58C, rp::F(pick(0, 3) == 0 ? 0.0f : pedal).bits);
                ee.w32(car + 0x590, rp::F(pick(0, 2) == 0 ? 0.0f : uni(0, 1)).bits);
                ee.w32(car + 0x38, rp::F(uni(0, 80)).bits), ee.w32(car + 0x3C, rp::F(uni(0, 80)).bits);
                for (u32 o = 0; o < 16; o += 4) ee.w32(kScratch + o, 0);
                ee.call(kShiftReach, {Arg::ptr(car), Arg::ptr(kScratch), Arg::ptr(kScratch + 4), Arg::ptr(kScratch + 8), Arg::ptr(kScratch + 12)});
                rp::ShiftInputs s;
                s.rpm = gt3r::ee::to_int(rf(ee, car + 0x738)), s.top = top, s.gear = gear;
                s.throttle = rf(ee, car + 0x58C), s.brake = rf(ee, car + 0x590);
                s.suggested = s32(ee.r32(kScratch + 12)), s.up_rpm = up, s.down_rpm = down;
                const rp::ShiftReach r = rp::shift_reach(s);
                const bool theirs[3] = {ee.r32(kScratch) != 0, ee.r32(kScratch + 4) != 0, ee.r32(kScratch + 8) != 0}, ours[3] = {r.up, r.down, r.far};
                ++reaches;
                for (int n = 0; n < 3; ++n) hits[n] += theirs[n];
                if (std::memcmp(theirs, ours, sizeof ours)) {
                    if (reaches_bad++ < 10)
                        std::printf("  reach car %06X gear %d rpm %d up %d down %d throttle %.3f brake %.3f suggested %d: ours %d %d %d program %d %d %d\n", car, gear,
                                    s.rpm, up, down, s.throttle.host(), s.brake.host(), s.suggested, ours[0], ours[1], ours[2], theirs[0], theirs[1], theirs[2]);
                }
            }
        }
        std::printf("  the shift reach, %d calls (up %d, down %d, more than one %d), %d differ\n", reaches, hits[0], hits[1], hits[2], reaches_bad);
        bad += reaches_bad, calls += reaches;
        // 0x411628: the hand moving the lever from one gear's place to another's, step by step.
        fresh();
        int hands = 0, hands_bad = 0, arrived = 0;
        for (const rp::GearLayout& g : layouts) {
            if (g.kind != 1 && g.kind != 4) continue;
            for (int run = 0; run < 12; ++run) {
                const int from = pick(-1, g.count + 1), to = pick(-1, g.count + 1);
                rp::F x, y, vx, vy, t, wx, wy;
                rp::gate_place(g, from, x, y);
                const u32 X = kScratch, Y = X + 4, VX = X + 8, VY = X + 12, T = X + 16, GX = X + 20, GY = X + 24;
                ee.w32(X, x.bits), ee.w32(Y, y.bits), ee.w32(VX, 0), ee.w32(VY, 0), ee.w32(T, rp::F(uni(0, 1.2f)).bits);
                t = rf(ee, T);
                const rp::F dt(1.0f / 60.0f);
                for (int step = 0; step < 200; ++step) {
                    ee.w32(Ee::kStackTop + 0x00, VY), ee.w32(Ee::kStackTop + 0x08, T), ee.w32(Ee::kStackTop + 0x10, GX), ee.w32(Ee::kStackTop + 0x18, GY);
                    ee.w32(GX, 0x7F7FFFFF), ee.w32(GY, 0x7F7FFFFF);
                    const s32 done = ee.call(kGateHand, {Arg::integer(g.kind), Arg::integer(g.offset), Arg::integer(g.reverse), Arg::integer(g.count),
                                                         Arg::integer(to), Arg::ptr(X), Arg::ptr(Y), Arg::ptr(VX), Arg::fbits(dt.bits)})
                                         .v0_s32();
                    const bool ours = rp::gate_hand(g, to, dt, x, y, vx, vy, t, wx, wy);
                    ++hands;
                    const u32 a[7] = {x.bits, y.bits, vx.bits, vy.bits, t.bits, wx.bits, wy.bits};
                    const u32 b[7] = {ee.r32(X), ee.r32(Y), ee.r32(VX), ee.r32(VY), ee.r32(T), ee.r32(GX), ee.r32(GY)};
                    if (std::memcmp(a, b, sizeof a) || ours != (done != 0)) {
                        if (hands_bad++ < 10) {
                            std::printf("  hand kind %d %d %d %d %d -> %d step %d: ours", g.kind, g.offset, g.reverse, g.count, from, to, step);
                            for (u32 v : a) std::printf(" %08X", v);
                            std::printf(" %d program", int(ours));
                            for (u32 v : b) std::printf(" %08X", v);
                            std::printf(" %d\n", done);
                        }
                        break;
                    }
                    if (ours) {
                        ++arrived;
                        break;
                    }
                }
            }
        }
        std::printf("  the hand along the gate, %d calls (%d runs to their gear), %d differ\n", hands, arrived, hands_bad);
        bad += hands_bad, calls += hands;
    }
    std::printf("gear: %d calls, %d differ\n", calls, bad);
    return bad ? 1 : 0;
}

} // namespace gt4r::diff
