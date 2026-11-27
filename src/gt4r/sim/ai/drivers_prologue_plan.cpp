// Gran Turismo 4 Prologue's drivers' lines and speed plan (drivers_prologue.h): the line queries of its course object and
// the driver's plan, outputs and start hold, ported from its program (SCPS-15055 v1.01; docs/formats/prologue.md "The
// computer drivers"). The arithmetic's math (atan2, sine, cosine, square root) is the engine's, equal to the program's at
// every call (gt4r_diff prologue-ai).

#include "gt4r/sim/ai/drivers_prologue.h"

#include "base/ee_math.h"
#include "sim/exact/build_math.h"

namespace gt4r::sim::ai::prologue {

namespace {

const F kZero, kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);
const F kTwoPi = F::raw(0x40C90FDA);
const F kKmh = F::raw(0x3E8E38E3), kmh2 = F::raw(0x3F0E38E3), kmh10 = F::raw(0x4031C71B), kmh20 = F::raw(0x40B1C71B);

F int_f(s32 v) { return gt3r::ee::int_to_f(v); }

// The race distance wrapped once into the lap.
F wrap_lap(F s, F lap) {
    if (s < kZero) return s + lap;
    if (lap <= s) return s - lap;
    return s;
}

const std::vector<ex::AiPoint>& points(const ex::AiCourse& k, const ex::Driver& d) { return k.lines[size_t(d.line & 7)]; } // (0x424BA8)

} // namespace

// ---- the course object's lines -------------------------------------------------------------------------------------

int resolve(const ex::AiCourse& k, int line) {
    if (!k.table) return -1;
    const std::array<s8, 6>& row = k.fallback[size_t(u32(line) < 8 ? line : 6)];
    for (const s8 l : row) {
        if (l == -1) return -1;
        if (l >= 0 && l < 8 && !k.lines[size_t(l)].empty()) return l; // (a line whose slot is set)
    }
    return -1;
}

int span(const ex::AiCourse& k, int line, int hint, F s, int& to, int& from) {
    const std::vector<ex::AiPoint>& p = k.lines[size_t(line)];
    const int n = int(p.size());
    if (s < p[0].dist) return from = n - 1, to = 0, 1;
    if (p[size_t(n - 1)].dist <= s) return from = n - 1, to = 0, 2;
    // From the hint out each way in turn: hint, hint + 1, hint - 1, hint + 2, ... (wrapped once into the line; point 0
    // is no span's end).
    int a = hint;
    for (int t = 0; t <= n; ++t) {
        a = (t & 1) ? a + t : a - t;
        int j = a;
        if (j >= 0) {
            if (!(j < n)) j -= n;
        } else
            j += n;
        if (j == 0) continue;
        int i = j - 1;
        if (i < 0) i += n;
        if (j < 0 || j >= n || i < 0 || i >= n) continue; // (a point past the line, which the program reads from what follows it)
        if (p[size_t(i)].dist <= s && s < p[size_t(j)].dist) return from = i, to = j, 3;
    }
    return 0;
}

int evaluate(const ex::AiCourse& k, int line, int hint, F s, F& x, F& y, F& h) {
    const F lap = k.lap_length;
    s = wrap_lap(s, lap);
    int to = 0, from = 0;
    const int found = span(k, line, hint, s, to, from);
    const std::vector<ex::AiPoint>& p = k.lines[size_t(line)];
    F num, den;
    if (found == 1) num = (s + lap) - p[size_t(from)].dist, den = (p[size_t(to)].dist + lap) - p[size_t(from)].dist;
    else if (found == 2) num = s - p[size_t(from)].dist, den = (p[size_t(to)].dist + lap) - p[size_t(from)].dist;
    else if (found == 3) num = s - p[size_t(from)].dist, den = p[size_t(to)].dist - p[size_t(from)].dist;
    else return -1;
    const F t = num / den;
    const ex::AiPoint &a = p[size_t(from)], &b = p[size_t(to)];
    if (a.kind == 0) {
        x = t * (b.x - a.x) + a.x;
        y = t * (b.y - a.y) + a.y;
    } else { // about the arc's center
        const F ns = -gt3r::ee::sin(a.heading), c = gt3r::ee::cos(a.heading);
        F r = a.radius;
        const F cx = a.x - c * r, cy = a.y + ns * r;
        F a0 = ex::atan2(k.behaviour, cx - a.x, a.y - cy), a1 = ex::atan2(k.behaviour, cx - b.x, b.y - cy);
        if (kZero < r) {
            if (a1 < a0) a1 = a1 + kTwoPi;
        } else {
            r = -r;
            if (a0 < a1) a0 = a0 + kTwoPi;
        }
        const F ang = a0 + t * (a1 - a0);
        x = cx - gt3r::ee::sin(ang) * r;
        y = cy + gt3r::ee::cos(ang) * r;
    }
    h = t * (b.height - a.height) + a.height;
    return from;
}

