#pragma once

// The course's cones and boards as bodies the cars knock about (race/gadgets.h has their kinds and
// places). ❓ The rules are the shared engine's for Gran Turismo 3's pylons (its race/pylons.h: asleep
// while three hull points rest on the ground; awake, they fall and tumble, the deepest hull point
// under the road gets an impulse with restitution 0.25 (none under 0.9 m/s) and friction 0.5; a car's
// box pushes a body out and hits it with restitution 0.01 and the inertia x 1.5, the car feeling
// nothing). Gran Turismo 4's own routines for its gadgets are not read. Some stand on ground the road
// file does not hold (Tsukuba's pit lane cones a metre above the track under them): within 3 m of
// its place a body rests on the floor it stood on when the road is lower there.

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
        course::Vec3 pos, vel, omega;            // omega in course space, radians a second
        std::array<float, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1}; // body to course, row-major
        bool awake = false, removed = false;
        bool spent = false; // met a car once and, by its kind's flag, meets none again (body +0x45)
        int still = 0; // steps resting
        course::Vec3 home; // where it stood, on the floor at `floor` (its lowest hull point's height)
        float floor = 0;
    };
    // A car's hit box: its frame (the middle of the wheelbase on the ground), its velocity, its front
    // and rear extents and half width.
    struct Car {
        course::Vec3 pos, right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1}, vel;
        float front = 2.1f, rear = 2.1f, half_width = 0.85f;
    };

    // Every body back at its place, asleep.
    void reset(const Gadgets& g);
    // One step of `dt` seconds. Returns a mask of the cars (the first 32) that touched a body.
    unsigned step(const Gadgets& g, const course::Runway& road, std::span<const Car> cars, float dt);
    // The last step's contacts per car (each a report of kind 2 to the car, 0x3E3C48 -> 0x350530).
    const std::vector<int>& contacts() const { return contacts_; }
    const std::vector<Body>& bodies() const { return bodies_; }
    // A body has moved from its place (the scene draws it where it is).
    bool moved() const { return moved_; }

private:
    void ground(Body& b, const Gadgets::Kind& k, const course::Runway& road);
    std::vector<Body> bodies_;
    std::vector<int> contacts_;
    bool moved_ = false;
};

} // namespace gt4r::race
