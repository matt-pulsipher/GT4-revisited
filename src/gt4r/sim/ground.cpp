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
    vf_.reserve(road.vertices.size());
    for (const course::Runway::Vertex& v : road.vertices) vf_.push_back({gt3r::ee::F(v.pos.x), gt3r::ee::F(v.pos.y), gt3r::ee::F(v.pos.z)});
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

namespace {

using gt3r::ee::F;
const F kZero = F::raw(0), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kSixtyFourth = F::raw(0x3C800000);
const F k255 = F::raw(0x437F0000), k127 = F::raw(0x42FE0000);
const F kFar = F::raw(0x7FFFFFFF); // 0x6A39F4: the triangle test's first bound

// 0x3E5F40 / 0x3E5FA8
void cross(F o[3], const F a[3], const F b[3]) {
    o[0] = (a[1] * b[2]) - (a[2] * b[1]);
    o[1] = (a[2] * b[0]) - (a[0] * b[2]);
    o[2] = (a[0] * b[1]) - (a[1] * b[0]);
}
F dot(const F a[3], const F b[3]) { return ((a[0] * b[0]) + (a[1] * b[1])) + (a[2] * b[2]); }

} // namespace

// 0x3E5FD8: the segment a -> b against a cluster's triangles: the earliest along it (the first of equals in the list)
// that it passes through from the upper side, edges included.
bool RoadGround::test_cluster(int leaf, const F a[3], const F b[3], GroundHit& out) const {
    const course::Runway& r = *road_;
    if (leaf < 0 || size_t(leaf) >= r.clusters.size()) return false;
    const F d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    F best = kFar, n[3] = {}, wb = kZero, wc = kZero, w0 = kZero;
    int found = -1;
    for (u16 ti : r.clusters[size_t(leaf)].triangles) {
        const course::Runway::Triangle& t = r.triangles[ti];
        const F* v0 = vf_[t.v[0]].data();
        const F* v1 = vf_[t.v[1]].data();
        const F* v2 = vf_[t.v[2]].data();
        const F p0[3] = {v0[0] - a[0], v0[1] - a[1], v0[2] - a[2]}, p1[3] = {v1[0] - a[0], v1[1] - a[1], v1[2] - a[2]};
        F c[3];
        cross(c, d, p0);
        const F s2 = dot(p1, c);
        if (kZero < s2) continue;
        const F p2[3] = {v2[0] - a[0], v2[1] - a[1], v2[2] - a[2]};
        const F s1 = dot(p2, c);
        if (s1 < kZero) continue;
        cross(c, p1, p2);
        const F s0 = dot(d, c);
        if (kZero < s0) continue;
        const F e1[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]}, e2[3] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2]};
        F nn[3];
        cross(nn, e1, e2);
        const F den = dot(nn, d);
        F tt = kZero;
        if (!(den == kZero)) {
            const F num = dot(nn, p0);
            if (kZero < num) continue;
            if (num < den) continue;
            tt = num / den;
        }
        if (!(tt < best)) continue;
        best = tt;
        n[0] = nn[0], n[1] = nn[1], n[2] = nn[2];
        wb = -s1, wc = s2, w0 = s0;
        found = int(ti);
    }
    if (found < 0) return false;
    const course::Runway::Triangle& t = r.triangles[size_t(found)];
    const std::array<F, 3>* v[3] = {&vf_[t.v[0]], &vf_[t.v[1]], &vf_[t.v[2]]};
    F p[3];
    for (int k = 0; k < 3; ++k) p[k] = ((b[k] - a[k]) * best) + a[k];
    out.triangle = found, out.cluster = leaf;
    // Clamped into the triangle's box.
    for (int k = 0; k < 3; ++k) {
        const F x0 = (*v[0])[size_t(k)], x1 = (*v[1])[size_t(k)], x2 = (*v[2])[size_t(k)];
        F lo, hi;
        if (x0 < x1) lo = gt3r::ee::min(x0, x2), hi = gt3r::ee::max(x1, x2);
        else hi = gt3r::ee::max(x0, x2), lo = gt3r::ee::min(x1, x2);
        p[k] = gt3r::ee::max(lo, gt3r::ee::min(hi, p[k]));
    }
    out.point = {p[0].host(), p[1].host(), p[2].host()};
    out.material = u8(t.bits & 0x1F), out.flags = t.kind;
    const F inv = gt3r::ee::rsqrt(kOne, ((n[0] * n[0]) + (n[1] * n[1])) + (n[2] * n[2]));
    const F un[3] = {n[0] * inv, n[1] * inv, n[2] * inv};
    out.normal = {un[0].host(), un[1].host(), un[2].host()};
    const F sum = (wc + w0) + wb;
    const course::Runway::Vertex* rv[3] = {&r.vertices[t.v[0]], &r.vertices[t.v[1]], &r.vertices[t.v[2]]};
    {
        const F a0 = gt3r::ee::int_to_f(rv[0]->b) / k255, a1 = gt3r::ee::int_to_f(rv[1]->b) / k255, a2 = gt3r::ee::int_to_f(rv[2]->b) / k255;
        const F m = a0 + ((((a1 - a0) * wb) + ((a2 - a0) * wc)) / sum);
        out.attr_a = gt3r::ee::min(gt3r::ee::max(m, kZero), kOne).host();
    }
    {
        const F b1 = gt3r::ee::int_to_f(rv[1]->c & 0x7F) / k127, b0 = gt3r::ee::int_to_f(rv[0]->c & 0x7F) / k127,
                b2 = gt3r::ee::int_to_f(rv[2]->c & 0x7F) / k127;
        const F m = b0 + ((((b1 - b0) * wb) + ((b2 - b0) * wc)) / sum);
        out.attr_b = gt3r::ee::min(gt3r::ee::max(m, kZero), kOne).host();
    }
    out.plane_d = (-dot(un, v[0]->data())).host();
    return true;
}

