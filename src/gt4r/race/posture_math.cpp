#include "gt4r/race/posture_math.h"

#include <cmath>

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

V3 cross(V3 a, V3 b) {
    // An outer product into the accumulator, then one subtracted from it.
    return {ee::msub(a.y * b.z, b.y, a.z), ee::msub(a.z * b.x, b.z, a.x), ee::msub(a.x * b.y, b.x, a.y)};
}

V3 normalized(V3 v) {
    const F r = ee::rsqrt(f(1), ((v.x * v.x) + (v.y * v.y)) + (v.z * v.z));
    return {v.x * r, v.y * r, v.z * r};
}

F asin(F x) {
    // The C library's: a rational function of x^2 to 0.5, of (1 - |x|) / 2 beyond.
    const u32 mag = x.bits & 0x7FFFFFFF;
    const F pio2 = F::raw(0x3FC90FDA), pio2_lo = F::raw(0x33A22168);
    if (mag == 0x3F800000) return (x * pio2) + (x * pio2_lo);
    if (mag > 0x3F800000) return (x - x) / (x - x);
    const auto series_p = [](F t) {
        return t * ((t * ((t * ((t * ((t * ((t * F::raw(0x3811EF08)) + F::raw(0x3A4F7F04))) + F::raw(0xBD241146))) + F::raw(0x3E4E0AA8))) +
                          F::raw(0xBEA6B08F))) +
                    F::raw(0x3E2AAAAA));
    };
    const auto series_q = [](F t) {
        return (t * ((t * ((t * ((t * F::raw(0x3D9DC62D)) + F::raw(0xBF303360))) + F::raw(0x4001572C))) + F::raw(0xC019D138))) + f(1);
    };
    if (mag <= 0x3EFFFFFF) {
        if (mag <= 0x31FFFFFF) return x;
        const F t = x * x;
        return x + (x * (series_p(t) / series_q(t)));
    }
    const F t = (f(1) - ee::abs(x)) * f(0.5f);
    const F p = series_p(t), q = series_q(t);
    const F s(std::sqrt(t.host())); // the library's bitwise root, to nearest
    F r;
    if (mag > 0x3F799999) {
        const F v = s + (s * (p / q));
        r = pio2 - ((v + v) - pio2_lo);
    } else {
        const F w = F::raw(s.bits & 0xFFFFF000);
        const F c = (t - (w * w)) / (s + w);
        const F pp = ((s + s) * (p / q)) - (pio2_lo - (c + c));
        const F pio4 = F::raw(0x3F490FDA);
        r = pio4 - (pp - (pio4 - (w + w)));
    }
    return s32(x.bits) > 0 ? r : -r;
}

F atan2(F y, F x) {
    if (x == f(0) && y == f(0)) return f(0);
    int s = 0;
    if (y < f(0)) y = -y, s = 4;
    if (x < f(0)) x = -x, s |= 2;
    F a;
    if (y < x) {
        a = ee::atan(y / x);
    } else {
        s |= 1;
        a = ee::atan(x / y);
    }
    if (s & 1) a = f(kHalfPi) - a;
    if (s & 2) a = f(kPi) - a;
    if (s & 4) a = -a;
    return a;
}

F wrap(F angle, F from) {
    const F t = angle - from;
    const F turns = t / f(kTwoPi);
    s32 n = ee::to_int(turns); // the floor (0x422A20)
    if (!(f(0) < turns) && turns - ee::int_to_f(n) != f(0)) --n;
    return (t - (ee::int_to_f(n) * f(kTwoPi))) + from;
}

Quat mul(Quat a, Quat b) {
    const F w = (((-a.x * b.x) - (a.y * b.y)) - (a.z * b.z)) - (a.w * b.w);
    const V3 c = cross({a.x, a.y, a.z}, {b.x, b.y, b.z});
    const auto lane = [&](F av, F bv, F add) { return ee::madd(av * b.w, bv, a.w) + add; };
    return {lane(a.x, b.x, c.x), lane(a.y, b.y, c.y), lane(a.z, b.z, c.z), lane(a.w, b.w, w)};
}

Quat axis_angle(V3 axis, F angle) {
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    return {axis.x * s, axis.y * s, axis.z * s, c};
}

Quat about_x(F angle) {
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    return {s, f(0), f(0), c};
}
Quat about_y(F angle) {
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    return {f(0), s, f(0), c};
}
Quat about_z(F angle) {
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    return {f(0), f(0), s, c};
}

