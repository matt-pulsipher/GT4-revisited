#pragma once

// The course's cones and boards as bodies the cars knock about (race/gadgets.h has their kinds and
// places), by Gran Turismo 4's own routines (docs/formats/course.md "Cones and boards: the bodies"):
// the step 0x3E2B28 (gravity; the turn by Euler's equations on the Euler angles, three stages; the
// move), the road under it 0x3E29F8 and its contact 0x3E3748 / 0x3E3840, the walls 0x3E44E8 /
// 0x3E4708, the cars' boxes 0x3E5108 and their contact 0x3E3C48 / 0x3E3ED0 / 0x3E43B8, the set-up
// 0x3E2710. A body rests (is not moved) while three of its hull points count 2 steps on the road.
// In host floats (the program's single precision, not its exact rounding).

#include "gt4r/formats/course/runway.h"
#include "gt4r/race/gadgets.h"

#include <array>
#include <span>
#include <vector>

namespace gt4r::race {

class GadgetBodies {
public:
    struct Body {
        int kind = 0;
        std::array<float, 3> angles{};          // +0x04 the Euler angles a, b, c (radians, 0 .. 2 pi)
        course::Vec3 pos, vel;                  // +0x10, +0x1C (course space)
        std::array<float, 3> omega{};           // +0x28 the turn rate in the body's frame (rad/s)
        std::array<float, 4> plane{0, 1, 0, 0}; // +0x34 the road's plane under it: n . p + d = 0
        bool on_road = false;                   // a plane was found (+0x48 a triangle)
        float hits = 0;                         // +0x54 this step's impulses (the road's and the cars')
        std::array<u8, 8> contact{};            // +0x58 each hull point's steps as the deepest under the road
        int spent = 0;                          // +0x45 steps since a once-only kind met a car (0: not)
        std::array<float, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1}; // body to course, row-major (from `angles`)
        bool awake = false;                     // moved this step (not resting)
        bool removed = false;
        course::Vec3 home; // where it stood (its floor: `floor`, the lowest hull point)
        float floor = 0;
    };
    // A car's box (0x3E5108): the car's center of mass, its heading's forward (level), its velocity
    // and yaw rate (radians a second about up), its extents ahead of and behind the center and its
    // half width; the box is 1.5 m high with its sides leaning in (normals (+-1, 0.15, 0)).
    struct Car {
        course::Vec3 pos, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1}, vel;
        float yaw_rate = 0;
        float front = 2.1f, rear = 2.1f, half_width = 0.85f;
    };

    // Every body set at its place (0x3E2710): turned to the road's slope, a hull point on the road,
    // resting.
    void reset(const Gadgets& g);
    void reset(const Gadgets& g, const course::Runway& road);
    // One step of `dt` seconds. Returns a mask of the cars (the first 32) that touched a body.
    unsigned step(const Gadgets& g, const course::Runway& road, std::span<const Car> cars, float dt);
    // The last step's contacts per car (each a report of kind 2 to the car, 0x3E3C48 -> 0x350530).
    const std::vector<int>& contacts() const { return contacts_; }
    // And per car the strongest of the step's contacts' impulses |j| (what 0x3E3C48 reports with each).
    const std::vector<float>& impulses() const { return impulses_; }
    const std::vector<Body>& bodies() const { return bodies_; }
    // A body has moved from its place (the scene draws it where it is).
    bool moved() const { return moved_; }

private:
    bool ground_plane(Body& b, const course::Runway& road) const;
    void place(Body& b, const Gadgets::Kind& k, const course::Runway* road) const;
    std::vector<Body> bodies_;
    std::vector<int> contacts_;
    std::vector<float> impulses_;
    bool moved_ = false;
};

} // namespace gt4r::race