int evaluate_stop(const ex::AiCourse& k, F s, F& x, F& y, F& h) {
    const F lap = k.lap_length;
    const std::vector<ex::AiPoint>& p = k.lines[4];
    const int n = int(p.size());
    s = wrap_lap(s, lap);
    if (s < lap * kHalf) s = s + lap;
    if (s < p[0].dist || p[size_t(n - 1)].dist <= s) return -1;
    int from = 0, to = 0;
    F t;
    for (int i = 0; i < n - 1; ++i) {
        from = i, to = i + 1;
        const F d0 = p[size_t(i)].dist;
        if (d0 <= s && s < p[size_t(i + 1)].dist) {
            t = (s - d0) / (p[size_t(i + 1)].dist - d0);
            break;
        }
    }
    const ex::AiPoint &a = p[size_t(from)], &b = p[size_t(to)];
    x = t * (b.x - a.x) + a.x;
    y = t * (b.y - a.y) + a.y;
    h = t * (b.height - a.height) + a.height;
    return from;
}

bool side_swapped(const ex::AiCourse& k, int hint, F s) {
    const std::vector<ex::AiPoint>& map = k.lines[5];
    if (map.empty()) return false;
    int to = 0, from = 0;
    if (!span(k, 5, int(map.size()) < hint ? 0 : hint, s, to, from)) return false;
    return map[size_t(from)].kind != 0;
}

F line_heading(const ex::AiCourse& k, int line, int hint, F s, F& x, F& y, F& h) {
    F x1, y1, h1; // (the program's stack words where an evaluation finds no span)
    const auto at = [&](F d, F& px, F& py, F& ph) {
        if (line == 4) evaluate_stop(k, d, px, py, ph);
        else evaluate(k, line, hint, d, px, py, ph);
    };
    at(s, x, y, h);
    F dx, dy;
    for (int i = 0; i < 10; ++i) {
        s = s + kHalf;
        at(s, x1, y1, h1);
        dx = x1 - x, dy = y1 - y;
        if (F::raw(0x3E4CCCCC) <= gt3r::ee::sqrt(dx * dx + dy * dy)) break; // 0.2 m
    }
    return ex::atan2(k.behaviour, -dx, dy);
}

// ---- the plan ------------------------------------------------------------------------------------------------------

void begin(ex::Car& c) {
    ex::Driver& d = c.ai;
    d.throttle = kZero;
    d.steer = c.steer_cmd;
    d.brake = d.throttle;
    d.reverse = 0, d.xa9 = 0;
    d.shift_lock = 0;
    if (d.throttle < d.blend) {
        const F left = d.blend - c.dt;
        d.blend = left < d.throttle ? d.throttle : left;
    }
}

void steer(ex::Car& c, F target) {
    ex::Driver& d = c.ai;
    const F cur = d.steer, step = d.steer_rate * c.dt;
    if (cur < target) {
        const F n = cur + step;
        d.steer = target < n ? target : n;
    } else if (target < cur) {
        const F n = cur - step;
        d.steer = n < target ? target : n;
    }
}

F lookahead(const ex::Driver& d, F v) {
    if (v <= kZero) return d.look_min;
    const F f = d.look_speed * v;
    if (kOne <= f) return d.look_max;
    return d.look_min + f * (d.look_max - d.look_min);
}

F target(const ex::Car& c, const ex::AiCourse& k, int line, int hint, F look, F& dx, F& dy) {
    F x, y, h;
    for (int i = 0; i < 10; ++i) {
        const F s = c.f3e4 + look;
        if (line == 4) evaluate_stop(k, s, x, y, h);
        else evaluate(k, line, hint, s, x, y, h);
        dx = x - c.pos.x, dy = y - c.pos.y;
        if (F::raw(0x40A00000) <= gt3r::ee::sqrt(dx * dx + dy * dy)) break; // 5 m
        if (i < 9) look = look + kOne;
    }
    return look;
}

F throttle_for(F v, F vt, F base, F gain) {
    F dv = v - vt;
    if (dv < -kmh10) dv = -kmh10;
    else if (kmh10 < dv) dv = kmh10;
    const F t = base + gain * dv;
    if (kOne < t) return kOne;
    return t < kZero ? kZero : t;
}

const ex::TireRecord& tire(const ex::Params& p, const rules::WheelLayout& l, int w, s8 surface) {
    // 0x3C6EA8: the record of wheel w's axle (0 for a wheel past the layout's) on the surface: P + 0xDE8 + 16 axle + surface.
    const size_t k = size_t(16 * (w < int(l.count) ? l.axle[size_t(w)] : 0) + surface);
    return p.tire[size_t(p.tire_index[k >> 4][k & 15])];
}