// 0x3E5BC8: the lookup tree along the segment a -> b, the near side of each split first; the first cluster with a hit
// ends the walk. `box` is the node's box (lower corner, upper corner).
bool RoadGround::walk(u32 node, int depth, u32 leaf, const F box[6], const F a[3], const F b[3], GroundHit& out) const {
    if (depth < 0) return test_cluster(int(leaf), a, b, out);
    const course::Runway& r = *road_;
    if (node >= r.tree.size()) return false;
    const u8 byte = r.tree[node];
    const u32 axis = byte >> 6;
    if (axis > 2) return false;
    const F f = (gt3r::ee::int_to_f(byte & 63) + kHalf) * kSixtyFourth;
    const F split = (box[axis] * (kOne - f)) + (box[axis + 3] * f);
    const F da = a[axis] - split, db = b[axis] - split;
    F nb[6] = {box[0], box[1], box[2], box[3], box[4], box[5]};
    const u32 low = node + 1, high = node + (1u << u32(depth)), l2 = leaf * 2;
    if ((da * db) < kZero) {
        // The segment crosses the split: the part on a's side first, then the rest.
        const F inv = kOne / (b[axis] - a[axis]);
        const F t = (split - a[axis]) * inv, u = (b[axis] - split) * inv;
        F m[3] = {(u * a[0]) + (t * b[0]), (u * a[1]) + (t * b[1]), (u * a[2]) + (t * b[2])};
        m[axis] = split;
        if (da < kZero) {
            nb[axis + 3] = split;
            if (walk(low, depth - 1, l2, nb, a, m, out)) return true;
            nb[axis] = split, nb[axis + 3] = box[axis + 3];
            return walk(high, depth - 1, l2 + 1, nb, m, b, out);
        }
        nb[axis] = split;
        if (walk(high, depth - 1, l2 + 1, nb, a, m, out)) return true;
        nb[axis] = box[axis], nb[axis + 3] = split;
        return walk(low, depth - 1, l2, nb, m, b, out);
    }
    if (da < kZero) {
        nb[axis + 3] = split;
        return walk(low, depth - 1, l2, nb, a, b, out);
    }
    nb[axis] = split;
    return walk(high, depth - 1, l2 + 1, nb, a, b, out);
}

bool RoadGround::query(const F p[3], GroundHit& out) const {
    const course::Runway& r = *road_;
    const F lo_y = F(r.min.y), hi_y = F(r.max.y);
    if (p[1] < lo_y) return false;
    const F a[3] = {p[0], hi_y < p[1] ? hi_y : p[1], p[2]};
    const F b[3] = {p[0], lo_y, p[2]};
    const F box[6] = {F(r.min.x), lo_y, F(r.min.z), F(r.max.x), hi_y, F(r.max.z)};
    GroundHit h;
    if (r.tree.empty() || r.clusters.empty() || r.tree_depth == 0) {
        // (a road without a tree: its one cluster)
        if (!test_cluster(0, a, b, h)) return false;
    } else if (!walk(0, int(r.tree_depth) - 1, 0, box, a, b, h)) {
        return false;
    }
    out = h;
    return true;
}

bool RoadGround::cast_down(const Vec3& from, GroundHit& out) const {
    const F p[3] = {F(from.x), F(from.y), F(from.z)};
    return query(p, out);
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
