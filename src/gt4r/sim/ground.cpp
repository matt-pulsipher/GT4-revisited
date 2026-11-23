#include "gt4r/sim/ground.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gt4r::sim {

int surface_index(u8 material, u8 flags) {
    if (!(flags & 2)) return material;
    switch (material) {
    case 0:
    case 7: return 20;
    case 1:
    case 10:
    case 11:
    case 12: return 21;
    case 9: return 22;
    default: return material;
    }
}

RoadGround::RoadGround(const course::Runway& road) : road_(&road) {
    // The checkpoints rise along the list from the one with the least distance (the list starts mid-lap).
    for (size_t i = 1; i < road.checkpoints.size(); ++i)
        if (road.checkpoints[i].v < road.checkpoints[first_].v) first_ = i;
    // The walls' segments by cell.
    const auto segs = road.wall_segments();
    walls_.reserve(segs.size());
    for (const auto& [a, b] : segs) walls_.push_back({road.wall_vertices[a].pos, road.wall_vertices[b].pos});
    nx_ = std::max(1, int((road.max.x - road.min.x) / cell_) + 1);
    nz_ = std::max(1, int((road.max.z - road.min.z) / cell_) + 1);
    if (u64(nx_) * u64(nz_) > 4'000'000) nx_ = nz_ = 1, cell_ = 1e9f;
    wall_cells_.assign(size_t(nx_) * size_t(nz_), {});
    auto cell = [&](float x, float z, int& cx, int& cz) {
        cx = std::clamp(int((x - road.min.x) / cell_), 0, nx_ - 1);
        cz = std::clamp(int((z - road.min.z) / cell_), 0, nz_ - 1);
    };
    for (u32 i = 0; i < walls_.size(); ++i) {
        int x0, z0, x1, z1;
        cell(std::min(walls_[i].a.x, walls_[i].b.x), std::min(walls_[i].a.z, walls_[i].b.z), x0, z0);
        cell(std::max(walls_[i].a.x, walls_[i].b.x), std::max(walls_[i].a.z, walls_[i].b.z), x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) wall_cells_[size_t(z) * size_t(nx_) + size_t(x)].push_back(i);
    }
}

// One cluster's triangles against the line down from `from` (0x3E5FD8): a triangle counts when the
// line passes inside it or on its edge, from its upper side; the first met from above wins, an equal
// one later in the list does not replace it.
void RoadGround::test_cluster(size_t leaf, const Vec3& from, float y_low, GroundHit& best, float& best_t) const {
    const course::Runway& r = *road_;
    if (leaf >= r.clusters.size()) return;
    for (u16 ti : r.clusters[leaf].triangles) {
        if (ti >= r.triangles.size()) continue;
        const course::Runway::Triangle& t = r.triangles[ti];
        const Vec3 &a = r.vertices[t.v[0]].pos, &b = r.vertices[t.v[1]].pos, &c = r.vertices[t.v[2]].pos;
        // The normal (b - a) x (c - a); only triangles facing up are road.
        const float e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z, e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
        const float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
        if (!(ny > 0)) continue;
        // The point's weights of b and c in the triangle seen from above (ny is twice its area there).
        const float px = from.x - a.x, pz = from.z - a.z;
        const float wb = (pz * e2x - px * e2z) / ny, wc = (px * e1z - pz * e1x) / ny;
        if (wb < 0 || wc < 0 || wb + wc > 1) continue;
        const float y = a.y + wb * e1y + wc * e2y;
        if (y > from.y || y < y_low) continue;
        const float tt = from.y - y; // how far down
        if (!(tt < best_t)) continue;
        best_t = tt;
        best.triangle = int(ti), best.cluster = int(leaf);
        best.material = u8(t.bits & 0x1F), best.flags = t.kind;
        const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
        best.normal = {nx * inv, ny * inv, nz * inv};
        auto within = [](float v, float p, float q, float s) { return std::clamp(v, std::min({p, q, s}), std::max({p, q, s})); };
        best.point = {within(from.x, a.x, b.x, c.x), within(y, a.y, b.y, c.y), within(from.z, a.z, b.z, c.z)};
        best.plane_d = -(best.normal.x * best.point.x + best.normal.y * best.point.y + best.normal.z * best.point.z);
        const course::Runway::Vertex &va = r.vertices[t.v[0]], &vb = r.vertices[t.v[1]], &vc = r.vertices[t.v[2]];
        auto mix = [&](float fa, float fb, float fc) { return std::clamp(fa + (fb - fa) * wb + (fc - fa) * wc, 0.0f, 1.0f); };
        best.attr_a = mix(float(va.b) / 255.0f, float(vb.b) / 255.0f, float(vc.b) / 255.0f);
        best.attr_b = mix(float(va.c & 0x7F) / 127.0f, float(vb.c & 0x7F) / 127.0f, float(vc.c & 0x7F) / 127.0f);
    }
}

