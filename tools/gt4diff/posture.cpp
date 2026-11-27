// The driver's posture against the program's own, from the original's saved states:
//
//   gt4r_diff <disc> posture <eeMemory.bin>...
//
// The program's posture (RaceCarModel vf30) is run for every car model of the state, and the solver's inputs are taken
// where it enters (0x416FD8): ours from the same inputs must leave the same twenty bones, to the last bit.

#include "orig.h"
#include "gt4r/race/driver_posture.h"
#include "gt4r/race/driver_state.h"
#include "gt4r/race/posture_hands.h"
#include "gt4r/race/posture_solver.h"

#include <algorithm>
#include <cmath>
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
        if (a.bits == b.bits || !((a.bits | b.bits) & 0x7F800000)) return;
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

// The program's posture (RaceCarModel vf30) of a model on the memory as it is, against ours from the same record: the solver's
// inputs and the bones.
static void check_solve(Ee& ee, u32 model, int recorded, float alpha, int& solves, int& equal, int& inputs, int& inputs_equal) {
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
        const bool second = at == model + kSecondHuman + 0x10;
        const u32 human = model + (second ? kSecondHuman : kHuman);
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
            const rp::Cockpit cockpit = rp::read_posture_cockpit(ByteSpan(car_bytes), second);
            rp::PostureInput in;
            if (e.r32(human + 0x7B4)) {
                in = rp::still_posture(cockpit);
            } else {
                Reader r{e};
                const u32 block = e.r32(model + 4) + 0x9A0 + (second ? 0x370 : 0);
                rp::DriverRecord before, after;
                for (u32 n = 0; n < 64; ++n) before.v[n] = r.f(block + 0x160 + 4 * n), after.v[n] = r.f(block + 0x260 + 4 * n);
                // (a call with the record flag clear: no reduction by class)
                in = rp::driving_posture(cockpit, rp::blend_records(before, after, rp::F(alpha)), r.v(human + 0x7C4), r.v(human + 0x7D0), second, recorded != 0);
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
        return;
    }
    if (!seen) return;
    ++solves;
    const int bad = compare("solver", s, read_skeleton(ee, at), alpha == 0.37f);
    if (inputs_bad > 0) std::printf("  model %06X recorded %d alpha %.2f: %d inputs differ\n", model, recorded, alpha, inputs_bad);
    inputs += inputs_bad >= 0, inputs_equal += inputs_bad == 0;
    if (!bad) ++equal;
    else std::printf("  model %06X recorded %d alpha %.2f: %d bones differ\n", model, recorded, alpha, bad);
}

