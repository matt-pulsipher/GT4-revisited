// gt4r_diff: the taps of a car's start (rules_start.h) against the program's: the car's set-up as the conductor's
// initialisation runs it (0x363B10, from the converter's return to its end), the race step's start hold per car, the
// attitude's start hold (0x36AE20 with 0x36A318 / 0x36A298), the rolling car's hold and its release (0x3452F0, 0x345228).
//
// GT4R_START_PITHOLD=<n>: car 0's hold counter (F + 0x4BA) set to n after the set-up, so that the attitude's start hold
// and the pit stop's counter run (a pit start's 120). GT4R_START_ROLLING=<n>: car 0 held in a rolling start after the
// set-up (the program's 0x3452F0) and let go after n steps (the single-player conductor's virtual 20 (+0xA0), 0x347928).

#include "taps.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_start.h"
#include "gt4r/sim/rules_wear.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
namespace rules = sim::rules;

constexpr u32 kLineObject = 0xDA50;  // conductor +: the computer drivers' line object
constexpr u32 kRuleBlock = 0xF818;   // conductor +: the rule block
constexpr u32 kCounters = 0xCBFC;    // conductor +: each car's start hold counter

F f(Ee& ee, int r) { return F::raw(ee.fpr[r]); }
u32 car_a0(Ee& e) { return u32(e.a(0)); }

// Our road of the race's course (the pose's query), opened once.
struct Road {
    std::unique_ptr<course::Runway> road;
    std::unique_ptr<sim::RoadGround> ground;
};
Road& road_of(const OrigRace& o) {
    static Road r;
    if (r.ground || !o.disc) return r;
    const gt3r::Volume::Entry* file = o.disc->volume().find("crs/" + o.course_label);
    if (!file) return r;
    const Bytes data = o.disc->volume().read(*file);
    const course::CourseFile parsed = course::CourseFile::parse(data);
    r.road = std::make_unique<course::Runway>(course::Runway::parse(parsed.section(data, course::kSlotRunway)));
    r.ground = std::make_unique<sim::RoadGround>(*r.road);
    return r;
}

rules::StartTables start_tables(Ee& ee) {
    rules::StartTables t;
    t.fuel_less = ee.r8(0x620E24);
    t.motor_scale = F::raw(ee.r32(0x8438DC));
    t.steer_mode = ee.r32(rules::kSteerMode);
    return t;
}

rules::PoseHooks pose_hooks(Ee& ee, const OrigRace& o) {
    rules::PoseHooks h;
    Road& r = road_of(o);
    h.query = [&r](ex::Ground& g) -> u8 {
        if (!r.ground) return 0;
        sim::GroundHit hit;
        const F at[3] = {g.x, g.h + F::raw(0x40A00000), g.z};
        if (!r.ground->query(at, hit)) {
            g.miss = 1;
            return 0;
        }
        g.miss = 0;
        g.height = F(hit.point.y);
        g.attr_a = F(hit.attr_a), g.attr_b = F(hit.attr_b);
        g.normal = {F(hit.normal.x), F(hit.normal.y), F(hit.normal.z)}, g.plane_d = F(hit.plane_d);
        g.seg = s32(u32(u16(s16(hit.cluster))) << 16 | u32(u16(hit.triangle)));
        g.material = hit.material, g.x32 = 0, g.flags = u8(hit.flags & 1);
        return u8(sim::surface_index(hit.material, hit.flags));
    };
    h.soft = [&ee](const ex::Ground& g) { return ee.r32(rules::kSoftMaterials + 4 * u32(g.material & 31)) != 0; };
    return h;
}

// The car's line in the line object (0x34C760 → 0x46BDA0): 8 less its index with lines loaded, else 0; the line's heading
// (0x34C7C0 → 0x46BE70) 0 without lines (❓ with lines: not read here).
bool have_lines(Ee& ee, u32 cond) { return ee.r16(cond + kLineObject + 0xC) != 0; }

// ---- span checks: ours run at a place inside a routine, compared at a later place ----------------------------------