// The lookup tree for a vertical line (0x3E5BC8): a node that splits x or z leads to one side, one
// that splits the height to both.
void RoadGround::walk(size_t node, size_t leaf, u32 depth, float lo[3], float hi[3], const Vec3& from, float y_low, GroundHit& best, float& best_t) const {
    const course::Runway& r = *road_;
    if (depth == 0) {
        test_cluster(leaf, from, y_low, best, best_t);
        return;
    }
    if (node >= r.tree.size()) return;
    const u8 b = r.tree[node];
    const u32 axis = b >> 6;
    const size_t below = (size_t(1) << (depth - 1)) - 1;
    if (axis >= 3) {
        walk(node + 1, leaf * 2, depth - 1, lo, hi, from, y_low, best, best_t);
        return;
    }
    const float split = lo[axis] + (hi[axis] - lo[axis]) * (float(b & 63) + 0.5f) / 64.0f;
    const float p[3] = {from.x, from.y, from.z};
    const bool left = axis == 1 ? y_low < split : p[axis] < split;
    const bool right = axis == 1 ? from.y >= split : p[axis] >= split;
    if (right) { // the upper side first: the first triangle met from above wins
        const float keep = lo[axis];
        lo[axis] = split;
        walk(node + 1 + below, leaf * 2 + 1, depth - 1, lo, hi, from, y_low, best, best_t);
        lo[axis] = keep;
    }
    if (left) {
        const float keep = hi[axis];
        hi[axis] = split;
        walk(node + 1, leaf * 2, depth - 1, lo, hi, from, y_low, best, best_t);
        hi[axis] = keep;
    }
}

bool RoadGround::cast_down(const Vec3& from_in, GroundHit& out) const {
    const course::Runway& r = *road_;
    if (from_in.y < r.min.y) return false;
    Vec3 from = from_in;
    if (r.max.y < from.y) from.y = r.max.y;
    GroundHit best;
    float best_t = std::numeric_limits<float>::max();
    if (!r.tree.empty() && !r.clusters.empty()) {
        float lo[3] = {r.min.x, r.min.y, r.min.z}, hi[3] = {r.max.x, r.max.y, r.max.z};
        walk(0, 0, r.tree_depth, lo, hi, from, r.min.y, best, best_t);
    } else {
        for (size_t c = 0; c < r.clusters.size(); ++c) test_cluster(c, from, r.min.y, best, best_t);
    }
    if (best.triangle < 0) return false;
    out = best;
    return true;
}

bool RoadGround::at_place(const Vec3& p, GroundHit& out) const {
    bool found = false;
    float y = road_->max.y;
    GroundHit h;
    for (int level = 0; level < 8 && cast_down({p.x, y, p.z}, h); ++level) {
        if (!found || std::fabs(h.point.y - p.y) < std::fabs(out.point.y - p.y)) out = h, found = true;
        y = h.point.y - 0.5f;
    }
    return found;
}

