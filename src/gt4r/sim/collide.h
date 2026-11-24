#pragma once

// Gran Turismo 4's collision step (0x45BB60 and its routines; docs/specs/physics.md "Walls and other cars") in the
// PlayStation 2's arithmetic: every car's body swept through the step against the road's walls and the other cars. The
// body record (0x4F0 bytes, the collision manager's at conductor + 0xDA78 + 0x4F0 i) is mirrored member by member; the
// offsets are given with each. Walls are the road's wall point records; a body meets those listed at the road triangle
// its car stands on (the hint, the car's F +0x4A8: wheel 0's ground record +0x0C).

#include "base/ee_float.h"
#include "gt4r/base/types.h"

#include <array>
#include <vector>

namespace gt4r::course {
struct Runway;
}

namespace gt4r::sim {
class RoadGround;
}

namespace gt4r::sim::collide {

using gt3r::ee::F;

constexpr int kMaxBodies = 6; // the pair tables' size (+0x4AC / +0x4C4)

// The road's walls as the step reads them (the road's +0x84 point records, +0x8C index lists, each triangle's +0x08
// word: its list's start in the low 24 bits, its length in the high 8).
class Walls {
public:
    struct Point {
        F x, y, z;
        s16 link = 0;  // +0x0C: a chain's first point holds the chain's length, its last minus it (back to the first)
        u16 flags = 0; // +0x0E: bit 0 a corner, bit 1 a sensor line
    };
    Walls(const course::Runway& road, const RoadGround& ground);
    const Point* points() const { return points_.data(); }
    int next(int i) const { return points_[size_t(i)].link >= 0 ? i + 1 : i + 1 + points_[size_t(i)].link; }
    int prev(int i) const { return points_[size_t(i)].link <= 0 ? i - 1 : i - 1 + points_[size_t(i)].link; }
    // 0x395A38 with 0x3E6A58: the walls of the hint's triangle (hint < 0: the road found straight down from 10,000 m at
    // the box's middle) whose segment is not wholly beyond one side of the box (x0 .. x1, z0 .. z1), in the list's order.
    void each(s32 hint, F x0, F z0, F x1, F z1, std::vector<int>& out) const;

private:
    std::vector<Point> points_;
    std::vector<s16> index_;
    std::vector<u32> lists_; // per triangle
    const RoadGround* ground_;
};

// A body's point (0x44 bytes from body + 0x08).
struct Point {
    F lx, ly;         // +0x00 in the body's frame
    F ux, uy;         // +0x08 the unit direction from the center
    F sx, sy;         // +0x10 at the sweep's start
    F ex, ey;         // +0x18 at its end
    u32 flags = 0;    // +0x20 bit 0 held on a wall, bit 1 on another body's edge
    u32 hit = 0;      // +0x24 crossed a wall this round
    F t;              // +0x28 when
    int wall = -1;    // +0x2C the wall point (a pointer in the record)
    F tx, ty;         // +0x30 that wall's unit tangent
    int other = 0;    // +0x3C the other body (bit 1)
    int edge = 0;     // +0x40 its edge
};

// An edge (0x2C bytes from body + 0x228): edge k runs from point k to point k + 1.
struct Edge {
    F sx, sy, ex, ey; // +0x00
    F nx, ny, d;      // +0x10 the outward normal and offset
    u32 x1c = 0;      // +0x1C cleared by the build, read by the stuck nudge
    u32 hit = 0;      // +0x20 a wall corner crossed it this round
    F t;              // +0x24 when
    int wall = -1;    // +0x28 the corner's wall point
};

struct Body {
    F im, ii;                           // +0x000 1 / mass, 1 / (10 yaw inertia)
    std::array<Point, 8> p;             // +0x008
    std::array<Edge, 8> e;              // +0x228
    F radius;                           // +0x388
    F ts, sx, sy, sz, sh;               // +0x38C the sweep's start time and pose
    F te, ex, ey, ez, eh;               // +0x3A0 its end
    F dx, dy, dz, dh;                   // +0x3B4
    F vx, vy, vz, w;                    // +0x3C4
    F bx0, by0, bx1, by1, bw, bh;       // +0x3D4 the swept box
    u32 have_points = 0;                // +0x3EC
    u32 pair = 0, wpoint = 0, wcorner = 0; // +0x3F0
    F t;                                // +0x3FC this round's earliest hit
    u32 sensor = 0;                     // +0x400
    F sensor_t;                         // +0x404
    int hit_edge = 0, other = 0, vertex = 0; // +0x408
    F cpx, cpy;                         // +0x414
    int wk = 0;                         // +0x41C
    F ux, uy;                           // +0x420
    int wseg = -1;                      // +0x428 (a pointer in the record)
    int we = 0;                         // +0x42C
    u32 enabled = 0;                    // +0x430
    u32 front = 0, rear = 0;            // +0x434
    u32 wall_hit = 0, car_hit = 0, sensor_hit = 0; // +0x43C
    F strength;                         // +0x448
    int car = 0;                        // +0x44C the car's index (+0x450 the car)
    F cx, cy, cz;                       // +0x454 the committed state: position
    F cvx, cvy, cvz;                    // +0x460 velocity
    F pitch, roll, heading;             // +0x46C
    F pitch_rate, roll_rate, yaw_rate;  // +0x478
    s32 hint = -1;                      // +0x484 the road triangle (cluster << 16 | triangle)
    F front_len, rear_len, half_width, mass, iz10, chamfer1, chamfer2; // +0x48C
    std::array<F, kMaxBodies> rsum, r2; // +0x4AC / +0x4C4
    F dt;                               // +0x4DC
    s32 rumble_steps = 0;               // +0x4E0
    F rumble_level;                     // +0x4E4
    s32 sound_steps = 0;                // +0x4E8
    F sound_level;                      // +0x4EC
};

// What the step reads of a body's car.
struct CarIn {
    F dt;              // F +0x448 the step length
    bool ghost = false; // 0x343BF0: not in the race (F +0x441), or its driver on line 4 (driver +0x1C)
    bool player = false; // 0x3451B8: control source and driver kind 0
    F steer;           // 0x344680: F +0x45C, the steering's value
    F* rumble = nullptr; // F +0x610: the last wall hit's strength for the pad's rumble
};

// 0x45C170 with 0x45C0F8's coefficients: a body from the car's extents (front, rear, width), mass and yaw inertia
// (the parameter block's +0x00, +0x04, +0x08, +0x48, +0x114).
void build(Body& b, int car, F front, F rear, F width, F mass, F iz);
// 0x45C438: every body's pair tables.
void pair_tables(std::vector<Body>& bodies);
// 0x45BB60: the step of all bodies. `cars[i]` for body i.
// A hit the step met, as the program hands it to the hit records (0x350530, rules_phases.h): the car, the kind (0 a wall, 1 a
// car), the part (a wall at a body's point: its index; a wall's corner on a body's side: the side + 8; two cars: the car whose
// side was met its side + 8, the other its corner's index) and the strength (a wall: the impulse |j|; two cars: |j| / 15,000).
struct Hit {
    int car = 0;
    s32 kind = 0, part = 0;
    F strength;
};
// `hits`: the step's hits in the program's order (none kept when null).
void step_all(std::vector<Body>& bodies, const std::vector<CarIn>& cars, const Walls& walls, std::vector<Hit>* hits = nullptr);

} // namespace gt4r::sim::collide
