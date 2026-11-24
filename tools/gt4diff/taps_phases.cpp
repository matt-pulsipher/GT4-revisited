// gt4r_diff: the taps of phases 1, 2, 3, 6, the preparation and the hit records (rules_phases.h) against the program's.

#include "taps.h"
#include "diff.h"

#include "gt4r/sim/ground.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_phases.h"
#include "gt4r/sim/rules_step.h"
#include "gt4r/sim/rules_wear.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
using gt3r::s8;
using gt3r::s16;
namespace rules = sim::rules;

constexpr u32 kCount = 0x10000 - 0x760;    // conductor +: the car count
constexpr u32 kCourse = 0x10000 - 0x3428;  // conductor +: the course object
constexpr u32 kPylonsHit = 0x1003C;        // conductor +: the pylons a license test's car has hit (s16)
constexpr u32 kPenalty = 0x35FCC8, kLicense = 0x35FE80, kLicenseVirtual = 0x3F6938, kMissionVirtual = 0x3F7038;

F f(Ee& ee, int r) { return F::raw(ee.fpr[r]); }
u32 car_at(u32 cond, u32 i) { return cond + kCar0 + kCarSize * i; }
u32 cond_of(Ee& ee, u32 car) { return ee.r32(car + 4); }
u32 car_a0(Ee& e) { return u32(e.a(0)); }
u32 car_a1(Ee& e) { return u32(e.a(1)); }

rules::MoodTables mood_tables(Ee& ee) {
    return rules::mood_tables([&](u32 a) { return ee.r8(a); });
}
std::array<u32, 32> soft_materials(Ee& ee) {
    std::array<u32, 32> s{};
    for (u32 m = 0; m < 32; ++m) s[m] = ee.r32(rules::kSoftMaterials + 4 * m);
    return s;
}
rules::HitRules hit_rules(Ee& ee, u32 cond) {
    return rules::hit_rules([&](u32 o) { return ee.r8(cond + rules::kRuleBlock + o); });
}
rules::WearRules wear_rules(Ee& ee, u32 cond) {
    u8 b[0x6C];
    ee.read_bytes(cond + 4, b, sizeof b);
    return rules::wear_rules(b);
}
// The course's lap length (0x34C230: the course object's road (+0x04, then +0xDC), its +0x14).
F lap_length(Ee& ee, u32 cond) {
    const u32 course = ee.r32(cond + kCourse);
    return F::raw(ee.r32(ee.r32(ee.r32(course + 4) + 0xDC) + 0x14));
}
rules::PassBy pass_by(Ee& ee, u32 cond) {
    rules::PassBy p;
    for (u32 i = 0; i < 6; ++i)
        for (u32 j = 0; j < 6; ++j) {
            p.state[i][j] = ee.r8(cond + rules::kPassByStates + 6 * i + j);
            p.laps[i][j] = s8(ee.r8(cond + rules::kGapLaps + 6 * i + j));
            p.gap[i][j] = F::raw(ee.r32(cond + rules::kGaps + 4 * (6 * i + j)));
        }
    return p;
}
// The conductor's judge of a hit, by its virtual +0x190 (the vtable at conductor + 0x10140, {s16 offset, routine} slots).
rules::Judge judge_of(Ee& ee, u32 cond) {
    const u32 routine = ee.r32(ee.r32(cond + kCondVtable) + 0x190 + 4);
    if (routine == kPenalty || routine == kMissionVirtual) return rules::Judge::Penalty;
    if (routine == kLicenseVirtual) return rules::Judge::License;
    return rules::Judge::None;
}

// A computer driver's grip by tire class (0x467470: control source 1 / 2 or a driver kind; its parameter block +0xA8).
std::optional<std::array<F, 3>> driver_grip_of(TapCall& t) {
    const u32 car = t.race.car(t.car_index), S = car + kF;
    const s8 control = s8(t.ee.r8(S + 0x466));
    if (control != 1 && control != 2 && t.ee.r8(S + 0x467) == 0) return std::nullopt;
    const u32 d = car + 0x14, v0 = t.ee.r32(d + 0xE8), a1 = t.ee.r32(d + 0xE4);
    const u32 pb = t.ee.r32(a1 + 4 * u32(t.ee.r8(v0 + 0x544)) + 0x10024);
    std::array<F, 3> g;
    for (u32 k = 0; k < 3; ++k) g[k] = F::raw(t.ee.r32(pb + 0xA8 + 4 * k));
    return g;
}

