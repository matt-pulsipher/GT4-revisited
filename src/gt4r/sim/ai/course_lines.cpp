#include "gt4r/sim/ai/course_lines.h"

#include "base/ee_float.h"
#include "base/ee_math.h"
#include "gt4r/sim/ai/drivers_prologue.h"
#include "gt4r/sim/ai/ee_math.h"
#include "sim/exact/build_math.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace gt4r::sim::ai {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2 * kPi;

Vec3 course_point(float x, float y, float h) { return {x, h, -y}; }

using gt3r::ee::F;

// 0x3E6610: point p in the quad a (the gate's start, left), b (the next gate's, left), c (the gate's start, right), d (the
// next gate's, right), across the ground (x, z): its place u from a to b and v from a to c (inverting the quad's
// bilinear map), and p's height above the quad's at (u, v). False outside the quad (or a quad of no extent).
bool quad_place(const Vec3& pp, const Vec3& aa, const Vec3& bb, const Vec3& cc, const Vec3& dd, F& u, F& v, F& above) {
    const F px(pp.x), pz(pp.z), ax(aa.x), az(aa.z), bx(bb.x), bz(bb.z), cx(cc.x), cz(cc.z), dx(dd.x), dz(dd.z);
    const F zero;
    const F acx = cx - ax, acz = cz - az, dbx = bx - dx, dbz = bz - dz;
    if (acx == zero && acz == zero && dbx == zero && dbz == zero) return false;
    const F apx = px - ax, apz = pz - az;
    const F s1 = apz * acx - apx * acz;
    if (zero < s1) return false;
    const F abx = bx - ax, abz = bz - az;
    const F s2 = apz * abx - apx * abz;
    if (s2 < zero) return false;
    const F dpx = px - dx, dpz = pz - dz;
    if (zero < dpz * dbx - dpx * dbz) return false;
    const F dcx = cx - dx, dcz = cz - dz;
    if (dpz * dcx - dpx * dcz < zero) return false;
    const F ex = (-dcx) - abx, ez = (-dcz) - abz;
    const F k = apz * ex - apx * ez;
    if (acx == zero && acz == zero) {
        u = (-s2) / k;
        v = k / (abz * ex - abx * ez);
    } else {
        const F q = abz * acx - abx * acz;
        const F r = acz * ex - acx * ez;
        const F b0 = k + q, b1 = q - k;
        const F disc = gt3r::ee::sqrt(b0 * b0 + (s2 * F::raw(0x40800000)) * r);
        v = (s2 * F::raw(0xC0000000)) / (b0 - disc);
        u = (s1 + s1) / (b1 - disc);
    }
    const F one = F::raw(0x3F800000);
    const F ay(aa.y), by(bb.y), cy(cc.y), dy(dd.y);
    const F h = (one - u) * (one - v) * ay + u * (one - v) * by + (one - u) * v * cy + u * v * dy;
    above = F(pp.y) - h;
    return true;
}

// 0x36CB90: an angle into [-pi, pi) (beyond three half turns by whole turns, 0x36CB20).
F angle_in_turn(F x) {
    const F pi = F::raw(0x40490FDA), two_pi = F::raw(0x40C90FDA), three_pi = F::raw(0x4116CBE3);
    auto turns = [&] { return gt3r::ee::int_to_f(gt3r::ee::to_int(((gt3r::ee::abs(x) + pi) / two_pi).bits)); };
    if (x < -pi) return x < -three_pi ? x + turns() * two_pi : x + two_pi;
    if (pi <= x) return three_pi < x ? x + turns() * (-two_pi) : x - two_pi;
    return x;
}

} // namespace