Quat about_y_z(F a, F b) {
    F sa, ca, sb, cb;
    ee::sincos(a * f(0.5f), sa, ca);
    ee::sincos(b * f(0.5f), sb, cb);
    return {sa * sb, sa * cb, ca * sb, ca * cb};
}

Quat turned_x(Quat q, F angle) {
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    return {(s * q.w) + (c * q.x), (c * q.y) - (s * q.z), (s * q.y) + (c * q.z), (c * q.w) - (s * q.x)};
}
Quat turned_y(Quat q, F angle) {
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    return {(c * q.x) + (s * q.z), (s * q.w) + (c * q.y), (-s * q.x) + (c * q.z), (c * q.w) - (s * q.y)};
}

Quat post_x(Quat q, F angle) {
    if (angle == f(0)) return q;
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    const F n = (((q.x * q.x) + (q.y * q.y)) + (q.z * q.z)) + (q.w * q.w);
    const F k = (n + n) - f(1);
    const Quat t{q.w * s, (q.z * k) * s, (-q.y * k) * s, -q.x * s};
    return {(q.x * c) + t.x, (q.y * c) + t.y, (q.z * c) + t.z, (q.w * c) + t.w};
}
Quat post_z(Quat q, F angle) {
    if (angle == f(0)) return q;
    F s, c;
    ee::sincos(angle * f(0.5f), s, c);
    const F n = (((q.x * q.x) + (q.y * q.y)) + (q.z * q.z)) + (q.w * q.w);
    const F k = (n + n) - f(1);
    const Quat t{(q.y * k) * s, (-q.x * k) * s, q.w * s, -q.z * s};
    return {(q.x * c) + t.x, (q.y * c) + t.y, (q.z * c) + t.z, (q.w * c) + t.w};
}

Quat from_euler(F x, F y, F z) {
    F sx, cx, sy, cy, sz, cz;
    ee::sincos(x * f(0.5f), sx, cx);
    ee::sincos(y * f(0.5f), sy, cy);
    ee::sincos(z * f(0.5f), sz, cz);
    const F a = sx * cy, b = cx * cy, c = sx * sy, d = cx * sy;
    return {(a * cz) + (d * sz), (d * cz) - (a * sz), (b * sz) - (c * cz), (b * cz) + (c * sz)};
}

V3 rotate(Quat q, V3 v) {
    const F yy = q.y * q.y, xy = q.x * q.y, zz = q.z * q.z, zw = q.z * q.w, yw = q.y * q.w, xz = q.x * q.z;
    const F xx = q.x * q.x, xw = q.x * q.w, yz = q.y * q.z, h = f(0.5f);
    const F x = ((((h - yy) - zz) * v.x) + ((xy - zw) * v.y)) + ((xz + yw) * v.z);
    const F y = (((xy + zw) * v.x) + (((h - zz) - xx) * v.y)) + ((yz - xw) * v.z);
    const F z = (((xz - yw) * v.x) + ((yz + xw) * v.y)) + (((h - xx) - yy) * v.z);
    return {x + x, y + y, z + z};
}

V3 x_axis(Quat q) {
    const F z = (q.x * q.z) - (q.y * q.w), x = (f(0.5f) - (q.y * q.y)) - (q.z * q.z), y = (q.x * q.y) + (q.z * q.w);
    return {x + x, y + y, z + z};
}
V3 y_axis(Quat q) {
    const F z = (q.y * q.z) + (q.x * q.w), y = (f(0.5f) - (q.z * q.z)) - (q.x * q.x), x = (q.x * q.y) - (q.z * q.w);
    return {x + x, y + y, z + z};
}
V3 z_axis(Quat q) {
    const F y = (q.y * q.z) - (q.x * q.w), z = (f(0.5f) - (q.x * q.x)) - (q.y * q.y), x = (q.x * q.z) + (q.y * q.w);
    return {x + x, y + y, z + z};
}

Quat between(V3 from, V3 to) {
    const F d = dot(from, to);
    V3 axis;
    F angle;
    if (f(0) < d + f(1)) {
        axis = normalized(cross(from, to));
        angle = f(kHalfPi) - asin(clamp(d, f(-1), f(1)));
    } else { // opposite: half a turn about a vector across `to` (0x488748)
        if (!(to.x == f(0))) axis = {-to.y, to.x, f(0)};
        else if (!(to.y == f(0))) axis = {f(0), -to.z, to.y};
        else if (!(to.z == f(0))) axis = {to.z, f(0), -to.x};
        else axis = axis_x();
        angle = f(kPi);
    }
    return axis_angle(axis, angle);
}

