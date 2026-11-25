#include "gt4r/race/posture_solver.h"

#include "gt4r/race/posture_hands.h"

namespace gt4r::race::posture {

namespace ee = gt3r::ee;

const char* const kNodeNames[kNodes] = {"PELVIS",        "LTHIGH_BONE",  "LSHANK_BONE", "LFOOT_BONE", "RTHIGH_BONE",
                                        "RSHANK_BONE",   "RFOOT_BONE",   "LOWER_SPINE", "MIDDLE_SPINE", "SCAPULA",
                                        "LUPARM_BONE",   "LFOREARM_BONE", "RUPARM_BONE", "RFOREARM_BONE", "CRANIUM",
                                        "LCLAVICLE",     "RCLAVICLE",    "LHAND_BONE",  "RHAND_BONE", "NECK_BONE"};

V3 bone_end(const Bone& b) { return rotate(b.q, b.vec) + b.pos; }
V3 bone_direction(const Bone& b) { return rotate(b.q, b.vec) * b.inv; }

namespace {

constexpr float kElbowLeast = 5.0f; // the least bend of an elbow or a knee, degrees

// A bone's frame as the solver applies it to a point: the matrix of its turn, then its place (0x4187A0).
struct Frame {
    V3 pos;
    Mat3 m;
};
Frame frame_of(V3 pos, Quat q) { return {pos, matrix(q)}; }
V3 apply(const Frame& fr, V3 v) { return transform(fr.m, v) + fr.pos; }

void set_vec(Bone& b, V3 from, V3 to) { // 0x41B778
    b.vec = to - from;
    b.len = ee::sqrt(dot(b.vec, b.vec));
    b.inv = b.len == f(0) ? f(0) : f(1) / b.len;
}

// Two bones from `start` towards `target` (0x420510): the bend lies across `side`; `a` and `b` are the first bone's axes
// that turn along the line and across it. Near full reach (`soft` before it) the line shortens.
void two_bones(Bone& one, Bone& two, V3 side, V3 start, V3 target, V3 a, V3 b, F len1, F len2, F soft, F least) {
    V3 d = target - start;
    F len = ee::sqrt(dot(d, d));
    if (len < f(9.99999975e-06f)) len = f(9.99999975e-06f);
    const F sum = len1 + len2, diff = ee::abs(len1 - len2);
    if (!(sum + soft <= len)) {
        const auto scaled = [&](F to) {
            d = (d * to) * (f(1) / len);
            len = to;
        };
        if (len < diff) {
            scaled(diff);
        } else {
            const F reach = sum - soft;
            if (!(len <= reach)) scaled(reach + (((len - sum) + soft) * f(0.5f)));
        }
    }
    const F ll = len * len, aa = len1 * len1, bb = len2 * len2;
    const F at_start = f(kHalfPi) - asin(clamp(((ll + aa) - bb) / ((len + len) * len1), f(-1), f(1)));
    F bend = f(kHalfPi) - asin(clamp(((aa + bb) - ll) / ((len1 + len1) * len2), f(-1), f(1)));
    if (bend < least) bend = least;
    const V3 along = d * (f(1) / len);
    V3 n = normalized(cross(along, side));
    const V3 across = rotate(axis_angle(n, f(kHalfPi)), along);
    Quat q = turned(align(a, b, along, across), n, at_start);
    one.pos = start;
    one.q = q;
    const V3 end = bone_end(one);
    n = normalized(cross(end - start, along));
    two.pos = end;
    two.q = turned(q, n, f(kPi) - bend);
}

// The angles of three bones a, b, c that span d, the outer two alike (0x420190): at each end and between.
void chain_angles(F& o0, F& o1, F& o2, F& o3, F a, F b, F c, F d) {
    const F bb = b * b, s = a + c, t = s * f(-2);
    const F disc = ((bb * t) * t) - (((a * f(16)) * c) * (((((a * a) + bb) + (c * c)) - (d * d)) - ((a + a) * c)));
    if (disc < f(0)) {
        if (b < s) {
            o0 = o1 = f(kHalfPi) - asin(b / s);
            o2 = f(4.71238852f) - o0;
            o3 = f(kHalfPi) - o0;
        } else {
            o0 = o1 = f(0);
            o2 = o3 = f(kPi);
        }
        return;
    }
    const F root = ee::sqrt(disc), m = -b * t, den = (a * f(8)) * c;
    const F x1 = (m + root) / den, x2 = (m - root) / den, lo = f(-1), hi = f(1);
    const bool in1 = lo <= x1 && x1 <= hi, in2 = lo <= x2 && x2 <= hi;
    F x;
    if (in1 && (x2 < lo || hi < x2)) x = x1;
    else if (in2 && (x1 < lo || hi < x1)) x = x2;
    else if (in1 && in2) x = ee::min(x1, x2);
    else x = f(-1);
    o0 = o1 = ee::abs(f(kHalfPi) - asin(x));
    const F sn = ee::sin(o0);
    F vx = b - (s * x), vy = (a - c) * sn;
    const F r = ee::rsqrt(f(1), (vx * vx) + (vy * vy));
    vx = vx * r, vy = vy * r;
    const F wx = -x, wy = sn;
    o3 = ee::abs(f(kHalfPi) - asin(clamp((vx * wx) + (vy * wy), f(-1), f(1)))); // 0x4234D8
    o2 = ((f(kTwoPi) - o0) - o1) - o3;
}

// The spine's three bones from `start` to `target`, bent across `side` (0x420AA8).
void chain(Bone& one, Bone& two, Bone& three, V3 side, V3 start, V3 target, V3 a, V3 b, F la, F lb, F lc) {
    const V3 d = target - start;
    const F len = ee::sqrt(dot(d, d));
    const F inv = f(1) / len;
    F o0, o1, o2, o3;
    chain_angles(o0, o1, o2, o3, la, lb, lc, len);
    const V3 along = d * inv;
    V3 n = normalized(cross(along, side));
    const V3 across = rotate(axis_angle(n, f(kHalfPi)), along);
    Quat q = turned(align(a, b, along, across), n, o3);
    one.pos = start;
    one.q = q;
    const V3 e1 = bone_end(one);
    n = normalized(cross(normalized(e1 - start), along));
    q = turned(q, n, f(kPi) - o0);
    two.pos = e1;
    two.q = q;
    const V3 e2 = bone_end(two);
    q = turned(q, n, f(kPi) - o1);
    three.pos = e2;
    three.q = q;
}

void solve_spine(const Controls& c, Skeleton& s, const Target& t, const V3* gaze) { // 0x41CB20
    Bone &pelvis = s[kPelvis], &low = s[kLowerSpine], &mid = s[kMiddleSpine], &scap = s[kScapula];
    pelvis.pos = t.pelvis;
    pelvis.q = turned(t.turn, z_axis(t.turn), -c.pelvis_roll);
    chain(low, mid, scap, t.chest_forward, bone_end(pelvis), t.chest, axis_y(), -axis_z(), low.len, mid.len, scap.len);
    if (!c.no_twist) {
        const F from = c.pelvis_twist, to = c.twist;
        const F rate = (to - from) / (pelvis.len + low.len);
        pelvis.q = turned(pelvis.q, bone_direction(pelvis), from);
        low.q = turned(low.q, bone_direction(low), from + (rate * pelvis.len));
        mid.q = turned(mid.q, bone_direction(mid), to - (rate * low.len));
        scap.q = turned(scap.q, bone_direction(scap), to);
        if (gaze) {
            V3 v = *gaze - scap.pos;
            v.y = f(0);
            const F dd = ((v.x * v.x) + v.y) + (v.z * v.z);
            if (f(9.99999975e-05f) < dd) {
                const V3 forward = rotate(scap.q, axis_z());
                v = v * ee::rsqrt(f(1), dd);
                const V3 up = bone_direction(scap);
                F angle = signed_angle_about(forward, v, up) * f(0.25f);
                if (up.y < f(0.199999988f)) angle = angle * clamp((f(5) * up.y) + (f(-5) * v.y), f(0), f(1));
                angle = clamp(angle, f(-0.34906581f), f(0.34906581f)) * c.gaze_twist;
                scap.q = turned(scap.q, bone_direction(scap), angle);
                mid.q = turned(mid.q, bone_direction(mid), angle * f(0.5f));
            }
        }
    }
    const Quat q = scap.q;
    const F r = ee::rsqrt(f(1), (((q.x * q.x) + (q.y * q.y)) + (q.z * q.z)) + (q.w * q.w));
    scap.q = {q.x * r, q.y * r, q.z * r, q.w * r};
}

// The vector an arm's bend lies across, from the shoulder to the hand (0x41C6F8).
V3 arm_side(const Bone& clavicle, V3 axis, V3 shoulder, V3 hand, F elbow, F palm) {
    const Mat3 to_local = matrix(conjugate(clavicle.q));
    const V3 d = normalized(transform(to_local, hand - shoulder));
    V3 n = f(0) < d.z ? V3{f(0), -d.z, d.y} : V3{-d.z, f(0), d.y};
    const F nn = dot(n, n);
    if (f(0) < nn) n = n * ee::rsqrt(f(1), nn);
    F part = elbow * clamp(d.z + f(0.5f), f(0), f(1));
    if (f(0) < palm) part = clamp(ee::max(part, (palm * f(0.75f)) / f(kHalfPi)), f(0), f(1));
    n = turned_towards(n, axis, nullptr, part);
    n = turned_towards(n, -axis_y(), &axis, dot(d, axis));
    if (f(0.98999995f) < ee::abs(dot(n, d))) n = d;
    const F mm = dot(n, n);
    if (f(0) < mm) n = n * ee::rsqrt(f(1), mm);
    return transform(transposed(to_local), n);
}

// Turns a hand about its tip so its palm faces `palm` (and its fingers `finger`), by `part` (0x41C458).
void turn_hand(Bone& hand, const Bone& forearm, V3 palm, V3 finger, F angle, F part) {
    const V3 tip = bone_end(hand), place = hand.pos;
    const Quat from = forearm.q;
    Quat q = from;
    const V3 back = -z_axis(q);
    if (finger.x == f(0) && finger.y == f(0) && finger.z == f(0)) q = mul(between(back, palm), q);
    else q = mul(align(back, y_axis(q), palm, finger), q);
    q = slerp(from, q, part);
    q = turned(q, -z_axis(q), angle);
    hand.q = q;
    hand.pos = place + (tip - bone_end(hand));
}

void solve_arms(Controls& c, Skeleton& s, const Target& t) { // 0x41D140
    Bone& scap = s[kScapula];
    const Frame chest = frame_of(scap.pos, scap.q);
    static constexpr int kClavicle[2] = {kLClavicle, kRClavicle}, kUpper[2] = {kLUpperArm, kRUpperArm}, kFore[2] = {kLForearm, kRForearm},
                         kHand[2] = {kLHand, kRHand};
    const V3 axis[2] = {axis_x(), -axis_x()};
    s[kLClavicle].pos = apply(chest, scap.left);
    s[kRClavicle].pos = apply(chest, scap.right);
    if (c.auto_clavicle) {
        const V3 reach[2] = {{s[kLClavicle].len, f(0), f(0)}, {-s[kRClavicle].len, f(0), f(0)}};
        const V3 joint[2] = {scap.left, scap.right};
        for (int k = 0; k < 2; ++k) {
            const V3 d = t.hand[k] - apply(chest, joint[k] + reach[k]);
            const F sum = (s[kUpper[k]].len + s[kFore[k]].len) + s[kHand[k]].len;
            const F ratio = clamp(dot(d, d) / (sum * sum), f(0), f(1));
            F far = f(0);
            if (!(ratio < f(0.5f))) far = (ratio - f(0.5f)) + (ratio - f(0.5f));
            const F range = f(0.314159214f) - f(0);
            c.clavicle[k] = clamp(((far + ((c.elbow[k] - f(0.5f)) * f(0.299999982f))) + ((c.grip_side[k] - f(0.5f)) * f(0.049999997f))) * range,
                                  f(0), f(0.314159214f));
        }
    }
    const Quat turn[2] = {about_y_z(-c.clavicle[0], c.clavicle_forward[0]), about_y_z(c.clavicle[1], -c.clavicle_forward[1])};
    V3 shoulder[2], side[2];
    for (int k = 0; k < 2; ++k) {
        Bone& clav = s[kClavicle[k]];
        clav.q = mul(scap.q, turn[k]);
        if (!c.hand_raised[k]) {
            const V3 v = rotate(conjugate(scap.q), t.palm[k]);
            clav.q = post_z(clav.q, clamp(v.y, f(0), f(1)) * f(k ? -0.314159214f : 0.314159214f));
        }
        shoulder[k] = bone_end(clav);
    }
    const V3 up = rotate(t.turn, axis_y());
    for (int k = 0; k < 2; ++k)
        side[k] = arm_side(s[kClavicle[k]], axis[k], shoulder[k], t.hand[k], c.elbow[k], f(kHalfPi) - angle_between(up, t.palm[k]));
    const F least = radians(f(kElbowLeast));
    const V3 down = -axis_y();
    for (int k = 0; k < 2; ++k)
        two_bones(s[kUpper[k]], s[kFore[k]], side[k], shoulder[k], t.hand[k], axis[k], down, s[kUpper[k]].len, s[kFore[k]].len + s[kHand[k]].len,
                  f(0), least);
    for (int k = 0; k < 2; ++k) {
        s[kHand[k]].pos = bone_end(s[kFore[k]]);
        s[kHand[k]].q = s[kFore[k]].q;
    }
    if (c.plain_hands) {
        F angle[2];
        for (int k = 0; k < 2; ++k) angle[k] = signed_angle_about(-z_axis(s[kHand[k]].q), t.palm[k], bone_direction(s[kHand[k]]));
        const auto turn_bone = [](Bone& b, F a) { b.q = turned(b.q, bone_direction(b), a); }; // 0x41BAC0
        for (int k = 0; k < 2; ++k) turn_bone(s[kHand[k]], angle[k] * f(0.799999952f));
        for (int k = 0; k < 2; ++k) turn_bone(s[kFore[k]], angle[k] * f(0.199999988f));
        return;
    }
    const F roll[2] = {(c.grip_side[0] - f(0.5f)) * f(kHalfPi), (f(0.5f) - c.grip_side[1]) * f(kHalfPi)};
    const auto reach_hand = [&](int k, V3 target) {
        two_bones(s[kUpper[k]], s[kFore[k]], side[k], shoulder[k], target, axis[k], down, s[kUpper[k]].len, s[kFore[k]].len, f(0.0199999996f), least);
        s[kHand[k]].pos = bone_end(s[kFore[k]]);
    };
    for (int k = 0; k < 2; ++k) {
        if (!(f(0) < c.hand_turn[k])) continue;
        turn_hand(s[kHand[k]], s[kFore[k]], t.palm[k], t.finger[k], -roll[k], c.hand_turn[k]);
        reach_hand(k, s[kHand[k]].pos);
    }
    for (int k = 0; k < 2; ++k) {
        if (c.keep_hands == 1) break;
        Bone& hand = s[kHand[k]];
        const V3 tip = bone_end(hand);
        hand.q = mul(between(bone_direction(hand), bone_direction(s[kFore[k]])), hand.q);
        hand.q = mul(axis_angle(z_axis(hand.q), roll[k]), hand.q);
        reach_hand(k, (tip - bone_end(hand)) + hand.pos);
    }
    for (int k = 0; k < 2; ++k)
        if (!(c.wrist[k] == f(0))) s[kHand[k]].q = turned(s[kHand[k]].q, bone_direction(s[kHand[k]]), k ? -c.wrist[k] : c.wrist[k]);
    // The hand's turn against the forearm is shared out along the arm.
    for (int k = 0; k < 2; ++k) {
        Bone &hand = s[kHand[k]], &fore = s[kFore[k]], &upper = s[kUpper[k]];
        const V3 along_fore = bone_direction(fore), along_upper = bone_direction(upper);
        F angle = signed_angle_about(y_axis(fore.q), y_axis(hand.q), along_fore);
        if (c.hand_raised[k]) {
            if (k == 0 && f(kHalfPi) < angle) angle = angle - f(kTwoPi);
            if (k == 1 && angle < f(-kHalfPi)) angle = angle + f(kTwoPi);
        }
        fore.q = turned(fore.q, along_fore, angle * f(k ? 0.665999949f : 0.699999988f));
        upper.q = turned(upper.q, along_upper, angle * f(k ? 0.332999974f : 0.25f));
    }
}

void solve_legs(const Controls& c, Skeleton& s, const Target& t) { // 0x41E678
    Bone& pelvis = s[kPelvis];
    static constexpr int kThigh[2] = {kLThigh, kRThigh}, kShank[2] = {kLShank, kRShank}, kFoot[2] = {kLFoot, kRFoot};
    const Mat3 m = matrix(pelvis.q);
    const Mat3 inverse = transposed(m);
    const V3 x{m.m[0], m.m[1], m.m[2]};
    const V3 joint[2] = {pelvis.left, pelvis.right};
    const V3 down = -axis_y();
    V3 hip[2], side[2];
    for (int k = 0; k < 2; ++k) {
        // The joint by a 4 x 4 matrix on the VU0 (0x48EB10).
        const V3 j = joint[k];
        const auto lane = [&](int i, F place) {
            return ee::madd(ee::madd(ee::madd(m.m[i] * j.x, m.m[3 + i], j.y), m.m[6 + i], j.z), place, f(1));
        };
        hip[k] = {lane(0, pelvis.pos.x), lane(1, pelvis.pos.y), lane(2, pelvis.pos.z)};
    }
    const Frame frame = {pelvis.pos, m};
    V3 d[2];
    for (int k = 0; k < 2; ++k) d[k] = normalized(transform(inverse, t.foot[k] - apply(frame, joint[k])));
    for (int k = 0; k < 2; ++k) {
        Quat q = about_y((c.knee[k] * f(k ? -0.5f : 0.5f)) * f(kPi));
        q = mul(between(down, d[k]), q);
        side[k] = transform(m, z_axis(q));
    }
    const F least = radians(f(kElbowLeast));
    for (int k = 0; k < 2; ++k)
        two_bones(s[kThigh[k]], s[kShank[k]], side[k], hip[k], t.foot[k], down, axis_z(), s[kThigh[k]].len, s[kShank[k]].len, f(0), least);
    const V3 out[2] = {x, -x};
    F lean[2];
    for (int k = 0; k < 2; ++k) {
        const F a = asin(clamp(dot(z_axis(s[kShank[k]].q), out[k]), f(-1), f(1)));
        lean[k] = f(kHalfPi) - (f(kHalfPi) - a);
        if (f(1.25663686f) < lean[k]) lean[k] = f(1.25663686f);
        else if (lean[k] < f(0)) lean[k] = f(0);
    }
    for (int k = 0; k < 2; ++k) {
        Bone &shank = s[kShank[k]], &foot = s[kFoot[k]];
        F a = lean[k] * (c.shank_in[k] * f(0.5f));
        if (k) a = -a;
        const Quat turn = axis_angle(bone_direction(shank), a);
        shank.q = mul(turn, shank.q);
        foot.pos = bone_end(shank);
        foot.q = mul(turn, shank.q);
    }
    for (int k = 0; k < 2; ++k) {
        Bone& foot = s[kFoot[k]];
        foot.q = turned(foot.q, bone_direction(foot), (c.heel_roll[k] - f(0.5f)) * f(k ? -1.25663686f : 1.25663686f));
        const V3 ax = x_axis(foot.q);
        foot.q = k ? turned(foot.q, -ax, -c.heel_tilt[k]) : turned(foot.q, ax, c.heel_tilt[k]);
    }
}

// The head's angles are kept within these.
constexpr float kHeadLimit = 1.41371655f;

void set_head_yaw(Controls& c, F angle, bool clamp_it, F range) { // 0x41F9C0
    const F w = wrap(angle, f(-kPi)), lim = range * f(kHeadLimit);
    if (lim <= w) {
        if (clamp_it) c.head[1] = lim;
    } else if (w <= -lim) {
        if (clamp_it) c.head[1] = -lim;
    } else {
        c.head[1] = w;
    }
}
void set_head_pitch(Controls& c, F angle) { // 0x41FA68
    const F w = wrap(angle, f(-kPi));
    if (f(kHeadLimit) <= w) c.head[0] = f(kHeadLimit);
    else if (w <= f(-kHeadLimit)) c.head[0] = f(-kHeadLimit);
    else c.head[0] = w;
}

// The pitch and yaw towards a unit vector in the neck's frame, no more than the limit off ahead; the vector used (0x41FC00).
V3 look_towards(Controls& c, V3 v, F range) {
    V3 out = v;
    if (v.z < ee::cos(f(kHeadLimit))) {
        const V3 axis = cross(v, axis_z());
        const F dd = dot(axis, axis);
        if (dd == f(0)) {
            out = {f(0), ee::sin(f(kHeadLimit)), f(0)};
        } else {
            const F r = ee::rsqrt(f(1), dd);
            const F angle = f(kHalfPi) - asin(clamp(v.z, f(-1), f(1)));
            out = rotate(axis_angle(axis * r, angle - f(kHeadLimit)), v);
        }
    }
    if ((out.x * out.x) + (out.z * out.z) == f(0)) set_head_yaw(c, f(0), false, f(1));
    else set_head_yaw(c, posture::atan2(out.x, out.z), true, range);
    set_head_pitch(c, asin(clamp(out.y, f(-1), f(1))));
    return out;
}

// The roll that keeps the head's top towards `up` about the direction looked in (0x41FEA0).
void roll_head(Controls& c, V3 up, V3 ahead, F part, F extra) {
    const F upright = clamp((dot(up, ahead) * f(9.99999714f)) + f(9.49999714f), f(0), f(1));
    const V3 y = y_axis(turned_x(about_y(c.head[1]), -c.head[0])); // 0x41FB90
    const V3 p = without(up, ahead);
    const F dd = dot(p, p);
    F& roll = c.head[2];
    if (dd == f(0)) {
        roll = f(0);
    } else {
        const F a = signed_angle(y, p * ee::rsqrt(f(1), dd), ahead);
        if (a < f(-2.35619426f)) roll = clamp((a * f(2.12206602f)) + f(5.99999952f), f(0), f(1)) * f(-0.471238822f);
        else if (f(2.35619426f) < a) roll = clamp((a * f(-2.12206602f)) + f(5.99999952f), f(0), f(1)) * f(0.471238822f);
        else roll = clamp(a, f(-0.471238822f), f(0.471238822f));
    }
    roll = roll * (upright * part);
    roll = roll + extra;
}

void solve_head(Controls& c, Skeleton& s, const V3* gaze) { // 0x41EFE0
    Bone &scap = s[kScapula], &neck = s[kNeck], &head = s[kCranium];
    neck.pos = bone_end(scap);
    neck.q = scap.q;
    head.pos = bone_end(neck);
    if (gaze) {
        const V3 v = *gaze - head.pos;
        const F dd = dot(v, v);
        if (!(dd == f(0))) {
            const V3 u = v * ee::rsqrt(f(1), dd);
            c.nod = clamp((dot(bone_direction(neck), u) * f(0.5f)) + f(0.5f), f(0), f(1));
        }
    }
    neck.q = turned(neck.q, -x_axis(scap.q), ((c.nod - f(0.5f)) * f(0.25f)) * f(kPi));
    head.pos = bone_end(neck);
    const Quat back = conjugate(neck.q);
    if (gaze) {
        const V3 v = *gaze - head.pos;
        const F dd = dot(v, v);
        if (!(dd == f(0))) {
            const V3 u = rotate(back, v) * ee::rsqrt(f(1), dd);
            const V3 ahead = look_towards(c, u, c.head_yaw_range);
            roll_head(c, rotate(back, c.head_up), ahead, c.head_up_part, c.head_roll);
        }
    } else {
        const V3 ahead = z_axis(post_x(neck.q, -c.head_pitch));
        roll_head(c, transform(matrix(back), c.head_up), ahead, c.head_up_part, c.head_roll);
    }
    set_head_pitch(c, c.head[0] - c.head_pitch);
    const Quat turn = turned_x(turned_y(about_z(c.head[2]), c.head[1]), -c.head[0]); // 0x41FB08
    head.q = mul(neck.q, turn);
    if (!c.keep_neck) neck.q = turned(neck.q, bone_direction(neck), wrap(c.head[1], f(-kHalfPi)));
}

// How far the body leans back for a spine of this length between the seat and the chest (0x417110).
F recline_for(const Controls& c, const Skeleton& s, F d) {
    const F a = s[kLowerSpine].len, b = s[kMiddleSpine].len, e = s[kScapula].len;
    const F bb = b * b, t = (a + e) * f(-2);
    const F root = ee::sqrt(((bb * t) * t) - (((a * f(16)) * e) * (((((a * a) + bb) + (e * e)) - (d * d)) - ((a + a) * e))));
    const F inv = f(1) / ((a * f(8)) * e), m = -b * t;
    const F x1 = (m - root) * inv, x2 = (m + root) * inv, lo = f(-1), hi = f(1);
    const bool in1 = lo <= x1 && x1 <= hi, in2 = lo <= x2 && x2 <= hi;
    F x;
    if (in1 && (x2 < lo || hi < x2)) x = x1;
    else if (in2 && (x1 < lo || hi < x1)) x = x2;
    else if (in1 && in2) x = ee::min(x1, x2);
    else return c.recline;
    const F angle = ee::abs(f(kHalfPi) - asin(x));
    if (!(f(kHalfPi) < angle)) return c.recline;
    const F more = (b * f(0.5f)) / ee::cos(f(kPi) - angle);
    const F y = triangle_angle(e + more, d, a + more);
    return f(0) < y ? c.recline - y : c.recline;
}

F phase_blend(const Controls& c) { // 0x416EB0
    const F p = c.phase;
    if (f(0) <= p && p <= f(1)) return (c.phase_steer[0] * (f(1) - p)) + (c.phase_steer[1] * p);
    if (p <= f(2)) {
        const F q = p - f(1);
        return (c.phase_steer[1] * (f(1) - q)) + (c.phase_steer[2] * q);
    }
    if (p <= f(3)) {
        const F q = p - f(2);
        return (c.phase_steer[2] * (f(1) - q)) + (c.phase_steer[0] * q);
    }
    return f(0);
}

// The hands' solvers as the controls choose them (the first half of 0x4173B0).
void solve_hands(Controls& c, const Places& p, HandSolve (&h)[2]) {
    const F blend = phase_blend(c);
    h[0].side = 0;
    h[1].side = 1;
    if (!c.arms) {
        c.keep_hands = 0;
        for (HandSolve& x : h) easy_hand(x, p);
    } else if (p.handlebars == 1) {
        c.keep_hands = p.handlebars;
        for (HandSolve& x : h) fixed_hand(x, p);
    } else if (c.free_hands) {
        for (HandSolve& x : h) free_hand(x, p);
    } else {
        V3 offset{};
        F out{}, along{}, right{}, left{};
        const F ph = c.phase;
        int from = -1;
        F part{};
        if (f(0) <= ph && ph <= f(1)) from = 0, part = ph;
        else if (ph <= f(2)) from = 1, part = ph - f(1);
        else if (ph <= f(3)) from = 2, part = ph - f(2);
        if (from >= 0) {
            const int to = (from + 1) % 3;
            const F rest = f(1) - part;
            offset = (c.gesture_offset[from] * rest) + (c.gesture_offset[to] * part);
            out = (c.gesture_grip[from][0] * rest) + (c.gesture_grip[to][0] * part);
            along = (c.gesture_grip[from][1] * rest) + (c.gesture_grip[to][1] * part);
            left = ((c.wrist_phase[from][0] * rest) + (c.wrist_phase[to][0] * part)) - c.wrist_base[0];
            right = ((c.wrist_phase[from][1] * rest) + (c.wrist_phase[to][1] * part)) - c.wrist_base[1];
        }
        c.keep_hands = 0;
        for (int k = 0; k < 2; ++k) {
            if (shifts(p, k)) {
                h[k].offset = offset;
                h[k].weight[0] = out;
                h[k].weight[1] = along;
                h[k].out = c.grip_out[k];
                h[k].back = c.grip_back[k];
            }
            h[k].wrist_add = k ? right : left;
            h[k].grip_kind = c.grip_kind;
        }
        int raised[2];
        for (int k = 0; k < 2; ++k) raised[k] = single_hand(h[k], c.grip_from[k], c.grip_to[k], p, c.grip_blend[k], blend);
        h[0].raised = raised[0];
        h[1].raised = raised[1];
    }
    for (int k = 0; k < 2; ++k) {
        int r = h[k].raised;
        if (c.raise_choice[k] == 1) r = 1;
        else if (c.raise_choice[k] == 2) r = 0;
        c.hand_raised[k] = r;
    }
}

// The target from the places and controls (0x4173B0).
void make_target(Controls& c, const Skeleton& s, const Places& p, Target& t) {
    t.turn = p.turn;
    HandSolve h[2];
    solve_hands(c, p, h);
    F lean = c.lean;
    if (!c.raw_lean) {
        const F least = f(-0.785398126f);
        lean = ((((lean - least) / (f(0.785398126f) - least)) - f(0.5f)) * f(0.149999991f)) * f(kPi);
    }
    F length = ((s[kPelvis].len + s[kLowerSpine].len) + s[kMiddleSpine].len) + s[kScapula].len;
    length = (length + c.spine_length) * (f(1) - (ee::cos(c.recline) * f(0.049999997f)));
    const V3 seat = p.seat;
    V3 foot[2] = {p.foot[0] + c.foot_offset[0], p.foot[1] + c.foot_offset[1]};
    V3 finger[2] = {h[0].finger, h[1].finger};
    const V3 hand[2] = {h[0].pos, h[1].pos};
    const F angle = c.fixed_recline ? c.recline : recline_for(c, s, length - s[kPelvis].len);
    F sn, cs;
    ee::sincos(angle, sn, cs);
    V3 chest{f(0), length * cs, length * sn};
    const V3 forward{f(0), sn, -cs};
    chest = rotate(axis_angle({f(0), -sn, cs}, lean), chest);
    chest = (chest + seat) + c.chest_offset;
    const F zero = f(0), one = f(1);
    if (c.arms) {
        // The chest turns and the shoulders come forward towards a far hand.
        const auto near_part = [&](F d, F reach) {
            const F r = zero - reach, inv = one / r;
            return clamp((inv * d) + (inv * r), zero, one);
        };
        const F turn_l = near_part(hand[0].x - chest.x, s[kLClavicle].len) * f(0.235619411f);
        const F near_l =
            near_part(hand[0].z - chest.z, (s[kLUpperArm].len + s[kLForearm].len) + s[kLHand].len) * f(-0.471238822f);
        const F turn_r = near_part(chest.x - hand[1].x, s[kRClavicle].len) * f(0.235619411f);
        const F near_r =
            near_part(hand[1].z - chest.z, (s[kRUpperArm].len + s[kRForearm].len) + s[kRHand].len) * f(-0.471238822f);
        const F pitch = ((turn_l * f(0.049999997f)) + zero) + (turn_r * f(0.049999997f));
        const F base = f(0.314159214f), quarter = f(0.25f);
        c.clavicle[1] = ((((((base - (turn_l * quarter)) - (near_l * quarter)) + turn_r) + near_r) * f(0.75f)) + c.clavicle_base[1]);
        c.clavicle[0] = ((((((turn_l + base) + near_l) - (turn_r * quarter)) - (near_r * quarter)) * f(0.75f)) + c.clavicle_base[0]);
        chest = rotate(about_x(pitch), chest - seat) + seat;
    }
    const F ahead = c.fixed_recline ? f(0.314159214f) : zero;
    c.auto_clavicle = 0;
    c.clavicle_forward[0] = c.clavicle_forward_base[0] + ahead;
    c.clavicle_forward[1] = c.clavicle_forward_base[1] + ahead;
    if (c.arms) {
        const V3 wheel = p.wheel;
        const auto length_of = [](V3 v) { return ee::sqrt(dot(v, v)); };
        const V3 to_chest = chest - wheel;
        const F across = length_of(to_chest);
        F off[2];
        for (int k = 0; k < 2; ++k) {
            const F a = length_of(hand[k] - chest), b = length_of(wheel - hand[k]);
            off[k] = triangle_angle(a, b, across);
        }
        for (F& a : off) a = clamp(f(kHalfPi) - a, f(-1.25663686f), f(1.25663686f));
        finger[0] = rotate(axis_angle(h[0].palm, -off[0]), finger[0]);
        finger[1] = rotate(axis_angle(h[1].palm, off[1]), finger[1]);
        t.palm[0] = h[0].palm;
        t.palm[1] = h[1].palm;
        t.finger[0] = finger[0];
        t.finger[1] = finger[1];
        for (int k = 0; k < 2; ++k) {
            c.grip_side[k] = clamp(h[k].grip_side, zero, one);
            c.hand_turn[k] = clamp(h[k].hand_turn, zero, one);
            c.wrist[k] = h[k].wrist + c.wrist_base[k];
        }
    }
    // Into the frame.
    const Frame frame = frame_of(p.pos, p.turn);
    t.pelvis = apply(frame, seat + c.seat_offset);
    t.hand[0] = apply(frame, hand[0]);
    t.hand[1] = apply(frame, hand[1]);
    t.chest = apply(frame, chest) + c.chest_offset_world;
    if (c.legs) {
        t.foot[0] = apply(frame, foot[0]);
        t.foot[1] = apply(frame, foot[1]);
    }
    t.chest_forward = rotate(t.turn, forward);
}

} // namespace

Skeleton build_skeleton(const std::array<F, 15>& z) {
    // The joints of the figure standing with its arms out, from the pelvis (0x419F80).
    const F hips = z[0], thigh = z[1], shank = z[2], foot = z[3], pelvis = z[4], lower = z[5], middle = z[6], chest = z[7], collar = z[8],
            neck = z[9], face = z[10], shoulders = z[11], upper = z[12], fore = z[13], hand = z[14];
    const F o = f(0);
    const V3 origin{};
    const auto up = [&](V3 p, F d) { return p + V3{o, d, o}; };
    const auto side = [&](V3 p, F d) { return p + V3{d, o, o}; };
    const auto ahead = [&](V3 p, F d) { return p + V3{o, o, d}; };
    Skeleton s;
    const V3 hip[2] = {side(origin, hips * f(0.5f)), side(origin, -hips * f(0.5f))};
    for (int k = 0; k < 2; ++k) {
        const V3 knee = up(hip[k], -thigh), ankle = up(knee, -shank);
        set_vec(s[k ? kRThigh : kLThigh], hip[k], knee);
        set_vec(s[k ? kRShank : kLShank], knee, ankle);
        set_vec(s[k ? kRFoot : kLFoot], ankle, ahead(ankle, foot));
    }
    const V3 waist = up(origin, pelvis), back = up(waist, lower), breast = up(back, middle), collar_at = up(breast, chest);
    set_vec(s[kPelvis], origin, waist);
    s[kPelvis].left = hip[0] - origin;
    s[kPelvis].right = hip[1] - origin;
    set_vec(s[kLowerSpine], waist, back);
    set_vec(s[kMiddleSpine], back, breast);
    const V3 nape = up(collar_at, collar), skull = up(nape, neck);
    const V3 clav[2] = {side(collar_at, shoulders * f(0.0199999996f)), side(collar_at, -shoulders * f(0.0199999996f))};
    set_vec(s[kScapula], breast, nape);
    s[kScapula].left = clav[0] - breast;
    s[kScapula].right = clav[1] - breast;
    set_vec(s[kNeck], nape, skull);
    set_vec(s[kCranium], skull, ahead(skull, face));
    for (int k = 0; k < 2; ++k) {
        const auto signed_side = [&](V3 p, F d) { return side(p, k ? -d : d); };
        const V3 shoulder = side(clav[k], (k ? -shoulders : shoulders) * f(0.479999989f));
        const V3 elbow = signed_side(shoulder, upper), wrist = signed_side(elbow, fore);
        set_vec(s[k ? kRClavicle : kLClavicle], clav[k], shoulder);
        set_vec(s[k ? kRUpperArm : kLUpperArm], shoulder, elbow);
        set_vec(s[k ? kRForearm : kLForearm], elbow, wrist);
        set_vec(s[k ? kRHand : kLHand], wrist, signed_side(wrist, hand));
    }
    return s;
}

void set_column(Places& p, V3 axis) {
    p.column = axis;
    p.wheel_frame = matrix(between(-axis_z(), axis));
}

void set_reclined(Controls& c) {
    c.fixed_recline = 1;
    c.head_yaw_range = clamp(f(0.349999994f), f(0), f(1));
}

void solve(Controls& c, Skeleton& s, const Places& p, const Gaze* gaze) {
    V3 at{};
    if (gaze) {
        at = gaze->at;
        if (gaze->kind == 1) at.x = -at.x, at.z = -at.z;
        if (gaze->kind == 0 || gaze->kind == 1) at = apply(frame_of(p.pos, p.turn), at);
    }
    const V3* look = gaze ? &at : nullptr;
    Target t;
    make_target(c, s, p, t);
    if (c.spine) solve_spine(c, s, t, look);
    if (c.arms) solve_arms(c, s, t);
    if (c.legs) solve_legs(c, s, t);
    if (c.head_on) solve_head(c, s, look);
}

} // namespace gt4r::race::posture
