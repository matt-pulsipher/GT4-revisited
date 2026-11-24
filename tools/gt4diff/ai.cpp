// gt4r_diff's run of the computer drivers (diff.h run_ai): a field of the program's own drivers in the program's own
// race, run in the interpreter, its lap times beside ours (sim::ai::run_field), the drivers' routines compared in
// place at every call (docs/specs/ai.md "Checks").

#include "diff.h"
#include "base/ee_math.h"
#include "gt4r/sim/ai/ee_math.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/exact_bridge.h"
#include "sim/exact/race_inputs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace gt4r::diff {

namespace {

constexpr u32 kAiCourse = 0xDA50;    // the conductor's AI course object (K)
constexpr u32 kLapLength = 0x46A0B0; // (K): the course's lap length
constexpr u32 kDistance = 0x4A4;     // F+: the car's distance along the course
constexpr u32 kSpeed = 0x53C;        // F+: its speed (m/s)

// The program's race's cars as our drivers read them (drivers.h Cars): each car's record read from the interpreter's
// memory into ours before every step.
class ProgramCars final : public sim::ai::Cars {
public:
    // The records start as our session's for the same cars (the parameter block's members the program's record has no
    // known place for), the state read over them at every refresh.
    ProgramCars(Ee& ee, const OrigRace& o, db::SpecDb& sdb, const sim::Program& program, const course::Runway& road)
        : ee_(ee), o_(o), ground_(road), recs_(size_t(o.cars)), states_(size_t(o.cars)), specs_(size_t(o.cars)), laps_(size_t(o.cars), 0),
          last_s_(size_t(o.cars), 0) {
        std::vector<sim::Entry> entries(size_t(o.cars));
        for (int i = 0; i < o.cars; ++i) {
            ee.read_bytes(o.entries[size_t(i)] + 0x20, entries[size_t(i)].equipment.bytes.data(), sim::Equipment::kSize);
            sim::build_spec(sdb, program, entries[size_t(i)].equipment, 0, specs_[size_t(i)]);
        }
        const sim::Session ours(sdb, program, std::make_shared<const course::Runway>(road), entries);
        for (int i = 0; i < o.cars; ++i) recs_[size_t(i)].car = ours.record(size_t(i)), recs_[size_t(i)].x = ours.gt4_record(size_t(i));
        loose_ = ee.call(0x34C998, {Arg::ptr(o.cond)}).v0_s32() != 0; // the course's kind: loose (dirt, snow)
        refresh();
    }
    void refresh() {
        for (int i = 0; i < o_.cars; ++i) {
            const size_t k = size_t(i);
            load(recs_[k], read_image(ee_, o_.car(i)));
            const u32 F = o_.car(i) + kF;
            race::CarState& st = states_[k];
            const float s = ee_.rf(F + kDistance);
            if (last_s_[k] > 0.75f * lap_ && s < 0.25f * lap_) ++laps_[k];
            last_s_[k] = s;
            st.course_v = s, st.lap = laps_[k];
            const auto& c = recs_[k].car;
            st.speed = c.speed.host();
            st.brake = c.brake_pedal.host();
            st.position = {c.pos.x.host(), c.pos.z.host(), -c.pos.y.host()};
            const float fx = c.fwd.x.host(), fy = c.fwd.y.host(), l = std::max(std::hypot(fx, fy), 1e-6f);
            st.forward = {fx / l, 0, -fy / l};
            // The parameter block's members the drivers read that the record's mirror has no place for, as the program has them.
            {
                const u32 P = ee_.r32(o_.car(i) + 0x10);
                recs_[k].car.p.top_speed = gt3r::ee::F::raw(ee_.r32(P + 0xFD8));
            }
            for (int w = 0; w < 4; ++w) st.wheels[w].on_ground = true, st.wheels[w].material = u8(c.w[size_t(w)].surface), st.wheels[w].surface = 0;
        }
    }
    void set_lap(float lap) { lap_ = lap; }
    size_t size() const override { return size_t(o_.cars); }
    const gt3r::sim::exact::Car& record(size_t i) const override { return recs_[i].car; }
    const race::CarState& state(size_t i) const override { return states_[i]; }
    const sim::Spec& spec(size_t i) const override { return specs_[i]; }
    bool loose() const override { return loose_; }
    const sim::RoadGround& ground() const override { return ground_; }
    const sim::Extra& extra(size_t i) const override { return recs_[i].x; }
    bool hits(size_t i, bool& front, bool& rear) const override {
        const u32 body = o_.cond + 0xDA6C + 0x4F0 * u32(i) + 0xC;
        front = ee_.r32(body + 0x434) != 0, rear = ee_.r32(body + 0x438) != 0;
        return true;
    }
    bool lat_filter(size_t i, gt3r::ee::F& x) const override {
        x = gt3r::ee::F::raw(ee_.r32(o_.car(int(i)) + kF + 0x560));
        return true;
    }
    bool course_rate(size_t i, gt3r::ee::F& x) const override {
        x = gt3r::ee::F::raw(ee_.r32(o_.car(int(i)) + kF + 0x650));
        return true;
    }
    bool rank(size_t i, int& r) const override {
        r = gt3r::s8(ee_.r8(o_.car(int(i)) + kF + 0x4AF));
        return true;
    }
    bool finished(size_t i) const override { return ee_.r8(o_.car(int(i)) + kF + 0x4B2) != 0; }
    bool clock(u32& c) const override {
        c = ee_.r32(o_.cond + 0xF89C);
        return true;
    }

private:
    Ee& ee_;
    const OrigRace& o_;
    sim::RoadGround ground_;
    std::vector<sim::Record> recs_;
    std::vector<race::CarState> states_;
    std::vector<sim::Spec> specs_;
    std::vector<int> laps_;
    std::vector<float> last_s_;
    float lap_ = 1e9f;
    bool loose_ = false;
};

// The driver object D of car i (the car's +0x14) as our drivers' probe state.
sim::ai::Field::Probe probe_of(Ee& ee, const OrigRace& o, int i, float lap) {
    using gt3r::ee::F;
    const u32 D = o.car(i) + 0x14, Fs = o.car(i) + kF;
    sim::ai::Field::Probe p;
    p.line = ee.r8(D + 0x1C), p.next = ee.r8(D + 0x1D), p.mode = ee.r8(D + 0x1E), p.next_kind = ee.r8(D + 0x1F), p.status = gt3r::s8(ee.r8(D + 0x43));
    p.follow = gt3r::s8(ee.r8(D + 0x51)), p.chain = ee.r8(D + 0x6A);
    p.v_cur = F::raw(ee.r32(D + 0x24)), p.v_next = F::raw(ee.r32(D + 0x28)), p.contact = F::raw(ee.r32(D + 0x44));
    float s = ee.rf(Fs + kDistance);
    if (s >= lap) s -= lap;
    if (s < 0) s += lap;
    p.s = s;
    p.rand = ee.r16(D + 0x68);
    p.corner_mult = F::raw(ee.r32(D + 0xB8)), p.brake_mult = F::raw(ee.r32(D + 0xBC));
    p.alongside_mult = F::raw(ee.r32(D + 0xC0));
    for (u32 k = 0; k < 8; ++k) p.zone[k] = ee.r8(D + 0x48 + k);
    p.cooldown = s16(ee.r16(D + 0x18));
    p.state = ee.r8(D + 0x1B), p.prev_line = ee.r8(D + 0x21), p.state_hold = ee.r8(D + 0x6B), p.start_flag = ee.r8(D + 0x1A);
    p.stop_count = ee.r8(D + 0x52), p.pit = ee.r8(D + 0x20), p.arc_ticks = ee.r16(D + 0x22);
    p.blend = F::raw(ee.r32(D + 0x2C)), p.offset = F::raw(ee.r32(D + 0x54));
    p.gap_left = F::raw(ee.r32(D + 0x58)), p.gap_right = F::raw(ee.r32(D + 0x5C));
    p.steer = F::raw(ee.r32(D + 0x30)), p.throttle = F::raw(ee.r32(D + 0x34)), p.brake = F::raw(ee.r32(D + 0x38));
    p.reverse = ee.r8(D + 0x3E) != 0, p.shift_lock = ee.r16(D + 0x3C);
    p.aim_x = F::raw(ee.r32(Fs + 0x61C)), p.aim_y = F::raw(ee.r32(Fs + 0x620));
    p.kind = ee.r8(Fs + 0x467);
    return p;
}

struct Tally {
    u64 n = 0, off = 0;
    double worst = 0;
    std::string example;
    void add(const char* what, double theirs, double ours, double tol = 1e-5) {
        ++n;
        const double d = std::fabs(theirs - ours), rel = d / std::max(1e-6, std::fabs(theirs));
        if (rel > tol && d > 1e-6) {
            ++off;
            if (rel > worst) {
                worst = rel;
                char b[160];
                std::snprintf(b, sizeof b, "%s: the program's %.6g, ours %.6g", what, theirs, ours);
                example = b;
            }
        }
    }
    // Bit for bit.
    void bits(const char* what, u32 theirs, gt3r::ee::F ours) {
        ++n;
        if (theirs == ours.bits) return;
        ++off;
        float t;
        std::memcpy(&t, &theirs, 4);
        const double rel = std::fabs(double(t) - ours.value()) / std::max(1e-6, std::fabs(double(t)));
        if (example.empty() || rel > worst) {
            worst = rel;
            char b[200];
            std::snprintf(b, sizeof b, "%s: the program's %.9g (%08X), ours %.9g (%08X)", what, double(t), theirs, ours.value(), ours.bits);
            example = b;
        }
    }
    void print(const char* name) const {
        std::printf("  %-34s %8llu compared, %6llu differ%s%s\n", name, (unsigned long long)n, (unsigned long long)off, off ? "; worst " : "",
                    off ? example.c_str() : "");
    }
};

// The program's prepared lines (K +0x08: per line a u32 count, the points at +0x40, 0x40 bytes each) against ours.
void check_lines(Ee& ee, const OrigRace& o, const sim::ai::Field& field, std::map<std::string, Tally>& t) {
    const u32 K = o.cond + kAiCourse, table = ee.r32(K + 8);
    for (int k = 1; k < 8; ++k) {
        const u32 line = table ? ee.r32(table + 4 * u32(k)) : 0;
        const auto& ours = field.lines().line(k);
        t["line present"].add("line", line ? 1 : 0, ours.empty() ? 0 : 1, 0);
        if (!line) continue;
        const u32 n = ee.r32(line);
        t["line points"].add("count", n, double(ours.size()), 0);
        for (u32 i = 0; i < n && i < ours.size(); ++i) {
            const u32 P = line + 0x40 + 0x40 * i;
            const sim::ai::LinePoint& q = ours[i];
            char w[48];
            std::snprintf(w, sizeof w, "line %d point %u", k, i);
            t["line distance"].bits(w, ee.r32(P + 0x14), gt3r::ee::F(q.dist));
            if (std::getenv("GT4R_AI_LINES"))
                std::printf("  line %d point %2u: distance %9.3f / %9.3f slope %+.5f / %+.5f bank %+.5f / %+.5f x %8.2f / %8.2f y %8.2f / %8.2f\n", k, i,
                            double(ee.rf(P + 0x14)), double(q.dist), double(ee.rf(P + 0x1C)), double(q.slope), double(ee.rf(P + 0x20)), double(q.bank),
                            double(ee.rf(P + 4)), double(q.x), double(ee.rf(P + 8)), double(q.y));
            t["line height"].bits(w, ee.r32(P + 0x0C), gt3r::ee::F(q.height));
            t["line x"].bits(w, ee.r32(P + 0x04), gt3r::ee::F(q.x));
            t["line y"].bits(w, ee.r32(P + 0x08), gt3r::ee::F(q.y));
            t["line radius"].bits(w, ee.r32(P + 0x18), gt3r::ee::F(q.radius));
            t["line heading"].bits(w, ee.r32(P + 0x24), gt3r::ee::F(q.heading));
            t["line slope"].bits(w, ee.r32(P + 0x1C), gt3r::ee::F(q.slope));
            t["line bank"].bits(w, ee.r32(P + 0x20), gt3r::ee::F(q.bank));
            t["line surface"].add(w, ee.r8(P + 0x12), q.surface, 0);
            t["line kind"].add(w, ee.r8(P + 0x00), q.kind, 0);
            t["line length"].bits(w, ee.r32(P + 0x2C), gt3r::ee::F(q.length));
            t["line rise"].bits(w, ee.r32(P + 0x30), gt3r::ee::F(q.rise));
            t["line pitch"].bits(w, ee.r32(P + 0x34), gt3r::ee::F(q.pitch));
            t["line crest"].add(w, ee.r8(P + 0x28), q.crest ? 1 : 0, 0);
        }
    }
}

// Each driver's parameter block against ours.
void check_params(Ee& ee, const OrigRace& o, const sim::ai::Field& field, std::map<std::string, Tally>& t) {
    for (int i = 0; i < o.cars; ++i) {
        const u32 PB = ee.r32(o.cond + 0x10024 + 4 * u32(i));
        if (!PB) continue;
        const sim::ai::Params& p = field.params(size_t(i));
        auto f = [&](const char* n, u32 off, gt3r::ee::F ours) { t[std::string("params ") + n].bits(n, ee.r32(PB + off), ours); };
        f("brake", 0x00, p.brake), f("corner scale", 0x04, p.corner_scale), f("corner offset", 0x08, p.corner_offset), f("yaw gain", 0x0C, p.yaw_gain);
        f("sat throttle", 0x10, p.sat_throttle), f("sat brake", 0x14, p.sat_brake), f("look speed", 0x18, p.look_speed), f("look min", 0x1C, p.look_min);
        f("look max", 0x20, p.look_max), f("front weight", 0x58, p.front_weight), f("corner bonus", 0x5C, p.corner_bonus), f("steer rate", 0x60, p.steer_rate);
        f("pit speed", 0x64, p.pit_speed), f("throttle", 0x6C, p.throttle), f("downforce", 0x70, p.downforce), f("aero bonus", 0x74, p.aero_bonus);
        f("bonus angle", 0x7C, p.bonus_angle), f("bonus speed", 0x80, p.bonus_speed);
        t["params bonus ticks"].add("ticks", double(ee.r32(PB + 0x68)), p.bonus_ticks, 0);
        t["params straight bonus"].add("flag", ee.r8(PB + 0x78), p.straight_bonus ? 1 : 0, 0);
        for (u32 k = 0; k < 3; ++k) f("grip", 0xA8 + 4 * k, p.grip[k]);
    }
}

const sim::ai::Cars* pcars_for_checks = nullptr;

// Every point's speed of each driver's line (0x4674F0) against ours, the driver's state as the program's.
void check_corners(Ee& ee, const OrigRace& o, sim::ai::Field& field, float lap, std::map<std::string, Tally>& t) {
    if (std::getenv("GT4R_AI_NOSPREAD")) ee.w8(0x623E5A, 0), const_cast<sim::ai::Tables&>(field.tables()).tuning.corner_spread = 0;
    for (int i = 0; i < o.cars; ++i) {
        const sim::ai::Field::Probe p = probe_of(ee, o, i, lap);
        field.probe_set(size_t(i), p);
        const auto& pts = field.lines().line(p.line);
        for (int k = 0; k < int(pts.size()); ++k) {
            const u32 bits = ee.call(0x4674F0, {Arg::ptr(o.car(i) + 0x14), Arg::integer(k)}).f0;
            char w[64];
            std::snprintf(w, sizeof w, "car %d line %d point %d", i, p.line, k);
            const gt3r::ee::F mine = field.probe_corner_speed(size_t(i), k);
            t["corner speed"].bits(w, bits, mine);
            static int shown = 0;
            if (std::getenv("GT4R_AI_CORNER") && bits != mine.bits && shown++ < 6) {
                const auto& q = pts[size_t(k)];
                const u32 P = ee.r32(o.car(i) + 0x10);
                const u32 lay = u32(ee.call(0x359510, {Arg::ptr(P)}).v0.lo);
                const u32 rf = u32(ee.call(0x360F80, {Arg::ptr(P), Arg::integer(ee.r8(lay + 0xC)), Arg::integer(q.surface)}).v0.lo);
                const u32 g = ee.call(0x467470, {Arg::ptr(o.car(i)), Arg::integer(q.surface)}).f0;
                const sim::Extra& x = pcars_for_checks->extra(size_t(i));
                const u8 cls = x.tires.tire_index[0][size_t(q.surface)];
                std::printf("  %s: surface %d, record +%X class %d/%d mu %08X/%08X grip %08X/%08X aigrip %08X w %08X/%08X bank %08X r %08X\n", w, q.surface,
                            rf - P, (rf - P - 0x410) / 0x1EC, cls, ee.r32(rf + 8 + 4 * u32(q.surface)), x.tires.tire[cls].mu[size_t(q.surface)].bits,
                            ee.r32(rf), x.tires.tire[cls].grip.bits, g, ee.r32(ee.r32(o.cond + 0x10024 + 4 * u32(i)) + 0x58), field.params(size_t(i)).front_weight.bits,
                            gt3r::ee::F(q.bank).bits, gt3r::ee::F(q.radius).bits);
                const u32 rr = u32(ee.call(0x360F80, {Arg::ptr(P), Arg::integer(ee.r8(lay + 0xD)), Arg::integer(q.surface)}).v0.lo);
                std::printf("    layout C %d D %d 10 %d; rear record +%X mu %08X grip %08X; ours class %d mu %08X grip %08X\n", ee.r8(lay + 0xC), ee.r8(lay + 0xD),
                            ee.r8(lay + 0x10), rr - P, ee.r32(rr + 8 + 4 * u32(q.surface)), ee.r32(rr), x.tires.tire_index[1][size_t(q.surface)],
                            x.tires.tire[x.tires.tire_index[1][size_t(q.surface)]].mu[size_t(q.surface)].bits, x.tires.tire[x.tires.tire_index[1][size_t(q.surface)]].grip.bits);
                const u32 Kl = ee.r32(ee.r32(o.cond + kAiCourse + 8) + 4 * u32(p.line)), Pt = Kl + 0x40 + 0x40 * u32(k);
                std::printf("    flags P+0x104A %d F+0x5C2 %d\n", ee.r8(P + 0x104A), ee.r8(o.car(i) + kF + 0x5C2));
                std::printf("    program's point: radius %08X bank %08X crest %d surf %d spd %d\n", ee.r32(Pt + 0x18), ee.r32(Pt + 0x20), ee.r8(Pt + 0x28), ee.r8(Pt + 0x12),
                            gt3r::s8(ee.r8(Pt + 2)));
                const u32 T = 0x623E50;
                std::printf("    T[A] %d T[B] %d T[9] %d lo(1) %08X hi %08X ours %08X %08X; T+0x90 %08X ours %08X; zone %02X%02X%02X%02X\n", ee.r8(T + 0xA), ee.r8(T + 0xB),
                            ee.r8(T + 9), u32(ee.call(0x359568, {Arg::ptr(T + 0xAC), Arg::fbits(0x3F800000)}).f0),
                            u32(ee.call(0x359568, {Arg::ptr(T + 0xB8), Arg::fbits(0x3F800000)}).f0), field.tables().tuning.corner_lo.at(gt3r::ee::F(1.0f)).bits,
                            field.tables().tuning.corner_hi.at(gt3r::ee::F(1.0f)).bits, ee.r32(T + 0x90), field.tables().tuning.alongside_share.bits,
                            ee.r8(o.car(i) + 0x14 + 0x49), ee.r8(o.car(i) + 0x14 + 0x4A), ee.r8(o.car(i) + 0x14 + 0x4B), ee.r8(o.car(i) + 0x14 + 0x4C));
            }
        }
    }
}

// The braking routine (0x463EA8) of each driver against ours, from the same state (the program's driver object kept as
// it was).
void check_braking(Ee& ee, const OrigRace& o, sim::ai::Field& field, float lap, std::map<std::string, Tally>& t) {
    for (int i = 0; i < o.cars; ++i) {
        const u32 D = o.car(i) + 0x14;
        u8 saved[0xF0];
        ee.read_bytes(D, saved, sizeof saved);
        const sim::ai::Field::Probe p = probe_of(ee, o, i, lap);
        field.probe_set(size_t(i), p);
        const u32 bits = ee.call(0x463EA8, {Arg::ptr(D)}).f0;
        float theirs;
        std::memcpy(&theirs, &bits, 4);
        (void)theirs;
        const sim::ai::Field::Probe after = probe_of(ee, o, i, lap);
        const gt3r::ee::F ours = field.probe_braking(size_t(i));
        const sim::ai::Field::Probe mine = field.probe_get(size_t(i));
        ee.write_bytes(D, ByteSpan(saved, sizeof saved));
        char w[96];
        std::snprintf(w, sizeof w, "car %d s %.1f next %d", i, double(p.s), p.next);
        t["braking multiplier"].bits(w, bits, ours);
        t["braking status"].add(w, after.status, mine.status, 0);
        t["braking mode"].add(w, after.mode, mine.mode, 0);
        t["braking next"].add(w, after.next, mine.next, 0);
        t["braking v_next"].bits(w, after.v_next.bits, mine.v_next);
        if (std::getenv("GT4R_AI_BRAKING") && (after.status != mine.status || after.mode != mine.mode))
            std::printf("  %s v %.2f: status %d/%d mode %d/%d next %d/%d v_next %.3f/%.3f mult %.3f/%.3f\n", w, double(ee.rf(o.car(i) + kF + kSpeed)),
                        after.status, mine.status, after.mode, mine.mode, after.next, mine.next, after.v_next.value(), mine.v_next.value(), double(theirs), ours.value());
    }
}

// A line's place at a distance (0x46B480) against ours: each car's line at its distance and on to 60 m ahead.
void check_places(Ee& ee, const OrigRace& o, sim::ai::Field& field, std::map<std::string, Tally>& t) {
    using gt3r::ee::F;
    const u32 out = ee.alloc(16);
    for (int i = 0; i < o.cars; ++i) {
        const u32 D = o.car(i) + 0x14;
        const int line = ee.r8(D + 0x1C), hint = ee.r8(D + 0x1D);
        if (line == 4) continue;
        const F s0 = F::raw(ee.r32(o.car(i) + kF + kDistance));
        for (int k = 0; k < 7; ++k) {
            const F s = s0 + F(float(k * 10) - 3.0f);
            ee.write_bytes(out, ByteSpan(reinterpret_cast<const u8*>("\0\0\0\0\0\0\0\0\0\0\0\0"), 12));
            ee.call(0x46B480, {Arg::ptr(o.cond + kAiCourse), Arg::integer(line), Arg::integer(hint), Arg::ptr(out), Arg::ptr(out + 4), Arg::ptr(out + 8),
                               Arg::integer(0), Arg::integer(0), Arg::fbits(s.bits)});
            F x, y, h;
            field.probe_place(line, hint, s, x, y, h);
            char w[64];
            std::snprintf(w, sizeof w, "car %d line %d s %.2f", i, line, s.value());
            t["place x"].bits(w, ee.r32(out), x);
            t["place y"].bits(w, ee.r32(out + 4), y);
            t["place height"].bits(w, ee.r32(out + 8), h);
        }
    }
}

// The program's sinf, cosf and atan2f (0x57D2B8, 0x57D1F0, 0x57D118) against the engine's.
void check_math(Ee& ee, std::map<std::string, Tally>& t) {
    using gt3r::ee::F;
    u32 x = 12345;
    for (int i = 0; i < 4000; ++i) {
        x = x * 1103515245u + 12345u;
        const F a = F(float(int(x >> 8) % 20000 - 10000) * 0.00071f);
        x = x * 1103515245u + 12345u;
        const F b = F(float(int(x >> 8) % 20000 - 10000) * 0.013f);
        char w[64];
        std::snprintf(w, sizeof w, "%.6g %.6g", a.value(), b.value());
        t["math sin"].bits(w, ee.call(0x57D2B8, {Arg::fbits(a.bits)}).f0, gt3r::ee::sin(a));
        t["math cos"].bits(w, ee.call(0x57D1F0, {Arg::fbits(a.bits)}).f0, gt3r::ee::cos(a));
        t["math atan2"].bits(w, ee.call(0x57D118, {Arg::fbits(a.bits), Arg::fbits(b.bits)}).f0, sim::ai::atan2(a, b));
        t["math atan"].bits(w, ee.call(0x57D008, {Arg::fbits(a.bits)}).f0, gt3r::ee::atan(a));
    }
}

// The decision (0x465078) and the proximity pass (0x3F4638) at every call: ours run from the program's state at the
// call's entry, compared with what the program leaves at the call's exit.
class CallChecks {
public:
    CallChecks(Ee& ee, const OrigRace& o, sim::ai::Field& field, ProgramCars& cars, float lap, std::map<std::string, Tally>& t)
        : ee_(ee), o_(o), field_(field), cars_(cars), lap_(lap), t_(t) {}
    void install() {
        ee_.add_probe(0x465078, [this](Ee&) { decision_in(); });
        ee_.add_probe(0x3F4638, [this](Ee&) { proximity_in(); });
    }

private:
    struct Pending {
        u32 sp;
        int car;
        sim::ai::Field::Probe ours;
    };
    void load_all() {
        cars_.refresh();
        for (int j = 0; j < o_.cars; ++j) field_.probe_set(size_t(j), probe_of(ee_, o_, j, lap_));
    }
    void at_return(u32 ra) {
        if (returns_.insert(ra).second) ee_.add_probe(ra, [this](Ee&) { decision_out(), proximity_out(); });
    }
    void decision_in() {
        const u32 D = u32(ee_.a(0));
        int car = -1;
        for (int i = 0; i < o_.cars; ++i)
            if (o_.car(i) + 0x14 == D) car = i;
        if (car < 0) return;
        load_all();
        decisions_.push_back({ee_.g32(29), car, field_.probe_decide(size_t(car))});
        at_return(ee_.g32(31));
    }
    void decision_out() {
        if (decisions_.empty() || decisions_.back().sp != ee_.g32(29)) return;
        const Pending p = decisions_.back();
        decisions_.pop_back();
        const sim::ai::Field::Probe th = probe_of(ee_, o_, p.car, lap_);
        const sim::ai::Field::Probe& my = p.ours;
        char w[64];
        std::snprintf(w, sizeof w, "car %d at %.1f s", p.car, double(ee_.r32(o_.cond + 0xF89C)) / 3000.0);
        t_["decision steer"].bits(w, th.steer.bits, my.steer);
        t_["decision throttle"].bits(w, th.throttle.bits, my.throttle);
        t_["decision brake"].bits(w, th.brake.bits, my.brake);
        t_["decision aim x"].bits(w, th.aim_x.bits, my.aim_x);
        t_["decision aim y"].bits(w, th.aim_y.bits, my.aim_y);
        t_["decision blend"].bits(w, th.blend.bits, my.blend);
        t_["decision offset"].bits(w, th.offset.bits, my.offset);
        t_["decision v_cur"].bits(w, th.v_cur.bits, my.v_cur);
        t_["decision v_next"].bits(w, th.v_next.bits, my.v_next);
        t_["decision state"].add(w, th.state, my.state, 0);
        t_["decision state hold"].add(w, th.state_hold, my.state_hold, 0);
        t_["decision status"].add(w, th.status, my.status, 0);
        t_["decision line"].add(w, th.line * 10 + th.prev_line, my.line * 10 + my.prev_line, 0);
        t_["decision next"].add(w, th.next, my.next, 0);
        t_["decision mode"].add(w, th.mode, my.mode, 0);
        t_["decision arc ticks"].add(w, th.arc_ticks, my.arc_ticks, 0);
        t_["decision cooldown"].add(w, th.cooldown, my.cooldown, 0);
        t_["decision reverse, shift lock"].add(w, (th.reverse ? 1000 : 0) + th.shift_lock, (my.reverse ? 1000 : 0) + my.shift_lock, 0);
        t_["decision start flag"].add(w, th.start_flag, my.start_flag, 0);
        t_["decision kind, stop count"].add(w, th.kind * 1000 + th.stop_count, my.kind * 1000 + my.stop_count, 0);
        if (th.kind) ++kinds_[th.kind];
        if (std::getenv("GT4R_AI_DBG") && th.throttle.bits != my.throttle.bits && shown_++ < 10) {
            const auto& c = cars_.record(size_t(p.car));
            const gt3r::ee::F load = -((c.rolling + c.drag) + (-((c.ground_fwd.z * c.p.mass) * gt3r::ee::F::raw(0x411CCCCC))));
            std::printf("  %s kind %d: throttle %.4f / %.4f; road load %08X / %08X; limits %d %d gear %d stop %d\n", w, th.kind, th.throttle.value(),
                        my.throttle.value(), ee_.r32(o_.car(p.car) + kF + 0x5BC), load.bits, ee_.r8(o_.cond + 0xF818 + 0x42),
                        ee_.r8(o_.cond + 0xF818 + 0x43), int(c.gear), th.stop_count);
        }
        if (std::getenv("GT4R_AI_DECISION") && (th.steer.bits != my.steer.bits || th.throttle.bits != my.throttle.bits || th.brake.bits != my.brake.bits ||
                                                th.aim_x.bits != my.aim_x.bits || th.offset.bits != my.offset.bits) && shown_++ < 40) {
            std::printf("  offset %08X / %08X contact %08X kind %d hold %d rank %d cool %d\n", th.offset.bits, my.offset.bits, th.contact.bits,
                        ee_.r8(o_.car(p.car) + kF + 0x467), ee_.r8(o_.car(p.car) + kF + 0x4BC), gt3r::s8(ee_.r8(o_.car(p.car) + kF + 0x4AF)), th.cooldown);
            std::printf("  %s: steer %.6f/%.6f thr %.4f/%.4f brk %.3f/%.3f aim %.4f,%.4f / %.4f,%.4f state %d/%d status %d/%d mode %d/%d line %d/%d\n", w,
                        th.steer.value(), my.steer.value(), th.throttle.value(), my.throttle.value(), th.brake.value(), my.brake.value(),
                        th.aim_x.value(), th.aim_y.value(), my.aim_x.value(), my.aim_y.value(), th.state, my.state, th.status, my.status, th.mode,
                        my.mode, th.line, my.line);
        }
    }
    void proximity_in() {
        load_all();
        field_.probe_proximity();
        prox_.clear();
        for (int j = 0; j < o_.cars; ++j) prox_.push_back(field_.probe_get(size_t(j)));
        prox_sp_ = ee_.g32(29);
        at_return(ee_.g32(31));
    }
    void proximity_out() {
        if (prox_.empty() || prox_sp_ != ee_.g32(29)) return;
        for (int j = 0; j < o_.cars; ++j) {
            const sim::ai::Field::Probe th = probe_of(ee_, o_, j, lap_);
            const sim::ai::Field::Probe& my = prox_[size_t(j)];
            char w[48];
            std::snprintf(w, sizeof w, "car %d", j);
            for (int k = 0; k < 8; ++k) t_["proximity zone"].add(w, th.zone[size_t(k)] + 10 * k, my.zone[size_t(k)] + 10 * k, 0);
            t_["proximity contact"].bits(w, th.contact.bits, my.contact);
            t_["proximity follow"].add(w, th.follow, my.follow, 0);
            t_["proximity chain"].add(w, th.chain, my.chain, 0);
            t_["proximity gaps"].bits(w, th.gap_left.bits, my.gap_left);
            t_["proximity gaps"].bits(w, th.gap_right.bits, my.gap_right);
        }
        prox_.clear();
    }
    Ee& ee_;
    const OrigRace& o_;
    sim::ai::Field& field_;
    ProgramCars& cars_;
    float lap_;
    std::map<std::string, Tally>& t_;
    std::vector<Pending> decisions_;
    std::vector<sim::ai::Field::Probe> prox_;
    u32 prox_sp_ = 0;
    std::set<u32> returns_;
    int shown_ = 0;

public:
    std::map<int, u64> kinds_; // the decisions compared by the driver's kind (past the finish)
};

std::string clock(double s) {
    char b[32];
    const int m = int(s / 60);
    std::snprintf(b, sizeof b, "%d'%06.3f", m, s - 60.0 * m);
    return b;
}

} // namespace