bool CourseLines::course_distance(const Vec3& p, int cluster, float& out) const {
    if (!ground_) return false;
    const course::Runway& r = ground_->road();
    if (cluster < 0) {
        GroundHit hit;
        if (!ground_->cast_down(p, hit)) return false;
        cluster = hit.cluster;
    }
    if (cluster < 0 || size_t(cluster) >= r.clusters.size()) return false;
    const course::Runway::Cluster& c = r.clusters[size_t(cluster)];
    const size_t n = r.checkpoints.size();
    F best = F::raw(0x7FFFFFFF), dist;
    bool found = false;
    for (u32 k = c.checkpoint_first; k < u32(c.checkpoint_first) + c.checkpoint_count && k < r.checkpoint_lookup.size(); ++k) {
        const size_t i = r.checkpoint_lookup[k];
        if (i >= n) continue;
        const size_t j = i + 1 == n ? 0 : i + 1;
        const course::Runway::Checkpoint &a = r.checkpoints[i], &b = r.checkpoints[j];
        F u, v, above;
        bool in = quad_place(p, a.left, b.left, a.middle, b.middle, u, v, above);
        if (!in) in = quad_place(p, a.middle, b.middle, a.right, b.right, u, v, above);
        if (!in) continue;
        const F h = gt3r::ee::abs(above);
        if (!(h < best)) continue;
        best = h, found = true;
        const F lap(r.length), from(a.v);
        F to(b.v);
        if (to < from) to = to + lap;
        dist = from + (to - from) * u;
        if (lap < dist) dist = dist - lap;
    }
    if (found) out = dist.host();
    return found;
}

CourseLines::CourseLines(const course::AiData& data, const RoadGround& ground, const Tables& tables) : ground_(&ground), fallback_(tables.fallback) {
    lap_ = ground.road().length;
    if (ground.road().gtrw) { // Prologue's road: its own preparation (0x4292C8)
        prologue::DriverTables t;
        t.fallback = fallback_, t.order = tables.order;
        prologue_ = prologue::driver_course(gt3r::sim::exact::CourseQuery(*ground.road().gtrw), data, t);
        const gt3r::sim::exact::AiCourse& course = *prologue_;
        lap_ = course.lap_length.host();
        for (size_t k = 0; k < 8; ++k)
            for (const gt3r::sim::exact::AiPoint& p : course.lines[k]) {
                LinePoint q;
                q.kind = p.kind, q.brake_adjust = p.brake_adjust, q.speed_adjust = p.speed_adjust;
                q.x = p.x.host(), q.y = p.y.host(), q.height = p.height.host(), q.surface = p.surface, q.pit = (p.x13 & 1) != 0;
                q.dist = p.dist.host(), q.radius = p.radius.host(), q.slope = p.slope.host(), q.bank = p.bank.host(), q.heading = p.heading.host();
                lines_[k].push_back(q);
            }
        return;
    }
    for (int k = 0; k < 8; ++k)
        for (const course::AiLinePoint& p : data.lines[size_t(k)]) {
            LinePoint q;
            q.kind = p.kind, q.brake_adjust = p.brake_adjust, q.speed_adjust = p.speed_adjust;
            // The file's frame is the simulation's turned half round: (x, y) there is (-x, -y) here.
            q.x = -p.x, q.y = -p.y, q.radius = p.radius;
            // 0x46B248 (a half turn added), or the older files' 0x46BCB0: the heading's whole number of 1/4096 turns x 2 pi / 4096
            // and the half turn, in the program's arithmetic (ours from the number the reader made a float of).
            F heading = F(p.heading);
            if (data.format == course::AiData::Format::Fixed)
                heading = (gt3r::ee::int_to_f(s32(std::lround(double(p.heading) * 4096.0 / (2 * std::numbers::pi)))) * F::raw(0x40C90FDA)) * F::raw(0x39800000);
            q.heading = (heading + F::raw(0x40490FDA)).host();
            lines_[size_t(k)].push_back(q);
        }
    // In the program's order (0x46A510).
    for (u8 k : tables.order) prepare(k);
}

bool CourseLines::empty() const {
    for (const auto& l : lines_)
        if (!l.empty()) return false;
    return true;
}

