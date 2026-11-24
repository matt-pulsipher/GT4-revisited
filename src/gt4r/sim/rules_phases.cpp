// Gran Turismo 4's own routines: phases 1, 2, 3, 6, the preparation and the hit records (rules_phases.h).

#include "gt4r/sim/rules_phases.h"

#include "base/ee_math.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_step.h"
#include "gt4r/sim/rules_wear.h"

namespace gt4r::sim::rules {

namespace {

const F k0 = F::raw(0), kOne = F::raw(0x3F800000), kTwo5th = F::raw(0x3E4CCCCC); // 0.2, a unit low
const F kHundred = F::raw(0x42C80000), kFifty = F::raw(0x42480000), kFiftyThousand = F::raw(0x47435000);
const F kKmh = F::raw(0x3E8E38E3);     // 1 km/h in m/s, a unit low
const F kFuelMass = F::raw(0x3F3D70A3); // 0.74 kg a liter, a unit low

constexpr u8 kSitSteps = 0x22;               // the pod's "sit": 34 steps
constexpr s8 kLeanMax = 30;                  // the lean's limit
constexpr u8 kNodPeriod = 0x30;              // the nod's period
constexpr u8 kFade = 0x18, kFadeOther = 0x24; // the lamps' fade from class 0, from another class
constexpr s32 kRivalTime = 0x78, kHitTime = 0xD2;

u8& special(Extra& x, int k) { return x.phases.special[size_t(k)]; }

} // namespace

bool pod(const Extra& x) { return x.aids.mark_mode == kPodCategory; }

// ---- phase 1 ----

void save_previous(ex::Car& c, const WheelLayout& l) {
    ex::Previous& p = c.prev;
    p.pitch = c.pitch, p.roll = c.roll, p.heading = c.heading;
    p.ground_pitch = c.ground_pitch, p.ground_roll = c.ground_roll;
    for (int i = 0; i < l.count && i < 4; ++i) {
        const ex::Wheel& w = c.w[size_t(i)];
        const ex::Ground& g = c.g[size_t(i)];
        p.susp[size_t(i)] = w.susp, p.steer[size_t(i)] = w.steer, p.camber[size_t(i)] = w.camber;
        p.half_track[size_t(i)] = w.half_track, p.spin[size_t(i)] = w.spin, p.brake_heat[size_t(i)] = w.brake_heat;
        p.attr_a[size_t(i)] = g.attr_a, p.attr_b[size_t(i)] = g.attr_b;
        p.hub[size_t(i)] = {g.x, w.ground, g.z};
        p.flap[size_t(i)] = w.flap;
    }
    p.lag_heading_diff = c.lag_heading_diff, p.lag_pitch_diff = c.lag_pitch_diff;
    p.f3e4 = c.f3e4;
}

// ---- phase 2 ----

void layout7_step(const ex::Car& c, PhaseExtra& x) {
    if (c.p.d.layout == 7) x.special[5] = 0;
}

// ---- phases 3 and 6 ----

MoodTables mood_tables(const Program& program) {
    return mood_tables([&](u32 a) { return program.b(a); });
}

s32 nod(u8 phase) {
    const s32 p = phase;
    if (!(p < 0x25)) return p - 0x30;
    if (p < 0xD) return p;
    return 0x18 - p;
}

void set_mood(Extra& x, const MoodTables& t, s32 m, s32 time) {
    if (!pod(x)) return;
    if (s8(special(x, 3)) > 0 && time != 0xFF) return;
    const u8 left = special(x, 2);
    if (left == 0xFF) return;
    if (time == 0 && left != 0) return;
    const u8 cur = mood(x.phases);
    if (s32(cur) != m) special(x, 6) = u8(m), special(x, 1) = u8((cur << 4) | u32(m));
    special(x, 2) = time == -1 ? t.time[size_t(m) & 15] : u8(time);
    const u8 was = t.lamp[cur], now = t.lamp[size_t(m) & 15];
    if (was != now) special(x, 3) = was != 0 ? kFadeOther : kFade;
}

bool all_soft(const ex::Car& c, const WheelLayout& l, const std::array<u32, 32>& soft) {
    for (int w = 0; w < l.count && w < 4; ++w)
        if (soft[size_t(c.g[size_t(w)].material) & 31] == 0) return false;
    return true;
}

s32 laps_apart(const PassBy& p, int i, int j, F lap, F& gap) {
    s32 laps = p.laps[size_t(i)][size_t(j)];
    F g = p.gap[size_t(i)][size_t(j)];
    const F half = lap * F::raw(0x3F000000);
    if (half < g) g = g - lap, ++laps;
    else if (g < -half) g = g + lap, --laps;
    gap = g;
    return laps;
}

s32 rival_level(const PassBy& p, int i, int count, F lap, s32* who) {
    s32 best = 0, rival = -1;
    for (int j = 0; j < count && j < PassBy::kCars; ++j) {
        if (j == i) continue;
        const u8 a = p.state[size_t(i)][size_t(j)], b = p.state[size_t(j)][size_t(i)];
        if (a < 2 && b < 2) continue;
        F gap;
        const s32 laps = laps_apart(p, i, j, lap, gap);
        s32 level = 0;
        if (laps != 0) level = laps < 0 ? 2 : 1;
        else if (a == 3 || b == 3) level = 5;
        else if (a == 2) level = 4;
        else if (b == 2) level = 3;
        if (best < level) best = level, rival = j;
    }
    if (who) *who = rival;
    return best;
}

void moods(const ex::Car& c, Extra& x, const MoodTables& t, const WheelLayout& l, const std::array<u32, 32>& soft, s32 rival) {
    if (!pod(x)) return;
    special(x, 6) = 0;
    bool timed = true; // (the moods asked for below go on to the time's count; the rest asks first)
    if (x.phases.finished != 0) set_mood(x, t, s8(c.rank) == 1 ? kMoodHappy : kMoodSad, 0);
    else if (x.phases.wrong_way == 1) set_mood(x, t, kMoodWrongWay, 0);
    else if (c.x535 & 0x10) set_mood(x, t, kMoodPit, 0);
    else if (rival == 5) set_mood(x, t, kMoodRival, kRivalTime);
    else if (c.slip_filter == kOne) set_mood(x, t, kMoodHappy, kRivalTime);
    else if (all_soft(c, l, soft)) set_mood(x, t, kMoodDirt, 0);
    else timed = false;
    if (!timed && special(x, 2) == 0) set_mood(x, t, kMoodRest, -1);
    // the mood's time
    if (const u8 left = special(x, 2); left != 0 && left != 0xFF) {
        special(x, 2) = u8(left - 1);
        if (special(x, 2) == 0) set_mood(x, t, kMoodRest, -1);
    }
    // the lamps' fade
    if (s8(special(x, 3)) > 0) {
        special(x, 3) = u8(special(x, 3) - 1);
        if (t.lamp[mood(x.phases)] == 0 && s8(special(x, 3)) < 0x19) special(x, 3) = 0;
    }
    // the lean, moved while the nod is level (0x35C938: the mood's lean when 1 or -1)
    const s8 want = t.lean[mood(x.phases)];
    const s8 lean = want == 1 || want == -1 ? want : 0;
    if (nod(special(x, 4)) == 0) {
        const s8 v = s8(special(x, 5));
        if (lean == 1) special(x, 5) = u8(s8(v + 1) < kLeanMax + 1 ? s8(v + 1) : kLeanMax);
        else if (lean == -1) special(x, 5) = u8(s8(v - 1) < -kLeanMax ? s8(-kLeanMax) : s8(v - 1));
        else if (v > 0) special(x, 5) = u8(v - 1);
        else if (v < 0) special(x, 5) = u8(v + 1);
    }
    if (special(x, 5) != 0) return;
    // the nod (0x35C980: a nodding mood runs it on; otherwise it runs until level)
    if (want != 2 && nod(special(x, 4)) == 0) return;
    const u8 n = u8(special(x, 4) + 1);
    special(x, 4) = n < kNodPeriod ? n : 0;
}

bool standing(const ex::Car& c, const Extra& x) {
    if (!pod(x)) return false;
    if (c.handbrake < kOne) return false;
    const F a = c.lat_speed * c.lat_speed;
    const F b = c.speed * c.speed;
    return !(kKmh < gt3r::ee::sqrt(b + a));
}

void stop_count(const ex::Car& c, Extra& x) {
    if (!pod(x)) return;
    u8& n = special(x, 0);
    if (standing(c, x)) n = u8(n + 1 < kSitSteps + 1 ? n + 1 : kSitSteps);
    else if (n != 0) n = u8(n - 1);
}

// ---- phase 4 ----

void message_timer(PhaseExtra& x) {
    if (x.message != 0 && x.message != 0xFF) --x.message;
}

void step_length(ex::Car& c) {
    const F inv = F::raw(0x42700000) / c.time_scale;
    c.dt = c.time_scale * F::raw(0x3C888888);
    c.inv_dt = inv;
}

void masses(const ex::Car& c, Extra& x) {
    const F fuel = x.aids.fuel * kFuelMass;
    x.chassis.mass = c.p.mass + fuel;
    x.body.sprung_mass = x.phases.body_mass + fuel;
}

void fresh_tires(ex::Car& c, Extra& x, int w, const WearRules& r) {
    ex::Wheel& wh = c.w[size_t(w)];
    F& second = x.wear.second[size_t(w)];
    wh.wear_grip = kOne;
    wh.wear = k0;
    wh.wear_level = 0;
    x.phases.wheel_b0[size_t(w)] = 0;
    wh.brake_heat = k0;
    second = k0;
    if (r.tread_on) {
        wh.wear = r.tread_neg;
        if (!r.second_on) wh.wear_level = wh.wear < k0 ? s8(-0x3F) : s8(0);
        wh.wear_grip = kOne - r.tread_neg_loss;
    }
    if (r.second_on) {
        second = r.second_neg;
        wh.wear_level = second < k0 ? s8(-0x3F) : s8(0);
        wh.wear_grip = wh.wear_grip * (kOne - r.second_neg_loss);
    }
    if (x.aids.mark_mode == 4) second = k0, wh.wear_grip = kOne, wh.wear = k0, wh.wear_level = 0;
}

void pit_step(ex::Car& c, Extra& x, const WheelLayout& l, const WearRules& r, const PitHooks* hooks) {
    PhaseExtra& p = x.phases;
    const u16 flags = p.pit_flags;
    if (c.hold == 0x2D1 && (flags & 0x10)) { // waiting in the pit
        if (p.pit_wait > 0 && --p.pit_wait == 0 && hooks && hooks->leave) hooks->leave();
        return;
    }
    if (c.hold == 0x5A && (flags & 0x100)) return; // held
    const u8 tires = u8((flags & 0x40 ? 1 : 0) | (flags & 0x80 ? 2 : 0));
    c.hold = u16(c.hold - 1);
    if (tires != 0) {
        const bool chosen = p.pit_tires[0] < 0xFE || p.pit_tires[1] < 0xFE;
        if (c.hold == 0x285 && hooks && hooks->change_tires) hooks->change_tires(tires, chosen);
        if (c.hold == 0x284 && chosen) {
            if (hooks && hooks->retune) hooks->retune(tires);
            p.pit_tires = {0xFF, 0xFF};
        }
        if (c.hold == 0x3C) {
            for (int w = 0; w < l.count && w < 4; ++w) {
                const s8 at = s8(l.fore_aft[size_t(w)]);
                const u16 f = p.pit_flags;
                const bool fit = at > 0 ? (f & 0x40) != 0 : at < 0 ? (f & 0x80) != 0 : (f & 0xC0) != 0;
                if (fit) fresh_tires(c, x, w, r);
            }
            p.pit_flags = u16(p.pit_flags & 0xFF3F);
        }
    }
    if (c.hold == 0x3B) p.pit_flags = u16(p.pit_flags | 0x200);
}

void prep(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const ex::Condition& cond, const std::array<F, 3>* grip,
          const PitHooks* hooks) {
    if (c.hold != 0) pit_step(c, x, l, t.wear, hooks);
    message_timer(x.phases);
    rules::step_length(c);
    masses(c, x);
    aero(c, x.chassis, x.body.speed_part_kind, x.body.speed_part_lift, t.aero);
    prep_tires(c, x, l, t, cond, grip);
}

// ---- phases 12 and 13 ----

void clear_hit(PhaseExtra& x) {
    if (x.hit_kind != 2) x.hit = k0;
}

void hit_record(PhaseExtra& x, const HitRules& r, s32 kind, s32 part, F strength) {
    if (kind == -1) return;
    F s = k0;
    if (kind == 0) s = strength / kFiftyThousand;
    else if (kind == 1) s = strength + strength;
    else if (kind == 2) {
        if (r.pylons == 0) return;
        s = strength / kFifty;
        if (s < k0) s = -s;
        if (s < kTwo5th) s = kTwo5th;
    }
    if (kOne < s) s = kOne;
    if (s < kTwo5th) s = k0;
    if (x.hit < s) x.hit_part = u8(part), x.hit = s, x.hit_kind = s8(kind);
}

void hit_mood(Extra& x, const MoodTables& t, s32 kind, s32 part) {
    if (!pod(x)) return;
    if (kind == 0) return set_mood(x, t, kMoodBump, -1);
    if (kind == 2) return set_mood(x, t, kMoodAngry, -1);
    if (kind != 1) return;
    if (u32(part - 1) < 2) return set_mood(x, t, kMoodAngry, -1);
    if (u32(part - 5) < 2) return set_mood(x, t, kMoodScared, -1);
    if (part < 8) return set_mood(x, t, kMoodHurt, -1);
    if (part < 0xB) return set_mood(x, t, kMoodAngry, kHitTime);
    if (part == 0xB || part == 0xF) return set_mood(x, t, kMoodHurt, kHitTime);
    set_mood(x, t, kMoodScared, kHitTime);
}

s32 part_class(s32 part) {
    static constexpr u8 kClass[16] = {2, 1, 1, 2, 3, 4, 4, 3, 1, 1, 1, 3, 4, 4, 4, 3};
    return u32(part) < 16 ? kClass[part] : 4;
}

bool past_limit(u8 limit, F value) {
    if (limit == 0) return true;
    if (limit == 0xFF) return false;
    return !(gt3r::ee::to_int(value) < s32(limit));
}

void penalty(const ex::Car& c, Extra& x, const HitRules& r, s32 kind, s32 part, F strength) {
    if (s8(c.control) != 0 || c.driver_kind != 0 || x.phases.finished != 0) return;
    const u8 secs = r.penalty;
    if (secs == 0) return;
    if (secs == 0xFF ? x.phases.penalty_marked != 0 : x.aids.start_count != 0) return;
    bool past = false;
    if (kind == 1) {
        if (part_class(part) < 3) past = past_limit(r.car_limit, strength * kHundred);
    } else if (kind == 0) {
        if (part_class(part) < 3) past = past_limit(r.wall_limit, strength / c.p.mass);
    }
    if (!past) return;
    if (secs == 0xFF) x.phases.penalty_marked = 1;
    else x.aids.start_count = u16(secs * 60);
}

void license_judge(const ex::Car& c, Extra& x, const HitRules& r, s16& pylons_hit, s32 kind, s32 part, F strength) {
    if (s8(c.control) != 0 || c.driver_kind != 0 || x.phases.finished != 0) return;
    bool failed = false;
    if (kind == 1) {
        if (part_class(part) < 3) failed = past_limit(r.car_limit, strength * kHundred);
    } else if (kind == 0) {
        failed = past_limit(r.wall_limit, strength / c.p.mass);
    } else if (kind == 2) {
        if (r.pylons == 0) failed = true;
        else if (pylons_hit < s16(r.pylons)) ++pylons_hit;
    }
    if (failed) x.phases.failed = u8(x.phases.failed | 1);
}

void hit(const ex::Car& c, Extra& x, const MoodTables& t, const HitRules& r, Judge judge, s16* pylons_hit, s32 kind, s32 part, F strength) {
    if (kind == 0 || kind == 1 || kind == 2) hit_mood(x, t, kind, part);
    if (c.driver_kind == 0) {
        PhaseExtra& p = x.phases;
        if (!(p.incident_flags & 2) && p.incidents < 100) ++p.incidents;
        p.hit_steps = 60;
        p.incident_flags = u8(p.incident_flags | 2);
    }
    if (judge == Judge::Penalty) penalty(c, x, r, kind, part, strength);
    else if (judge == Judge::License && pylons_hit) license_judge(c, x, r, *pylons_hit, kind, part, strength);
    hit_record(x.phases, r, kind, part, strength);
}

void course_out(const ex::Car& c, Extra& x, const WheelLayout& l, const std::array<u32, 32>& soft) {
    PhaseExtra& p = x.phases;
    u8 on_soft = 0, on_pit = 0;
    for (int w = 0; w < l.count && w < 4; ++w) {
        const ex::Ground& g = c.g[size_t(w)];
        if (g.miss != 0) continue;
        on_pit = u8(on_pit + (g.flags & 1));
        if (soft[size_t(g.material) & 31] != 0) ++on_soft;
    }
    p.soft_wheels = on_soft, p.pit_wheels = on_pit;
    if (c.driver_kind != 0) return;
    if (!(p.incident_flags & 1)) {
        if (on_soft == l.count) {
            p.incident_flags = u8(p.incident_flags | 1);
            if (p.incidents < 100) ++p.incidents;
        }
    } else if (on_soft == 0) {
        p.incident_flags = u8(p.incident_flags ^ 1);
    }
}

void hit_steps(PhaseExtra& x) {
    if (x.hit_steps == 0) return;
    if (--x.hit_steps == 0) x.incident_flags = u8(x.incident_flags ^ 2);
}

} // namespace gt4r::sim::rules
