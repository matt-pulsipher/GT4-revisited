// Gran Turismo 4's collision step; see collide.h. Each function follows one routine of the program: every operator is
// one EE operation on the operands the program uses (add and multiply are commutative bit for bit, so only the grouping
// matters), comparisons the program's strict or non-strict ones.

#include "gt4r/sim/collide.h"

#include "base/ee_math.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/sim/ground.h"

namespace gt4r::sim::collide {

namespace ee = gt3r::ee;

namespace {

const F kZero = F::raw(0), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000), kTwo = F::raw(0x40000000);
const F kQuarter = F::raw(0x3E800000), kFour = F::raw(0x40800000), kTwenty = F::raw(0x41A00000);
const F kChamfer = F::raw(0x3E4CCCCC);      // 0x45B978: 0.2 of the width at each corner
const F kPi = F::raw(0x40490FDA), kTwoPi = F::raw(0x40C90FDA), kMinusTwoPi = F::raw(0xC0C90FDA); // 0x45C928
const F kMaxYaw = F::raw(0x427B53D0), kMinYaw = F::raw(0xC27B53D0); // 0x45C9B0
const F kEarly = F::raw(0x3DCCCCCC);        // 0x45CA60
const F kSoundDecay = F::raw(0x3F7AE147);   // 0x45CC60
const F kPairStrength = F::raw(0x466A6000); // 0x45F340: 15,000
const F kWallStrength = F::raw(0x47C35000); // 0x45E288 / 0x45E768: 100,000
const F kHigh = F::raw(0x461C4000);         // 0x395A38: 10,000 m, where a body without a road triangle looks down from
constexpr s32 kRumbleSteps = 23;            // 0x45CC60

// 0x45C0F8 (given 0): the coefficients the routines read (0x846630 .. 0x84663C).
const F kZeroF = kZero;
const F kCarBounce = (kZeroF * kZero) + kHalf;                        // 0x846630: the cars' restitution
const F kWallBounce = (kZeroF * kZero) + F::raw(0x3D4CCCCC);          // 0x846634: a wall's
const F kInertiaScale = F::raw(0x41200000) - (kZeroF * F::raw(0x40E00000)); // 0x846638: the yaw inertia's factor (10)
const F kFriction = (kZeroF * F::raw(0x3E4CCCCC)) + kHalf;            // 0x84663C: a wall's friction

F neg_if_negative(F v) { return v < kZero ? -v : v; }

// 0x45C750 / 0x45C7E8: the points at the start / end pose (sincosf 0x57D8A0).
void start_points(Body& b) {
    F s, c;
    ee::sincos(b.sh, s, c);
    for (Point& q : b.p) {
        q.sx = ((c * q.lx) - (s * q.ly)) + b.sx;
        q.sy = ((s * q.lx) + (c * q.ly)) + b.sy;
    }
}
void end_points(Body& b) {
    F s, c;
    ee::sincos(b.eh, s, c);
    for (Point& q : b.p) {
        q.ex = ((c * q.lx) - (s * q.ly)) + b.ex;
        q.ey = ((s * q.lx) + (c * q.ly)) + b.ey;
    }
}

// 0x45C898: the box swept from the start to the end pose, grown by the radius.
void sweep_box(Body& b) {
    F lo = b.sx, hi = b.ex;
    if (b.dx < kZero) lo = b.ex, hi = b.sx;
    b.bx0 = lo - b.radius;
    b.bx1 = hi + b.radius;
    lo = b.sy, hi = b.ey;
    if (b.dy < kZero) lo = b.ey, hi = b.sy;
    b.by0 = lo - b.radius;
    b.by1 = hi + b.radius;
    b.bw = b.bx1 - b.bx0;
    b.bh = b.by1 - b.by0;
}

// 0x45CBB8: moves the start pose and the start points.
void shift(Body& b, F dx, F dy) {
    b.sx = b.sx + dx;
    b.sy = b.sy + dy;
    for (Point& q : b.p) {
        q.sx = q.sx + dx;
        q.sy = q.sy + dy;
    }
}

void min_max(F v, F& lo, F& hi) {
    if (v < lo) lo = v;
    if (hi < v) hi = v;
}

// The two bodies are on one level: their sweeps' start heights within 4 m (0x45FAD0, 0x45ECB0).
bool same_level(const Body& a, const Body& b) { return !(kFour < ee::abs(a.sz - b.sz)); }

// 0x45D550: a wall segment (P -> its next point) against the body's free points (the end beyond the wall, the start
// inside, the motion crossing the segment), then, for a corner P, P against the body's edges. Each point's and edge's
// earliest crossing is kept; a sensor line records only the body's earliest sensor crossing.
void sweep_segment(Body& b, const Walls& walls, int P) {
    const Walls::Point* W = walls.points();
    const int N = walls.next(P);
    const F by = -W[N].z, bx = W[N].x, ay = -W[P].z, ax = W[P].x;
    const bool sensor = (W[P].flags & 2) != 0;
    const F dx = bx - ax, dy = by - ay;
    for (Point& q : b.p) {
        if (q.flags) continue;
        const F s1 = (dx * (q.ey - ay)) - (dy * (q.ex - ax));
        if (kZero <= s1) continue;
        const F ux = q.sx - ax, uy = q.sy - ay;
        const F s0 = (dx * uy) - (dy * ux);
        if (s0 <= kZero) continue;
        const F mx = q.ex - q.sx, my = q.ey - q.sy;
        if ((mx * uy) - (my * ux) < kZero) continue;
        if (kZero < (mx * (q.sy - by)) - (my * (q.sx - bx))) continue;
        const F den = s0 - s1;
        F t = kOne;
        if (!(den == kZero)) t = s0 / den;
        if (!(t < q.t)) continue;
        if (sensor) {
            if (t < b.sensor_t) b.sensor_t = t, b.sensor = 1;
            continue;
        }
        q.t = t;
        q.hit = 1;
        q.wall = P;
        const F len = ee::sqrt((dx * dx) + (dy * dy));
        if (len == kZero) {
            q.tx = kOne;
            q.ty = kZero;
        } else {
            q.ty = dy / len;
            q.tx = dx / len;
        }
    }
    if (!(W[P].flags & 1)) return;
    for (int j = 0; j < 8; ++j) { // edge j - 1: point j - 1 -> point j
        const Point& p1 = b.p[size_t((j + 7) & 7)];
        const Point& p2 = b.p[size_t(j)];
        const F e1 = ((p2.ex - p1.ex) * (ay - p1.ey)) - ((p2.ey - p1.ey) * (ax - p1.ex));
        if (e1 <= kZero) continue;
        const F ry = ay - p1.sy, rx = ax - p1.sx, ex = p2.sx - p1.sx, ey = p2.sy - p1.sy;
        const F e0 = (ex * ry) - (ey * rx);
        if (kZero <= e0) continue;
        const F m1x = p1.ex - p1.sx, m1y = p1.ey - p1.sy, m2x = p2.ex - p2.sx, m2y = p2.ey - p2.sy;
        const F c1 = (m1x * ey) - (m1y * ex);
        const F a1 = (m1x * ry) - (m1y * rx);
        const F a2 = (m2x * (ay - p2.sy)) - (m2y * (ax - p2.sx));
        if (c1 < kZero) {
            if (kZero < a1) continue;
            if (kZero < a2) continue;
        }
        if ((m2x * ey) - (m2y * ex) < kZero) {
            if (a1 < kZero) continue;
            if (a2 < kZero) continue;
        }
        if (a1 < kZero) continue;
        if (kZero < a2) continue;
        const F den = e0 - e1;
        F t = kOne;
        if (!(den == kZero)) t = e0 / den;
        if (sensor) {
            if (t < b.sensor_t) b.sensor_t = t, b.sensor = 1;
            continue;
        }
        Edge& e = b.e[size_t((j + 7) & 7)];
        if (t < e.t) {
            e.t = t;
            e.wall = P;
            e.hit = 1;
        }
    }
}

// 0x45DBB8: the end-of-step push-out against one wall segment: each free point whose end is beyond the wall while a
// neighbour (the previous one in the first pass, the next in the second) is inside, the body's edge between them
// crossing the segment, moves the end pose at once.
void push_segment(Body& b, const Walls& walls, int P) {
    const Walls::Point* W = walls.points();
    const int N = walls.next(P);
    const F by = -W[N].z, ax = W[P].x, ay = -W[P].z, bx = W[N].x;
    const F dx = bx - ax, dy = by - ay;
    for (int k = 0; k < 16; ++k) {
        Point& q = b.p[size_t(k & 7)];
        if (q.flags) continue;
        const Point& o = b.p[size_t((k & 8) ? (k + 1) & 7 : (k - 1) & 7)];
        const F qy = q.ey - ay, qx = q.ex - ax, ox = o.ex - ax, oy = o.ey - ay;
        const F side_q = (dx * qy) - (dy * qx), side_o = (dx * oy) - (dy * ox);
        const F cx = q.ex - o.ex, cy = q.ey - o.ey, obx = o.ex - bx, oby = o.ey - by;
        if (kZero <= side_q) continue;
        if (side_o <= kZero) continue;
        if ((cx * oy) - (cy * ox) < kZero) continue;
        if (kZero < (cx * oby) - (cy * obx)) continue;
        const F ur = (qx * dx) + (qy * dy);
        const F d2 = (dx * dx) + (dy * dy);
        if (ur < kZero || d2 < ur) {
            // Beyond an end of the segment: the body's edge moved through that end.
            const F rx = ur < kZero ? ox : obx, ry = ur < kZero ? oy : oby;
            const F c2 = (cx * cx) + (cy * cy);
            F lam = kZero;
            if (!(c2 == kZero)) lam = (((-rx) * cx) - (ry * cy)) / c2;
            const F mx = (lam * cx) + rx, my = (lam * cy) + ry;
            b.ex = b.ex - mx;
            b.ey = b.ey - my;
            end_points(b);
            continue;
        }
        F inv = kOne, u = ur;
        if (!(d2 == kZero)) inv = ee::rsqrt(kOne, d2), u = ur / d2;
        const F px = (u * dx) - qx, py = (u * dy) - qy;
        if (kQuarter < (px * px) + (py * py)) {
            // More than 0.5 m: to the segment's far end instead.
            const F f = k < 8 ? kOne - u : -u;
            b.ex = b.ex + (f * dx);
            b.ey = b.ey + (f * dy);
            end_points(b);
            continue;
        }
        b.ex = b.ex + px;
        b.ey = b.ey + py;
        end_points(b);
        q.wall = P;
        q.flags |= 1;
        q.ty = dy * inv;
        q.tx = dx * inv;
    }
}

// 0x45E898: the friction of a wall's impulse J at a point moving with vp: against the velocity's part off J (and the
// vertical velocity), at most 0.5 |J| and the mass.
void friction(const Body& b, F jx, F jy, F vpx, F vpy, F& fx, F& fy, F& fz) {
    const F j2 = (jx * jx) + (jy * jy);
    F lam = kZero;
    if (!(j2 == kZero)) lam = ((vpx * jx) + (vpy * jy)) / j2;
    const F tx = (lam * jx) - vpx, ty = (lam * jy) - vpy, tz = -b.vz;
    const F t2 = ((tx * tx) + (ty * ty)) + (tz * tz);
    if (t2 == kZero) {
        fx = fy = fz = kZero;
        return;
    }
    const F inv = ee::rsqrt(kOne, t2);
    F mag = (kFriction * ee::sqrt(j2)) * inv;
    if (b.mass < mag) mag = b.mass;
    fx = mag * tx;
    fy = mag * ty;
    fz = mag * tz;
}

// The vertical velocity after a wall's impulse never changes sign (0x45E028 / 0x45E570).
void vertical(Body& b, F fz) {
    const bool down = b.vz < kZero;
    const F vz = b.vz + (fz * b.im);
    b.vz = vz;
    if (down ? kZero < vz : vz < kZero) b.vz = kZero;
}

// 0x45E450: after a point's impulse, a start point the advance left beyond the wall's line goes back onto it.
void correct_point(Body& b, const Walls& walls) {
    const Walls::Point& W = walls.points()[b.wseg];
    const F ax = W.x, ay = -W.z;
    const Point& q = b.p[size_t(b.wk)];
    const F dx = q.sx - ax, dy = q.sy - ay;
    if (!(kZero < (dx * b.uy) - (dy * b.ux))) return;
    const F u2 = (b.ux * b.ux) + (b.uy * b.uy);
    F u = kOne;
    if (!(u2 == kZero)) u = ((dx * b.ux) + (dy * b.uy)) / u2;
    const F mx = ((u * b.ux) + ax) - q.sx, my = ((u * b.uy) + ay) - q.sy;
    b.sx = b.sx + mx;
    b.sy = b.sy + my;
    start_points(b);
}

// A point's new wall tangent, E / |E| ((1, 0) for a zero length).
void set_tangent(Point& q, F ex, F ey) {
    const F len = ee::sqrt((ex * ex) + (ey * ey));
    if (len == kZero) {
        q.tx = kOne;
        q.ty = kZero;
        return;
    }
    const F inv = kOne / len;
    q.tx = ex * inv;
    q.ty = ey * inv;
}

// 0x45F5F8: the impulse between B's vertex and A's edge at A's crossing point, along A's edge normal.
F pair_impulse(const Body& A, const Body& B, const F* ra, const F* rb, F& jx, F& jy) {
    const Edge& ae = A.e[size_t(A.hit_edge)];
    const Edge& bv = B.e[size_t(A.vertex)];
    const F ncx = -A.cpx;
    const F ray = (ra[0] * A.cpy) + (ra[1] * ncx), nrax = (ra[2] * A.cpy) + (ra[3] * ncx);
    const F vax = A.vx - (A.w * ray), vay = A.vy - (A.w * nrax);
    const F nx = (ra[0] * ae.nx) + (ra[1] * ae.ny), ny = (ra[2] * ae.nx) + (ra[3] * ae.ny);
    const F ka = (-((A.cpx * ae.ny) - (A.cpy * ae.nx))) * A.ii;
    const F nlx = -bv.sx;
    const F rby = (rb[0] * bv.sy) + (rb[1] * nlx), nrbx = (rb[2] * bv.sy) + (rb[3] * nlx);
    const F nbx = (rb[0] * nx) + (rb[2] * ny), nby = (rb[1] * nx) + (rb[3] * ny);
    const F relx = vax - (B.vx - (B.w * rby)), rely = vay - (B.vy - (B.w * nrbx));
    const F kb = (-((bv.sx * nby) - (bv.sy * nbx))) * B.ii;
    const F sy = (ka * ray) + (kb * rby), sx = (ka * nrax) + (kb * nrbx);
    const F k = (A.im + B.im) + ((sy * nx) + (sx * ny));
    F j = kZero;
    if (!(k == kZero)) j = (-(kCarBounce + kOne) * ((relx * nx) + (rely * ny))) / k;
    jx = j * nx;
    jy = j * ny;
    return j;
}

// 0x45F928: B's vertex still outside A's edge line after the response's pose: half the gap each, along the normal.
void touch_up(Body& A, Body& B, const F* ra, const F* rb) {
    const Edge& bv = B.e[size_t(A.vertex)];
    const Edge& ae = A.e[size_t(A.hit_edge)];
    const F rbx = (rb[0] * bv.sx) + (rb[1] * bv.sy), rby = (rb[2] * bv.sx) + (rb[3] * bv.sy);
    const F qx = (B.sx - A.sx) + rbx, qy = (B.sy - A.sy) + rby;
    const F nx = (ra[0] * ae.nx) + (ra[1] * ae.ny), ny = (ra[2] * ae.nx) + (ra[3] * ae.ny);
    const F gap = ((nx * qx) + (ny * qy)) + ae.d;
    if (gap <= kZero) return;
    const F ng = -gap;
    const F my = (ng * ny) * kHalf, mx = (ng * nx) * kHalf;
    B.sx = B.sx + mx;
    B.sy = B.sy + my;
    A.sx = A.sx - mx;
    A.sy = A.sy - my;
}

// 0x350328: the hit sound's recorder, for kinds 0 (a wall: the impulse's size) and 1 (a car: the strength the
// contact added). It keeps the step's strongest.
void record_hit(Body& b, s32 kind, F value, s32 other) {
    const F k50000 = F::raw(0x47435000), k02 = F::raw(0x3E4CCCCC);
    F f = kind == 0 ? value / k50000 : value + value;
    if (kOne < f) f = kOne;
    if (f < k02) f = kZero;
    if (b.hit_sound < f) b.hit_sound = f, b.hit_kind = kind, b.hit_other = other;
}

// 0x45C488: the sweep from the committed state; the start points carry over from the last step's end points.
void init(Body& b, F dt) {
    b.dt = dt;
    b.te = kOne;
    b.sx = b.cx, b.sy = b.cy, b.sz = b.cz, b.sh = b.heading;
    b.vx = b.cvx, b.vy = b.cvy, b.vz = b.cvz, b.w = b.yaw_rate;
    b.front = b.rear = b.car_hit = b.wall_hit = b.sensor_hit = 0;
    b.strength = kZero;
    b.hit_sound = kZero, b.hit_kind = -1, b.hit_other = -1;
    b.ts = kZero;
    if (b.have_points) {
        for (Point& q : b.p) q.sx = q.ex, q.sy = q.ey;
    } else {
        b.have_points = 1;
        start_points(b);
    }
}

// 0x45C530: car-to-car contact switched off drops the points' holds on other bodies.
void enable(Body& b, bool on) {
    if (b.enabled && !on)
        for (Point& q : b.p) q.flags &= ~2u;
    b.enabled = on;
}

// 0x45FAD0: the start points of o inside t's outline: the deepest along the line of the centres moves both bodies
// half that each.
void separate(std::vector<Body>& bd, int it, int io) {
    Body& t = bd[size_t(it)];
    if (!t.enabled) return;
    Body& o = bd[size_t(io)];
    if (!o.enabled) return;
    if (!same_level(t, o)) return;
    const F dcx = o.sx - t.sx, dcy = o.sy - t.sy;
    if (t.r2[size_t(io)] < (dcx * dcx) + (dcy * dcy)) return;
    F best = kZero, bx = kZero, by = kZero;
    for (const Point& v : o.p) {
        bool found = false;
        int inside = 0;
        F px = kZero, py = kZero, depth = kZero;
        for (int k = 0; k < 8; ++k) {
            const Point& p0 = t.p[size_t((k + 7) & 7)];
            const Point& p1 = t.p[size_t(k)];
            const F wx = v.sx - p0.sx, wy = v.sy - p0.sy, ex = p1.sx - p0.sx, ey = p1.sy - p0.sy;
            if ((ex * wy) - (ey * wx) < kZero) break;
            if (!found) {
                const F a = (dcx * wy) - (dcy * wx);
                if (!(a < kZero)) {
                    const F bb = (dcx * (v.sy - p1.sy)) - (dcy * (v.sx - p1.sx));
                    if (!(kZero < bb)) {
                        const F den = a - bb;
                        F s = kZero;
                        if (!(den == kZero)) s = a / den;
                        found = true;
                        py = (s * ey) - wy;
                        px = (s * ex) - wx;
                        depth = (px * dcx) + (py * dcy);
                    }
                }
            }
            ++inside;
        }
        if (inside == 8 && best < depth) best = depth, bx = px, by = py;
    }
    if (kZero < best) {
        by = by * kHalf;
        bx = bx * kHalf;
        shift(o, bx, by);
        shift(t, -bx, -by);
    }
}

// 0x45E9D8: a vertex held on another body's edge keeps the hold while it projects inside the edge and is not outside
// it, both poses then moving half the gap each; projecting inside, while the bodies approach the relative velocity
// loses its part across the edge (half each).
void persist(std::vector<Body>& bd, int i) {
    Body& me = bd[size_t(i)];
    if (!me.enabled) return;
    for (Point& q : me.p) {
        if (!(q.flags & 2)) continue;
        Body& o = bd[size_t(q.other)];
        const Point& p = o.p[size_t(q.edge)];
        const Point& n = o.p[size_t((q.edge + 1) & 7)];
        const F wx = q.sx - p.sx, wy = q.sy - p.sy, ex = n.sx - p.sx, ey = n.sy - p.sy;
        const F inv = kOne / ((ex * ex) + (ey * ey));
        const F u = ((wx * ex) + (wy * ey)) * inv;
        if (u < kZero || kOne < u) {
            q.flags &= ~2u;
            continue;
        }
        if (kZero < (wx * ey) - (wy * ex)) {
            q.flags &= ~2u; // outside the edge: the hold goes, the velocity's part below still runs
        } else {
            const F mx = ((ex * u) - wx) * kHalf, my = ((ey * u) - wy) * kHalf;
            me.sx = me.sx + mx;
            me.sy = me.sy + my;
            o.sx = o.sx - mx;
            o.sy = o.sy - my;
        }
        const F rvx = me.vx - o.vx, rvy = me.vy - o.vy;
        if ((rvx * ey) - (rvy * ex) < kZero) {
            const F f = ((rvx * ex) + (rvy * ey)) * inv;
            const F ax = ((ex * f) - rvx) * kHalf, ay = ((ey * f) - rvy) * kHalf;
            me.vx = me.vx + ax;
            me.vy = me.vy + ay;
            o.vx = o.vx - ax;
            o.vy = o.vy - ay;
        }
    }
}

// 0x45D0E0: a point held on a wall walks to the neighbouring segment it projects onto (one step); then, while the car
// moves into the wall and the point is beyond it, the pose moves the point back onto the line, the velocity becomes
// its projection on the point's wall tangent and the yaw rate loses the moment of that change.
void hold(Body& b, const Walls& walls) {
    const Walls::Point* W = walls.points();
    for (Point& q : b.p) {
        if (!(q.flags & 1)) continue;
        int v = q.wall;
        F dx = q.sx - W[v].x, dy = q.sy + W[v].z;
        F s = (dx * q.tx) + (dy * q.ty);
        int to = -1;
        if (s < kZero) {
            const int pv = walls.prev(v);
            set_tangent(q, W[v].x - W[pv].x, W[pv].z - W[v].z);
            to = pv;
        } else {
            const int n = walls.next(v);
            if (((W[n].x - W[v].x) * q.tx) - ((W[n].z - W[v].z) * q.ty) < s) {
                const int nn = walls.next(n);
                set_tangent(q, W[nn].x - W[n].x, W[n].z - W[nn].z);
                to = n;
            }
        }
        if (to >= 0) {
            v = to;
            q.wall = v;
            dx = q.sx - W[v].x;
            dy = q.sy + W[v].z;
            s = (dx * q.tx) + (dy * q.ty);
        }
        if (!(kZero <= (b.vx * q.ty) - (b.vy * q.tx)) || !(kZero < (dx * q.ty) - (dy * q.tx))) {
            q.flags &= ~1u;
            continue;
        }
        const F mx = (W[v].x + (q.tx * s)) - q.sx;
        const F my = ((q.ty * s) - W[v].z) - q.sy;
        b.sy = b.sy + my;
        b.sx = b.sx + mx;
        const F ovx = b.vx, ovy = b.vy;
        const F dot = (q.tx * b.vx) + (q.ty * b.vy);
        b.vx = dot * q.tx;
        b.vy = dot * q.ty;
        const F dvx = ovx - b.vx, dvy = ovy - b.vy;
        const F rx = q.sx - b.sx, ry = q.sy - b.sy;
        b.w = b.w - ((((rx * dvy) - (ry * dvx)) * b.mass) * b.ii);
    }
}

// 0x45C5A8: the end pose from the start pose and the velocities over the rest of the step.
void end_pose(Body& b) {
    const F f = (b.te - b.ts) * b.dt;
    b.dx = b.vx * f;
    b.dy = b.vy * f;
    b.dz = b.vz * f;
    b.dh = b.w * f;
    b.ex = b.sx + b.dx;
    b.ey = b.sy + b.dy;
    b.ez = b.sz + b.dz;
    b.eh = b.sh + b.dh;
    sweep_box(b);
    end_points(b);
}

// 0x45C648
void reset_hit(Body& b) {
    b.pair = 0;
    b.sensor_t = kTwo;
    b.wpoint = 0;
    b.wcorner = 0;
    b.sensor = 0;
}

// 0x45D8E0: the walls of the box of the body's start and end points, then its earliest point or corner crossing (a
// point first on a tie).
bool wall_sweep(Body& b, const Walls& walls) {
    F xmin = b.p[0].sx, xmax = xmin, ymin = b.p[0].sy, ymax = ymin;
    for (size_t k = 0; k < 8; ++k) {
        Point& q = b.p[k];
        min_max(q.sx, xmin, xmax);
        min_max(q.sy, ymin, ymax);
        min_max(q.ex, xmin, xmax);
        min_max(q.ey, ymin, ymax);
        q.hit = 0;
        q.t = kTwo;
        b.e[k].hit = 0;
        b.e[k].t = kTwo;
    }
    static thread_local std::vector<int> cand;
    cand.clear();
    walls.each(b.hint, xmin, -ymax, xmax, -ymin, cand);
    for (int P : cand) sweep_segment(b, walls, P);
    F best = kTwo;
    int hit = -1;
    for (int k = 0; k < 8; ++k) {
        const Point& q = b.p[size_t(k)];
        if (q.hit && !q.flags && q.t < best) best = q.t, hit = k;
        const Edge& e = b.e[size_t(k)];
        if (e.hit && e.t < best) best = e.t, hit = k + 8;
    }
    if (hit < 0) return false;
    b.t = ((b.te - b.ts) * best) + b.ts;
    b.wall_hit = 1;
    if (hit < 8) {
        const Point& q = b.p[size_t(hit)];
        b.wk = hit;
        b.wpoint = 1;
        b.wseg = q.wall;
        b.ux = q.tx;
        b.uy = q.ty;
        if (hit < 4) b.front = 1;
        else b.rear = 1;
    } else {
        b.wcorner = 1;
        b.we = hit - 8;
        if (b.we < 3) b.front = 1;
        if (unsigned(b.we - 4) < 3) b.rear = 1;
    }
    return true;
}

// 0x45ECB0: B's free vertices against A's edges, in A's frame at the start and at the end pose (each vertex on a
// straight line between them), after a level and box test and the closest approach of the two centres' paths. The
// earliest crossing is A's hit unless A already holds an earlier car or point-wall hit.
bool pair_sweep(std::vector<Body>& bd, int ia, int ib) {
    Body& A = bd[size_t(ia)];
    if (!A.enabled) return false;
    Body& B = bd[size_t(ib)];
    if (!B.enabled) return false;
    if (!same_level(A, B)) return false;
    const F gx = B.bx1 - A.bx0, gy = B.by1 - A.by0;
    if (gx < kZero || gy < kZero) return false;
    if ((B.bw + A.bw) < gx || (B.bh + A.bh) < gy) return false;
    const F dx = B.dx - A.dx, dy = B.dy - A.dy, d2 = (dx * dx) + (dy * dy);
    const F c0x = B.sx - A.sx, c0y = B.sy - A.sy;
    const F f = ((-c0x) * dx) - (c0y * dy);
    F cx = c0x, cy = c0y;
    if (!(f <= kZero)) {
        if (d2 <= f) {
            cx = c0x + dx;
            cy = c0y + dy;
        } else {
            const F q = f / d2;
            cy = c0y + (dy * q);
            cx = c0x + (dx * q);
        }
    }
    if (A.r2[size_t(ib)] < (cx * cx) + (cy * cy)) return false;
    const F dx1 = B.ex - A.ex, dy1 = B.ey - A.ey, rs = B.sh - A.sh, re = B.eh - A.eh;
    const F s0 = ee::sin(-A.sh), c0 = ee::cos(-A.sh), s1 = ee::sin(-A.eh), c1 = ee::cos(-A.eh);
    const F p0y = (s0 * c0x) + (c0 * c0y), p0x = (c0 * c0x) - (s0 * c0y);
    const F p1x = (c1 * dx1) - (s1 * dy1), p1y = (s1 * dx1) + (c1 * dy1);
    const F ss = ee::sin(rs), cs = ee::cos(rs), se = ee::sin(re), ce = ee::cos(re);
    F best = kTwo, cpx, cpy;
    int bv = -1, be = -1;
    for (int j = 0; j < 8; ++j) {
        if (B.p[size_t(j)].flags) continue;
        const Edge& v = B.e[size_t(j)]; // its start is vertex j
        const F x0 = ((cs * v.sx) - (ss * v.sy)) + p0x, y0 = ((ss * v.sx) + (cs * v.sy)) + p0y;
        const F x1 = ((ce * v.sx) - (se * v.sy)) + p1x, y1 = ((se * v.sx) + (ce * v.sy)) + p1y;
        for (int i = 0; i < 8; ++i) {
            const Edge& e = A.e[size_t(i)];
            const F d1 = ((x1 * e.nx) + (y1 * e.ny)) + e.d, d0 = ((x0 * e.nx) + (y0 * e.ny)) + e.d;
            if (kZero < d1 || d0 < kZero) continue;
            const F mx = x1 - x0, my = y1 - y0;
            const F sa = ((e.sx - x0) * my) - ((e.sy - y0) * mx), sb = ((e.ex - x0) * my) - ((e.ey - y0) * mx);
            if (sa < kZero && sb < kZero) continue;
            if (kZero < sa && kZero < sb) continue;
            const F den = d0 - d1;
            F fr = kOne;
            if (!(den == kZero)) fr = d0 / den;
            const F t = ((A.te - A.ts) * fr) + A.ts;
            if (t < kZero || kOne < t) continue;
            if (t < best) {
                best = t;
                bv = j;
                be = i;
                cpx = x0 + (mx * fr);
                cpy = y0 + (my * fr);
            }
        }
    }
    if (bv < 0) return false;
    if ((A.pair || A.wpoint) && A.t < best) return false;
    A.hit_edge = be;
    A.t = best;
    A.other = ib;
    A.vertex = bv;
    A.pair = 1;
    A.cpx = cpx;
    A.cpy = cpy;
    A.car_hit = 1;
    if (be < 3) A.front = 1;
    else if (unsigned(be - 4) < 3) A.rear = 1;
    B.car_hit = 1;
    if (bv < 4) B.front = 1;
    else B.rear = 1;
    return true;
}

// 0x45DB78: a sensor crossing before `t` sets the body's sensor flag.
void sensor_check(Body& b, F t) {
    if (b.sensor && ((b.te - b.ts) * b.sensor_t) + b.ts < t) b.sensor_hit = 1;
}

// 0x45C668: the start pose to sweep time t, along the displacement.
void advance(Body& b, F t) {
    const F span = b.te - b.ts;
    F f = kZero;
    if (!(span == kZero)) f = (t - b.ts) / span;
    b.dx = b.dx * f;
    b.dy = b.dy * f;
    b.dz = b.dz * f;
    b.dh = b.dh * f;
    b.sx = b.sx + b.dx;
    b.sy = b.sy + b.dy;
    b.sz = b.sz + b.dz;
    b.sh = b.sh + b.dh;
    b.ts = t;
    start_points(b);
}

// 0x45E028 (0x45E288 the impulse, 0x45E898 friction, 0x45E450 the correction): point wk on wall wseg (tangent u,
// normal n = (-u.y, u.x)): j = -(1 + e) (v_p . n) / K, the friction, the point held on the wall, J's moment alone.
void point_response(Body& b, const Walls& walls, std::vector<Hit>* hits) {
    const F cs = ee::cos(b.sh), sn = ee::sin(b.sh), msn = -sn;
    const F r[4] = {cs, msn, sn, cs};
    const Edge& e = b.e[size_t(b.wk)]; // starts at point wk
    const F nlx = -e.sx;
    const F rpy = (cs * e.sy) + (msn * nlx), nrx = (sn * e.sy) + (cs * nlx);
    const F vpx = b.vx - (b.w * rpy), vpy = b.vy - (b.w * nrx);
    // 0x45E288
    const F nx = -b.uy, ny = b.ux;
    const F lnx = (r[0] * nx) + (r[2] * ny), lny = (r[1] * nx) + (r[3] * ny);
    const F rwy = (r[0] * e.sy) + (r[1] * nlx), nrwx = (r[2] * e.sy) + (r[3] * nlx);
    const F k = b.im + (((rwy * nx) + (nrwx * ny)) * ((-((e.sx * lny) - (e.sy * lnx))) * b.ii));
    F j = kZero;
    if (!(k == kZero)) j = (-(kWallBounce + kOne) * ((vpx * nx) + (vpy * ny))) / k;
    const F jx = j * nx, jy = j * ny;
    b.strength = b.strength + (neg_if_negative(j) / kWallStrength);
    if (hits) hits->push_back({b.car, 0, b.wk, neg_if_negative(j)});
    record_hit(b, 0, neg_if_negative(j), -1);
    F fx, fy, fz;
    friction(b, jx, jy, vpx, vpy, fx, fy, fz);
    const F jlx = (r[0] * jx) + (r[2] * jy), jly = (r[1] * jx) + (r[3] * jy);
    const F sx = jx + fx, sy = jy + fy;
    correct_point(b, walls);
    b.vx = b.vx + (sx * b.im);
    b.vy = b.vy + (sy * b.im);
    b.p[size_t(b.wk)].flags |= 1;
    vertical(b, fz);
    b.w = b.w + (((e.sx * jly) - (e.sy * jlx)) * b.ii);
}

// 0x45E570 (0x45E768 the impulse, 0x45E898 friction): a wall corner on edge we: n = the edge's normal, the arm from the
// center to the corner, j = -(1 + e) (v_p . n) / K, friction; no hold, no correction.
void corner_response(Body& b, const Walls& walls, std::vector<Hit>* hits) {
    const F cs = ee::cos(b.sh), sn = ee::sin(b.sh);
    const Edge& e = b.e[size_t(b.we)];
    const Walls::Point& W = walls.points()[e.wall];
    const F rx = W.x - b.sx, ry = (-W.z) - b.sy;
    const F nx = (cs * e.nx) + ((-sn) * e.ny), ny = (sn * e.nx) + (cs * e.ny);
    const F vpx = b.vx - (b.w * ry), vpy = b.vy + (b.w * rx);
    const F cr = (rx * ny) - (ry * nx);
    const F k = b.im + ((cr * cr) * b.ii);
    F j = kZero;
    if (!(k == kZero)) j = (-(kWallBounce + kOne) * ((vpx * nx) + (vpy * ny))) / k;
    const F jx = j * nx, jy = j * ny;
    b.strength = b.strength + (neg_if_negative(j) / kWallStrength);
    if (hits) hits->push_back({b.car, 0, b.we + 8, neg_if_negative(j)});
    record_hit(b, 0, neg_if_negative(j), -1);
    F fx, fy, fz;
    friction(b, jx, jy, vpx, vpy, fx, fy, fz);
    const F tq = (rx * jy) - (ry * jx);
    const F sx = jx + fx, sy = jy + fy;
    b.vx = b.vx + (sx * b.im);
    b.vy = b.vy + (sy * b.im);
    b.w = b.w + (tq * b.ii);
    vertical(b, fz);
}

// 0x45F340 (0x45F5F8 the impulse, 0x45F928 the touch-up): B's vertex on A's edge: the impulse, the touch-up, both
// bodies' velocities and moments, B's vertex held on A's edge.
void pair_response(std::vector<Body>& bd, int ia, std::vector<Hit>* hits) {
    Body& A = bd[size_t(ia)];
    Body& B = bd[size_t(A.other)];
    Point& bp = B.p[size_t(A.vertex)];
    const F ca = ee::cos(A.sh), sa = ee::sin(A.sh);
    const F ra[4] = {ca, -sa, sa, ca};
    const F cb = ee::cos(B.sh), sb = ee::sin(B.sh);
    const F rb[4] = {cb, -sb, sb, cb};
    F jx, jy;
    const F j = pair_impulse(A, B, ra, rb, jx, jy);
    const F s = neg_if_negative(j) / kPairStrength;
    A.strength = A.strength + s;
    B.strength = B.strength + s;
    if (hits) {
        const F px = A.sx + ((ra[0] * A.cpx) + (ra[1] * A.cpy)), py = A.sy + ((ra[2] * A.cpx) + (ra[3] * A.cpy));
        hits->push_back({A.car, 1, A.hit_edge + 8, s, B.car, px, py, jx, jy});
        hits->push_back({B.car, 1, A.vertex, s, A.car, px, py, -jx, -jy});
    }
    record_hit(A, 1, s, A.other), record_hit(B, 1, s, ia);
    touch_up(A, B, ra, rb);
    const F jlx = (ra[0] * jx) + (ra[2] * jy), jly = (ra[1] * jx) + (ra[3] * jy);
    A.vy = A.vy + (jy * A.im);
    A.vx = A.vx + (jx * A.im);
    A.w = A.w + (((A.cpx * jly) - (A.cpy * jlx)) * A.ii);
    bp.flags |= 2;
    bp.other = A.car;
    bp.edge = A.hit_edge;
    B.vx = B.vx - (jx * B.im);
    B.vy = B.vy - (jy * B.im);
    const F jbx = (rb[0] * jx) + (rb[2] * jy), jby = (rb[1] * jx) + (rb[3] * jy);
    B.w = B.w - (((bp.lx * jby) - (bp.ly * jbx)) * B.ii);
}

// 0x45CA60: a player's car nearly at rest, touching a wall, whose last hit came early in the step turns by its
// steering: yaw rate += steering x 20 x dt, the end pose again.
void stuck(Body& b, const CarIn& car, F last) {
    if (!car.player) return;
    bool touch = b.wcorner || b.wpoint || b.wall_hit;
    for (size_t k = 0; k < 8 && !touch; ++k) touch = (b.p[k].flags & 1) || b.p[k].hit || b.e[k].x1c || b.e[k].hit;
    if (!(ee::abs(b.vx) < kOne) || !(ee::abs(b.vy) < kOne) || !(last < kEarly) || !touch) return;
    b.w = b.w + ((car.steer * kTwenty) * b.dt);
    end_pose(b);
}

// 0x45DF80: the end-of-step push-out against the walls of the box of end points 0 .. 6.
void push_out(Body& b, const Walls& walls) {
    F xmin = b.p[0].ex, xmax = xmin, ymin = b.p[0].ey, ymax = ymin;
    for (size_t k = 0; k < 7; ++k) {
        min_max(b.p[k].ex, xmin, xmax);
        min_max(b.p[k].ey, ymin, ymax);
    }
    static thread_local std::vector<int> cand;
    cand.clear();
    walls.each(b.hint, xmin, -ymax, xmax, -ymin, cand);
    for (int P : cand) push_segment(b, walls, P);
}

// 0x45C928: the heading less whole turns once it is 8 turns or more from zero.
F wrap_heading(F h) {
    const s32 n = ee::to_int((ee::abs(h) + kPi) / kTwoPi);
    if (n < 8) return h;
    const F nf = ee::int_to_f(n);
    return h < kZero ? h + (nf * kTwoPi) : h + (nf * kMinusTwoPi);
}

// 0x45C9B0
F clamp_yaw(F w) {
    if (ee::abs(w) < kMaxYaw) return w;
    return w < kZero ? kMinYaw : kMaxYaw;
}

// 0x45C9F8
void commit(Body& b) {
    b.cx = b.ex, b.cy = b.ey, b.cz = b.ez;
    b.cvx = b.vx, b.cvy = b.vy, b.cvz = b.vz;
    b.heading = wrap_heading(b.eh);
    b.yaw_rate = clamp_yaw(b.w);
}

// 0x45CC60: the hit sound's hold (60 steps from a stronger hit, its level fading by 0.98 a step otherwise; its call
// 0x350328 with kind -1 does nothing), the strength capped at 1, the pad's rumble (23 steps from a stronger hit, then
// the car's value cleared).
void effects(Body& b, F* rumble) {
    const F s0 = b.strength;
    if (kZero < s0 && (b.sound_steps == 0 || b.sound_level < s0)) {
        b.sound_steps = 60;
        b.sound_level = b.strength;
    } else {
        const s32 n = b.sound_steps - 1;
        b.sound_steps = n < 0 ? 0 : n;
        b.sound_level = b.sound_level * kSoundDecay;
    }
    F s = b.strength;
    if (kOne < s) b.strength = s = kOne;
    if (b.rumble_steps > 0) {
        if (b.rumble_level < s) {
            if (rumble && *rumble < s) *rumble = s, s = b.strength;
            b.rumble_level = s;
            b.rumble_steps = kRumbleSteps;
        } else if ((b.rumble_steps = b.rumble_steps - 1) == 0 && rumble) {
            *rumble = kZero;
        }
    } else if (kZero < s) {
        if (rumble) *rumble = s;
        b.rumble_steps = kRumbleSteps;
        b.rumble_level = b.strength;
    }
}

} // namespace