void CourseLines::prepare(int k) {
    using gt3r::ee::sqrt;
    std::vector<LinePoint>& pts = lines_[size_t(k)];
    if (pts.empty()) return;
    const size_t n = pts.size();
    const F zero = F::raw(0), one = F::raw(0x3F800000), half = F::raw(0x3F000000), lap(lap_);
    // 0x46A0C8: the road under (x, y) from 5 m above the height q (0x395760 at (x, q, -y)).
    auto road = [&](F x, F y, F q, GroundHit& hit) {
        const F p[3] = {x, q + F::raw(0x40A00000), -y};
        return ground_->query(p, hit);
    };
    bool unordered = false, lap_added = false;
    // Each line starts from 1,000 m; the last point's place (as the file has it), its height, its distance.
    F query = F::raw(0x447A0000), last_x = zero, last_y = zero, last_dist = zero;
    for (size_t i = 0; i < n; ++i) {
        LinePoint& p = pts[i];
        const F px(p.x), py(p.y);
        // The height the road is looked for from: from a little above the last height (more along a longer stretch),
        // then 100 m higher; its whole metres + 2 (the last's + 3 with no road there).
        if (i > 0) {
            const F dx = px - last_x, dy = py - last_y;
            const F from = query + (sqrt(dx * dx + dy * dy) * F::raw(0x3DCCCCCC) + one);
            GroundHit hit;
            F h = query + F::raw(0x40400000);
            if (road(px, py, from, hit) || road(px, py, from + F::raw(0x42C80000), hit)) h = F(hit.point.y);
            query = gt3r::ee::int_to_f(gt3r::ee::to_int(h) + 2);
        }
        last_x = px, last_y = py;
        if (p.kind != 0) p.kind = k == kSideMap ? 1 : zero < F(p.radius) ? 1 : 2;
        // The stop line's middle points stand on the road's pit stops (the first within 5 m, a nearer one after it),
        // facing the stop's way.
        if (k == kPitStop && i >= 3 && i + 4 <= n) {
            const auto& stops = ground_->road().pit_stops;
            int best = -1;
            F best_d = F::raw(0x47C35000);
            for (size_t j = 0; j < stops.size(); ++j) {
                const F dx = F(stops[j].pos.x) - px, dy = (-F(stops[j].pos.z)) - py;
                const F d = sqrt(dx * dx + dy * dy);
                if (F::raw(0x40A00000) < d) continue;
                if (d < best_d || best < 0) best_d = d, best = int(j);
            }
            if (best >= 0) {
                const auto& st = stops[size_t(best)];
                p.x = st.pos.x, p.y = (-F(st.pos.z)).host();
                // A half turn added, then into [-pi, pi) (0x36CB90).
                u32 bits;
                std::memcpy(&bits, &st.heading, 4);
                p.heading = angle_in_turn(F::raw(bits) + F::raw(0x40490FDA)).host();
            }
        }
        const F x(p.x), y(p.y);
        // The distance along the course at the query's height (0x3955E8: the road cluster under it from 1,000 m above).
        {
            const F at[3] = {x, query, -y};
            bool found = false;
            F s = ground_->course_distance(at, -1, &found);
            p.dist = s.host();
        }
        p.height = 0, p.slope = 0, p.bank = 0, p.surface = 0, p.pit = false;
        // 0x46A140 (not on line 5): the road under the point from the query's height, and its slope and bank from the
        // road half a metre ahead and behind, left and right (a probe that meets no road: the point's own height half a
        // metre from the other; neither of a pair: none kept).
        if (k != kSideMap) {
            GroundHit hit;
            if (road(x, y, query, hit)) {
                const F at(hit.point.y);
                const F hd(p.heading), hs = gt3r::ee::sin(hd) * half, hc = gt3r::ee::cos(hd) * half;
                F ahead = zero, behind = zero, left = zero, right = zero;
                GroundHit g;
                const bool a = road(x - hs, y + hc, query, g);
                if (a) ahead = F(g.point.y);
                const bool b = road(x + hs, y - hc, query, g);
                if (b) behind = F(g.point.y);
                const bool l = road(x - hc, y - hs, query, g);
                if (l) left = F(g.point.y);
                const bool r = road(x + hc, y + hs, query, g);
                if (r) right = F(g.point.y);
                if ((a || b) && (l || r)) {
                    p.height = hit.point.y;
                    p.surface = surface_index(hit.material, hit.flags);
                    p.pit = hit.flags & 1;
                    p.slope = sim::ai::atan2(!a ? at - behind : !b ? ahead - at : ahead - behind, a && b ? one : half).host();
                    p.bank = sim::ai::atan2(!l ? at - right : !r ? left - at : left - right, l && r ? one : half).host();
                    query = at;
                }
            }
        }
        F dist(p.dist);
        if (k == kPitStop) {
            if (dist < lap * half) lap_added = true;
            if (lap_added) dist = dist + lap, p.dist = dist.host();
        }
        if (!(last_dist < dist)) unordered = true;
        last_dist = dist;
    }
    // The stretch to the next point: a chord, or the arc's length; the rise to it and its pitch.
    for (size_t i = 0; i < n; ++i) {
        LinePoint& p = pts[i];
        const LinePoint& q = pts[i + 1 < n ? i + 1 : 0];
        p.crest = false;
        const F dx = F(q.x) - F(p.x), dy = F(q.y) - F(p.y);
        F length = sqrt(dx * dx + dy * dy);
        if (p.kind == 1 || p.kind == 2) {
            F r(p.radius), h0(p.heading), h1(q.heading);
            if (r < zero) r = -r;
            const F two_pi = F::raw(0x40C90FDA);
            if (p.kind == 1 && h1 < h0) h0 = h0 - two_pi;
            if (p.kind == 2 && h0 < h1) h1 = h1 - two_pi;
            F turn = h1 - h0;
            if (turn < zero) turn = -turn;
            length = r * turn;
        }
        const F rise = F(q.height) - F(p.height);
        p.length = length.host(), p.rise = rise.host();
        p.pitch = zero < length ? sim::ai::atan2(rise, length).host() : 0;
    }
    // A crest where the pitch falls by more than a degree from the last point's.
    for (size_t i = 0; i < n; ++i) {
        const LinePoint& prev = pts[i > 0 ? i - 1 : n - 1];
        if (F(pts[i].pitch) - F(prev.pitch) < F::raw(0xBC8EFA34)) pts[i].crest = true;
    }
    // A passing line or the variant whose distances do not rise is not used.
    if (unordered && (k == 1 || k == 2 || k == 7)) pts.clear();
}