struct Span {
    const char* name;
    u32 from, to;
    std::function<u32(Ee&)> car_of;  // at `from`
    std::function<void(TapCall&)> ours;
    std::vector<std::string> skip;
};
struct SpanPending {
    size_t span;
    u32 sp, car;
    int index;
    sim::Record mine;
    Image before;
    std::vector<Out> outs;
};
struct SpanStat {
    std::string name;
    u64 calls = 0, clean = 0;
    std::map<std::string, std::pair<u64, std::string>> fields;
    std::map<std::string, u64> written;
};
struct SpanRun {
    const OrigRace* race = nullptr;
    const ex::RaceInputs* inputs = nullptr;
    std::vector<Span> spans;
    std::vector<SpanStat> stats;
    std::vector<SpanPending> pending;
};
SpanRun& run() {
    static auto* r = new SpanRun; // (kept for the print at exit)
    return *r;
}

void span_from(Ee& ee, size_t k) {
    SpanRun& r = run();
    const Span& s = r.spans[k];
    const u32 car = s.car_of(ee);
    int index = -1;
    for (int i = 0; i < r.race->cars; ++i)
        if (r.race->car(i) == car) index = i;
    if (index < 0) return;
    SpanPending p{k, ee.g32(29), car, index, sim::Record{}, read_image(ee, car), {}};
    p.mine.car.behaviour = rules::behaviour();
    load(p.mine, p.before);
    u8 row[rules::kWheelLayoutSize];
    ee.read_bytes(rules::kWheelLayouts + rules::kWheelLayoutSize * p.before.params[0x30], row, sizeof row);
    p.mine.layout = rules::wheel_layout(row);
    TapCall call{ee, *r.race, index, p.mine, p.mine.car, *r.inputs, {}};
    s.ours(call);
    p.outs = std::move(call.outs);
    r.pending.push_back(std::move(p));
}

void span_to(Ee& ee, size_t k) {
    SpanRun& r = run();
    if (r.pending.empty() || r.pending.back().span != k || r.pending.back().sp != ee.g32(29)) return;
    SpanPending p = std::move(r.pending.back());
    r.pending.pop_back();
    SpanStat& st = r.stats[k];
    ++st.calls;
    const Image after = read_image(ee, p.car);
    bool clean = true;
    auto note = [&](const std::string& name, double theirs, double ours) {
        clean = false;
        auto& [n, example] = st.fields[name];
        if (n++ == 0) {
            char text[160];
            std::snprintf(text, sizeof text, "the program's %.9g, ours %.9g (car %d, call %llu)", theirs, ours, p.index, static_cast<unsigned long long>(st.calls));
            example = text;
        }
    };
    for (const Out& o : p.outs) {
        u32 theirs = 0;
        ee.read_bytes(o.address, reinterpret_cast<u8*>(&theirs), o.size);
        if (theirs == o.bits) continue;
        F x, y;
        x.bits = theirs, y.bits = o.bits;
        note(o.name, o.real ? double(x.host()) : double(theirs), o.real ? double(y.host()) : double(o.bits));
    }
    const std::vector<std::string>& skip = r.spans[k].skip;
    for (const Slot& s : slots()) {
        const u8* theirs = (s.place.block == sim::layout::Block::Car ? after.car.data() : after.params.data()) + s.place.offset;
        const u8* was = (s.place.block == sim::layout::Block::Car ? p.before.car.data() : p.before.params.data()) + s.place.offset;
        const u8* mine = reinterpret_cast<const u8*>(&p.mine) + s.member;
        if (std::memcmp(theirs, was, s.size) != 0) ++st.written[s.name];
        if (std::memcmp(theirs, mine, s.size) == 0) continue;
        bool skipped = false;
        for (const std::string& x : skip) skipped = skipped || s.name.compare(0, x.size(), x) == 0;
        if (skipped) continue;
        double a = 0, b = 0;
        if (s.real) {
            F x, y;
            std::memcpy(&x, theirs, 4), std::memcpy(&y, mine, 4);
            a = double(x.host()), b = double(y.host());
        } else {
            u32 x = 0, y = 0;
            std::memcpy(&x, theirs, s.size), std::memcpy(&y, mine, s.size);
            a = double(x), b = double(y);
        }
        note(s.name, a, b);
    }
    st.clean += clean;
}

void add_span(Ee& ee, Span s) {
    SpanRun& r = run();
    const size_t k = r.spans.size();
    r.stats.emplace_back();
    r.stats.back().name = s.name;
    ee.add_probe(s.from, [k](Ee& e) { span_from(e, k); });
    ee.add_probe(s.to, [k](Ee& e) { span_to(e, k); });
    r.spans.push_back(std::move(s));
}

} // namespace

