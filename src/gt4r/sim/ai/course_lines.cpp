#include "gt4r/sim/ai/course_lines.h"

#include "base/ee_float.h"
#include "base/ee_math.h"
#include "gt4r/sim/rules.h"
#include "sim/exact/build_math.h"

#include <algorithm>
#include <cmath>
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
    for (int k = 0; k < 8; ++k)
        for (const course::AiLinePoint& p : data.lines[size_t(k)]) {
            LinePoint q;
            q.kind = p.kind, q.brake_adjust = p.brake_adjust, q.speed_adjust = p.speed_adjust;
            // The file's frame is the simulation's turned half round: (x, y) there is (-x, -y) here.
            q.x = -p.x, q.y = -p.y, q.radius = p.radius, q.heading = (F(p.heading) + F::raw(0x40490FDA)).host(); // (0x46B248, in the program's arithmetic)
            lines_[size_t(k)].push_back(q);
        }
    // In the program's order: each line's first height query starts from where the last line's ended.
    float query = 1000.0f;
    for (u8 k : tables.order) prepare(k, query);
}

bool CourseLines::empty() const {
    for (const auto& l : lines_)
        if (!l.empty()) return false;
    return true;
}

void CourseLines::prepare(int k, float& query) {
    std::vector<LinePoint>& pts = lines_[size_t(k)];
    if (pts.empty()) return;
    const size_t n = pts.size();
    bool unordered = false, lap_added = false;
    int hint = -1;
    float prev_h = 0;
    for (size_t i = 0; i < n; ++i) {
        LinePoint& p = pts[i];
        if (p.kind != 0) p.kind = k == kSideMap ? 1 : p.radius > 0 ? 1 : 2;
        // The stop line's middle points stand on the road's pit stops (the nearest within 5 m), facing the stop's way.
        if (k == kPitStop && i >= 3 && i + 4 <= n) {
            const auto& stops = ground_->road().pit_stops;
            int best = -1;
            float best_d = 5.0f;
            for (size_t j = 0; j < stops.size(); ++j) {
                const float d = std::hypot(stops[j].pos.x - p.x, -stops[j].pos.z - p.y);
                if (d < best_d) best_d = d, best = int(j);
            }
            if (best >= 0) {
                const auto& st = stops[size_t(best)];
                p.x = st.pos.x, p.y = -st.pos.z;
                // A half turn added, then into [-pi, pi) (0x36CB90).
                const F pi = F::raw(0x40490FDA), two_pi = F::raw(0x40C90FDA);
                F hd = F(st.heading) + pi;
                if (hd < -pi) hd = hd + two_pi;
                else if (pi <= hd) hd = hd - two_pi;
                p.heading = hd.host();
            }
        }
        // The road under the point: from well above the first point, then from a little above the last height (more
        // along a longer stretch), then from 100 m above.
        GroundHit hit;
        bool found = false;
        if (i == 0) found = ground_->cast_down(course_point(p.x, p.y, query), hit);
        else {
            const LinePoint& q = pts[i - 1];
            const float chord = std::hypot(p.x - q.x, p.y - q.y);
            found = ground_->cast_down(course_point(p.x, p.y, prev_h + 0.1f * chord + 1.0f), hit) ||
                    ground_->cast_down(course_point(p.x, p.y, prev_h + 100.0f), hit);
        }
        if (found) {
            p.height = hit.point.y;
            p.surface = surface_index(hit.material, hit.flags);
            p.pit = hit.flags & 1;
            // 0x46A140: the slope and bank from the road's heights half a metre ahead and behind, left and right (a probe
            // that meets no road: the point's own height half a metre from the other).
            const F h(p.heading), half = F::raw(0x3F000000), one = F::raw(0x3F800000);
            const F hs = gt3r::ee::sin(h) * half, hc = gt3r::ee::cos(h) * half, x(p.x), y(p.y), at(p.height);
            auto probe = [&](F px, F py, F& out) {
                GroundHit g;
                if (!ground_->under(course_point(px.host(), py.host(), p.height), g)) return false;
                out = F(g.point.y);
                return true;
            };
            F ahead, behind, left, right;
            const bool a = probe(x - hs, y + hc, ahead), b = probe(x + hs, y - hc, behind);
            const bool l = probe(x - hc, y - hs, left), r = probe(x + hc, y + hs, right);
            const gt3r::sim::Behaviour beh = rules::behaviour();
            p.slope = p.bank = 0;
            if (a || b)
                p.slope = gt3r::sim::exact::atan2(beh, !a ? at - behind : !b ? ahead - at : ahead - behind, a && b ? one : half).host();
            if (l || r)
                p.bank = gt3r::sim::exact::atan2(beh, !l ? at - right : !r ? left - at : left - right, l && r ? one : half).host();
        } else {
            p.height = i == 0 ? 0 : prev_h;
        }
        prev_h = p.height;
        query = std::trunc(p.height) + 2.0f;
        float s = 0;
        if (course_distance(course_point(p.x, p.y, p.height), found ? hit.cluster : -1, s)) p.dist = s;
        // (a road without the clusters' checkpoint lists, Prologue's: between the checkpoint lines)
        else if (ground_->distance_along(course_point(p.x, p.y, p.height), s, &hint)) p.dist = s >= lap_ ? s - lap_ : s < 0 ? s + lap_ : s;
        else p.dist = i ? pts[i - 1].dist : 0;
        if (k == kPitStop) {
            if (!lap_added && p.dist < lap_ * 0.5f) lap_added = true;
            if (lap_added) p.dist += lap_;
        }
        if (i > 0 && p.dist <= pts[i - 1].dist) unordered = true;
    }
    // The stretch to the next point: a chord, or the arc's length.
    for (size_t i = 0; i < n; ++i) {
        LinePoint& p = pts[i];
        const LinePoint& q = pts[(i + 1) % n];
        p.crest = false;
        if (p.kind == 0) p.length = std::hypot(q.x - p.x, q.y - p.y);
        else {
            float h0 = p.heading, h1 = q.heading;
            if (p.kind == 1 && h1 < h0) h0 -= kTwoPi;
            if (p.kind == 2 && h0 < h1) h1 -= kTwoPi;
            p.length = std::fabs(p.radius) * std::fabs(h1 - h0);
        }
        p.rise = q.height - p.height;
        p.pitch = p.length > 0 ? std::atan2(p.rise, p.length) : 0;
    }
    for (size_t i = 0; i < n; ++i) {
        const LinePoint& prev = pts[(i + n - 1) % n];
        if (pts[i].pitch - prev.pitch < -kPi / 180.0f) pts[i].crest = true;
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