// The race's tables as our session builds them from the program, with what the race's set-up left in memory where a
// tap's run changes it (the wear rules, GT4R_STEP_WEAR) or the program's image holds another value (the aero globals).
const rules::Tables& tables(TapCall& t) {
    static std::unique_ptr<rules::Tables> made;
    if (!made) {
        Bytes image(kBssStart - 0x100000);
        t.ee.read_bytes(0x100000, image.data(), u32(image.size()));
        const sim::Program program = sim::Program::from_image(std::move(image), 0x100000);
        made = std::make_unique<rules::Tables>(rules::tables(program, t.inputs.statics));
        std::printf("  phases: the race's tables %s\n", program.known() ? "from the program" : "(no known program: the engine's)");
    }
    static rules::Tables now;
    now = *made;
    const u32 cond = cond_of(t.ee, t.race.car(t.car_index));
    now.wear = wear_rules(t.ee, cond);
    now.aero = {F::raw(t.ee.r32(rules::kAeroGlobals - 4)), F::raw(t.ee.r32(rules::kAeroGlobals)), F::raw(t.ee.r32(rules::kAeroGlobals + 4)),
                F::raw(t.ee.r32(rules::kAeroGlobals + 8)), F::raw(t.ee.r32(rules::kAeroGlobals + 12))};
    return now;
}

// The routine's call from inside one of two others: the car they hold (0x35FBC8's caller keeps it in $s6, or the state
// block in $s1).
u32 class_caller_car(Ee& e) {
    const u32 ra = e.g32(31);
    if (ra >= kPenalty && ra < kPenalty + 0x1B4) return e.g32(22);
    if (ra >= kLicense && ra < kLicense + 0x130) return e.g32(17) - kF;
    return 0;
}

// GT4R_PHASES_BRANCHES: how often the pods' and the hits' routines were called with each kind (counted at the routines'
// entries; printed when the run ends). The calls are compared by the taps.
std::map<std::string, u64>& branches() {
    static auto* m = new std::map<std::string, u64>; // (kept for the print at exit)
    return *m;
}
void print_branches() {
    std::printf("  phases branches:\n");
    for (const auto& [name, n] : branches()) std::printf("      %-40s %llu\n", name.c_str(), static_cast<unsigned long long>(n));
}
void add_branch_counts(Taps& taps) {
    static bool once = false;
    if (!once) once = true, std::atexit(print_branches);
    taps.add({0x350530, "(branches)", [](Ee& e) {
                  char n[64];
                  std::snprintf(n, sizeof n, "hit kind %d part %d", s32(e.a(1)), s32(e.a(2)));
                  ++branches()[n];
                  return 0u;
              }, [](TapCall&) {}});
    taps.add({0x35C848, "(branches)", [](Ee& e) {
                  char n[64];
                  std::snprintf(n, sizeof n, "set mood %d time %d", s32(e.a(1)), s32(e.a(2)));
                  ++branches()[n];
                  return 0u;
              }, [](TapCall&) {}});
}


// ---- checks of a routine compared where it jumps to its tail call (which shares its return: taps.h's comparison at the
// return takes it for the other's), or at its own return where the step's other taps watch the same routine (two taps of
// one routine share a return the same way): with its frame not yet given back ----

struct Split {
    const char* name;
    u32 entry, jump, frame; // the routine, its tail jump, its frame's size
    std::function<u32(Ee&)> car_of;
    std::function<void(TapCall&)> ours;
    std::vector<std::string> skip;
};
struct SplitPending {
    size_t split;
    u32 sp, car;
    int index;
    sim::Record mine;
    std::vector<Out> outs;
};
struct SplitRun {
    const OrigRace* race = nullptr;
    const ex::RaceInputs* inputs = nullptr;
    bool installed = false;
    std::vector<SplitPending> pending;
};
std::vector<Split>& splits() {
    static auto* v = new std::vector<Split>; // (kept for the print at exit)
    return *v;
}
SplitRun& split_run() {
    static SplitRun r;
    return r;
}
struct PhaseSplitStat {
    std::string name;
    u64 calls = 0, clean = 0;
    std::map<std::string, std::pair<u64, std::string>> fields; // the fields that differed: how often, an example
};
std::vector<PhaseSplitStat>& split_stats_store() {
    static auto* v = new std::vector<PhaseSplitStat>; // (kept for the print at exit)
    return *v;
}