bool CourseLines::distance(float x, float y, float h, float& s, int* hint) const {
    if (!ground_ || !ground_->distance_along(course_point(x, y, h), s, hint)) return false;
    s = wrap_lap(s);
    return true;
}

float CourseLines::wrap_lap(float s) const {
    if (lap_ <= 0) return s;
    if (s >= lap_) s -= lap_;
    else if (s < 0) s += lap_;
    return s;
}

int CourseLines::resolve(int k) const {
    if (k < 0 || k > 7) k = 6;
    for (s8 c : fallback_[size_t(k)]) {
        if (c < 0) break;
        if (has(c)) return c;
    }
    return -1;
}

int CourseLines::left(int k) const {
    if (k == 1) return has(6) ? 6 : has(2) ? 2 : -1;
    if (k == 6) return has(2) ? 2 : -1;
    return -1;
}

int CourseLines::right(int k) const {
    if (k == 2) return has(6) ? 6 : has(1) ? 1 : -1;
    if (k == 6) return has(1) ? 1 : -1;
    return -1;
}

int CourseLines::span(int k, int hint, float s, int& from, int& to) const {
    const std::vector<LinePoint>& p = lines_[size_t(k) & 7];
    const int n = int(p.size());
    if (n < 2) return 0;
    if (s < p[0].dist) {
        from = n - 1, to = 0;
        return 1;
    }
    if (s >= p[size_t(n - 1)].dist) {
        from = n - 1, to = 0;
        return 2;
    }
    hint = std::clamp(hint, 0, n);
    for (int t = 0; t <= n; ++t) {
        const int step = (t + 1) / 2;
        int j = t % 2 ? hint + step : hint - step;
        j = ((j % n) + n) % n;
        if (j == 0) continue;
        if (p[size_t(j - 1)].dist <= s && s < p[size_t(j)].dist) {
            from = j - 1, to = j;
            return 3;
        }
    }
    return 0;
}