F corner_speed(const ex::Car& c, const Step& in, int i) {
    const ex::Driver& d = c.ai;
    const ex::AiPoint& pt = points(*in.course, d)[size_t(i)];
    const rules::WheelLayout& l = in.part->layout;
    const F w = d.front_weight, r = pt.radius;
    const s8 s = pt.surface;
    const ex::TireRecord &rf = tire(c.p, l, l.group_first[0], s), &rr = tire(c.p, l, l.group_first[1], s);
    std::array<F, 2> mu;
    mu[0] = (w * rf.mu[size_t(s)]) * rf.grip;
    mu[1] = ((kOne - w) * rr.mu[size_t(s)]) * rr.grip;
    F a;
    if (!(in.wear.max == kZero) && !(c.x535 & 0x10))
        for (int j = 0; j < int(l.count); ++j) a = a + (c.w[size_t(j)].wear_grip * mu[l.axle[size_t(j)] & 1]) * kHalf; // (axles 0 and 1)
    else
        a = mu[0] + mu[1];
    // (the bank divides only where it leans into the arc)
    if (kZero <= r) {
        a = a * r;
        if (pt.bank < kZero) a = a / (gt3r::ee::sin(pt.bank) + kOne);
    } else {
        a = a * -r;
        if (kZero < pt.bank) a = a / (kOne - gt3r::ee::sin(pt.bank));
    }
    // v = sqrt(a (1 + k v^2)), three times from v = 0
    F v, q = kOne;
    for (int n = 0; n < 3; ++n) {
        v = gt3r::ee::sqrt(a * q);
        q = (d.downforce * v) * v + kOne;
    }
    F out = d.speed_scale * v + (d.speed_offset + int_f(pt.speed_adjust) * kKmh);
    if (c.p.top_speed < out) out = c.p.top_speed;
    else if (out < kmh20) out = kmh20;
    return out;
}

void next_segment(ex::Car& c, const Step& in) {
    ex::Driver& d = c.ai;
    const std::vector<ex::AiPoint>& p = points(*in.course, d);
    const int count = int(p.size()), n = d.next;
    d.mode = d.next_kind;
    d.v_cur = d.v_next;
    d.next_kind = p[size_t(n != count ? n : count - 1)].kind;
    if (d.next_kind == 0) d.v_next = c.p.top_speed;
    else if (d.next_kind < 3) d.v_next = corner_speed(c, in, n != count ? n : count - 1);
}

void replan(ex::Car& c, const Step& in) {
    ex::Driver& d = c.ai;
    const std::vector<ex::AiPoint>& p = points(*in.course, d);
    const int count = int(p.size());
    d.next = u8(count);
    if (count > 0) {
        if (c.f3e4 < p[0].dist) d.next = 0;
        else
            for (int v = 1; v < count; ++v)
                if (c.f3e4 < p[size_t(v)].dist) {
                    d.next = u8(v);
                    break;
                }
    }
    int cur = int(d.next) + count - 1; // (the point before the next one)
    while (!(cur < count)) cur -= count;
    d.next_kind = p[size_t(cur)].kind;
    d.v_next = d.next_kind == 0 ? c.p.top_speed : corner_speed(c, in, cur);
    next_segment(c, in);
}

void set_line(ex::Car& c, const Step& in, int line) {
    c.ai.prev_line = u8(line);
    c.ai.blend = kZero;
    c.ai.line = u8(line);
    replan(c, in);
}

bool request_line(ex::Car& c, const Step& in, int line, bool at_once) {
    const int r = resolve(*in.course, line);
    if (r == -1) return false;
    if (at_once) return set_line(c, in, r), true;
    c.ai.prev_line = c.ai.line;
    c.ai.line = u8(r);
    c.ai.blend = kOne;
    replan(c, in);
    return true;
}

void hold(ex::Car& c, s32 n) {
    ex::Driver& d = c.ai;
    const F rank = int_f(c.rank);
    c.clutch_req = kZero;
    const s32 up = s32(c.p.d.up_rpm), rpm = c.rpm;
    if (n < 13) { // the last steps: the throttle held by the rpm against the up-shift's
        const F q = int_f(rpm - up) / int_f(s32(c.p.e.rev_limit));
        F x = (F::raw(0x3F200000) - rank * F::raw(0x3B03126E)) - (q + q);
        if (kOne < x) x = kOne;
        else if (x < kZero) x = kZero;
        d.throttle = x;
        return;
    }
    bool rise;
    if (c.blip == 0) {
        rise = rpm < ((3 * up) >> 2);
        if (rise) c.blip = 1;
    } else {
        rise = rpm < (15 * up) / 16;
        if (!rise) c.blip = 0;
    }
    if (rise) {
        const F x = d.throttle + c.dt * (F::raw(0x40A00000) - rank * F::raw(0x3E4CCCCC));
        d.throttle = kOne < x ? kOne : x;
    } else {
        const F x = d.throttle - c.dt * (F::raw(0x41200000) - rank * F::raw(0x3ECCCCCC));
        d.throttle = x < kZero ? kZero : x;
    }
}

} // namespace gt4r::sim::ai::prologue