void print_start_spans() {
    for (const SpanStat& st : run().stats) {
        std::printf("  %-28s %llu calls, %llu with every placed field equal\n", st.name.c_str(), static_cast<unsigned long long>(st.calls),
                    static_cast<unsigned long long>(st.clean));
        for (const auto& [name, v] : st.fields) std::printf("      %-22s differs in %llu: %s\n", name.c_str(), static_cast<unsigned long long>(v.first), v.second.c_str());
        if (!st.written.empty()) {
            std::printf("      writes:");
            size_t n = 0;
            for (const auto& [name, c] : st.written)
                if (n++ < 60) std::printf(" %s", name.c_str());
            std::printf("%s\n", st.written.size() > 60 ? " ..." : "");
        }
    }
}

namespace {

// The car's set-up from the converter's return (0x363FB8: the computer driver's object made) to its end (0x364718), with
// the set-up's own arguments read from its frame.
void setup_ours(TapCall& t) {
    Ee& ee = t.ee;
    const u32 sp = ee.g32(29), cond = t.race.cond;
    rules::SetupArgs a;
    a.index = s32(ee.r32(sp + 0x58));
    a.not_racing = t.car.ghost;
    const gt3r::s8 wt = t.rec.x.input.wheel_type;
    a.entry_driver = wt == 2 ? 1 : wt == 3 ? 2 : 0;
    a.control = gt3r::s8(t.car.control);
    a.pedal_style = u8(ee.r32(sp + 0x54));
    a.start_kind = s32(ee.r32(sp + 0x5C));
    a.speed = gt3r::ee::int_to_f(s32(ee.r32(sp + 0x108)));
    a.fuel_rule = ee.r8(cond + kRuleBlock + 0x4C);
    a.hint = s32(ee.r32(sp + 0));
    a.x = F::raw(ee.r32(sp + 4)), a.y = F::raw(ee.r32(sp + 8)), a.z = F::raw(ee.r32(sp + 0xC)), a.heading = F::raw(ee.r32(sp + 0x10));
    a.start_mark = u8(ee.r32(sp + 0x70));
    a.line = have_lines(ee, cond) ? u8(8 - a.index) : 0;
    rules::SetupHooks h;
    h.pose = pose_hooks(ee, t.race);
    u8 wb[0x6C];
    ee.read_bytes(cond + 4, wb, sizeof wb);
    rules::car_setup(t.car, t.rec.x, t.rec.layout, start_tables(ee), rules::wear_rules(wb), a, h);
}

// The race step's start hold for the car at the loop's $v0 (0x34DDA0 .. 0x34DDF8: $s6 the countdown, $s3 the counter).
void hold_ours(TapCall& t) {
    Ee& ee = t.ee;
    u32 counter = ee.r32(ee.g32(19));
    rules::start_hold(t.car, ee.g32(22) != 0, counter);
    t.mem_i("counter", ee.g32(19), counter);
}

} // namespace

void install_setup_taps(Ee& ee, const OrigRace& race, const ex::RaceInputs& inputs) {
    SpanRun& r = run();
    r = SpanRun{};
    r.race = &race, r.inputs = &inputs;
    // (the distance along the course is the conductor's, F + 0x4A4 through its virtual +0x108; the laps and sectors
    // from the place the race's; the computer driver's part of the record its own, with the B-spec driver's pace and TCS
    // class the race's command 2 sets, 0x3F1BD8)
    add_span(ee, {"start: car set-up", 0x363FB8, 0x364718, [](Ee& e) { return u32(e.g32(19)); }, setup_ours,
                  {"f3e4", "laps", "lap_base", "near.", "aids.tcs_class", "input.pace_"}});
    // The race step's start hold, per car (0x34DDA0: $v0 the car).
    add_span(ee, {"start: countdown hold", 0x34DDA0, 0x34DDF8, [](Ee& e) { return u32(e.g32(2)); }, hold_ours, {}});
    if (std::getenv("GT4R_START_DELAY")) ee.hle(0x35DF80, [](Ee&) {}, "the race's message"); // (the race's part: left out)
}