AiRunResult run_ai(Disc& disc, bool print, const std::string& course, const std::vector<std::string>& cars, int laps, AiRunOptions options) {
    if (std::getenv("GT4R_AI_OURS")) options.ours = true;
    if (std::getenv("GT4R_AI_CHECK")) options.checks = true;
    if (std::getenv("GT4R_AI_BAND")) options.band = true;
    AiRunResult result;
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    OrigRace o;
    std::string err;
    if (!setup_race(ee, disc, sdb, course, cars, 0, o, err, true)) {
        result.error = err;
        if (print) std::printf("ai: %s\n", err.c_str());
        return result;
    }
    // The rubber band off (the race's set-up from an event row is left out: the conductor's own values would hurry the
    // cars behind car 0; GT4R_AI_BAND keeps them).
    if (!options.band) ee.wf(o.cond + 0x7C, 0.0f), ee.wf(o.cond + 0x88, 0.0f), ee.wf(o.cond + 0x94, 1.0f);
    // GT4R_AI_AIDS: the driving aids' taps (taps_aids.cpp) on the computer drivers' cars.
    std::unique_ptr<ex::RaceInputs> aid_inputs;
    std::unique_ptr<Taps> aid_taps;
    if (std::getenv("GT4R_AI_AIDS")) {
        aid_inputs = std::make_unique<ex::RaceInputs>(ex::race_inputs(sim::core_program(sim::Program::load(disc)), ex::Conductor::SinglePlayer, ex::RaceConfig{}, false, 0));
        aid_taps = std::make_unique<Taps>(ee, o, *aid_inputs);
        add_taps_aids(*aid_taps);
    }
    // GT4R_AI_STEER: the steering phase's taps (taps_steer.cpp) on the computer drivers' cars.
    if (std::getenv("GT4R_AI_STEER")) {
        if (!aid_taps) {
            aid_inputs = std::make_unique<ex::RaceInputs>(ex::race_inputs(sim::core_program(sim::Program::load(disc)), ex::Conductor::SinglePlayer, ex::RaceConfig{}, false, 0));
            aid_taps = std::make_unique<Taps>(ee, o, *aid_inputs);
        }
        add_taps_steer(*aid_taps);
    }
    // GT4R_AI_STEPTAPS: the step's other ported routines' taps (step.cpp) on the computer drivers' cars.
    if (std::getenv("GT4R_AI_STEPTAPS")) {
        if (!aid_taps) {
            aid_inputs = std::make_unique<ex::RaceInputs>(ex::race_inputs(sim::core_program(sim::Program::load(disc)), ex::Conductor::SinglePlayer, ex::RaceConfig{}, false, 0));
            aid_taps = std::make_unique<Taps>(ee, o, *aid_inputs);
        }
        add_step_taps(*aid_taps);
    }
    if (std::getenv("GT4R_AI_DUMP")) {
        for (u32 a : {0x622080u, 0x622088u, 0x622090u, 0x622098u, 0x6220A0u, 0x6220A8u, 0x6220B0u, 0x6220C8u, 0x6220E0u, 0x623E40u}) {
            std::printf("%06X:", a);
            for (u32 k = 0; k < 24; ++k) std::printf(" %02X", ee.r8(a + k));
            std::printf("\n");
        }
    }
    const u32 lap_bits = ee.call(kLapLength, {Arg::ptr(o.cond + kAiCourse)}).f0;
    float lap_length;
    std::memcpy(&lap_length, &lap_bits, 4);
    result.lap_length = lap_length;
    // GT4R_AI_OURS: our drivers in the program's race (the decision 0x465078 replaced by ours on the program's cars).
    std::unique_ptr<course::Runway> road;
    std::unique_ptr<ProgramCars> pcars;
    std::unique_ptr<sim::ai::Field> field;
    std::unique_ptr<CallChecks> calls;
    int bspec_car = -1, bspec_level = -1, bspec_step = -1, finish_mode = 0, finish_step = -1;
    if (const char* f = std::getenv("GT4R_AI_FINISH")) {
        finish_mode = std::atoi(f);
        if (const char* c = std::strchr(f, ',')) finish_step = int(std::atof(c + 1) * 60.0);
    }
    const sim::Program program = sim::Program::load(disc);
    const bool ours = options.ours, checks = options.checks;
    std::map<std::string, Tally> tally;
    if (ours || checks) {
        const gt3r::Volume::Entry* file = disc.volume().find("crs/" + course);
        const Bytes data = disc.volume().read(*file);
        const course::CourseFile parsed = course::CourseFile::parse(data);
        road = std::make_unique<course::Runway>(course::Runway::parse(parsed.section(data, course::kSlotRunway)));
        pcars = std::make_unique<ProgramCars>(ee, o, sdb, program, *road);
        pcars->set_lap(lap_length);
        pcars_for_checks = pcars.get();
        const std::optional<course::AiData> ad = sim::ai::Field::read(disc, course);
        std::vector<size_t> computer;
        for (int i = 0; i < o.cars; ++i) computer.push_back(size_t(i));
        field = std::make_unique<sim::ai::Field>(*pcars, *ad, program, computer);
        sim::ai::Field* f = field.get();
        const OrigRace* race = &o;
        if (checks) check_lines(ee, o, *field, tally), check_params(ee, o, *field, tally), check_math(ee, tally);
        {
            // The band as the conductor holds it (+0x7C .. +0x94) and the race's mode (the configuration's +0x22).
            sim::ai::Field::Band b;
            auto rf = [&](u32 off) { return gt3r::ee::F::raw(ee.r32(o.cond + off)); };
            b.slow = rf(0x7C), b.slow_start = rf(0x80), b.slow_full = rf(0x84), b.fast = rf(0x88), b.fast_start = rf(0x8C), b.fast_full = rf(0x90);
            b.global = rf(0x94), b.mode = gt3r::s8(ee.r8(o.cond + 0xF818 + 0x22)), b.on = true;
            field->set_band(b);
        }
        // GT4R_AI_BSPEC=<car>[,<level>,<seconds>]: that car's driver as B-spec's (control source 1, its setup 0x3F1890), in
        // the program's race and ours; at <seconds> the pace command <level> (0x3F19E0) to both.
        if (const char* b = std::getenv("GT4R_AI_BSPEC")) {
            bspec_car = std::atoi(b);
            if (const char* c = std::strchr(b, ',')) {
                bspec_level = std::atoi(c + 1);
                if (const char* t = std::strchr(c + 1, ',')) bspec_step = int(std::atof(t + 1) * 60.0);
            }
            if (bspec_car >= 0 && bspec_car < o.cars) {
                ee.w8(o.car(bspec_car) + kF + 0x466, 1);
                ee.call(0x3F1890, {Arg::ptr(o.cond), Arg::integer(bspec_car)});
                field->set_bspec(size_t(bspec_car));
                // The program's car was set up as a computer car's: its yaw gain as a B-spec driver's (no spread by the
                // grid index, 0x467148) and its draws seeded with 1 (0x4639E0), as the race sets a B-spec car up.
                const u32 PB = ee.r32(o.cond + 0x10024 + 4 * u32(bspec_car));
                ee.w32(PB + 0x0C, field->params(size_t(bspec_car)).yaw_gain.bits);
                ee.w16(o.car(bspec_car) + 0x14 + 0x68, field->probe_get(size_t(bspec_car)).rand);
            }
        }
        if (checks && !ours) {
            calls = std::make_unique<CallChecks>(ee, o, *field, *pcars, lap_length, tally);
            calls->install();
        }
        if (ours) ee.hle(
            0x465078,
            [f, race](Ee& e) {
                const u32 D = u32(e.a(0));
                const int i = int((D - 0x14 - race->cond - kCar0) / kCarSize);
                race::CarInput in;
                f->decide_car(size_t(i), in);
                const sim::ai::Field::Probe p = f->probe_get(size_t(i));
                e.w32(D + 0x30, p.steer.bits), e.w32(D + 0x34, p.throttle.bits), e.w32(D + 0x38, p.brake.bits);
                e.w8(D + 0x3E, p.reverse ? 1 : 0), e.w16(D + 0x3C, p.shift_lock);
                e.w8(D + 0x1E, u8(p.mode)), e.w8(D + 0x1B, u8(p.state)), e.w8(D + 0x1C, u8(p.line)), e.w8(D + 0x1D, u8(p.next));
                e.w8(D + 0x43, u8(p.status)), e.w8(D + 0x21, u8(p.prev_line)), e.w16(D + 0x22, p.arc_ticks);
                e.w32(D + 0x24, p.v_cur.bits), e.w32(D + 0x28, p.v_next.bits), e.w32(D + 0x2C, p.blend.bits), e.w32(D + 0x54, p.offset.bits);
            },
            "our decision");
    }
    result.cars.resize(cars.size());
    std::vector<double> lap_start(cars.size(), 0.0);
    std::vector<float> last_s(cars.size());
    std::vector<bool> started(cars.size(), false);
    for (size_t i = 0; i < cars.size(); ++i) {
        result.cars[i].car = cars[i];
        last_s[i] = ee.rf(o.car(int(i)) + kF + kDistance);
    }
    const int trace = std::getenv("GT4R_AI_TRACE") ? std::atoi(std::getenv("GT4R_AI_TRACE")) : -1;
    int limit = int((std::max(400.0, double(lap_length) / 5.0) * std::max(laps, 1) + 60.0) * 60.0);
    if (const char* l = std::getenv("GT4R_AI_SECONDS")) limit = int(std::atof(l) * 60.0); // a set length of run
    int step = 0;
    try {
        for (; step < limit; ++step) {
            if (field) {
                pcars->refresh(), field->begin_step();
                // The time scales the program set at the last step's end against ours from the same cars.
                if (checks && step > 0)
                    for (int i = 0; i < o.cars; ++i) {
                        char w[48];
                        std::snprintf(w, sizeof w, "car %d step %d", i, step);
                        tally["band time scale"].bits(w, ee.r32(o.car(i) + kF + 0x444), gt3r::ee::F::raw(field->time_scale_bits(size_t(i))));
                    }
            }
            // GT4R_AI_FINISH=<mode>,<seconds>: every car's run ended so (0x343D78, the conductor's virtual 21) at that time.
            if (finish_step == step && field)
                for (int i = 0; i < o.cars; ++i) {
                    ee.call(0x343D78, {Arg::ptr(o.car(i)), Arg::integer(finish_mode)});
                    field->finish(size_t(i), finish_mode);
                }
            if (step == bspec_step && bspec_car >= 0) {
                ee.call(0x3F19E0, {Arg::ptr(o.car(bspec_car)), Arg::integer(0), Arg::integer(4), Arg::integer(bspec_level)});
                field->command(size_t(bspec_car), {bspec_level, -1});
            }
            if (checks && !ours && step % 120 == 60) check_corners(ee, o, *field, lap_length, tally);
            if (checks && !ours && step % 4 == 1) check_braking(ee, o, *field, lap_length, tally);
            if (checks && !ours && step % 60 == 30) check_places(ee, o, *field, tally);
            orig_step(ee, o);
            if (std::getenv("GT4R_AI_GEARTRACE") && step % 15 == 0) { // car 0's gearbox (F = car + 0x104)
                const u32 F = o.car(0) + 0x104;
                if (step == 0) {
                    const u32 D = ee.r32(o.car(0) + 0x10) + 0x234;
                    std::printf("up %u down %u %u %u %u course kind %u\n", ee.r16(D + 0x2E), ee.r16(D + 0x30), ee.r16(D + 0x32), ee.r16(D + 0x34), ee.r16(D + 0x36),
                                ee.r8(o.cond + 0x10000 - 0x3423));
                }
                std::printf("t %.2f d %.1f tcs %d ascc %d brk %.2f v %.1f gear %d rpm %d clutch %d req %.2f lock %d hunt %d thr %.2f pedal %.2f\n", step / 60.0,
                            double(ee.rf(F + 0x4A4)), int(ee.r8(F + 0x5F9)), int(ee.r8(F + 0x5F8)), double(ee.rf(F + 0x48C)), double(ee.rf(F + 0x53C)) * 3.6,
                            int(ee.r8(F + 0x520)), int(s16(ee.r16(F + 0x4FA))), int(ee.r8(F + 0x522)), double(ee.rf(F + 0x514)), int(ee.r8(F + 0x526)),
                            int(ee.r16(F + 0x52A)), double(ee.rf(F + 0x494)), double(ee.rf(F + 0x488)));
            }
            if (field) field->end_step();
            const double t = double(step + 1) / 60.0;
            bool all = true;
            for (size_t i = 0; i < cars.size(); ++i) {
                AiCarRun& r = result.cars[i];
                const u32 F = o.car(int(i)) + kF;
                const float s = ee.rf(F + kDistance), v = ee.rf(F + kSpeed);
                r.top_speed = std::max(r.top_speed, v);
                if (t > 10.0 && !r.finished && std::fabs(v) < 5.0f / 3.6f) r.stuck += 1.0 / 60.0;
                if (!r.finished && last_s[i] > 0.75f * lap_length && s < 0.25f * lap_length) {
                    if (started[i]) {
                        r.laps.push_back(t - lap_start[i]);
                        r.total += r.laps.back();
                    }
                    started[i] = true;
                    lap_start[i] = t;
                    if (int(r.laps.size()) >= laps) r.finished = true;
                }
                last_s[i] = s;
                if (!r.finished) all = false;
                if (int(i) == trace && field && ours && step % 15 == 0) {
                    const sim::ai::Field::Snapshot sn = field->snapshot(i);
                    std::printf("  ours: dist %.1f to %.1f look %.1f\n", double(sn.brake_dist), double(sn.brake_to), double(sn.look));
                }
                if (int(i) == trace && step % 30 == 0) {
                    const u32 D = o.car(int(i)) + 0x14;
                    std::printf("%7.2f s %7.1f %5.1f km/h line %d next %2d mode %d status %d state %d cur %5.1f next %5.1f thr %.2f brk %.2f steer %+.3f\n", t,
                                double(s), double(v) * 3.6, ee.r8(D + 0x1C), ee.r8(D + 0x1D), ee.r8(D + 0x1E), ee.r8(D + 0x43), ee.r8(D + 0x1B),
                                double(ee.rf(D + 0x24)) * 3.6, double(ee.rf(D + 0x28)) * 3.6, double(ee.rf(D + 0x34)), double(ee.rf(D + 0x38)),
                                double(ee.rf(D + 0x30)));
                }
            }
            if (std::getenv("GT4R_AI_SCALE") && step % 600 == 0) {
                std::printf("%6.1f s scales:", t);
                for (int i = 0; i < o.cars; ++i) std::printf(" %.3f", double(ee.rf(o.car(i) + kF + 0x444)));
                std::printf("\n");
            }
            if (all) break;
        }
    } catch (const std::exception& e) {
        char b[160];
        std::snprintf(b, sizeof b, "stopped after %d steps: %s (pc %08X ra %08X)", step, e.what(), ee.cur_pc, ee.g32(31));
        result.error = b;
        if (print) std::printf("ai: %s\n", b);
    }
    if (aid_taps && print) aid_taps->print();
    if (calls && print)
        for (const auto& [kind, n] : calls->kinds_) std::printf("  decisions of driver kind %d: %llu\n", kind, (unsigned long long)n);
    for (const auto& [name, tl] : tally) {
        result.checks[name] = {tl.n, tl.off};
        if (print) tl.print(name.c_str());
    }
    result.seconds = double(step) / 60.0;
    result.ran = result.error.empty();
    if (print) {
        std::printf("ai: %s drivers, %s, lap %.1f m, %.1f s run\n", ours ? "our" : "the program's", course.c_str(), double(lap_length), result.seconds);
        for (const AiCarRun& r : result.cars) {
            std::printf("  %-28s %-10s", r.car.c_str(), r.finished ? clock(r.total).c_str() : "-");
            for (double l : r.laps) std::printf(" %s", clock(l).c_str());
            std::printf("  top %.0f km/h, %.1f s below 5 km/h\n", double(r.top_speed) * 3.6, r.stuck);
        }
    }
    return result;
}

} // namespace gt4r::diff
