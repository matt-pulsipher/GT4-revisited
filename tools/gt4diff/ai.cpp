// gt4r_diff's run of the computer drivers (diff.h run_ai): a field of the program's own drivers in the program's own
// race, run in the interpreter, its lap times beside ours (sim::ai::run_field), the drivers' routines compared in
// place at every call (docs/specs/ai.md "Checks").

#include "diff.h"
#include "orig.h"
#include "taps.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

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
    bool loose() const override { return false; }
    const sim::RoadGround& ground() const override { return ground_; }
    const sim::Extra& extra(size_t i) const override { return recs_[i].x; }
    bool hits(size_t i, bool& front, bool& rear) const override {
        const u32 body = o_.cond + 0xDA6C + 0x4F0 * u32(i) + 0xC;
        front = ee_.r32(body + 0x434) != 0, rear = ee_.r32(body + 0x438) != 0;
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
};

// The driver object D of car i (the car's +0x14) as our drivers' probe state.
sim::ai::Field::Probe probe_of(Ee& ee, const OrigRace& o, int i, float lap) {
    const u32 D = o.car(i) + 0x14, F = o.car(i) + kF;
    sim::ai::Field::Probe p;
    p.line = ee.r8(D + 0x1C), p.next = ee.r8(D + 0x1D), p.mode = ee.r8(D + 0x1E), p.next_kind = ee.r8(D + 0x1F), p.status = ee.r8(D + 0x43);
    p.follow = gt3r::s8(ee.r8(D + 0x51)), p.chain = ee.r8(D + 0x6A);
    p.v_cur = gt3r::ee::F::raw(ee.r32(D + 0x24)), p.v_next = gt3r::ee::F::raw(ee.r32(D + 0x28)), p.contact = ee.rf(D + 0x44);
    float s = ee.rf(F + kDistance);
    if (s >= lap) s -= lap;
    if (s < 0) s += lap;
    p.s = s;
    p.rand = ee.r16(D + 0x68);
    p.corner_mult = gt3r::ee::F::raw(ee.r32(D + 0xB8)), p.brake_mult = gt3r::ee::F::raw(ee.r32(D + 0xBC));
    p.alongside_mult = gt3r::ee::F::raw(ee.r32(D + 0xC0));
    for (u32 k = 0; k < 8; ++k) p.zone[k] = ee.r8(D + 0x48 + k);
    p.cooldown = s16(ee.r16(D + 0x18));
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
    const u32 lap_bits = ee.call(kLapLength, {Arg::ptr(o.cond + kAiCourse)}).f0;
    float lap_length;
    std::memcpy(&lap_length, &lap_bits, 4);
    result.lap_length = lap_length;
    // GT4R_AI_OURS: our drivers in the program's race (the decision 0x465078 replaced by ours on the program's cars).
    std::unique_ptr<course::Runway> road;
    std::unique_ptr<ProgramCars> pcars;
    std::unique_ptr<sim::ai::Field> field;
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
        if (checks) check_lines(ee, o, *field, tally), check_params(ee, o, *field, tally);
        if (ours) ee.hle(
            0x465078,
            [f, race](Ee& e) {
                const u32 D = u32(e.a(0));
                const int i = int((D - 0x14 - race->cond - kCar0) / kCarSize);
                race::CarInput in;
                f->decide_car(size_t(i), in);
                const sim::ai::Field::Snapshot sn = f->snapshot(size_t(i));
                e.wf(D + 0x30, sn.steer), e.wf(D + 0x34, sn.wanted_throttle), e.wf(D + 0x38, sn.brake);
                e.w8(D + 0x3E, sn.reverse ? 1 : 0), e.w16(D + 0x3C, 0);
                e.w8(D + 0x1E, u8(sn.mode)), e.w8(D + 0x1B, u8(sn.state)), e.w8(D + 0x1C, u8(sn.line)), e.w8(D + 0x1D, u8(sn.next));
                e.w8(D + 0x43, u8(sn.status));
                e.wf(D + 0x24, sn.v_cur), e.wf(D + 0x28, sn.v_next);
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
    const int limit = int((std::max(400.0, double(lap_length) / 5.0) * std::max(laps, 1) + 60.0) * 60.0);
    int step = 0;
    try {
        for (; step < limit; ++step) {
            if (field) pcars->refresh(), field->begin_step();
            if (checks && !ours && step % 120 == 60) check_corners(ee, o, *field, lap_length, tally);
            if (checks && !ours && step % 4 == 1) check_braking(ee, o, *field, lap_length, tally);
            orig_step(ee, o);
            if (field) field->end_step();
            const double t = double(step + 1) / 60.0;
            bool all = true;
            for (size_t i = 0; i < cars.size(); ++i) {
                AiCarRun& r = result.cars[i];
                const u32 F = o.car(int(i)) + kF;
                const float s = ee.rf(F + kDistance), v = ee.rf(F + kSpeed);
                r.top_speed = std::max(r.top_speed, v);
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
            std::printf("  top %.0f km/h\n", double(r.top_speed) * 3.6);
        }
    }
    return result;
}

} // namespace gt4r::diff
