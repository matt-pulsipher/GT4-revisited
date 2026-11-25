#include "gt4r/race/gadget_bodies.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gt4r::race {

namespace {

using V = course::Vec3;
V add(const V& a, const V& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V sub(const V& a, const V& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V mul(const V& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// 0x3E2140: a x b.
V cross(const V& a, const V& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V div3(const V& a, const std::array<float, 3>& k) { return {a.x / k[0], a.y / k[1], a.z / k[2]}; }
V vec(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

using M = std::array<float, 9>; // row-major, body to course
// 0x3E21D8 (body to course) and 0x3E2220 (course to body).
V to_world(const M& m, const V& v) { return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z}; }
V to_body(const M& m, const V& v) { return {m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z, m[2] * v.x + m[5] * v.y + m[8] * v.z}; }
M matmul(const M& a, const M& b) {
    M r{};
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    return r;
}

constexpr float kTwoPi = 6.283185f;
constexpr float kGravity = 9.81f;
// The turn's three stages (0x2 steps of Euler's equations between angle steps): 0x8456B0 (set on first use) and 0x6A3960.
constexpr float kAngleStage[4] = {0.6756036f, -0.1756036f, -0.1756036f, 0.6756036f};
constexpr float kRateStage[3] = {1.351207f, -1.702414f, 1.351207f};
// The cars' box planes in the car's frame (0x6A3978): normals with the box's corner each passes through.
constexpr float kBoxPlanes[5][3] = {{0, 0.15f, -1}, {-1, 0.15f, 0}, {0, 0.15f, 1}, {1, 0.15f, 0}, {0, 1, 0}};

// 0x3E2600: the Euler angles of a turn (cos a taken positive).
std::array<float, 3> angles_of(const M& rot) {
    // rot is the program's matrix transposed: its row i, column j at rot[j * 3 + i].
    auto e = [&](int i, int j) { return rot[size_t(j * 3 + i)]; };
    const float sa = e(2, 1), ca = std::sqrt(1 - sa * sa);
    const float sb = -e(2, 0) / ca, cb = e(2, 2) / ca, sc = -e(0, 1) / ca, cc = e(1, 1) / ca;
    std::array<float, 3> a{std::atan2(sa, ca), std::atan2(sb, cb), std::atan2(sc, cc)};
    return a;
}

// 0x3E3218: a turn rate over 20 rad/s about any axis is cut to a fifth.
void cap_turn(GadgetBodies::Body& b) {
    if (std::fabs(b.omega[0]) > 20 || std::fabs(b.omega[1]) > 20 || std::fabs(b.omega[2]) > 20)
        for (float& w : b.omega) w *= 0.2f;
}

// 0x3E3840: an impulse at hull point r (body frame) along the course-space normal n: restitution 0.25
// (none when the point moves slower than 0.9 m/s), friction 0.5 of the impulse, at most the point's
// sliding speed. Returns false when the point already leaves.
bool road_impulse(GadgetBodies::Body& b, const Gadgets::Kind& k, const V& r, const V& n) {
    const float inv_m = 1.0f / k.mass;
    const V nb = to_body(b.rot, n);
    const V rn = cross(r, nb);
    const V lever = to_world(b.rot, cross(div3(rn, k.inertia), r));
    const float kk = inv_m + dot(n, lever);
    const V vp = add(to_world(b.rot, cross(vec(b.omega), r)), b.vel);
    const float e = dot(vp, vp) < 0.81f ? 0.0f : 0.25f;
    const float j = -(e + 1) * dot(n, vp) / kk;
    if (j < 0) return false;
    const float dv = j * inv_m;
    b.hits += j;
    const V vt = sub(vp, mul(n, dot(n, vp)));
    const float vt2 = dot(vt, vt);
    float f = vt2 > 0 ? (dv * 0.5f) / std::sqrt(vt2) : std::numeric_limits<float>::infinity();
    if (f > 1) f = 1;
    b.vel = add(b.vel, sub(mul(n, dv), mul(vt, f)));
    const V tb = cross(r, to_body(b.rot, vt));
    const float fm = f * k.mass;
    b.omega[0] += (j * rn.x - fm * tb.x) / k.inertia[0];
    b.omega[1] += (j * rn.y - fm * tb.y) / k.inertia[1];
    b.omega[2] += (j * rn.z - fm * tb.z) / k.inertia[2];
    return true;
}

struct CarPlanes {
    std::array<std::array<float, 4>, 5> p; // n . q + d (inside: <= 0)
};

// 0x3E5108: a car's five planes: the box's corners (half width w, `rear` behind and `front` ahead of
// the center, the top 1.5 m above it) turned by the car's heading and moved to its center.
CarPlanes car_planes(const GadgetBodies::Car& c) {
    V fwd = c.forward;
    fwd.y = 0;
    const float fl = std::sqrt(dot(fwd, fwd));
    fwd = fl > 1e-6f ? mul(fwd, 1.0f / fl) : V{0, 0, 1};
    const V right{fwd.z, 0, -fwd.x}; // (x across, z ahead in the car's frame: right-handed with up)
    auto turn = [&](float x, float y, float z) { return add(add(mul(right, x), V{0, y, 0}), mul(fwd, z)); };
    const float w = c.half_width;
    const V corner[5] = {turn(w, 0, -c.rear), turn(-w, 0, -c.rear), turn(-w, 0, c.front), turn(w, 0, c.front), turn(0, 1.5f, 0)};
    CarPlanes out;
    for (int k = 0; k < 5; ++k) {
        const V n = turn(kBoxPlanes[k][0], kBoxPlanes[k][1], kBoxPlanes[k][2]);
        const V at = add(c.pos, corner[k]);
        out.p[size_t(k)] = {n.x, n.y, n.z, -dot(n, at)};
    }
    return out;
}
float side(const std::array<float, 4>& p, const V& q) { return p[0] * q.x + p[1] * q.y + p[2] * q.z + p[3]; }

} // namespace

bool GadgetBodies::ground_plane(Body& b, const course::Runway& road) const {
    // 0x3E29F8: the road's triangle under the body and its plane (❓ the program's query 0x3E5A30 is not
    // read: here the highest surface up to 1 m above the body's center).
    float h = 0;
    int t = -1;
    b.on_road = false;
    if (road.height_at(b.pos.x, b.pos.z, b.pos.y + 1.0f, h, &t) && t >= 0 && size_t(t) < road.triangles.size()) {
        const course::Runway::Triangle& tri = road.triangles[size_t(t)];
        const V a = road.vertices[tri.v[0]].pos, p1 = road.vertices[tri.v[1]].pos, p2 = road.vertices[tri.v[2]].pos;
        V n = cross(sub(p1, a), sub(p2, a));
        const float len = std::sqrt(dot(n, n));
        if (len > 1e-9f) {
            n = mul(n, (n.y < 0 ? -1.0f : 1.0f) / len);
            b.plane = {n.x, n.y, n.z, -dot(n, a)};
            b.on_road = true;
        }
    }
    // (❓ Surfaces the road file lacks, as Tsukuba's pit lane a metre above the road under it: near its
    // place a body keeps the level floor it stood on when the road there is lower.)
    const bool near_home = std::hypot(b.pos.x - b.home.x, b.pos.z - b.home.z) < 3.0f;
    if (near_home && (!b.on_road || -(b.plane[0] * b.pos.x + b.plane[2] * b.pos.z + b.plane[3]) / b.plane[1] < b.floor - 0.05f))
        b.plane = {0, 1, 0, -b.floor}, b.on_road = true;
    return b.on_road;
}

void GadgetBodies::place(Body& b, const Gadgets::Kind& k, const course::Runway* road) const {
    // 0x3E2710: at rest; its first three hull points counted as resting; on the road's slope.
    b.vel = {}, b.omega = {}, b.contact = {}, b.spent = 0, b.hits = 0, b.awake = false;
    b.contact[0] = b.contact[1] = b.contact[2] = 2;
    b.rot = Gadgets::rotation(b.angles);
    if (!road) return;
    b.pos.y += 1;
    const bool found = ground_plane(b, *road);
    b.pos.y -= 1;
    if ((k.flags & 2) || !found) return;
    // Turned from up onto the plane's normal (0x3E2268 from (0, 1, 0), 0x6A3950), the angles taken back.
    const V n{b.plane[0], b.plane[1], b.plane[2]};
    const V axis = cross(V{0, 1, 0}, n);
    const float s = std::sqrt(dot(axis, axis)), c = n.y;
    M align{1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (s > 1e-6f) {
        const V u = mul(axis, 1.0f / s);
        const float t = 1 - c;
        align = {t * u.x * u.x + c,       t * u.x * u.y - s * u.z, t * u.x * u.z + s * u.y, t * u.x * u.y + s * u.z, t * u.y * u.y + c,
                 t * u.y * u.z - s * u.x, t * u.x * u.z - s * u.y, t * u.y * u.z + s * u.x, t * u.z * u.z + c};
    }
    b.rot = matmul(align, b.rot);
    b.angles = angles_of(b.rot);
    for (float& a : b.angles)
        if (a < 0) a += kTwoPi;
    b.rot = Gadgets::rotation(b.angles);
    // Hull point 0 set on the plane.
    if (!k.hull.empty()) {
        const V p = add(b.pos, to_world(b.rot, k.hull[0]));
        b.pos.y += -(b.plane[0] * p.x + b.plane[2] * p.z + b.plane[3]) / b.plane[1] - p.y;
    }
}

void GadgetBodies::reset(const Gadgets& g) {
    bodies_.clear();
    moved_ = false;
    for (const Gadgets::Placement& p : g.placements) {
        Body b;
        b.kind = p.kind, b.pos = p.position, b.angles = p.angles;
        b.removed = p.kind < 0 || size_t(p.kind) >= g.kinds.size();
        b.home = p.position, b.floor = p.position.y;
        b.rot = Gadgets::rotation(p.angles);
        if (!b.removed) {
            for (const V& hp : g.kinds[size_t(p.kind)].hull) b.floor = std::min(b.floor, to_world(b.rot, hp).y + p.position.y);
            place(b, g.kinds[size_t(p.kind)], nullptr);
        }
        bodies_.push_back(b);
    }
}

void GadgetBodies::reset(const Gadgets& g, const course::Runway& road) {
    reset(g);
    for (Body& b : bodies_)
        if (!b.removed) place(b, g.kinds[size_t(b.kind)], &road);
}

unsigned GadgetBodies::step(const Gadgets& g, const course::Runway& road, std::span<const Car> cars, float dt) {
    if (bodies_.size() != g.placements.size()) reset(g, road);
    unsigned hits = 0;
    contacts_.assign(cars.size(), 0);
    impulses_.assign(cars.size(), 0.0f);
    std::vector<CarPlanes> planes;
    planes.reserve(cars.size());
    for (const Car& c : cars) planes.push_back(car_planes(c));
    const std::vector<std::pair<u32, u32>> walls = road.wall_segments();
    for (Body& b : bodies_) {
        if (b.removed) continue;
        const Gadgets::Kind& k = g.kinds[size_t(b.kind)];
        b.hits = 0;
        // A once-only kind that met a car is gone 1.7 s later.
        if (b.spent != 0 && float(++b.spent) * (1.0f / 60) > 1.7f) {
            b.removed = true;
            continue;
        }
        if (k.flags & 2) continue; // a kind that never moves
        const size_t n = std::min(k.hull.size(), b.contact.size());
        int rest = 0;
        for (size_t i = 0; i < n; ++i) rest += b.contact[i] == 2;
        const bool resting = rest >= 3;
        const V old_pos = b.pos;
        b.awake = !resting;
        if (!resting) {
            moved_ = true;
            // The turn: Euler's equations on the Euler angles' rates, three stages.
            const std::array<float, 3> I = k.inertia;
            auto angle_step = [&](float h) {
                const float sa = std::sin(b.angles[0]), ca = std::cos(b.angles[0]), sc = std::sin(b.angles[2]), cc = std::cos(b.angles[2]);
                const float wx = b.omega[0], wy = b.omega[1], wz = b.omega[2];
                const float q = (wx * sc - wy * cc) * (1.0f / ca);
                b.angles[1] += q * h;
                b.angles[0] += -(wx * cc + wy * sc) * h;
                b.angles[2] += -(q * sa + wz) * h;
            };
            angle_step(kAngleStage[0] * dt);
            for (int s = 0; s < 3; ++s) {
                const float h = kRateStage[s] * dt;
                const float wx = b.omega[0], wy = b.omega[1], wz = b.omega[2];
                b.omega[1] = wy + ((I[2] - I[0]) * wz * wx) * h / I[1];
                b.omega[2] = wz + ((I[0] - I[1]) * wx * wy) * h / I[2];
                b.omega[0] = wx + ((I[1] - I[2]) * wy * wz) * h / I[0];
                angle_step(kAngleStage[s + 1] * dt);
            }
            for (float& a : b.angles) {
                if (a < 0) a += kTwoPi;
                if (a >= kTwoPi) a -= kTwoPi;
            }
            // The move, then gravity.
            b.pos = add(b.pos, mul(b.vel, dt));
            b.vel.y += (-k.mass * kGravity) * (1.0f / k.mass) * dt;
        }
        b.rot = Gadgets::rotation(b.angles);
        // The hull's points in course space (0x3E3458).
        std::array<V, 8> pts{};
        for (size_t i = 0; i < n; ++i) pts[i] = add(b.pos, to_world(b.rot, k.hull[i]));
        if (!resting) {
            // The walls between where it was and is (0x395A38 -> 0x3E26D0 -> 0x3E44E8): the first one met.
            const float r = k.radius;
            const float x0 = std::min(old_pos.x, b.pos.x) - r, x1 = std::max(old_pos.x, b.pos.x) + r;
            const float z0 = std::min(old_pos.z, b.pos.z) - r, z1 = std::max(old_pos.z, b.pos.z) + r;
            for (const auto& [ia, ib] : walls) {
                const course::Runway::WallVertex &wa = road.wall_vertices[ia], &wb = road.wall_vertices[ib];
                if (wa.tag & 0x20000) continue; // (+0x0E bit 1: not a wall bodies meet)
                if (std::max(wa.pos.x, wb.pos.x) < x0 || std::min(wa.pos.x, wb.pos.x) > x1 || std::max(wa.pos.z, wb.pos.z) < z0 ||
                    std::min(wa.pos.z, wb.pos.z) > z1)
                    continue;
                const float ex = wb.pos.x - wa.pos.x, ez = wb.pos.z - wa.pos.z, cx = b.pos.x - wa.pos.x, cz = b.pos.z - wa.pos.z;
                const float ee = ex * ex + ez * ez;
                if (!(ee > 0)) continue;
                const float t = (ex * cx + ez * cz) / ee;
                bool met = false;
                if (t > 0 && t < 1) {
                    const float dx = t * ex - cx, dz = t * ez - cz;
                    if (dx * dx + dz * dz < r * r) {
                        // Overlapping: put at the radius on the wall's side (ez, -ex).
                        const float s = r / std::sqrt(ee);
                        b.pos.x += dx + ez * s, b.pos.z += dz - ex * s;
                        met = true;
                    }
                }
                if (!met) {
                    // Crossed this step: back along the motion onto the wall's line.
                    const float now = ex * cz - ez * cx;
                    if (now < 0) continue;
                    const float was = ex * (old_pos.z - wa.pos.z) - ez * (old_pos.x - wa.pos.x);
                    if (was > 0) continue;
                    const float mx = b.pos.x - old_pos.x, my = b.pos.y - old_pos.y, mz = b.pos.z - old_pos.z;
                    if (mx * (wa.pos.z - b.pos.z) - mz * (wa.pos.x - b.pos.x) < 0) continue;
                    if (mx * (wb.pos.z - b.pos.z) - mz * (wb.pos.x - b.pos.x) > 0) continue;
                    const float s = now / (now - was);
                    b.pos.x -= s * mx, b.pos.y -= s * my, b.pos.z -= s * mz;
                }
                // 0x3E4708: an impulse along the wall's level normal at the hull point most against it
                // (❓ the program starts its search from 0x6A396C, a NaN, so its first point always), the
                // body stopped when that would add energy.
                const float inv = 1.0f / std::sqrt(ee);
                const V wn{ez * inv, 0, -ex * inv};
                auto energy = [&] {
                    return k.inertia[0] * b.omega[0] * b.omega[0] + k.inertia[1] * b.omega[1] * b.omega[1] + k.inertia[2] * b.omega[2] * b.omega[2] +
                           k.mass * dot(b.vel, b.vel);
                };
                const float before = energy();
                if (n > 0) road_impulse(b, k, k.hull[0], wn);
                if (energy() > before) b.vel = {}, b.omega = {};
                cap_turn(b);
                break;
            }
            // The road under it (0x3E29F8, 0x3E3748): the deepest point below its plane pushes it up.
            if (ground_plane(b, road)) {
                float best = 0;
                int at = -1;
                const std::array<float, 4>& pl = b.plane;
                for (size_t i = 0; i < n; ++i) {
                    const V& p = pts[i];
                    if (!(pl[0] * p.x + pl[1] * p.y + pl[2] * p.z + pl[3] < 0)) {
                        b.contact[i] = 0;
                        continue;
                    }
                    const float depth = -(pl[0] * p.x + pl[2] * p.z + pl[3]) / pl[1] - p.y;
                    if (best < depth) best = depth, at = int(i), ++b.contact[i];
                }
                b.pos.y += best;
                if (at >= 0) road_impulse(b, k, k.hull[size_t(at)], {pl[0], pl[1], pl[2]});
                cap_turn(b);
            } else if (std::hypot(b.pos.x - b.home.x, b.pos.z - b.home.z) > 50 && b.pos.y < b.floor - 50) {
                b.removed = true; // fallen off the course for good
                continue;
            }
        }
        // The cars (0x3E3C48): the first car whose box holds a hull point.
        if (b.spent != 0) continue;
        for (size_t c = 0; c < cars.size(); ++c) {
            const CarPlanes& cp = planes[c];
            bool near = true;
            for (const auto& p : cp.p) near = near && !(k.radius < side(p, b.pos));
            if (!near) continue;
            size_t hit = n;
            for (size_t i = 0; i < n && hit == n; ++i) {
                bool inside = true;
                for (const auto& p : cp.p) inside = inside && !(0 < side(p, pts[i]));
                if (inside) hit = i;
            }
            if (hit == n) continue;
            // 0x3E3ED0: the car's point there moves at its velocity and yaw; the body pushed out through
            // the nearest face (0x3E43B8); an impulse along that face's normal, restitution 0.05 for a
            // once-only kind, else 0.01, the moments of inertia x 1.5; the car feels nothing.
            const Car& car = cars[c];
            const V& p = pts[hit];
            const V rc = sub(p, car.pos);
            const V vcar{car.vel.x + car.yaw_rate * rc.z, car.vel.y, car.vel.z - car.yaw_rate * rc.x};
            const V& r = k.hull[hit];
            const V vrel = sub(add(to_world(b.rot, cross(vec(b.omega), r)), b.vel), vcar);
            int face = -1;
            float least = 100000.0f;
            for (int f = 0; f < 5; ++f) {
                const float d = -side(cp.p[size_t(f)], p);
                if (d < least) least = d, face = f;
            }
            if (face < 0) continue;
            const auto& fp = cp.p[size_t(face)];
            const V fn{fp[0], fp[1], fp[2]};
            b.pos = add(b.pos, mul(fn, least));
            const std::array<float, 3> I15{k.inertia[0] * 1.5f, k.inertia[1] * 1.5f, k.inertia[2] * 1.5f};
            const V rn = cross(r, to_body(b.rot, fn));
            const float kk = 1.0f / k.mass + dot(fn, to_world(b.rot, cross(div3(rn, I15), r)));
            const float e = (k.flags & 1) ? 0.05f : 0.01f;
            const float j = -(e + 1) * dot(fn, vrel) / kk;
            b.vel = add(b.vel, mul(fn, j * (1.0f / k.mass)));
            b.omega[0] += j * rn.x / I15[0], b.omega[1] += j * rn.y / I15[1], b.omega[2] += j * rn.z / I15[2];
            b.hits += j;
            b.contact = {}; // (0x3E29C8: woken)
            if (c < 32) hits |= 1u << c;
            ++contacts_[c];
            impulses_[c] = std::max(impulses_[c], std::fabs(j)); // (0x3E3C48 hands j to 0x350530, kind 2)
            moved_ = true, b.awake = true;
            if (k.flags & 1) b.spent = 1;
            break;
        }
    }
    return hits;
}

GadgetBodies::Car gadget_box(const gt3r::sim::exact::Car& r, const CarState& c) {
    GadgetBodies::Car b;
    b.right = c.right, b.up = c.up, b.forward = c.forward, b.vel = c.velocity;
    b.pos = {r.pos.x.host(), r.pos.z.host(), -r.pos.y.host()};
    b.yaw_rate = r.yaw_rate.host();
    b.front = r.p.body_front.host(), b.rear = r.p.body_rear.host(), b.half_width = r.p.body_width.host() / 2;
    return b;
}

} // namespace gt4r::race
