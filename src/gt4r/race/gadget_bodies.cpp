#include "gt4r/race/gadget_bodies.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

namespace {

using V = course::Vec3;
V add(const V& a, const V& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V sub(const V& a, const V& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V mul(const V& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(const V& a, const V& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(const V& a) { return std::sqrt(dot(a, a)); }

using M = std::array<float, 9>; // row-major, body to course
V turn(const M& m, const V& v) { return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z}; }
V turn_back(const M& m, const V& v) { return {m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z, m[2] * v.x + m[5] * v.y + m[8] * v.z}; }
M matmul(const M& a, const M& b) {
    M r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[size_t(i * 3 + j)] = a[size_t(i * 3)] * b[size_t(j)] + a[size_t(i * 3 + 1)] * b[size_t(3 + j)] + a[size_t(i * 3 + 2)] * b[size_t(6 + j)];
    return r;
}
// The turn by |w| about w.
M axis_angle(const V& w) {
    const float a = len(w);
    if (a < 1e-9f) return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const V k = mul(w, 1.0f / a);
    const float c = std::cos(a), s = std::sin(a), t = 1 - c;
    return {t * k.x * k.x + c,       t * k.x * k.y - s * k.z, t * k.x * k.z + s * k.y, t * k.x * k.y + s * k.z, t * k.y * k.y + c,
            t * k.y * k.z - s * k.x, t * k.x * k.z - s * k.y, t * k.y * k.z + s * k.x, t * k.z * k.z + c};
}
void orthonormalize(M& m) {
    V x{m[0], m[3], m[6]}, y{m[1], m[4], m[7]};
    x = mul(x, 1.0f / std::max(1e-9f, len(x)));
    y = sub(y, mul(x, dot(x, y)));
    y = mul(y, 1.0f / std::max(1e-9f, len(y)));
    const V z = cross(x, y);
    m = {x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z};
}
V inertia_of(const Gadgets::Kind& k) {
    V i{k.inertia[0], k.inertia[1], k.inertia[2]};
    if (!(i.x > 0) || !(i.y > 0) || !(i.z > 0)) i = {1, 1, 1};
    return i;
}
// The inverse inertia (course space) times v, the body's moments scaled by s.
V inv_inertia(const M& r, const V& in, float s, const V& v) {
    const V b = turn_back(r, v);
    return turn(r, {b.x / (in.x * s), b.y / (in.y * s), b.z / (in.z * s)});
}
// A contact impulse at offset r from the middle along unit normal n against a partner moving at
// `other` there: restitution e (none under 0.9 m/s), the inertia scaled, Coulomb friction mu.
void impulse(GadgetBodies::Body& b, const Gadgets::Kind& k, const V& r, const V& n, const V& other, float e, float scale, float mu) {
    const float mass = std::max(0.1f, k.mass);
    const V in = inertia_of(k);
    const V vp = sub(add(b.vel, cross(b.omega, r)), other);
    const float vn = dot(vp, n);
    if (e > 0 && dot(vp, vp) < 0.81f) e = 0;
    const float kk = 1.0f / mass + dot(n, cross(inv_inertia(b.rot, in, scale, cross(r, n)), r));
    const float j = -(1 + e) * vn / std::max(kk, 1e-6f);
    if (j < 0) return;
    V impulse_v = mul(n, j);
    if (mu > 0) {
        const V vt = sub(vp, mul(n, vn));
        const float lt = len(vt);
        if (lt > 1e-6f) impulse_v = add(impulse_v, mul(vt, -std::min(mu * j / mass / lt, 1.0f) * mass));
    }
    b.vel = add(b.vel, mul(impulse_v, 1.0f / mass));
    b.omega = add(b.omega, inv_inertia(b.rot, in, scale, cross(r, impulse_v)));
}
// The road's height and slope under (x, z): its normal from heights half a metre either way.
bool ground_at(const course::Runway& road, const V& p, float& h, V& n) {
    if (!road.height_at(p.x, p.z, p.y + 3.0f, h)) return false;
    float hx0 = h, hx1 = h, hz0 = h, hz1 = h;
    road.height_at(p.x - 0.5f, p.z, p.y + 3.0f, hx0), road.height_at(p.x + 0.5f, p.z, p.y + 3.0f, hx1);
    road.height_at(p.x, p.z - 0.5f, p.y + 3.0f, hz0), road.height_at(p.x, p.z + 0.5f, p.y + 3.0f, hz1);
    n = {hx0 - hx1, 1.0f, hz0 - hz1};
    n = mul(n, 1.0f / len(n));
    return true;
}

} // namespace

void GadgetBodies::reset(const Gadgets& g) {
    bodies_.clear();
    moved_ = false;
    for (const Gadgets::Placement& p : g.placements) {
        Body b;
        b.kind = p.kind, b.pos = p.position, b.rot = Gadgets::rotation(p.angles);
        b.removed = p.kind < 0 || size_t(p.kind) >= g.kinds.size();
        b.home = p.position, b.floor = p.position.y;
        if (!b.removed)
            for (const V& hp : g.kinds[size_t(p.kind)].hull) b.floor = std::min(b.floor, turn(b.rot, hp).y + p.position.y);
        bodies_.push_back(b);
    }
}

void GadgetBodies::ground(Body& b, const Gadgets::Kind& k, const course::Runway& road) {
    float h;
    V n;
    const bool near_home = std::hypot(b.pos.x - b.home.x, b.pos.z - b.home.z) < 3.0f;
    if (!ground_at(road, b.pos, h, n)) {
        if (!near_home) {
            b.removed = true; // off the course: gone for the rest of the race
            return;
        }
        h = b.floor, n = {0, 1, 0};
    }
    if (near_home && h < b.floor - 0.05f) h = b.floor, n = {0, 1, 0};
    const float d = -(n.x * b.pos.x + n.y * h + n.z * b.pos.z);
    float deepest = 0;
    int resting = 0;
    V at;
    for (const V& hp : k.hull) {
        const V r = turn(b.rot, hp), p = add(b.pos, r);
        const float under = std::fabs(n.y) > 1e-3f ? -(d + n.x * p.x + n.z * p.z) / n.y - p.y : 0.0f;
        if (under > -0.02f) ++resting;
        if (under > deepest) deepest = under, at = r;
    }
    if (deepest > 0) {
        b.pos.y += deepest;
        impulse(b, k, at, n, {}, 0.25f, 1.0f, 0.5f);
    }
    // Rolling on the road slows it (❓ judged: a cone on its side would roll on for ever otherwise).
    if (resting >= 2) b.omega = mul(b.omega, 0.97f), b.vel = mul(b.vel, 0.99f);
    // Asleep again once three hull points rest on the road and it has stopped.
    if (resting >= 3 && dot(b.vel, b.vel) < 0.04f && dot(b.omega, b.omega) < 0.25f) {
        if (++b.still > 30) b.awake = false, b.vel = {}, b.omega = {};
    } else {
        b.still = 0;
    }
}

unsigned GadgetBodies::step(const Gadgets& g, const course::Runway& road, std::span<const Car> cars, float dt) {
    if (bodies_.size() != g.placements.size()) reset(g);
    unsigned hits = 0;
    for (Body& b : bodies_) {
        if (b.removed) continue;
        const Gadgets::Kind& k = g.kinds[size_t(b.kind)];
        const float radius = std::max(k.radius, 0.1f);
        for (size_t c = 0; c < cars.size() && c < 32; ++c) {
            const Car& car = cars[c];
            const V dc = sub(b.pos, car.pos);
            const float reach = radius + car.front + car.rear + car.half_width + 2;
            if (dot(dc, dc) > reach * reach) continue;
            // The car's box: five faces in (across, up, ahead), the sides leaning in 5 %, 1 m high.
            V fwd = car.forward;
            fwd.y = 0;
            const float fl = len(fwd);
            if (fl < 1e-6f) continue;
            fwd = mul(fwd, 1.0f / fl);
            const V right{-fwd.z, 0, fwd.x};
            const V across = dot(right, car.right) >= 0 ? right : V{-right.x, 0, -right.z};
            struct Face {
                float x, y, u, d;
            };
            const float s = 1.0f / std::sqrt(1.0f + 0.05f * 0.05f);
            const Face faces[5] = {{0, 0.05f * s, s, -(car.front + 0.05f) * s},
                                   {0, 0.05f * s, -s, -(car.rear + 0.05f) * s},
                                   {s, 0.05f * s, 0, -(car.half_width + 0.05f) * s},
                                   {-s, 0.05f * s, 0, -(car.half_width + 0.05f) * s},
                                   {0, 1, 0, -1}};
            int face = -1;
            float depth = 0;
            V r_hit;
            for (const V& hp : k.hull) {
                const V r = turn(b.rot, hp), p = sub(add(b.pos, r), car.pos);
                const float x = dot(p, across), y = p.y, u = dot(p, fwd);
                float best = 1e9f;
                int bf = -1;
                bool inside = true;
                for (int f = 0; f < 5 && inside; ++f) {
                    const float sd = faces[f].x * x + faces[f].y * y + faces[f].u * u + faces[f].d;
                    if (sd > 0) inside = false;
                    else if (-sd < best) best = -sd, bf = f;
                }
                if (inside) {
                    face = bf, depth = best, r_hit = r;
                    break;
                }
            }
            if (face < 0) continue;
            hits |= 1u << c;
            const Face& f = faces[face];
            const V n = add(add(mul(across, f.x), V{0, f.y, 0}), mul(fwd, f.u));
            b.pos = add(b.pos, mul(n, depth));
            impulse(b, k, r_hit, n, car.vel, 0.01f, 1.5f, 0);
            b.awake = true, b.still = 0, moved_ = true;
            break;
        }
        if (!b.awake) continue;
        // Turning (damped, at most 20 rad/s a component), falling, then the road.
        b.rot = matmul(axis_angle(mul(b.omega, dt)), b.rot);
        orthonormalize(b.rot);
        b.omega = mul(b.omega, 0.999f);
        b.omega = {std::clamp(b.omega.x, -20.0f, 20.0f), std::clamp(b.omega.y, -20.0f, 20.0f), std::clamp(b.omega.z, -20.0f, 20.0f)};
        b.pos = add(b.pos, mul(b.vel, dt));
        b.vel.y -= 9.81f * dt;
        ground(b, k, road);
    }
    return hits;
}

} // namespace gt4r::race