Quat align(V3 a, V3 b, V3 to_a, V3 to_b) {
    const V3 n1 = normalized(cross(a, b)), n2 = normalized(cross(to_a, to_b));
    Quat q;
    if (f(-0.5f) < dot(n1, n2)) {
        q = between(n1, n2);
    } else {
        q = axis_angle(a, f(kHalfPi));
        q = mul(between(rotate(q, n1), n2), q);
    }
    const V3 v = rotate(q, a);
    F angle = f(kHalfPi) - asin(clamp(dot(v, to_a), f(-1), f(1)));
    if (dot(n2, cross(v, to_a)) < f(0)) angle = f(kTwoPi) - angle;
    return turned(q, n2, angle);
}

Quat slerp(Quat a, Quat b, F t) {
    if (t == f(0)) return a;
    if (t == f(1)) return b;
    const F d = (((a.x * b.x) + (a.y * b.y)) + (a.z * b.z)) + (a.w * b.w);
    if (d < f(0)) return slerp(a, {-b.x, -b.y, -b.z, -b.w}, t);
    const F angle = f(kHalfPi) - asin(clamp(d, f(-1), f(1)));
    const F s = ee::sin(angle);
    if (s == f(0)) return a;
    const F inv = f(1) / s;
    const F ka = ee::sin(angle * (f(1) - t)) * inv, kb = ee::sin(angle * t) * inv;
    return {(a.x * ka) + (b.x * kb), (a.y * ka) + (b.y * kb), (a.z * ka) + (b.z * kb), (a.w * ka) + (b.w * kb)};
}

Mat3 matrix(Quat q) {
    const F z2 = q.z + q.z, y2 = q.y + q.y, x2 = q.x + q.x;
    const F zz = z2 * q.z, yy = y2 * q.y, xx = x2 * q.x, xw = x2 * q.w, zw = z2 * q.w, xy = x2 * q.y, yw = y2 * q.w, xz = x2 * q.z, yz = y2 * q.z;
    const F one = f(1);
    return {{(one - yy) - zz, xy + zw, xz - yw, xy - zw, (one - zz) - xx, yz + xw, xz + yw, yz - xw, (one - xx) - yy}};
}

V3 transform(const Mat3& m, V3 v) {
    return {((m.m[0] * v.x) + (m.m[3] * v.y)) + (m.m[6] * v.z), ((m.m[1] * v.x) + (m.m[4] * v.y)) + (m.m[7] * v.z),
            ((m.m[2] * v.x) + (m.m[5] * v.y)) + (m.m[8] * v.z)};
}

Mat3 transposed(const Mat3& m) { return {{m.m[0], m.m[3], m.m[6], m.m[1], m.m[4], m.m[7], m.m[2], m.m[5], m.m[8]}}; }

V3 without(V3 v, V3 n) { return v - n * dot(v, n); }

F angle_between(V3 a, V3 b) { return f(kHalfPi) - asin(clamp(dot(a, b), f(-1), f(1))); }

F signed_angle(V3 a, V3 b, V3 axis) {
    const F angle = angle_between(a, b);
    return dot(b, cross(axis, a)) < f(0) ? -angle : angle;
}

F signed_angle_about(V3 a, V3 b, V3 axis) {
    const V3 pa = without(a, axis), pb = without(b, axis);
    const F la = dot(pa, pa), lb = dot(pb, pb);
    if (la == f(0) || lb == f(0)) return f(0);
    return signed_angle(pa * ee::rsqrt(f(1), la), pb * ee::rsqrt(f(1), lb), axis);
}

V3 turned_towards(V3 a, V3 b, const V3* fallback, F part) {
    V3 axis = cross(a, b);
    const F len = ee::sqrt(dot(axis, axis));
    F angle;
    if (len <= f(0.000999999931f)) {
        if (f(0) < dot(a, b)) return a;
        axis = fallback ? *fallback : V3{-a.y, a.x, f(0)};
        angle = f(kPi);
    } else {
        axis = axis * (f(1) / len);
        angle = asin(clamp(len, f(-1), f(1)));
    }
    return rotate(axis_angle(axis, angle * part), a);
}

F triangle_angle(F a, F b, F c) {
    if (a + b < c || a * b == f(0)) return f(0);
    return f(kHalfPi) - asin((((a * a) + (b * b)) - (c * c)) / ((a + a) * b));
}

} // namespace gt4r::race::posture