Walls::Walls(const course::Runway& road, const RoadGround& ground) : ground_(&ground) {
    points_.reserve(road.wall_vertices.size());
    for (const course::Runway::WallVertex& v : road.wall_vertices)
        points_.push_back({F(v.pos.x), F(v.pos.y), F(v.pos.z), s16(u16(v.tag)), u16(v.tag >> 16)});
    index_ = road.wall_indices;
    lists_.reserve(road.triangles.size());
    for (const course::Runway::Triangle& t : road.triangles) lists_.push_back(t.flags);
}

void Walls::each(s32 hint, F x0, F z0, F x1, F z1, std::vector<int>& out) const {
    if (hint < 0) {
        // 0x395A38: the road under the box's middle, from high above.
        GroundHit h;
        const F at[3] = {(x0 + x1) * kHalf, kHigh, (z0 + z1) * kHalf};
        if (!ground_->query(at, h)) return;
        hint = s32(u32(u16(s16(h.cluster))) << 16 | u32(u16(h.triangle)));
    }
    // 0x3E6A58
    if (s16(u32(hint) >> 16) < 0) return;
    const u32 tri = u32(hint) & 0xFFFF;
    if (tri >= lists_.size()) return;
    const u32 word = lists_[tri], first = word & 0xFFFFFF, end = first + (word >> 24);
    auto code = [&](const Point& p) {
        u32 c = 0;
        if (p.x < x0) c |= 1;
        if (x1 < p.x) c |= 2;
        if (p.z < z0) c |= 4;
        if (z1 < p.z) c |= 8;
        return c;
    };
    for (u32 k = first; k < end && k < index_.size(); ++k) {
        const int i = index_[k];
        if (i < 0 || size_t(i) >= points_.size()) continue;
        const u32 a = code(points_[size_t(i)]);
        if (a != 0 && (a & code(points_[size_t(next(i))])) != 0) continue;
        out.push_back(i);
    }
}