void split_enter(Ee& ee, size_t k) {
    SplitRun& run = split_run();
    const Split& s = splits()[k];
    if (!run.race || !run.inputs) return;
    const u32 car = s.car_of(ee);
    int index = -1;
    for (int i = 0; i < run.race->cars; ++i)
        if (run.race->car(i) == car) index = i;
    if (index < 0) return;
    SplitPending p{k, ee.g32(29), car, index, sim::Record{}, {}};
    p.mine.car.behaviour = rules::behaviour();
    const Image before = read_image(ee, car);
    load(p.mine, before);
    u8 row[rules::kWheelLayoutSize];
    ee.read_bytes(rules::kWheelLayouts + rules::kWheelLayoutSize * before.params[0x30], row, sizeof row);
    p.mine.layout = rules::wheel_layout(row);
    TapCall call{ee, *run.race, index, p.mine, p.mine.car, *run.inputs, {}};
    s.ours(call);
    p.outs = std::move(call.outs);
    run.pending.push_back(std::move(p));
}

void split_jump(Ee& ee, size_t k) {
    SplitRun& run = split_run();
    const Split& s = splits()[k];
    if (run.pending.empty() || run.pending.back().split != k || run.pending.back().sp != ee.g32(29) + s.frame) return;
    SplitPending p = std::move(run.pending.back());
    run.pending.pop_back();
    PhaseSplitStat& st = split_stats_store()[k];
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
        if (theirs != o.bits) note(o.name, double(theirs), double(o.bits));
    }
    for (const Slot& slot : slots()) {
        bool skipped = false;
        for (const std::string& pre : s.skip) skipped = skipped || slot.name.rfind(pre, 0) == 0;
        if (skipped) continue;
        const u8* theirs = (slot.place.block == sim::layout::Block::Car ? after.car.data() : after.params.data()) + slot.place.offset;
        const u8* ours = reinterpret_cast<const u8*>(&p.mine) + slot.member;
        if (std::memcmp(theirs, ours, slot.size) == 0) continue;
        u32 a = 0, b = 0;
        std::memcpy(&a, theirs, std::min<u32>(slot.size, 4)), std::memcpy(&b, ours, std::min<u32>(slot.size, 4));
        if (slot.real) note(slot.name, double(F::raw(a).host()), double(F::raw(b).host()));
        else note(slot.name, double(a), double(b));
    }
    if (clean) ++st.clean;
}

void print_splits() {
    for (const PhaseSplitStat& st : split_stats_store()) {
        std::printf("  %-28s %llu calls, %llu with every placed field equal (at its tail jump or return)\n", st.name.c_str(), static_cast<unsigned long long>(st.calls),
                    static_cast<unsigned long long>(st.clean));
        for (const auto& [name, f] : st.fields) std::printf("      %-22s differs in %llu: %s\n", name.c_str(), static_cast<unsigned long long>(f.first), f.second.c_str());
    }
}

void install_splits(TapCall& t) {
    SplitRun& run = split_run();
    if (run.installed) return;
    run.installed = true, run.race = &t.race, run.inputs = &t.inputs;
    // GT4R_PHASES_POD: every car given the category with moods (P + 0x32 = 1; no car of the disc has it), to run phases 3
    // and 6 and the hits' moods.
    if (std::getenv("GT4R_PHASES_POD"))
        for (int i = 0; i < t.race.cars; ++i) t.ee.w8(t.race.params(i) + 0x32, sim::rules::kPodCategory);
    if (std::getenv("GT4R_PHASES_POD")) t.rec.x.aids.mark_mode = sim::rules::kPodCategory;
    for (size_t k = 0; k < splits().size(); ++k) {
        t.ee.add_probe(splits()[k].entry, [k](Ee& ee) { split_enter(ee, k); });
        t.ee.add_probe(splits()[k].jump, [k](Ee& ee) { split_jump(ee, k); });
    }
}

void add_split(Split s) {
    splits().push_back(std::move(s));
    split_stats_store().push_back({splits().back().name, 0, 0, {}});
}

} // namespace

// The checks at a tail jump of the last run (the tests read them: they are not among the Taps' own).
std::vector<StepTap> phase_split_results() {
    std::vector<StepTap> v;
    for (const PhaseSplitStat& st : split_stats_store()) v.push_back({st.name, size_t(st.calls), size_t(st.clean)});
    return v;
}

