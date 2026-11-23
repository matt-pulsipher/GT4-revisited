#include "gt4r/sim/ai/course_lines.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gt4r::sim::ai {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2 * kPi;

Vec3 course_point(float x, float y, float h) { return {x, h, -y}; }

} // namespace

CourseLines::CourseLines(const course::AiData& data, const RoadGround& ground, const Tables& tables) : ground_(&ground), fallback_(tables.fallback) {
    lap_ = ground.road().length;
    for (int k = 0; k < 8; ++k)
        for (const course::AiLinePoint& p : data.lines[size_t(k)]) {
            LinePoint q;
            q.kind = p.kind, q.brake_adjust = p.brake_adjust, q.speed_adjust = p.speed_adjust;
            // The file's frame is the simulation's turned half round: (x, y) there is (-x, -y) here.
            q.x = -p.x, q.y = -p.y, q.radius = p.radius, q.heading = p.heading + kPi;
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
            // The normal in the simulation's frame (x, y, up) against the heading's direction and its left.
            const float nx = hit.normal.x, ny = -hit.normal.z;
            const float dx = -std::sin(p.heading), dy = std::cos(p.heading);
            p.slope = -std::asin(std::clamp(nx * dx + ny * dy, -1.0f, 1.0f));
            p.bank = -std::asin(std::clamp(nx * -dy + ny * dx, -1.0f, 1.0f));
        } else {
            p.height = i == 0 ? 0 : prev_h;
        }
        prev_h = p.height;
        query = std::trunc(p.height) + 2.0f;
        float s = 0;
        if (ground_->distance_along(course_point(p.x, p.y, p.height), s, &hint)) p.dist = s >= lap_ ? s - lap_ : s < 0 ? s + lap_ : s;
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