bool RoadGround::distance_along(const Vec3& p, float& v, int* hint) const {
    const course::Runway& r = *road_;
    const int n = int(r.checkpoints.size());
    if (n < 2) return false;
    // How far p is past checkpoint i's line, along the course.
    auto past = [&](int i, float& side) {
        const course::Runway::Checkpoint& c = r.checkpoints[size_t(i)];
        float lx = c.right.x - c.left.x, lz = c.right.z - c.left.z;
        const float len = std::sqrt(lx * lx + lz * lz);
        if (len < 1e-6f) return 0.0f;
        lx /= len, lz /= len;
        // Along the course: up x (left to right), the course's frame being right-handed.
        const float fx = lz, fz = -lx;
        side = (p.x - c.middle.x) * lx + (p.z - c.middle.z) * lz;
        return (p.x - c.middle.x) * fx + (p.z - c.middle.z) * fz;
    };
    float best = std::numeric_limits<float>::max();
    bool found = false;
    auto try_pair = [&](int i) {
        i = ((i % n) + n) % n;
        const int j = (i + 1) % n;
        float side_i = 0, side_j = 0;
        const float a = past(i, side_i), b = past(j, side_j);
        if (a < 0 || b >= 0) return;
        const course::Runway::Checkpoint &c = r.checkpoints[size_t(i)], &d = r.checkpoints[size_t(j)];
        const float t = a / (a - b);
        // The nearest pair: by the distance from the middle line, a level apart counting ten times.
        const float off = std::fabs(side_i + (side_j - side_i) * t); // (the checkpoints' heights are not the road's)
        if (off >= best) return;
        float vj = d.v;
        if (vj < c.v) vj += r.length; // the pair across the lap's end
        best = off, found = true;
        v = c.v + (vj - c.v) * t;
        if (hint) *hint = i;
    };
    if (hint && *hint >= 0) {
        // Around the last answer first: where gates of two stretches overlap, the car stays on its own.
        const int from = *hint;
        for (int k = 0; k <= 4 && !found; ++k) {
            try_pair(from + k);
            if (!found && k > 0) try_pair(from - k);
        }
        if (found) return true;
    }
    for (int i = 0; i < n; ++i) try_pair(i);
    return found;
}

void RoadGround::gate_at(float v, Vec3& left, Vec3& right) const {
    const course::Runway& r = *road_;
    const int n = int(r.checkpoints.size());
    left = right = {};
    if (n < 2) return;
    const float first = r.checkpoints[first_].v;
    if (r.length > 0) {
        v = std::fmod(v - first, r.length);
        if (v < 0) v += r.length;
        v += first;
    }
    // The last checkpoint at or before v, in the order of rising distance.
    auto at = [&](int k) -> const course::Runway::Checkpoint& { return r.checkpoints[(first_ + size_t(k)) % size_t(n)]; };
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (at(mid).v <= v) lo = mid;
        else hi = mid - 1;
    }
    const course::Runway::Checkpoint &c = at(lo), &d = at(lo + 1);
    float span = d.v - c.v;
    if (span <= 0) span += r.length;
    const float t = span > 1e-6f ? std::clamp((v - c.v) / span, 0.0f, 1.0f) : 0.0f;
    auto mix = [&](const Vec3& p, const Vec3& q) { return Vec3{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t}; };
    left = mix(c.left, d.left), right = mix(c.right, d.right);
}

void RoadGround::middle_at(float v, Vec3& point, Vec3& direction) const {
    Vec3 l0, r0, l1, r1;
    gate_at(v, l0, r0), gate_at(v + 1.0f, l1, r1);
    point = {(l0.x + r0.x) * 0.5f, (l0.y + r0.y) * 0.5f, (l0.z + r0.z) * 0.5f};
    direction = {(l1.x + r1.x) * 0.5f - point.x, (l1.y + r1.y) * 0.5f - point.y, (l1.z + r1.z) * 0.5f - point.z};
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (len > 1e-6f) direction = {direction.x / len, direction.y / len, direction.z / len};
    else direction = {0, 0, 1};
}

