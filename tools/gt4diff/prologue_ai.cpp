// gt4r_diff <prologue disc> prologue-ai [course] [cars] [steps]: Gran Turismo 4 Prologue's computer drivers, checked call by
// call. The program's own race runs in the interpreter with its own drivers (every car a computer's; GT4R_AI_HUMAN=n: the
// first n driven by the test drive's pad), and at every call of each of its driver routines ours runs on the record as the
// program has it at the call's entry; the record (the driver object included) and the routine's results are compared at its
// exit. First the lines as the race prepares them (0x4292C8), point by point. Routines are named by address; nothing of
// the program is kept.

#include "orig.h"
#include "prologue_race.h"
#include "prologue_taps.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/drivers_prologue.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_prologue.h"
#include "sim/exact/build_math.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/tick.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

namespace gt4r::diff::prologue {

namespace ex = gt3r::sim::exact;
namespace ap = sim::ai::prologue;
using gt3r::ee::F;

namespace {

F fpr(Ee& e, int r) { return F::raw(e.fpr[size_t(r)]); }

// What ours reads outside the car, as the program has it.
struct World {
    Race* race = nullptr;
    ex::AiCourse course;
    ap::DriverTables tables;
    sim::prologue::Tables t;
    u8 start_speed = 0;
    bool dirt = false;
    std::vector<sim::prologue::CarPart> parts;
};

// The driver's parameters (the engine's D +0x0C .. +0x7F): Prologue keeps them in the conductor, +0x10808 + 0x74 k for the
// car's index k (0x424BC8).
void params_fill(Ee& e, u32 car, ex::Car& c) {
    std::vector<u8> im(ex::kCarSize, 0);
    ex::store_image(c, im.data());
    const u32 at = e.r32(car + 4) + 0x10808 + 0x74 * u32(e.r8(car + 0xD8 + 0x3EC));
    e.read_bytes(at, im.data() + 0xEC8 + 0xC, 0x74);
    const F ratio = c.later.ratio;
    ex::load_image(c, im.data());
    c.later.ratio = ratio;
}

ap::Step step_of(Ee& e, World& w, u32 car, int index) {
    ap::Step s;
    s.course = &w.course, s.tables = &w.tables;
    sim::prologue::CarPart& x = w.parts[size_t(index)];
    x.layout = sim::prologue::wheel_layout(w.t, e.r8(e.r32(car + 0x10) + 0x30));
    x.tires.mean_slip = F::raw(e.r32(car + 0xD8 + 0x424));
    s.part = &x;
    const u32 cond = w.race->cond;
    s.wear.max = F::raw(e.r32(cond + 0x9C));
    s.start_count = s32(e.r32(e.r32(cond) + 0x74));
    s.clock = e.r32(cond + 0x107E0);
    const u32 i = e.r8(car + 0xD8 + 0x3EC);
    const u32 body = cond + 0xDFF4 + 0x4F0 * i + 0xC;
    s.hit_front = e.r32(body + 0x434) != 0, s.hit_rear = e.r32(body + 0x438) != 0;
    s.dirt = w.dirt, s.start_speed = w.start_speed;
    return s;
}

// The program's line l (the course object K: [[K +4] + 8 + 4 l] -> {count, the points of 0x28 bytes}).
std::vector<ex::AiPoint> program_line(Ee& e, u32 K, int l) {
    std::vector<ex::AiPoint> out;
    const u32 lp = e.r32(e.r32(K + 4) + 8 + 4 * u32(l));
    if (!lp) return out;
    const u32 n = e.r32(lp);
    for (u32 i = 0; i < n && i < 4096; ++i) {
        const u32 a = lp + 4 + 0x28 * i;
        ex::AiPoint p;
        p.kind = e.r8(a), p.brake_adjust = gt3r::s8(e.r8(a + 1)), p.speed_adjust = gt3r::s8(e.r8(a + 2)), p.x03 = e.r8(a + 3);
        p.x = F::raw(e.r32(a + 4)), p.y = F::raw(e.r32(a + 8)), p.height = F::raw(e.r32(a + 0xC));
        p.seg = s16(e.r16(a + 0x10)), p.surface = gt3r::s8(e.r8(a + 0x12)), p.x13 = e.r8(a + 0x13);
        p.dist = F::raw(e.r32(a + 0x14)), p.radius = F::raw(e.r32(a + 0x18)), p.slope = F::raw(e.r32(a + 0x1C)), p.bank = F::raw(e.r32(a + 0x20));
        p.heading = F::raw(e.r32(a + 0x24));
        out.push_back(p);
    }
    return out;
}

// A routine with no car of its own (the line queries, the arithmetic): ours on the arguments at the entry, `check` at the
// exit (the values that differ counted).
struct PureStat {
    std::string name;
    u64 calls = 0, clean = 0;
    std::map<std::string, u64> off;
    std::string example;
};
class PureTaps {
public:
    explicit PureTaps(Ee& e) : ee_(e) {}
    using Check = std::function<void(Ee&, const std::function<void(const char*, u32, u32)>&)>;
    // `enter` runs ours at the entry and returns the check for the exit.
    void add(u32 address, const char* name, std::function<Check(Ee&)> enter) {
        const size_t i = stats_.size();
        stats_.push_back({name});
        ee_.add_probe(address, [this, i, enter](Ee& e) {
            Check c = enter(e);
            const u32 ra = e.g32(31), sp = e.g32(29);
            const bool fresh = pending_.find(ra) == pending_.end();
            pending_[ra].push_back({i, sp, std::move(c)});
            if (fresh) e.add_probe(ra, [this, ra](Ee& x) { leave(x, ra); });
        });
    }
    const std::vector<PureStat>& stats() const { return stats_; }

private:
    struct P {
        size_t i;
        u32 sp;
        Check check;
    };
    void leave(Ee& e, u32 ra) {
        auto it = pending_.find(ra);
        if (it == pending_.end() || it->second.empty() || it->second.back().sp != e.g32(29)) return;
        P p = std::move(it->second.back());
        it->second.pop_back();
        PureStat& st = stats_[p.i];
        ++st.calls;
        bool clean = true;
        p.check(e, [&](const char* what, u32 program, u32 ours) {
            if (program == ours) return;
            clean = false;
            if (st.off[what]++ == 0 && st.example.empty()) {
                char b[160];
                std::snprintf(b, sizeof b, "%s: the program's %08X (%.9g), ours %08X (%.9g) at call %llu", what, program, double(F::raw(program).host()), ours,
                              double(F::raw(ours).host()), (unsigned long long)st.calls);
                st.example = b;
            }
        });
        st.clean += clean;
        leave(e, ra);
    }
    Ee& ee_;
    std::vector<PureStat> stats_;
    std::map<u32, std::vector<P>> pending_;
};

World w; // (what the taps' ours read)

} // namespace

struct AiCheck {  // (prologue_race.h)
    bool lines_equal = false;
    std::unique_ptr<PTaps> taps;
    std::unique_ptr<PureTaps> pure;
};

std::shared_ptr<AiCheck> ai_check(Ee& ee, Race& r, int cars, Disc& disc, const std::string& course, std::string& err) {
    // Ours: the course's lines prepared from our parse of the course file on its GTRW road, the program's tables.
    w.race = &r;
    const sim::Program program = sim::Program::load(disc);
    w.tables = ap::driver_tables(program);
    w.t = sim::prologue::tables(program);
    w.parts.assign(size_t(cars), {});
    const std::optional<course::AiData> data = sim::ai::Field::read(disc, course);
    const gt3r::Volume::Entry* fe = disc.volume().find("crs/" + course);
    if (!data || !fe) return err = "no lines", nullptr;
    const Bytes file = disc.volume().read(*fe);
    const course::CourseFile parsed = course::CourseFile::parse(file);
    const std::optional<course::Runway> road = course::course_road(parsed, file);
    if (!road || !road->gtrw) return err = "no GTRW road", nullptr;
    static std::unique_ptr<ex::CourseQuery> query;
    query = std::make_unique<ex::CourseQuery>(*road->gtrw);
    w.course = ap::driver_course(*query, *data, w.tables);
    auto out = std::make_shared<AiCheck>();

    // The routines with a car.
    static ex::RaceInputs inputs;
    out->taps = std::make_unique<PTaps>(ee, r, inputs);
    PTaps& taps = *out->taps;
    const auto drv = [](Ee& e) { return u32(e.a(0)) - 0x1C; };
    static F r_brake, r_corner, r_look, result;
    static int iresult;
    using After = std::function<void(Ee&, u32, int, const std::function<void(const char*, u32, u32)>&)>;
    const auto f0 = [](F& slot) -> After { return [&slot](Ee& e, u32, int, const std::function<void(const char*, u32, u32)>& f) { f("result (f0)", e.fpr[0], slot.bits); }; };
    const auto v0 = [](Ee& e, u32, int, const std::function<void(const char*, u32, u32)>& f) { f("result (v0)", e.g32(2), u32(iresult)); };
    taps.add({0x424AB8, "set-up plan 0x424AB8", [](PTapCall& c) { ap::driver_start(c.car, step_of(c.ee, w, c.car_addr, c.car_index), w.tables, int(s32(c.ee.a(1)))); }, {},
              {}, {}, drv, params_fill});
    taps.add({0x3E8C98, "step 0x3E8C98", [](PTapCall& c) { ap::step(c.car, step_of(c.ee, w, c.car_addr, c.car_index)); }, {}, {}, {}, {}, params_fill});
    taps.add({0x425F20, "decision 0x425F20", [](PTapCall& c) { ap::decide(c.car, step_of(c.ee, w, c.car_addr, c.car_index)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x424E80, "braking 0x424E80", [](PTapCall& c) { r_brake = ap::braking(c.car, step_of(c.ee, w, c.car_addr, c.car_index)); }, {}, {}, f0(r_brake), drv, params_fill});
    taps.add({0x428610, "traffic 0x428610", [](PTapCall& c) { ap::traffic(c.car, step_of(c.ee, w, c.car_addr, c.car_index)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x425B28, "steer 0x425B28", [](PTapCall& c) { ap::steer(c.car, fpr(c.ee, 12)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x425D70, "begin 0x425D70", [](PTapCall& c) { ap::begin(c.car); }, {}, {}, {}, drv, params_fill});
    taps.add({0x427E00, "next segment 0x427E00", [](PTapCall& c) { ap::next_segment(c.car, step_of(c.ee, w, c.car_addr, c.car_index)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x427ED0, "replan 0x427ED0", [](PTapCall& c) { ap::replan(c.car, step_of(c.ee, w, c.car_addr, c.car_index)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x427AE8, "corner speed 0x427AE8",
              [](PTapCall& c) { r_corner = ap::corner_speed(c.car, step_of(c.ee, w, c.car_addr, c.car_index), int(c.ee.a(1))); }, {}, {}, f0(r_corner), drv, params_fill});
    taps.add({0x424C20, "set line 0x424C20", [](PTapCall& c) { ap::set_line(c.car, step_of(c.ee, w, c.car_addr, c.car_index), int(c.ee.a(1))); }, {}, {}, {}, drv, params_fill});
    taps.add({0x424C48, "request line 0x424C48",
              [](PTapCall& c) { iresult = ap::request_line(c.car, step_of(c.ee, w, c.car_addr, c.car_index), int(c.ee.a(1)), c.ee.a(2) != 0); }, {}, {},
              v0, drv, params_fill});
    taps.add({0x425718, "speed control 0x425718", [](PTapCall& c) { ap::speed_control(c.car, fpr(c.ee, 12), fpr(c.ee, 13)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x424CC8, "hold 0x424CC8", [](PTapCall& c) { ap::hold(c.car, s32(c.ee.a(1))); }, {}, {}, {}, drv, params_fill});
    taps.add({0x425BB0, "stop 0x425BB0", [](PTapCall& c) { ap::stop(c.car, fpr(c.ee, 12)); }, {}, {}, {}, drv, params_fill});
    taps.add({0x425CD8, "park 0x425CD8", [](PTapCall& c) { ap::park(c.car); }, {}, {}, {}, drv, params_fill});
    taps.add({0x425D20, "look ahead 0x425D20", [](PTapCall& c) { r_look = ap::lookahead(c.car.ai, fpr(c.ee, 12)); }, {}, {}, f0(r_look),
              [](Ee& e) {
                  for (int i = 0; i < w.race->cars; ++i)
                      if (e.r32(w.race->car(i) + 4) + 0x10808 + 0x74 * u32(e.r8(w.race->car(i) + 0xD8 + 0x3EC)) == u32(e.a(0))) return w.race->car(i);
                  return 0u;
              },
              params_fill});
    {
        static F dx, dy;
        static u32 px, py;
        taps.add({0x4257F8, "target 0x4257F8",
                  [](PTapCall& c) {
                      px = c.ee.g32(8), py = c.ee.g32(9); // (the outputs' places, $t0 / $t1)
                      dx = F::raw(c.ee.r32(px)), dy = F::raw(c.ee.r32(py));
                      result = ap::target(c.car, w.course, int(c.ee.a(2)), int(c.ee.a(3)), fpr(c.ee, 12), dx, dy);
                  },
                  {}, {},
                  [](Ee& e, u32, int, const std::function<void(const char*, u32, u32)>& f) {
                      f("result (f0)", e.fpr[0], result.bits);
                      f("dx", e.r32(px), dx.bits), f("dy", e.r32(py), dy.bits);
                  },
                  [](Ee& e) { return u32(e.a(1)) - 0xD8; }, params_fill});
    }

    // The routines of no car.
    out->pure = std::make_unique<PureTaps>(ee);
    PureTaps& pure = *out->pure;
    pure.add(0x4256A0, "throttle for 0x4256A0", [](Ee& e) -> PureTaps::Check {
        const F ours = ap::throttle_for(fpr(e, 12), fpr(e, 13), fpr(e, 14), fpr(e, 15));
        return [ours](Ee& x, const std::function<void(const char*, u32, u32)>& f) { f("result", x.fpr[0], ours.bits); };
    });
    pure.add(0x42A330, "resolve 0x42A330", [](Ee& e) -> PureTaps::Check {
        const int ours = ap::resolve(w.course, int(s32(e.a(1))));
        return [ours](Ee& x, const std::function<void(const char*, u32, u32)>& f) { f("result", x.g32(2), u32(ours)); };
    });
    pure.add(0x429CA0, "evaluate 0x429CA0", [](Ee& e) -> PureTaps::Check {
        F x, y, h;
        const u32 px = u32(e.a(3)), py = e.g32(8), ph = e.g32(9);
        x = F::raw(e.r32(px)), y = F::raw(e.r32(py)), h = F::raw(e.r32(ph));
        const int k = ap::evaluate(w.course, int(e.a(1)), int(e.a(2)), fpr(e, 12), x, y, h);
        return [=](Ee& q, const std::function<void(const char*, u32, u32)>& f) {
            f("point", q.g32(2), u32(k));
            f("x", q.r32(px), x.bits), f("y", q.r32(py), y.bits), f("h", q.r32(ph), h.bits);
        };
    });
    pure.add(0x42A090, "stop line 0x42A090", [](Ee& e) -> PureTaps::Check {
        F x, y, h;
        const u32 px = u32(e.a(3)), py = e.g32(8), ph = e.g32(9);
        x = F::raw(e.r32(px)), y = F::raw(e.r32(py)), h = F::raw(e.r32(ph));
        const int k = ap::evaluate_stop(w.course, fpr(e, 12), x, y, h);
        return [=](Ee& q, const std::function<void(const char*, u32, u32)>& f) {
            f("point", q.g32(2), u32(k));
            f("x", q.r32(px), x.bits), f("y", q.r32(py), y.bits), f("h", q.r32(ph), h.bits);
        };
    });
    pure.add(0x42A2B0, "side map 0x42A2B0", [](Ee& e) -> PureTaps::Check {
        const bool ours = ap::side_swapped(w.course, int(e.a(1)), fpr(e, 12));
        return [ours](Ee& x, const std::function<void(const char*, u32, u32)>& f) { f("result", x.g32(2) != 0, ours); };
    });
    pure.add(0x425968, "line heading 0x425968", [](Ee& e) -> PureTaps::Check {
        const u32 px = e.g32(7), py = e.g32(8), ph = e.g32(9);
        F x = F::raw(e.r32(px)), y = F::raw(e.r32(py)), h = F::raw(e.r32(ph));
        const F ours = ap::line_heading(w.course, int(e.a(1)), int(e.a(2)), fpr(e, 12), x, y, h);
        return [=](Ee& q, const std::function<void(const char*, u32, u32)>& f) {
            f("result", q.fpr[0], ours.bits);
            f("x", q.r32(px), x.bits), f("y", q.r32(py), y.bits), f("h", q.r32(ph), h.bits);
        };
    });
    pure.add(0x42A850, "atan2 0x42A850", [](Ee& e) -> PureTaps::Check {
        const F ours = ex::atan2(sim::rules::behaviour(), fpr(e, 12), fpr(e, 13));
        return [ours](Ee& x, const std::function<void(const char*, u32, u32)>& f) { f("result", x.fpr[0], ours.bits); };
    });
    pure.add(0x377620, "angle difference 0x377620", [](Ee& e) -> PureTaps::Check {
        const F ours = ex::angle_diff(fpr(e, 12), fpr(e, 13), sim::rules::behaviour().rule(gt3r::sim::Rule::AngleWrap));
        return [ours](Ee& x, const std::function<void(const char*, u32, u32)>& f) { f("result", x.fpr[0], ours.bits); };
    });

    return out;
}

void ai_check_lines(Ee& ee, Race& r, AiCheck& out, const std::string& course, bool print) {
    {
        const u32 vt = ee.r32(r.cond + kCondVtable), v33 = vt + 8 * 33, v_kind = 0x3B63B0;
        ee.call(ee.r32(v33 + 4), {Arg::ptr(r.cond + u32(s32(s16(ee.r16(v33)))))}, 10'000'000);
        w.start_speed = u8(ee.g32(2));
        ee.call(v_kind, {Arg::ptr(r.cond)}, 10'000'000);
        w.dirt = ee.g32(2) == 2;
    }

    // The lines as the race prepared them.
    const u32 K = ee.r32(r.car(0) + 0x1C + 8);
    u64 points = 0, equal = 0;
    std::string first;
    for (int l = 0; l < 8; ++l) {
        const std::vector<ex::AiPoint> p = program_line(ee, K, l);
        const std::vector<ex::AiPoint>& q = w.course.lines[size_t(l)];
        if (p.size() != q.size()) {
            char b[96];
            std::snprintf(b, sizeof b, "line %d: %zu points, ours %zu; ", l, p.size(), q.size());
            first += b;
        }
        for (size_t i = 0; i < std::min(p.size(), q.size()); ++i) {
            ++points;
            const ex::AiPoint &a = p[i], &b = q[i];
            const bool same = a.kind == b.kind && a.brake_adjust == b.brake_adjust && a.speed_adjust == b.speed_adjust && a.x.bits == b.x.bits &&
                              a.y.bits == b.y.bits && a.height.bits == b.height.bits && a.seg == b.seg && a.surface == b.surface && a.x13 == b.x13 &&
                              a.dist.bits == b.dist.bits && a.radius.bits == b.radius.bits && a.slope.bits == b.slope.bits && a.bank.bits == b.bank.bits &&
                              a.heading.bits == b.heading.bits;
            equal += same;
            if (!same && first.size() < 400) {
                char t[200];
                std::snprintf(t, sizeof t, "line %d point %zu: kind %d/%d seg %d/%d dist %.6g/%.6g h %.6g/%.6g slope %.6g/%.6g bank %.6g/%.6g surf %d/%d; ", l, i, a.kind,
                              b.kind, a.seg, b.seg, double(a.dist.host()), double(b.dist.host()), double(a.height.host()), double(b.height.host()),
                              double(a.slope.host()), double(b.slope.host()), double(a.bank.host()), double(b.bank.host()), a.surface, b.surface);
                first += t;
            }
        }
    }
    std::string counts;
    for (int l = 0; l < 8; ++l) counts += (l ? "/" : "") + std::to_string(w.course.lines[size_t(l)].size());
    if (print)
        std::printf("prologue-ai: %s, %d cars; the lines (%s points): %llu of %llu points equal%s%s\n", course.c_str(), r.cars, counts.c_str(), (unsigned long long)equal,
                (unsigned long long)points, first.empty() ? "" : ": ", first.c_str());
    out.lines_equal = equal == points;
}

bool ai_check_print(const AiCheck& c) {
    c.taps->print();
    bool all = c.lines_equal;
    for (const PTapStat& s : c.taps->stats()) all = all && s.calls == s.clean;
    for (const PureStat& s : c.pure->stats()) {
        std::printf("  %-28s %llu calls, %llu equal\n", s.name.c_str(), (unsigned long long)s.calls, (unsigned long long)s.clean);
        for (const auto& [k, n] : s.off) std::printf("      %-22s differs in %llu\n", k.c_str(), (unsigned long long)n);
        if (!s.example.empty()) std::printf("      first: %s\n", s.example.c_str());
        all = all && s.calls == s.clean;
    }
    return all;
}

std::vector<AiCount> ai_counts(const AiCheck& c) {
    std::vector<AiCount> out;
    out.push_back({"the lines", 1, c.lines_equal ? 1u : 0u});
    for (const PTapStat& s : c.taps->stats()) out.push_back({s.name, s.calls, s.clean});
    for (const PureStat& s : c.pure->stats()) out.push_back({s.name, s.calls, s.clean});
    return out;
}

std::vector<AiCount> ai_run(Disc& disc, const std::string& course, const std::string& cars, int steps, const AiOptions& o, bool print, std::string& err) {
    if (!is_prologue(disc)) return err = "not Prologue's disc", std::vector<AiCount>{};
    std::vector<std::string> labels;
    {
        std::stringstream s(cars);
        std::string l;
        while (std::getline(s, l, ',')) labels.push_back(l);
    }
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc);
    static Race r; // (the pad stub reads it while the race runs)
    const std::shared_ptr<AiCheck> check = ai_check(ee, r, int(labels.size()), disc, course, err);
    if (!check) return {};
    if (!setup_race(ee, disc, sdb, course, labels, o.human, r, err, aids_from_env())) return {};
    ai_check_lines(ee, r, *check, course, print);
    for (int i = 0; i < steps; ++i) {
        for (int k = 0; k < o.human; ++k) r.pad[k] = test_drive(i);
        if (o.hold) ee.w32(r.info + 0x74, u32(std::max(0, o.hold - i)));
        if (i == o.pit_at) // (the computers' cars sent onto a line there by the program's own request 0x424C48)
            for (int k = o.human; k < r.cars; ++k) {
                ee.call(0x424C48, {Arg::ptr(r.car(k) + 0x1C), Arg::integer(o.pit_line), Arg::integer(0)}, 10'000'000);
                if (print) std::printf("  step %d: car %d asked onto line %d: %u, its line %u\n", i, k, o.pit_line, ee.g32(2), ee.r8(r.car(k) + 0x1C + 0x81));
            }
        if (i == o.stop_at) // (the computers' cars told to stop there, driver kinds 6 and 7 by turns)
            for (int k = o.human; k < r.cars; ++k) ee.w8(r.car(k) + 0xD8 + 0x538, u8(k % 2 ? 6 : 7));
        step(ee, r);
    }
    if (print) {
        ai_check_print(*check);
        for (int i = 0; i < r.cars; ++i)
            std::printf("  car %d: %.1f m, %.1f km/h\n", i, double(ee.rf(r.car(i) + 0xD8 + 0x3E8)), double(ee.rf(r.car(i) + 0xD8 + 0x48C)) * 3.6);
    }
    return ai_counts(*check);
}

int run_ai(Disc& disc, const std::string& course, const std::string& cars, int steps) {
    AiOptions o;
    const auto num = [](const char* name, int none) { return std::getenv(name) ? std::atoi(std::getenv(name)) : none; };
    o.human = num("GT4R_AI_HUMAN", 0), o.hold = num("GT4R_AI_HOLD", 0), o.stop_at = num("GT4R_AI_STOP", -1);
    if (const char* pit = std::getenv("GT4R_AI_PIT")) o.pit_at = std::atoi(pit), o.pit_line = std::strchr(pit, ',') ? std::atoi(std::strchr(pit, ',') + 1) : 3;
    std::string err;
    const std::vector<AiCount> n = ai_run(disc, course, cars, steps, o, true, err);
    if (!err.empty()) return std::printf("prologue-ai: %s\n", err.c_str()), 1;
    bool all = !n.empty();
    for (const AiCount& c : n) all = all && c.calls == c.equal;
    return all ? 0 : 1;
}

} // namespace gt4r::diff::prologue