// The driver's step (0x403260) on a state's memory against ours: the fields ours has.
static int gaze_steps = 0, gaze_equal = 0, gaze_aimed = 0;
static int stop_solves = 0, stop_equal = 0, stop_inputs = 0, stop_inputs_equal = 0;
// `stop_at`: at that step the program's posture run on the stepped record (check_solve) and the steps stopped.
static void compare_steps(Ee& ee, const Bytes& mem, u32 model, int& steps, int& equal, int stop_at = -1) {
    bool gaze_on = true;
    const bool gaze_drive = std::getenv("GT4R_POSTURE_GAZE") != nullptr;
    ee.reset();
    ee.protect_text = false;
    ee.write_bytes(0, mem);
    ee.clear_probes();
    const u32 car = ee.r32(model + 4), block = car + 0x9A0;
    Reader r{ee};
    if (!ee.r32(block + 0x368)) return;
    u32 car_file = 0;
    const u32 set = ee.r32(model + kHuman);
    for (u32 a = set & ~15u; a > 0x100000 && set - a < 0x200000; a -= 16)
        if (ee.r32(a) == 0x34524143) {
            car_file = a;
            break;
        }
    if (!car_file) return;
    Bytes file(ee.r32(car_file + 8));
    ee.read_bytes(car_file, file.data(), u32(file.size()));
    const auto rebase = [&](u32 at) {
        u32 w;
        std::memcpy(&w, file.data() + at, 4);
        if (w) w -= car_file;
        std::memcpy(file.data() + at, &w, 4);
        return w;
    };
    for (u32 at = 0x14; at < 0x38; at += 4) rebase(at);
    rebase(rebase(0x10) + 0x50);
    const rp::Cockpit cockpit = rp::read_posture_cockpit(ByteSpan(file), false);
    if (cockpit.style != 0 || rp::figure_class(cockpit.kind) == rp::FigureClass::Board) return;
    rp::HandSpeeds speeds;
    for (u32 k = 0; k < 11; ++k) speeds[k] = r.f(0x622A20 + 4 * k);
    // Several steps on, the steering command's follower swung from one lock to the other.
    rp::DriverState s;
    const auto load = [&] {
        for (u32 n = 0; n < 64; ++n) s.before.v[n] = r.f(block + 0x160 + 4 * n), s.after.v[n] = r.f(block + 0x160 + 4 * n);
        const u32 k = block + 0x40;
        for (u32 h = 0; h < 2; ++h) {
            s.hand[h] = r.f(k + 0x90 + 4 * h), s.way[h] = r.f(k + 0x88 + 4 * h);
            s.state[h] = u8(ee.r32(block + 0x30) >> (8 * h)), s.was[h] = u8(ee.r32(block + 0x30) >> (16 + 8 * h));
        }
        s.idle = r.f(k + 0x74);
        for (u32 n = 0; n < 4; ++n) s.rates[n] = r.f(k + 0xAC + 4 * n);
        s.seat = r.v(k + 0x3C), s.seat_rate = r.v(k + 0x48), s.seat_accel = r.v(k + 0x54);
        s.lean_neck_rate = r.f(k), s.nod_neck_rate = r.f(k + 4), s.lean_rate = r.f(k + 8), s.nod_rate = r.f(k + 0xC), s.heave_rate = r.f(k + 0x10);
        s.height = r.f(k + 0x60), s.lean_left = r.f(k + 0x64), s.nod_left = r.f(k + 0x68);
        s.tilt = r.f(k + 0xD0), s.tilt_rate = r.f(k + 0xD4), s.head_rate = r.f(k + 0xD8);
        s.first = ee.r32(block + 0x28) == 1;
        s.hand_rate = r.f(k + 0x1C), s.lever_rate[0] = r.f(k + 0x20), s.lever_rate[1] = r.f(k + 0x24);
        s.grip_rate[0] = r.f(k + 0x28), s.grip_rate[1] = r.f(k + 0x2C), s.rest = r.f(k + 0x6C), s.gate_wait = r.f(k + 0x70), s.busy = r.f(k + 0x78);
        s.held_gear = int(ee.r32(block)), s.seen_gear = int(ee.r32(block + 4)), s.phase = int(ee.r32(block + 0x18));
        s.gaze.state = int(ee.r32(block + 0x1C));
        s.gaze.rate = r.v(k + 0x30), s.gaze.target = r.v(k + 0x7C), s.gaze.held = r.f(k + 0xF4), s.gaze.aim_angle = r.f(k + 0xF8);
    };
    load();
    // (GT4R_POSTURE_GEAR: the car's gear, engine speed and pedals driven, the gear change compared)
    const bool gear_on = std::getenv("GT4R_POSTURE_GEAR") != nullptr;
    rp::GearInput gin;
    if (const char* lay = std::getenv("GT4R_POSTURE_LAYOUT"); gear_on && lay) { // (another layout put in the car's: "1,1,6,5")
        int v[4] = {0, 0, 0, 0};
        if (std::sscanf(lay, "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4)
            for (u32 n = 0; n < 4; ++n) ee.w32(car + 0x1080 + 4 * n, u32(v[n]));
    }
    gin.layout = {int(ee.r32(car + 0x1080)), int(ee.r32(car + 0x1084)), int(ee.r32(car + 0x1088)), int(ee.r32(car + 0x108C))};
    const u32 params = ee.r32(car + 0x10);
    const int top = ee.r8(params + 0x236);
    int phases_seen = 0;
    for (int step = 0; step < (gear_on ? 720 : 240); ++step) {
        float follow = 12.0f * std::sin(float(step) * 0.06f) * std::sin(float(step) * 0.013f);
        if (gear_on) {
            if (step >= 240) follow *= 0.05f; // (the wheel near the middle: the reach not held off)
            static constexpr int kGears[] = {2, 3, 4, 3, 2, 1, 0, 1, 2, 3, 2, 4, 5, 4, 3};
            const int gear = std::min(kGears[(step / 45) % 15], top);
            ee.w8(car + 0x624, u8(gear));
            const s32 up = ee.call(0x36BEE0, {Arg::ptr(car), Arg::integer(gear)}).v0_s32();
            const s32 down = gear >= 2 ? ee.call(0x36BF28, {Arg::ptr(car), Arg::integer(gear)}).v0_s32() : 0;
            const float wave = std::sin(float(step) * 0.21f);
            const float rpm = (step / 23) % 2 ? float(up) - 250.0f + 300.0f * wave : float(down) + 700.0f * wave;
            ee.w32(car + 0x738, rp::F(rpm).bits);
            ee.w32(car + 0x58C, rp::F(step % 90 < 55 ? 0.95f : 0.0f).bits);
            ee.w32(car + 0x590, rp::F(step % 90 >= 70 ? 0.6f : 0.0f).bits);
            const rp::F t0 = r.f(car + 0x38), t1 = r.f(car + 0x3C);
            const rp::F least = t1 < t0 ? t1 : t0;
            rp::ShiftInputs si;
            si.rpm = gt3r::ee::to_int(r.f(car + 0x738)), si.top = top, si.gear = gear, si.throttle = r.f(car + 0x58C), si.brake = r.f(car + 0x590);
            si.suggested = ee.call(0x36BF68, {Arg::ptr(car), Arg::fbits(least.bits)}).v0_s32();
            si.up_rpm = up, si.down_rpm = down;
            gin.gear = gear, gin.reach = rp::shift_reach(si);
            gin.start_hold = ee.call(0x34C210, {Arg::ptr(car)}).v0_s32() != 0;
        }
        ee.w32(car + 0x768, rp::F(follow).bits);
        for (u32 n = 0; n < 64; ++n) ee.w32(block + 0x160 + 4 * n, s.after.v[n].bits);
        // The car moved along, shaken and turned.
        const rp::F part = r.f(car + 0x554), rest = rp::F(1.0f) - part;
        const auto mixed = [&](u32 now, u32 then) { return (r.f(car + now) * part) + (r.f(car + then) * rest); };
        const float wave = std::sin(float(step) * 0.11f), slow = std::sin(float(step) * 0.023f);
        if (gaze_drive) { // (GT4R_POSTURE_GAZE: the car on at 30 m/s along its heading, for the gaze's usual way)
            const rp::V3 ahead = rp::rotate(rp::from_euler(r.f(car + 0x8BC), r.f(car + 0x8C4), -r.f(car + 0x8C0)), {rp::F(0.0f), rp::F(0.0f), rp::F(-0.5f)});
            for (u32 at : {0x888u, 0x8A4u})
                ee.w32(car + at, (r.f(car + at) + ahead.x).bits), ee.w32(car + at + 4, (r.f(car + at + 4) + ahead.y).bits),
                    ee.w32(car + at + 8, (r.f(car + at + 8) + ahead.z).bits);
        } else
        for (u32 at : {0x888u, 0x8A4u}) {
            ee.w32(car + at, (r.f(car + at) + rp::F(0.5f + 0.3f * wave)).bits);
            ee.w32(car + at + 4, (r.f(car + at + 4) + rp::F(0.02f * wave * slow)).bits);
            ee.w32(car + at + 8, (r.f(car + at + 8) + rp::F(0.4f * slow)).bits);
        }
        for (u32 at : {0x66Cu, 0x8BCu}) {
            ee.w32(car + at, rp::F(0.02f * wave).bits);
            ee.w32(car + at + 4, rp::F(0.03f * wave + 1.2f * slow).bits);
            ee.w32(car + at + 8, rp::F(0.9f + 0.4f * slow).bits);
        }
        rp::DriverMotion motion;
        motion.pos = {mixed(0x8A4, 0x888), mixed(0x8A8, 0x88C), mixed(0x8AC, 0x890)};
        motion.roll = mixed(0x8C0, 0x670);
        motion.turn = rp::from_euler(mixed(0x8BC, 0x66C), mixed(0x8C4, 0x674), -motion.roll);
        motion.velocity = {r.f(car + 0x648), r.f(car + 0x650), -r.f(car + 0x64C)};
        if (gear_on) motion.gear = &gin;
        // The gaze's inputs: the front wheels' angles (0x344DA8), the computer driver's aim (0x350CE8).
        {
            const rp::F t = rp::F(0.0f) + r.f(car + 0x554);
            const auto wheel = [&](u32 i) { return (r.f(car + 0x8E0 + 4 * i) * t) + (r.f(car + 0x164 + 236 * i) * (rp::F(1.0f) - t)); };
            motion.front_wheels = (wheel(0) + wheel(1)) * rp::F(0.5f);
            const u32 p = car + 0x104;
            if (ee.r8(ee.r32(car + 4) + 0x10000 - 0x3422) != 2 && s32(ee.r32(p + 0x628)) >= 0)
                motion.aim = rp::V3{r.f(p + 0x61C), r.f(p + 0x624), -r.f(p + 0x620)};
        }
        const int gaze_was = int(ee.r32(block + 0x1C));
        try {
            ee.call(0x403260, {Arg::ptr(block), Arg::ptr(car)}, 50'000'000);
        } catch (const std::exception& e) {
            std::printf("  model %06X step %d: %s\n", model, step, e.what());
            return;
        }
        s.step(cockpit, rp::F(follow), r.f(car + 0x54C), speeds, &motion);
        ++steps;
        int bad = 0;
        // The gaze while the program's driver keeps its usual way of looking (state 1).
        if (gaze_was == 1 && ee.r32(block + 0x1C) == 1 && gaze_on) {
            ++gaze_steps;
            gaze_aimed += motion.aim ? 1 : 0;
            int gbad = 0;
            for (u32 at : {0x58u, 0x5Cu, 0x60u}) {
                const rp::F theirs = r.f(block + 0x160 + at);
                if (s.after.at(at) == theirs) continue;
                if (!gbad++) std::printf("  model %06X step %d gaze:", model, step);
                std::printf(" +%02X ours %.6f program %.6f", at, s.after.at(at).host(), theirs.host());
            }
            const rp::V3 tk = r.v(block + 0x40 + 0x7C);
            if (gbad || !(tk.x == s.gaze.target.x && tk.y == s.gaze.target.y && tk.z == s.gaze.target.z)) {
                std::printf("%s target ours %.4f %.4f %.4f program %.4f %.4f %.4f (aim %d)\n", gbad ? "" : "  gaze target:", s.gaze.target.x.host(),
                            s.gaze.target.y.host(), s.gaze.target.z.host(), tk.x.host(), tk.y.host(), tk.z.host(), motion.aim ? 1 : 0);
                gaze_on = false;
            } else {
                ++gaze_equal;
            }
        } else if (gaze_on && ee.r32(block + 0x1C) != 1) {
            std::printf("  model %06X step %d: the program's driver glances (state %u)\n", model, step, ee.r32(block + 0x1C));
            gaze_on = false;
        }
        for (u32 at : {0x00u, 0x04u, 0x08u, 0x0Cu, 0x10u, 0x64u, 0x68u, 0x34u, 0x38u, 0x3Cu, 0x40u, 0x44u, 0x48u, 0x4Cu, 0x50u, 0x54u, 0x78u, 0x7Cu, 0x8Cu, 0x90u}) {
            const rp::F theirs = r.f(block + 0x160 + at);
            if (s.after.at(at) == theirs) continue;
            if (!bad++) std::printf("  model %06X step %d:", model, step);
            std::printf(" +%02X ours %.6f program %.6f", at, s.after.at(at).host(), theirs.host());
        }
        if (gear_on) { // the gear change
            for (u32 at : {0x1Cu, 0x20u, 0x24u, 0x28u, 0x2Cu, 0x30u, 0x80u, 0x84u}) {
                const rp::F theirs = r.f(block + 0x160 + at);
                if (s.after.at(at) == theirs) continue;
                if (!bad++) std::printf("  model %06X step %d:", model, step);
                std::printf(" +%02X ours %.6f program %.6f", at, s.after.at(at).host(), theirs.host());
            }
            const int ours_i[3] = {s.held_gear, s.seen_gear, s.phase};
            const u32 at_i[3] = {0, 4, 0x18};
            for (int n = 0; n < 3; ++n) {
                if (ours_i[n] == int(ee.r32(block + at_i[n]))) continue;
                if (!bad++) std::printf("  model %06X step %d:", model, step);
                std::printf(" ints +%02X ours %d program %d", at_i[n], ours_i[n], int(ee.r32(block + at_i[n])));
            }
            phases_seen |= 1 << (s.phase & 15);
        }
        if (bad) {
            std::printf(" (states ours %d %d program %08X; follow %.3f; phase ours %d program %d)\n", s.state[0], s.state[1], ee.r32(block + 0x30), follow, s.phase,
                        int(ee.r32(block + 0x18)));
            return;
        }
        ++equal;
        if (step == stop_at) {
            if (std::getenv("GT4R_POSTURE_SHOW"))
                std::printf("  model %06X step %d: phase %d hand %.3f lever %.3f %.3f grip %.3f %.3f foot %.3f\n", model, step, s.phase, s.after.at(0x1C).host(),
                            s.after.at(0x20).host(), s.after.at(0x24).host(), s.after.at(0x28).host(), s.after.at(0x2C).host(), s.after.at(0x30).host());
            for (int recorded = 0; recorded < 2; ++recorded)
                for (float alpha : {0.0f, 0.37f, 1.0f}) check_solve(ee, model, recorded, alpha, stop_solves, stop_equal, stop_inputs, stop_inputs_equal);
            return;
        }
    }
    if (gear_on && stop_at < 0) std::printf("  model %06X: lever layout %d %d %d %d, the phases seen %03X\n", model, gin.layout.kind, gin.layout.offset, gin.layout.reverse, gin.layout.count, phases_seen);
}

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
        int steps = 0, steps_equal = 0;
        for (u32 model : models) compare_steps(ee, mem, model, steps, steps_equal);
        if (std::getenv("GT4R_POSTURE_GEAR")) { // the posture on the records of a gear change, every 29 steps
            stop_solves = stop_equal = stop_inputs = stop_inputs_equal = 0;
            int more = 0, more_equal = 0;
            for (u32 model : models)
                for (int stop = 250; stop < 720; stop += 29) compare_steps(ee, mem, model, more, more_equal, stop);
            std::printf("  the posture in the gear change: %d solves, %d with every bone equal; %d inputs, %d equal\n", stop_solves, stop_equal, stop_inputs,
                        stop_inputs_equal);
            solves += stop_solves, equal += stop_equal, inputs += stop_inputs, inputs_equal += stop_inputs_equal;
        }
        std::printf("  the driver's step: %d, %d with the wheel, the body, the hands, the elbows and the clavicles equal\n", steps, steps_equal);
        std::printf("  the gaze: %d steps in the usual way (%d with the computer driver's aim), %d equal\n", gaze_steps, gaze_aimed, gaze_equal);
        gaze_steps = gaze_equal = gaze_aimed = 0;
        if (std::getenv("GT4R_POSTURE_STEPS_ONLY")) continue; // (the driver's step alone, not the solves)
        for (u32 model : models)
            for (int recorded = 0; recorded < 2; ++recorded)
                for (float alpha : {0.0f, 0.37f, 1.0f}) {
                    ee.reset();
                    ee.protect_text = false;
                    ee.write_bytes(0, mem);
                    ee.clear_probes();
                    check_solve(ee, model, recorded, alpha, solves, equal, inputs, inputs_equal);
                }
    }
    std::printf("posture: %d solves, %d with every bone equal; %d inputs, %d equal\n", solves, equal, inputs, inputs_equal);
    return solves == equal ? 0 : 1;
}

} // namespace gt4r::diff
