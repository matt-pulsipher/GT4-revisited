#pragma once

// The vector and quaternion arithmetic of the driver's posture (docs/formats/model.md "The driver in the car"), in the
// PS2's floats and in the program's order of operations, so that a posture can be compared with the program's bit for bit
// (gt4r_diff <disc> posture). Addresses are the program's routines. A quaternion turns a vector as q v q*, w last.

#include "base/ee_math.h"
#include "gt4r/base/types.h"

namespace gt4r::race::posture {

using F = gt3r::ee::F;

struct V3 {
    F x, y, z;
};
struct Quat {
    F x, y, z, w;
};
// A rotation matrix as the program stores one: the rows are the turned x, y and z axes.
struct Mat3 {
    F m[9];
};

constexpr float kPi = 3.1415925f, kHalfPi = 1.57079625f, kTwoPi = 6.28318501f;

inline F f(float v) { return F(v); }
inline F clamp(F v, F lo, F hi) { return gt3r::ee::min(gt3r::ee::max(v, lo), hi); }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, F s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline F dot(V3 a, V3 b) { return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z); }
V3 cross(V3 a, V3 b);                  // 0x48D4C0 (VU0)
V3 normalized(V3 v);                   // v / |v| by the reciprocal square root
inline V3 axis_x() { return {f(1), f(0), f(0)}; }
inline V3 axis_y() { return {f(0), f(1), f(0)}; }
inline V3 axis_z() { return {f(0), f(0), f(1)}; }

F asin(F x);   // 0x59E890
inline F acos(F x) { return f(kHalfPi) - asin(x); }
F atan2(F y, F x); // 0x57D118
F wrap(F angle, F from);               // 0x420F00: into [from, from + 2 pi)
inline F radians(F degrees) { return degrees * f(0.0174532924f); } // 0x420F70

inline Quat identity() { return {f(0), f(0), f(0), f(1)}; }
Quat mul(Quat a, Quat b);              // 0x48D530 (VU0)
Quat axis_angle(V3 axis, F angle);     // 0x487830
inline Quat turned(Quat q, V3 axis, F angle) { return mul(axis_angle(axis, angle), q); } // 0x4878C0
Quat about_x(F angle);                 // 0x487950
Quat about_y(F angle);                 // 0x487A58
Quat about_z(F angle);                 // 0x487B60
Quat about_y_z(F a, F b);              // 0x487EA8: about z by b, then y by a
Quat turned_x(Quat q, F angle);        // 0x4879A0: the turn about x applied after q
Quat turned_y(Quat q, F angle);        // 0x487AA8
Quat post_x(Quat q, F angle);          // 0x422380: q after a turn about its own x
Quat post_z(Quat q, F angle);          // 0x4225E0
Quat from_euler(F x, F y, F z);        // 0x488528
inline Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
V3 rotate(Quat q, V3 v);               // 0x486FF8
V3 x_axis(Quat q);                     // 0x487210
V3 y_axis(Quat q);                     // 0x4872D0
V3 z_axis(Quat q);                     // 0x487390
Quat between(V3 from, V3 to);          // 0x4887D8: the shortest turn between two unit vectors
Quat align(V3 a, V3 b, V3 to_a, V3 to_b); // 0x488B98: takes a to to_a and b towards to_b
Quat slerp(Quat a, Quat b, F t);       // 0x488E28
Mat3 matrix(Quat q);                   // 0x487450
V3 transform(const Mat3& m, V3 v);     // 0x489300
Mat3 transposed(const Mat3& m);        // 0x486B38
V3 without(V3 v, V3 n);                // 0x424140: v less its part along n
F angle_between(V3 a, V3 b);           // 0x423450
F signed_angle(V3 a, V3 b, V3 axis);   // 0x423988: from a to b, positive about the axis
F signed_angle_about(V3 a, V3 b, V3 axis); // 0x423AA0: the same of their parts across the axis
V3 turned_towards(V3 a, V3 b, const V3* fallback, F part); // 0x423BF8
F triangle_angle(F a, F b, F c);       // 0x4233C0: the angle between sides a and b, opposite c (0: no triangle)

} // namespace gt4r::race::posture
