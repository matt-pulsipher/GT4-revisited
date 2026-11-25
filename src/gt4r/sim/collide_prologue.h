#pragma once

// Gran Turismo 4 Prologue's collision step (the conductor's phase 0x3B6FB8 with 0x41CA90, Gran Turismo 3's 0x1FA8F8 in
// another layout; docs/formats/prologue.md "The race step"): every car's body swept through the step against its course's
// walls (the GTRW road's wall points, wall grid and segment links, Gran Turismo 3's format) and the other cars. It is the
// shared engine's port of Gran Turismo 3's step (sim/collision_exact.h) on the engine's reading of the GTRW road
// (sim/course_collision.h), each body's committed state copied in from its car and out after (0x1E8730 / 0x1E8828's
// roles).

#include "gt4r/sim/collide.h"
#include "sim/collision_exact.h"
#include "sim/course_collision.h"
#include "sim/exact/car_record.h"

#include <memory>
#include <vector>

namespace gt3r::course {
struct Road;
}

namespace gt4r::sim::prologue {

class Collision {
public:
    // The bodies of `cars` (their parameter blocks' extents, mass and yaw inertia: 0x1FA740) on the road `road`.
    Collision(const gt3r::course::Road& road, const std::vector<gt3r::sim::exact::Car>& cars);
    // What the step leaves a car: the summed impulses' strength (at most 1), whether a wall (1) or a car (2) was met, and the
    // car met (-1: none).
    struct Contact {
        float strength = 0;
        gt3r::u8 kind = 0;
        int car = -1;
    };
    // One step of every car (the cars' positions, velocities, headings and yaw rates in and out). `hits`: the step's hits in
    // order, as 0x3B9430 is handed each (a wall's impulse |j| with the corner 0 .. 7 or the side + 8 that met it, a car's
    // |j| / 15,000 to the car whose side was met, its side + 8, then to the other, its corner).
    void step(std::vector<gt3r::sim::exact::Car>& cars, std::vector<Contact>& contacts, std::vector<collide::Hit>* hits = nullptr);
    const gt3r::sim::collide::exact::Body& body(size_t i) const { return bodies_[i]; }

private:
    std::unique_ptr<gt3r::sim::CourseCollision> course_;
    std::vector<gt3r::sim::collide::exact::Body> bodies_;
};

} // namespace gt4r::sim::prologue