void build(Body& b, int car, F front, F rear, F width, F mass, F iz) {
    b = Body{};
    b.car = car;
    b.front_len = front;
    b.rear_len = rear;
    b.half_width = width * kHalf;
    b.chamfer1 = width * kChamfer;
    b.chamfer2 = width * kChamfer;
    b.mass = mass;
    b.iz10 = iz * kInertiaScale;
    const F h = b.half_width, nh = -h, c1 = b.chamfer1, c2 = b.chamfer2, nr = -rear;
    b.im = kOne / b.mass;
    b.ii = kOne / b.iz10;
    const F lx[8] = {h, h - c1, c1 - h, nh, nh, c2 - h, h - c2, h};
    const F ly[8] = {front - c1, front, front, front - c1, c2 - rear, nr, nr, c2 - rear};
    for (size_t k = 0; k < 8; ++k) b.p[k].lx = lx[k], b.p[k].ly = ly[k];
    F r2 = kZero;
    for (size_t k = 0; k < 8; ++k) {
        Point& q = b.p[k];
        const Point& n = b.p[(k + 1) & 7];
        const F l = ee::sqrt((q.lx * q.lx) + (q.ly * q.ly));
        q.ux = q.lx / l;
        q.uy = q.ly / l;
        q.flags = 0;
        q.hit = 0;
        Edge& e = b.e[k];
        e.x1c = 0;
        e.sx = q.lx, e.sy = q.ly, e.ex = n.lx, e.ey = n.ly;
        const F ex = n.lx - q.lx, ey = n.ly - q.ly, nex = -ex;
        const F len = ee::sqrt((ey * ey) + (nex * nex));
        e.nx = ey / len;
        e.ny = nex / len;
        e.d = ((-e.nx) * q.lx) - (e.ny * q.ly);
        const F d = (q.lx * q.lx) + (q.ly * q.ly);
        if (r2 < d) r2 = d;
    }
    b.radius = ee::sqrt(r2);
    b.enabled = 1;
    b.sound_level = kZero;
    b.have_points = 0;
    b.rumble_steps = 0;
    b.rumble_level = kZero;
    b.sound_steps = 0;
}