bool CourseLines::place(int line, int hint, F s, F& x, F& y, F& h) const {
    if (line < 0 || line > 7) return false;
    const std::vector<LinePoint>& p = lines_[size_t(line)];
    if (p.empty()) return false;
    if (prologue_) return line == kPitStop ? prologue_->evaluate_stop(line, s, x, y, h) >= 0 : prologue_->evaluate(line, hint, s, x, y, h);
    const F zero = F::raw(0), half = F::raw(0x3F000000), two_pi = F::raw(0x40C90FDA), lap(lap_);
    if (line == kPitStop) {
        // 0x46B7B8: the stop line, its distances a lap on in the lap's first half: straight between its points.
        if (s < zero) s = s + lap;
        else if (lap <= s) s = s - lap;
        if (s < lap * half) s = s + lap;
        const size_t n = p.size();
        if (s < F(p[0].dist) || F(p[n - 1].dist) <= s) return false;
        size_t i = 0;
        while (i + 1 < n && !(F(p[i].dist) <= s && s < F(p[i + 1].dist))) ++i;
        if (i + 1 >= n) return false;
        const LinePoint &a = p[i], &b = p[i + 1];
        const F t = (s - F(a.dist)) / (F(b.dist) - F(a.dist));
        x = t * (F(b.x) - F(a.x)) + F(a.x);
        y = t * (F(b.y) - F(a.y)) + F(a.y);
        h = t * (F(b.height) - F(a.height)) + F(a.height);
        return true;
    }
    if (s < zero) s = s + lap;
    else if (lap <= s) s = s - lap;
    // 0x46B310: the span: before the first point or past the last (the last to the first), else searched from the hint
    // outwards (hint, hint + 1, hint - 1, hint + 2, ...).
    const int n = int(p.size());
    int from = -1, to = -1, kind = 0;
    if (s < F(p[0].dist)) from = n - 1, to = 0, kind = 1;
    else if (F(p[size_t(n - 1)].dist) <= s) from = n - 1, to = 0, kind = 2;
    else {
        int at = hint;
        for (int step = 0; step <= n; ++step) {
            int cand = (step & 1) ? at + step : at - step;
            at = cand;
            if (cand < 0) cand += n;
            else if (!(cand < n)) cand -= n;
            if (cand == 0 || cand < 0 || cand >= n) continue;
            if (F(p[size_t(cand - 1)].dist) <= s && s < F(p[size_t(cand)].dist)) {
                from = cand - 1, to = cand, kind = 3;
                break;
            }
        }
    }
    if (kind == 0) return false;
    const LinePoint &a = p[size_t(from)], &b = p[size_t(to)];
    const F ad(a.dist), bd(b.dist);
    F num, den;
    if (kind == 1) num = (s + lap) - ad, den = (bd + lap) - ad;
    else if (kind == 2) num = s - ad, den = (bd + lap) - ad;
    else num = s - ad, den = bd - ad;
    const F t = num / den;
    const F ax(a.x), ay(a.y), bx(b.x), by(b.y);
    if (a.kind == 0) {
        x = t * (bx - ax) + ax;
        y = t * (by - ay) + ay;
    } else {
        const F hd(a.heading);
        const F sn = -gt3r::ee::sin(hd);
        const F cs = gt3r::ee::cos(hd);
        F r(a.radius);
        const F cx = ax - cs * r, cy = ay + sn * r;
        const F a0 = sim::ai::atan2(cx - ax, ay - cy);
        F a1 = sim::ai::atan2(cx - bx, by - cy);
        F from_ang = a0, d;
        if (zero < r) {
            if (a1 < from_ang) a1 = a1 + two_pi;
            d = a1 - from_ang;
        } else {
            r = -r;
            if (from_ang < a1) from_ang = from_ang + two_pi;
            d = a1 - from_ang;
        }
        const F ang = from_ang + t * d;
        x = cx - gt3r::ee::sin(ang) * r;
        y = cy + gt3r::ee::cos(ang) * r;
    }
    const F ah(a.height), bh(b.height);
    h = t * (bh - ah) + ah;
    return true;
}

int CourseLines::mode_at(int line, F s) const {
    const int k = resolve(line);
    if (k < 0) return 0;
    const std::vector<LinePoint>& p = lines_[size_t(k)];
    const size_t n = p.size();
    size_t i = 0;
    while (i < n && !(s < F(p[i].dist))) ++i;
    size_t at = (i + n - 1) % n;
    if (p[at].speed_adjust == -128) at = (at + n - 1) % n;
    return p[at].kind;
}