bool RoadGround::road_middle(float v, int road, const Vec3& near, Vec3& middle, float* width) const {
    Vec3 l, r;
    gate_at(v, l, r);
    const float dx = r.x - l.x, dz = r.z - l.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1.0f) return false;
    auto paved = [](int m) { return m == 0 || m == 1 || (m >= 7 && m <= 12); };
    const int steps = std::min(600, int(len) + 1);
    const float top = road_->max.y + 1.0f;
    // The runs of road surface along the gate: the one nearest to `near` (in the plane, then in height).
    bool found = false, in_run = false;
    float best = std::numeric_limits<float>::max(), run_from = 0, run_y = 0;
    auto close_run = [&](float to) {
        const float mid = (run_from + to) * 0.5f;
        const Vec3 p = {l.x + dx * mid / len, run_y, l.z + dz * mid / len};
        const float ax = near.x - l.x, az = near.z - l.z;
        const float along = (ax * dx + az * dz) / len; // near's place along the gate
        float off = along < run_from ? run_from - along : along > to ? along - to : 0.0f;
        off += 4.0f * std::fabs(run_y - near.y);
        if (off < best) {
            best = off, found = true, middle = p;
            if (width) *width = to - run_from;
        }
    };
    for (int i = 0; i <= steps; ++i) {
        const float s = len * float(i) / float(steps);
        // Every level under the gate: from the top down, below each hit.
        GroundHit h;
        bool on = false;
        float y = top, hit_y = 0;
        for (int level = 0; level < 4 && cast_down({l.x + dx * s / len, y, l.z + dz * s / len}, h); ++level) {
            const bool ok = paved(road) ? paved(h.material) : int(h.material) == road;
            if (ok && (!on || std::fabs(h.point.y - near.y) < std::fabs(hit_y - near.y))) on = true, hit_y = h.point.y;
            y = h.point.y - 0.5f;
        }
        // A wall across the gate ends a stretch.
        if (on) {
            WallHit w;
            if (nearest_wall({l.x + dx * s / len, hit_y, l.z + dz * s / len}, len / float(steps) * 0.6f, w)) on = false;
        }
        if (on && !in_run) in_run = true, run_from = s, run_y = hit_y;
        else if (on && std::fabs(hit_y - run_y) > 3.0f) close_run(s), run_from = s, run_y = hit_y; // another level
        else if (!on && in_run) in_run = false, close_run(s);
        if (on) run_y = hit_y;
    }
    if (in_run) close_run(len);
    return found;
}

bool RoadGround::nearest_wall(const Vec3& p, float reach, WallHit& out) const {
    const course::Runway& r = *road_;
    if (walls_.empty()) return false;
    const int x0 = std::clamp(int((p.x - reach - r.min.x) / cell_), 0, nx_ - 1), x1 = std::clamp(int((p.x + reach - r.min.x) / cell_), 0, nx_ - 1);
    const int z0 = std::clamp(int((p.z - reach - r.min.z) / cell_), 0, nz_ - 1), z1 = std::clamp(int((p.z + reach - r.min.z) / cell_), 0, nz_ - 1);
    float best = reach;
    bool found = false;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            for (u32 i : wall_cells_[size_t(z) * size_t(nx_) + size_t(x)]) {
                const WallSeg& w = walls_[i];
                const float dx = w.b.x - w.a.x, dz = w.b.z - w.a.z;
                const float len2 = dx * dx + dz * dz;
                const float t = len2 > 1e-9f ? std::clamp(((p.x - w.a.x) * dx + (p.z - w.a.z) * dz) / len2, 0.0f, 1.0f) : 0.0f;
                const float qx = w.a.x + dx * t, qz = w.a.z + dz * t, qy = w.a.y + (w.b.y - w.a.y) * t;
                if (std::fabs(qy - p.y) > 6.0f) continue; // a wall of another level
                const float ex = p.x - qx, ez = p.z - qz;
                const float d = std::sqrt(ex * ex + ez * ez);
                if (d >= best) continue;
                best = d, found = true;
                out.point = {qx, qy, qz}, out.distance = d;
                out.normal = d > 1e-6f ? Vec3{ex / d, 0, ez / d} : Vec3{-dz, 0, dx};
            }
    return found;
}

} // namespace gt4r::sim
