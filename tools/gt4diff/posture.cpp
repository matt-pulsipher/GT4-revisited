// The driver's posture against the program's own, from the original's saved states:
//
//   gt4r_diff <disc> posture <eeMemory.bin>...
//
// The program's posture (RaceCarModel vf30) is run for every car model of the state, and the solver's inputs are taken
// where it enters (0x416FD8): ours from the same inputs must leave the same twenty bones, to the last bit.

#include "orig.h"
#include "gt4r/race/driver_posture.h"
#include "gt4r/race/posture_hands.h"
#include "gt4r/race/posture_solver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace gt4r::diff {

namespace rp = gt4r::race::posture;

namespace {

constexpr u32 kSolve = 0x416FD8, kRaceDriverVtable = 0x0067FB50, kModelPosture = 0x390F70, kScreenLevel = 0x3B1750;
constexpr u32 kHuman = 0x5E0, kSecondHuman = 0xE20, kHumanVtable = 0x7DC;

// Where each bone (in the name table's order) is in the skeleton, and which have two joints.
constexpr u32 kBoneAt[rp::kNodes] = {0,     0x70,  0x110, 0x1B0, 0xC0,  0x160, 0x200, 0x250, 0x2A0, 0x2F0,
                                     0x360, 0x400, 0x3B0, 0x450, 0x4A0, 0x4F0, 0x540, 0x590, 0x5E0, 0x630};

struct Reader {
    Ee& ee;
    rp::F f(u32 a) { return rp::F::raw(ee.r32(a)); }
    rp::V3 v(u32 a) { return {f(a), f(a + 4), f(a + 8)}; }
    rp::Quat q(u32 a) { return {f(a), f(a + 4), f(a + 8), f(a + 12)}; }
};

rp::Skeleton read_skeleton(Ee& ee, u32 a) {
    Reader r{ee};
    rp::Skeleton s;
    for (int n = 0; n < rp::kNodes; ++n) {
        const u32 b = a + kBoneAt[n];
        s[n].pos = r.v(b);
        s[n].q = r.q(b + 0x10);
        s[n].vec = r.v(b + 0x30);
        s[n].len = r.f(b + 0x40);
        s[n].inv = r.f(b + 0x44);
        if (n == rp::kPelvis || n == rp::kScapula) s[n].left = r.v(b + 0x50), s[n].right = r.v(b + 0x60);
    }
    return s;
}

rp::Places read_places(Ee& ee, u32 a) {
    Reader r{ee};
    rp::Places p;
    p.pos = r.v(a);
    p.turn = r.q(a + 0x10);
    p.foot[0] = r.v(a + 0x20), p.foot[1] = r.v(a + 0x30);
    p.seat = r.v(a + 0x40);
    p.wheel = r.v(a + 0x50);
    for (u32 k = 0; k < 3; ++k) p.lever[k] = r.v(a + 0x60 + 0x10 * k);
    for (u32 k = 0; k < 2; ++k) p.grip[k] = r.v(a + 0x90 + 0x10 * k), p.grip_axis[k] = r.v(a + 0xC0 + 0x10 * k);
    p.column = r.v(a + 0xB0);
    p.wheel_radius = r.f(a + 0xE0);
    for (u32 k = 0; k < 9; ++k) p.wheel_frame.m[k] = r.f(a + 0xF0 + 4 * k);
    p.shift_hand = int(ee.r32(a + 0x120));
    p.steer_low = r.f(a + 0x124);
    p.steer_high = r.f(a + 0x128);
    p.handlebars = int(ee.r32(a + 0x12C));
    p.gesture = r.f(a + 0x130);
    return p;
}

rp::Controls read_controls(Ee& ee, u32 a) {
    Reader r{ee};
    rp::Controls c;
    const auto pair = [&](rp::F (&out)[2], u32 at) { out[0] = r.f(a + at), out[1] = r.f(a + at + 4); };
    const auto ints = [&](int (&out)[2], u32 at) { out[0] = int(ee.r32(a + at)), out[1] = int(ee.r32(a + at + 4)); };
    pair(c.elbow, 0), pair(c.knee, 8), pair(c.shank_in, 0x10), pair(c.hand_turn, 0x18), pair(c.grip_side, 0x20), pair(c.wrist, 0x28);
    pair(c.heel_roll, 0x30), pair(c.heel_tilt, 0x38), pair(c.clavicle, 0x40), pair(c.clavicle_forward, 0x48);
    c.twist = r.f(a + 0x50), c.pelvis_roll = r.f(a + 0x54), c.pelvis_twist = r.f(a + 0x58), c.lean = r.f(a + 0x5C);
    c.nod = r.f(a + 0x60), c.head_pitch = r.f(a + 0x64), c.head_up_part = r.f(a + 0x68), c.head_roll = r.f(a + 0x6C);
    c.head_yaw_range = r.f(a + 0x70), c.gaze_twist = r.f(a + 0x74);
    for (u32 k = 0; k < 3; ++k) c.head[k] = r.f(a + 0x78 + 4 * k);
    c.head_up = r.v(a + 0x90);
    c.auto_clavicle = int(ee.r32(a + 0xA8)), c.keep_hands = int(ee.r32(a + 0xAC));
    ints(c.hand_raised, 0xB0);
    c.spine = int(ee.r32(a + 0xB8)), c.arms = int(ee.r32(a + 0xBC)), c.legs = int(ee.r32(a + 0xC0)), c.head_on = int(ee.r32(a + 0xC4));
    c.no_twist = int(ee.r32(a + 0xC8)), c.plain_hands = int(ee.r32(a + 0xCC)), c.keep_neck = int(ee.r32(a + 0xD4));
    c.raw_lean = int(ee.r32(a + 0x200));
    for (u32 k = 0; k < 3; ++k) c.phase_steer[k] = r.f(a + 0x210 + 4 * k);
    c.wheel_angle = r.f(a + 0x21C);
    pair(c.grip_from, 0x220), pair(c.grip_to, 0x228), pair(c.grip_blend, 0x230), pair(c.grip_out, 0x238), pair(c.grip_back, 0x240);
    pair(c.wrist_base, 0x248);
    for (u32 k = 0; k < 3; ++k) pair(c.wrist_phase[k], 0x250 + 8 * k);
    c.recline = r.f(a + 0x268), c.recline_max = r.f(a + 0x26C), c.recline_min = r.f(a + 0x270);
    c.foot_offset[0] = r.v(a + 0x280), c.foot_offset[1] = r.v(a + 0x290);
    pair(c.clavicle_base, 0x2A0), pair(c.clavicle_forward_base, 0x2A8);
    c.chest_offset = r.v(a + 0x2B0), c.chest_offset_world = r.v(a + 0x2C0), c.spine_length = r.f(a + 0x2D0);
    c.free_hands = int(ee.r32(a + 0x2D4)), c.fixed_recline = int(ee.r32(a + 0x2D8));
    ints(c.raise_choice, 0x2DC);
    c.grip_kind = int(ee.r32(a + 0x2E4));
    for (u32 k = 0; k < 3; ++k) c.gesture_offset[k] = r.v(a + 0x2F0 + 0x10 * k), pair(c.gesture_grip[k], 0x320 + 8 * k);
    c.seat_offset = r.v(a + 0x340), c.phase = r.f(a + 0x350);
    return c;
}

// The fields of two blocks that differ.
struct FieldCheck {
    int bad = 0;
    bool verbose;
    void operator()(const char* name, rp::F a, rp::F b) {
        if (a.bits == b.bits || !((a.bits | b.bits) & 0x7FFFFFFF)) return;
        ++bad;
        if (verbose) std::printf("    %s ours %.7g (%08X) program %.7g (%08X)\n", name, a.host(), a.bits, b.host(), b.bits);
    }
    void operator()(const char* name, rp::V3 a, rp::V3 b) { (*this)(name, a.x, b.x), (*this)(name, a.y, b.y), (*this)(name, a.z, b.z); }
    void operator()(const char* name, int a, int b) {
        if (a == b) return;
        ++bad;
        if (verbose) std::printf("    %s ours %d program %d\n", name, a, b);
    }
    template <class T, size_t N> void operator()(const char* name, const T (&a)[N], const T (&b)[N]) {
        for (size_t i = 0; i < N; ++i) (*this)(name, a[i], b[i]);
    }
};

int compare_inputs(const rp::PostureInput& a, const rp::Controls& c, const rp::Places& p, const rp::Gaze* g, bool verbose) {
    FieldCheck k{0, verbose};
    const rp::Controls& x = a.controls;
#define FIELD(n) k(#n, x.n, c.n)
    FIELD(elbow), FIELD(knee), FIELD(shank_in), FIELD(hand_turn), FIELD(grip_side), FIELD(wrist), FIELD(heel_roll), FIELD(heel_tilt);
    FIELD(clavicle), FIELD(clavicle_forward), FIELD(twist), FIELD(pelvis_roll), FIELD(pelvis_twist), FIELD(lean), FIELD(nod);
    FIELD(head_pitch), FIELD(head_up_part), FIELD(head_roll), FIELD(head_yaw_range), FIELD(gaze_twist), FIELD(head), FIELD(head_up);
    FIELD(auto_clavicle), FIELD(keep_hands), FIELD(hand_raised), FIELD(spine), FIELD(arms), FIELD(legs), FIELD(head_on), FIELD(no_twist);
    FIELD(plain_hands), FIELD(keep_neck), FIELD(raw_lean), FIELD(phase_steer), FIELD(wheel_angle), FIELD(grip_from), FIELD(grip_to);
    FIELD(grip_blend), FIELD(grip_out), FIELD(grip_back), FIELD(wrist_base), FIELD(wrist_phase[0]);
    // (the other hand's later two are what the stack held)
    if (p.shift_hand) FIELD(wrist_phase[1][p.shift_hand - 1]), FIELD(wrist_phase[2][p.shift_hand - 1]);
    FIELD(recline), FIELD(recline_max), FIELD(recline_min), FIELD(foot_offset), FIELD(clavicle_base), FIELD(clavicle_forward_base);
    FIELD(chest_offset), FIELD(chest_offset_world), FIELD(spine_length), FIELD(free_hands), FIELD(fixed_recline), FIELD(raise_choice);
    FIELD(grip_kind), FIELD(gesture_offset), FIELD(gesture_grip[0]), FIELD(gesture_grip[1]), FIELD(gesture_grip[2]), FIELD(seat_offset), FIELD(phase);
#undef FIELD
    const rp::Places& y = a.places;
#define FIELD(n) k(#n, y.n, p.n)
    FIELD(pos), FIELD(turn.x), FIELD(turn.y), FIELD(turn.z), FIELD(turn.w), FIELD(foot), FIELD(seat), FIELD(wheel), FIELD(lever), FIELD(grip);
    FIELD(column), FIELD(grip_axis), FIELD(wheel_radius), FIELD(wheel_frame.m), FIELD(shift_hand), FIELD(steer_low), FIELD(steer_high);
    FIELD(handlebars), FIELD(gesture);
#undef FIELD
    k("gaze", int(a.has_gaze), int(g != nullptr));
    if (g && a.has_gaze) k("gaze kind", a.gaze.kind, g->kind), k("gaze at", a.gaze.at, g->at);
    return k.bad;
}

int compare(const char* what, const rp::Skeleton& ours, const rp::Skeleton& theirs, bool verbose) {
    int bad = 0;
    for (int n = 0; n < rp::kNodes; ++n) {
        const rp::Bone &a = ours[n], &b = theirs[n];
        const u32 x[7] = {a.pos.x.bits, a.pos.y.bits, a.pos.z.bits, a.q.x.bits, a.q.y.bits, a.q.z.bits, a.q.w.bits};
        const u32 y[7] = {b.pos.x.bits, b.pos.y.bits, b.pos.z.bits, b.q.x.bits, b.q.y.bits, b.q.z.bits, b.q.w.bits};
        if (!std::memcmp(x, y, sizeof x)) continue;
        bool zero = true; // +0 against -0 is the same value
        for (int k = 0; k < 7; ++k)
            if (x[k] != y[k] && ((x[k] | y[k]) & 0x7FFFFFFF)) zero = false;
        if (zero) continue;
        ++bad;
        if (verbose)
            std::printf("    %s %-13s ours %.5f %.5f %.5f | %.5f %.5f %.5f %.5f\n    %*s program %.5f %.5f %.5f | %.5f %.5f %.5f %.5f\n", what,
                        rp::kNodeNames[n], a.pos.x.host(), a.pos.y.host(), a.pos.z.host(), a.q.x.host(), a.q.y.host(), a.q.z.host(), a.q.w.host(),
                        int(std::strlen(what)) + 10, "", b.pos.x.host(), b.pos.y.host(), b.pos.z.host(), b.q.x.host(), b.q.y.host(), b.q.z.host(),
                        b.q.w.host());
    }
    return bad;
}

Bytes read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int run_posture(Disc& disc, const std::vector<std::string>& paths) {
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    int solves = 0, equal = 0, inputs = 0, inputs_equal = 0;
    for (const std::string& path : paths) {
        const Bytes mem = read_all(path);
        if (mem.size() != Ee::kRamSize) {
            std::printf("%s: not a 32 MB main memory\n", path.c_str());
            continue;
        }
        std::vector<u32> models; // RaceCarModels whose first figure has a set
        for (u32 a = 0x100000; a + 4 <= mem.size(); a += 4) {
            u32 w;
            std::memcpy(&w, mem.data() + a, 4);
            if (w != kRaceDriverVtable || a < kHumanVtable + kHuman) continue;
            const u32 model = a - kHumanVtable - kHuman;
            u32 set, sponsor, made;
            std::memcpy(&set, mem.data() + model + kHuman, 4);
            std::memcpy(&sponsor, mem.data() + model + 4, 4);
            std::memcpy(&made, mem.data() + model + 0x1670, 4);
            if (set >= 0x100000 && set < Ee::kRamSize && sponsor >= 0x100000 && sponsor < Ee::kRamSize && made) models.push_back(model);
        }
        std::printf("%s: %zu models\n", path.c_str(), models.size());
        for (u32 model : models)
            for (int recorded = 0; recorded < 2; ++recorded)
                for (float alpha : {0.0f, 0.37f, 1.0f}) {
                    ee.reset();
                    ee.protect_text = false;
                    ee.write_bytes(0, mem);
                    ee.clear_probes();
                    ee.hle(kScreenLevel, [](Ee& e) { e.w32(u32(e.a(0)) + 0x6A0, 0); }, "screen level");
                    const u32 human = model + kHuman;
                    ee.w32(human + 0x7B8, 1);
                    ee.w32(human + 0x7C0, 0);
                    bool seen = false;
                    int inputs_bad = -1;
                    // The car file in memory: the block before the driver's set.
                    u32 car_file = 0;
                    {
                        const u32 set = ee.r32(human);
                        for (u32 a = set & ~15u; a > 0x100000 && set - a < 0x200000; a -= 16)
                            if (ee.r32(a) == 0x34524143) {
                                car_file = a;
                                break;
                            }
                    }
                    u32 at = 0;
                    rp::Skeleton s;
                    ee.probe(kSolve, [&](Ee& e) {
                        rp::Controls c = read_controls(e, u32(e.a(0)));
                        at = u32(e.a(1));
                        s = read_skeleton(e, at);
                        const rp::Places p = read_places(e, u32(e.a(2)));
                        rp::Gaze g;
                        const u32 gp = u32(e.a(3));
                        if (gp) {
                            Reader r{e};
                            g.kind = int(e.r32(gp));
                            g.at = r.v(gp + 0x10);
                        }
                        if (std::getenv("GT4R_POSTURE_SHOW"))
                            std::printf("  model %06X rec %d alpha %.2f: spine %d arms %d legs %d head %d; plain %d no twist %d keep neck %d free %d lever hand %d gesture %.3f phase %.3f wheel %.3f grips %.3f %.3f recline %.3f lean %.3f gaze %.2f %.2f %.2f seat %.3f %.3f %.3f pos %.2f %.2f %.2f\n",
                                        model, recorded, alpha, c.spine, c.arms, c.legs, c.head_on, c.plain_hands, c.no_twist, c.keep_neck, c.free_hands, p.shift_hand,
                                        p.gesture.host(), c.phase.host(), c.wheel_angle.host(), c.grip_from[0].host(), c.grip_from[1].host(), c.recline.host(),
                                        c.lean.host(), g.at.x.host(), g.at.y.host(), g.at.z.host(), p.seat.x.host(), p.seat.y.host(), p.seat.z.host(), p.pos.x.host(), p.pos.y.host(), p.pos.z.host());
                        // The inputs, from the car's parts block, its place and the record of its driver.
                        if (car_file) {
                            // (the file as it is on the disc: its offsets are addresses in memory)
                            Bytes car_bytes(e.r32(car_file + 8));
                            e.read_bytes(car_file, car_bytes.data(), u32(car_bytes.size()));
                            const auto rebase = [&](u32 at) {
                                u32 w;
                                std::memcpy(&w, car_bytes.data() + at, 4);
                                if (w) w -= car_file;
                                std::memcpy(car_bytes.data() + at, &w, 4);
                                return w;
                            };
                            for (u32 at = 0x14; at < 0x38; at += 4) rebase(at);
                            rebase(rebase(0x10) + 0x50);
                            const rp::Cockpit cockpit = rp::read_posture_cockpit(ByteSpan(car_bytes), false);
                            rp::PostureInput in;
                            if (e.r32(human + 0x7B4)) {
                                in = rp::still_posture(cockpit);
                            } else {
                                Reader r{e};
                                const u32 block = e.r32(model + 4) + 0x9A0;
                                rp::DriverRecord before, after;
                                for (u32 n = 0; n < 64; ++n) before.v[n] = r.f(block + 0x160 + 4 * n), after.v[n] = r.f(block + 0x260 + 4 * n);
                                in = rp::driving_posture(cockpit, rp::blend_records(before, after, rp::F(alpha)), r.v(human + 0x7C4), r.v(human + 0x7D0), false);
                                if (!recorded) { // (a call with no reduction by class)
                                    const rp::Controls keep = in.controls;
                                    in.controls.arms = in.controls.legs = 1;
                                    in.controls.plain_hands = in.controls.no_twist = 0;
                                    (void)keep;
                                }
                            }
                            inputs_bad = compare_inputs(in, c, p, gp ? &g : nullptr, alpha == 0.37f);
                        }
                        rp::solve(c, s, p, gp ? &g : nullptr);
                        seen = true;
                    });
                    try {
                        ee.call(kModelPosture, {Arg::ptr(model), Arg::integer(0), Arg::integer(recorded), Arg::flt(alpha)}, 50'000'000);
                    } catch (const std::exception& e) {
                        std::printf("  model %06X recorded %d alpha %.2f: %s\n", model, recorded, alpha, e.what());
                        continue;
                    }
                    if (!seen) continue;
                    ++solves;
                    const int bad = compare("solver", s, read_skeleton(ee, at), alpha == 0.37f);
                    if (inputs_bad > 0) std::printf("  model %06X recorded %d alpha %.2f: %d inputs differ\n", model, recorded, alpha, inputs_bad);
                    inputs += inputs_bad >= 0, inputs_equal += inputs_bad == 0;
                    if (!bad) ++equal;
                    else std::printf("  model %06X recorded %d alpha %.2f: %d bones differ\n", model, recorded, alpha, bad);
                }
    }
    std::printf("posture: %d solves, %d with every bone equal; %d inputs, %d equal\n", solves, equal, inputs, inputs_equal);
    return solves == equal ? 0 : 1;
}

} // namespace gt4r::diff