bool CourseLines::start_place(int line, F s, bool one_way, StartPlace& out) const {
    const int k = resolve(line);
    if (k < 0 || !ground_) return false;
    const std::vector<LinePoint>& p = lines_[size_t(k)];
    const F lap(lap_), zero = F::raw(0);
    if (one_way) { // (no further than the line's last point, a lap short of it)
        const F last = F(p.back().dist) - lap;
        if (s < last) s = last;
    }
    while (s < zero) s = s + lap;
    while (lap <= s) s = s - lap;
    // The first point at or past it (the last without one).
    size_t i = 0;
    while (i < p.size() && !(s <= F(p[i].dist))) ++i;
    if (i == p.size()) i = p.size() - 1;
    F x, y, h, x1, y1, h1;
    if (!place(k, int(i), s, x, y, h)) return false;
    if (!place(k, int(i), s + F::raw(0x41200000), x1, y1, h1)) x1 = x, y1 = y;
    out.x = x, out.y = y, out.h = h;
    if (prologue_) { // (0x429860: the road there from the point's segment, else a meter higher from every one)
        const gt3r::sim::exact::CourseQuery q(*ground_->road().gtrw);
        auto under = [&](s32 seg, F from) {
            gt3r::sim::exact::Ground g;
            g.x = x, g.h = from, g.z = -y, g.seg = seg;
            q.ground(g);
            out.h = g.miss ? zero : g.height;
            if (!g.miss) out.hint = g.seg;
            return !g.miss;
        };
        out.hint = prologue_->lines[size_t(k)][i].seg;
        if (!under(out.hint, h)) under(-1, h + F::raw(0x3F800000));
        out.heading = gt3r::sim::exact::atan2(prologue_->behaviour, x - x1, y1 - y);
        return true;
    }
    // The road there (0x46A0C8: from 5 m above the line's height), else from a metre higher.
    GroundHit hit;
    auto road = [&](F height) {
        const F at[3] = {x, height + F::raw(0x40A00000), -y};
        return ground_->query(at, hit);
    };
    bool found = road(h);
    if (!found) found = road(h + F::raw(0x3F800000));
    if (found) out.h = F(hit.point.y), out.hint = s32(u32(u16(gt3r::s16(hit.cluster))) << 16 | u32(u16(hit.triangle)));
    else out.h = zero;
    out.heading = sim::ai::atan2(x - x1, y1 - y);
    return true;
}

bool CourseLines::evaluate(int k, int hint, float s, float& x, float& y, float& h) const {
    const std::vector<LinePoint>& p = lines_[size_t(k) & 7];
    if (p.empty()) return false;
    if (k == kPitStop) {
        // The stop line: straight between its points, a lap added in the lap's first half as its distances have.
        if (s < 0) s += lap_;
        if (s >= lap_) s -= lap_;
        if (s < lap_ * 0.5f) s += lap_;
        if (s < p.front().dist || s >= p.back().dist) return false;
        size_t i = 0;
        while (i + 1 < p.size() && !(p[i].dist <= s && s < p[i + 1].dist)) ++i;
        if (i + 1 >= p.size()) return false;
        const float t = (s - p[i].dist) / std::max(p[i + 1].dist - p[i].dist, 1e-6f);
        x = p[i].x + (p[i + 1].x - p[i].x) * t, y = p[i].y + (p[i + 1].y - p[i].y) * t, h = p[i].height + (p[i + 1].height - p[i].height) * t;
        return true;
    }
    if (s < 0) s += lap_;
    else if (s >= lap_) s -= lap_;
    int from = 0, to = 0;
    const int r = span(k, hint, s, from, to);
    if (r == 0) return false;
    const LinePoint &a = p[size_t(from)], &b = p[size_t(to)];
    float t;
    if (r == 3) t = (s - a.dist) / std::max(b.dist - a.dist, 1e-6f);
    else {
        const float len = std::max(b.dist + lap_ - a.dist, 1e-6f);
        t = r == 1 ? (s + lap_ - a.dist) / len : (s - a.dist) / len;
    }
    h = a.height + (b.height - a.height) * t;
    if (a.kind == 0 || a.radius == 0) {
        x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t;
        return true;
    }
    const float r_ = a.radius;
    const float cx = a.x - r_ * std::cos(a.heading), cy = a.y - r_ * std::sin(a.heading);
    float a0 = std::atan2(cx - a.x, a.y - cy), a1 = std::atan2(cx - b.x, b.y - cy);
    if (r_ > 0) {
        if (a1 < a0) a1 += kTwoPi;
    } else if (a0 < a1) a0 += kTwoPi;
    const float ang = a0 + t * (a1 - a0), rad = std::fabs(r_);
    x = cx + rad * -std::sin(ang), y = cy + rad * std::cos(ang);
    return true;
}

float CourseLines::heading_at(int k, int hint, float s) const {
    float x0, y0, h0;
    if (!evaluate(k, hint, s, x0, y0, h0)) return 0;
    float x1 = x0, y1 = y0;
    for (int i = 1; i <= 10; ++i) {
        float h1;
        if (!evaluate(k, hint, s + 0.5f * float(i), x1, y1, h1)) break;
        if (std::hypot(x1 - x0, y1 - y0) >= 0.2f) break;
    }
    return std::atan2(-(x1 - x0), y1 - y0);
}

} // namespace gt4r::sim::ai