void pair_tables(std::vector<Body>& bodies) {
    for (Body& b : bodies)
        for (size_t j = 0; j < bodies.size() && j < size_t(kMaxBodies); ++j) {
            b.rsum[j] = b.radius + bodies[j].radius;
            b.r2[j] = b.rsum[j] * b.rsum[j];
        }
}

void step_all(std::vector<Body>& bd, const std::vector<CarIn>& cars, const Walls& walls, std::vector<Hit>* hits) {
    const int n = int(bd.size());
    for (int i = 0; i < n; ++i) {
        init(bd[size_t(i)], cars[size_t(i)].dt);
        enable(bd[size_t(i)], !cars[size_t(i)].ghost);
    }
    F last = kOne;
    for (int round = 0; round < 2;) {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (i != j) separate(bd, i, j);
        for (int i = 0; i < n; ++i) {
            persist(bd, i);
            hold(bd[size_t(i)], walls);
        }
        for (Body& b : bd) end_pose(b);
        int hit = -1;
        F best = kTwo;
        for (int i = 0; i < n; ++i) {
            Body& b = bd[size_t(i)];
            reset_hit(b);
            if (wall_sweep(b, walls) && b.t < best) best = b.t, hit = i + 0x200;
            for (int j = 0; j < n; ++j)
                if (j != i) pair_sweep(bd, i, j);
            if (b.pair && b.t < best) best = b.t, hit = i;
        }
        for (Body& b : bd) sensor_check(b, best);
        if (hit < 0) break;
        last = best;
        for (Body& b : bd) advance(b, best);
        if (hit & 0x200) {
            Body& b = bd[size_t(hit & ~0x200)];
            if (b.wpoint) point_response(b, walls, hits);
            else corner_response(b, walls, hits);
        } else {
            pair_response(bd, hit, hits);
        }
        ++round;
    }
    for (int i = 0; i < n; ++i) {
        Body& b = bd[size_t(i)];
        stuck(b, cars[size_t(i)], last);
        push_out(b, walls);
        commit(b);
        effects(b, cars[size_t(i)].rumble);
    }
}

} // namespace gt4r::sim::collide