void add_taps_phases(Taps& taps) {
    if (std::getenv("GT4R_PHASES_BRANCHES")) add_branch_counts(taps);
    // (the checks at a tail jump: installed on the run's interpreter at its first call below, their stats anew each run)
    {
        static bool printing = false;
        if (!printing) printing = true, std::atexit(print_splits);
        splits().clear(), split_stats_store().clear();
        split_run() = {};
        add_split({"phases: prep", 0x357700, 0x3577E4, 0x30, car_a0, [](TapCall& t) {
                       const auto g = driver_grip_of(t);
                       rules::prep(t.car, t.rec.x, t.rec.layout, tables(t), t.inputs.prep.condition, g ? &*g : nullptr);
                   },
                   {"w.slip_dir", "w.slip_fade"}}); // (the slip angles: the tail call 0x357690, the step's own tap)
        add_split({"phases: hit", 0x350530, 0x350654, 0x30, car_a0, [](TapCall& t) {
                       const u32 cond = cond_of(t.ee, t.race.car(t.car_index));
                       s16 pylons = s16(t.ee.r16(cond + kPylonsHit));
                       const rules::PhaseExtra was = t.rec.x.phases;
                       rules::hit(t.car, t.rec.x, mood_tables(t.ee), hit_rules(t.ee, cond), judge_of(t.ee, cond), &pylons, s32(t.ee.a(1)), s32(t.ee.a(2)), f(t.ee, 12));
                       // (the pad's record: the tail call 0x350328, its own tap)
                       t.rec.x.phases.hit = was.hit, t.rec.x.phases.hit_kind = was.hit_kind, t.rec.x.phases.hit_part = was.hit_part;
                       t.mem_i("pylons hit", cond + kPylonsHit, u16(pylons), 2);
                   },
                   {}});
        // (phase 1 and phase 18 at their returns: the step's own taps at their entries, step.cpp, run the engine's
        // save_previous and the bumps alone)
        add_split({"phases: save previous", 0x343ED0, 0x344034, 0x20, car_a0, [](TapCall& t) { rules::save_previous(t.car, t.rec.layout); }, {}});
        add_split({"phases: course out", 0x369888, 0x369B40, 0x70, car_a0,
                   [](TapCall& t) {
                       std::array<u8, 4> surface{}, material{};
                       for (size_t w = 0; w < 4; ++w)
                           material[w] = t.car.g[w].material, surface[w] = u8(sim::surface_index(t.car.g[w].material, t.car.g[w].flags));
                       const rules::BumpTables b = rules::bump_tables([&](u32 a) { return t.ee.r32(a); }, [&](u32 a) { return t.ee.r8(a); });
                       rules::road_bumps(t.car, t.rec.layout, b, surface, material);
                       rules::course_out(t.car, t.rec.x, t.rec.layout, b.soft);
                   },
                   {"w.ground", "w.surface", "w.x25", "w.surface_flags", "road_seg", "flags", "x535", "aids."}});
    }
    // ---- phase 2 ----
    // (and the run's race and inputs for the checks at a tail jump, with their probes, at its first call)
    taps.add({0x355E10, "phases: layout 7", car_a0, [](TapCall& t) {
                  install_splits(t);
                  rules::layout7_step(t.car, t.rec.x.phases);
              }});
    // ---- phase 3: the moods (with the rival's level from the race's pass-by states) ----
    taps.add({0x35CA60, "phases: moods", car_a0, [](TapCall& t) {
                  const u32 cond = cond_of(t.ee, t.race.car(t.car_index));
                  const s32 n = s32(t.ee.r32(cond + kCount));
                  const s32 rival = rules::rival_level(pass_by(t.ee, cond), t.car_index, n, lap_length(t.ee, cond));
                  rules::moods(t.car, t.rec.x, mood_tables(t.ee), t.rec.layout, soft_materials(t.ee), rival);
              }});
    // (conductor + 0xCD7C, car index, &rival): the car the index names
    taps.add({0x4149D8, "phases: pod rival",
              [](Ee& e) {
                  const u32 cond = e.r32(u32(e.a(0)));
                  return u32(e.a(1)) < 6 ? car_at(cond, u32(e.a(1))) : 0u;
              },
              [](TapCall& t) {
                  const u32 cond = t.ee.r32(u32(t.ee.a(0)));
                  s32 who = -1;
                  const s32 n = s32(t.ee.r32(cond + kCount));
                  t.ret_i("returns", u32(rules::rival_level(pass_by(t.ee, cond), t.car_index, n, lap_length(t.ee, cond), &who)));
                  t.mem_i("rival", u32(t.ee.a(2)), u32(who));
              }});
    taps.add({0x35C848, "phases: pod set mood", car_a0,
              [](TapCall& t) { rules::set_mood(t.rec.x, mood_tables(t.ee), s32(t.ee.a(1)), s32(t.ee.a(2))); }});
    taps.add({0x35C9E0, "phases: pod all soft", car_a0,
              [](TapCall& t) { t.ret_i("returns", rules::all_soft(t.car, t.rec.layout, soft_materials(t.ee)) ? 1 : 0); }});
    // ---- phase 6 ----
    // GT4R_PHASES_HANDBRAKE: the handbrake fully pulled (F + 0x490 = 1) on a car below 1 km/h, as phase 6 starts, to run its
    // count up (before the tap below reads the record).
    if (std::getenv("GT4R_PHASES_HANDBRAKE"))
        taps.add({0x35D1D0, "(phases: handbrake)", [](Ee& e) {
                      const u32 S = u32(e.a(0)) + kF;
                      const float v = e.rf(S + 0x53C);
                      if (v * v < 0.07f) e.wf(S + 0x490, 1.0f);
                      return 0u;
                  }, [](TapCall&) {}});
    taps.add({0x35D1D0, "phases: stop count", car_a0, [](TapCall& t) { rules::stop_count(t.car, t.rec.x); }});
    // ---- phase 4 ----
    taps.add({0x35C0B8, "phases: pit hold", car_a0, [](TapCall& t) {
                  rules::pit_step(t.car, t.rec.x, t.rec.layout, wear_rules(t.ee, cond_of(t.ee, t.race.car(t.car_index))));
              }});
    taps.add({0x35ACB0, "phases: message timer", car_a0, [](TapCall& t) { rules::message_timer(t.rec.x.phases); }});
    taps.add({0x354AC0, "phases: masses", car_a0, [](TapCall& t) { rules::masses(t.car, t.rec.x); }});
    taps.add({0x362E58, "phases: pit fresh tires", car_a0, [](TapCall& t) {
                  rules::fresh_tires(t.car, t.rec.x, s32(t.ee.a(1)), wear_rules(t.ee, cond_of(t.ee, t.race.car(t.car_index))));
              }});
    // ---- phase 12 ----
    taps.add({0x350308, "phases: clear hit", car_a0, [](TapCall& t) { rules::clear_hit(t.rec.x.phases); }});
    // ---- the hit records (the collision step's calls) ----
    taps.add({0x350328, "phases: hit record", car_a0, [](TapCall& t) {
                  rules::hit_record(t.rec.x.phases, hit_rules(t.ee, cond_of(t.ee, t.race.car(t.car_index))), s32(t.ee.a(1)), s32(t.ee.a(2)), f(t.ee, 12));
              }});
    taps.add({kPenalty, "phases: hit penalty", car_a1, [](TapCall& t) {
                  rules::penalty(t.car, t.rec.x, hit_rules(t.ee, u32(t.ee.a(0))), s32(t.ee.a(2)), s32(t.ee.a(3)), f(t.ee, 12));
              }});
    taps.add({kLicense, "phases: hit license", car_a1, [](TapCall& t) {
                  const u32 cond = u32(t.ee.a(0));
                  s16 pylons = s16(t.ee.r16(cond + kPylonsHit));
                  rules::license_judge(t.car, t.rec.x, hit_rules(t.ee, cond), pylons, s32(t.ee.a(2)), s32(t.ee.a(3)), f(t.ee, 12));
                  t.mem_i("pylons hit", cond + kPylonsHit, u16(pylons), 2);
              }});
    taps.add({0x35FBC8, "phases: hit part class", class_caller_car, [](TapCall& t) { t.ret_i("returns", u32(rules::part_class(s32(t.ee.a(0))))); }});
    // (0x35CF40, 0x35CF88, 0x35D0C0, the hit's moods: a car with moods ends them in a jump to 0x35C848, sharing its return;
    // the check of 0x350530 above takes them in)
}

} // namespace gt4r::diff