void start_spans(const std::function<void(const std::string& name, u64 calls, u64 clean)>& each) {
    for (const SpanStat& st : run().stats) each(st.name, st.calls, st.clean);
}

void start_before_step(Ee& ee, OrigRace& o, int step) {
    const u32 car = o.car(0), F0 = car + kF;
    if (step == 0) {
        if (const char* n = std::getenv("GT4R_START_PITHOLD")) ee.w16(F0 + 0x4BA, u16(std::atoi(n)));
        if (const char* n = std::getenv("GT4R_START_COUNTDOWN")) ee.w32(o.info + 0xBC, u32(std::atoi(n)));
        if (const char* n = std::getenv("GT4R_START_DELAY")) ee.w32(o.cond + kCounters, u32(std::atoi(n)));
        if (std::getenv("GT4R_START_ROLLING")) { // (called on its own: compared here, the tap sees no return)
            sim::Record mine;
            mine.car.behaviour = rules::behaviour();
            load(mine, read_image(ee, car));
            const bool to6 = rules::rolling_hold(mine.car, mine.x);
            const u32 line = ee.r8(car + 0x30);
            const gt3r::diff::CallResult r = ee.call(0x3452F0, {Arg::ptr(car)}, 10'000'000);
            sim::Record theirs;
            theirs.car.behaviour = rules::behaviour();
            load(theirs, read_image(ee, car));
            std::printf("  start: rolling hold (car 0, called): returns %llu (ours 1), driver kind %u (ours %u), the driver's line %u -> %u (ours to "
                        "line 6: %d)\n",
                        static_cast<unsigned long long>(r.v0_s32()), theirs.car.driver_kind, mine.car.driver_kind, line, ee.r8(car + 0x30), int(to6));
        }
    } else if (const u32 left = ee.r32(o.info + 0xBC); left > 0) {
        ee.w32(o.info + 0xBC, left - 1); // (the race flow's countdown, one a step)
    }
    if (const char* n = std::getenv("GT4R_START_ROLLING"); n && step == std::atoi(n)) {
        const u32 vt = ee.r32(o.cond + kCondVtable) + 0x14 * 8; // (virtual 20: +0xA0)
        ee.call(ee.r32(vt + 4), {Arg::ptr(o.cond + u32(s32(s16(ee.r16(vt))))), Arg::integer(0)}, 10'000'000);
    }
}

void add_taps_start(Taps& taps) {
    using namespace sim::rules;
    // (the line's heading 0 without lines, as the step harness runs; ❓ with lines not read here)
    taps.add({0x36AE20, "start: attitude hold", car_a0, [](TapCall& t) {
                  t.ret_i("returns", attitude_hold(t.car, t.rec.x.body, F()) ? 1 : 0);
              }});
    taps.add({0x36A318, "start: hold lift", car_a0, [](TapCall& t) {
                  t.ret_i("returns", hold_lift_body(t.car, t.rec.x.body, f(t.ee, 12)) ? 1 : 0);
              }});
    taps.add({0x36A298, "start: hold lift height", [](Ee& e) {
                  const u32 ra = e.g32(31);
                  return ra >= 0x36A318 && ra < 0x36A48C ? u32(e.g32(18)) : 0u;
              },
              [](TapCall& t) {
                  u32 kind = 0;
                  t.ret_f("returns", hold_lift(u32(t.ee.a(0)), f(t.ee, 12), kind));
                  t.mem_i("kind", u32(t.ee.a(1)), kind);
              }});
    taps.add({0x3452F0, "start: rolling hold", car_a0, [](TapCall& t) { t.ret_i("returns", gt3r::s8(t.car.control) == 4 ? 0 : 1), rolling_hold(t.car, t.rec.x); }});
    taps.add({0x345228, "start: release", car_a0, [](TapCall& t) {
                  // (the driver's line change through the line object: made when the line object has lines, or at once on a
                  // course whose byte conductor + 0x10000 - 0x341F is set)
                  const bool line_ok = t.ee.r8(t.race.cond + 0x10000 - 0x341F) != 0 || have_lines(t.ee, t.race.cond);
                  t.ret_i("returns", release(t.car, t.rec.x, line_ok) ? 1 : 0);
              },
              {"start.rolling_steps"}}); // (0x3F37B8: the caller's)
}

} // namespace gt4r::diff
