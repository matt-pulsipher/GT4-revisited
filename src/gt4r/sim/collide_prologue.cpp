// Gran Turismo 4 Prologue's collision step (collide_prologue.h).

#include "gt4r/sim/collide_prologue.h"

#include "formats/course/road.h"

#include <algorithm>

namespace gt4r::sim::prologue {

namespace cx = gt3r::sim::collide::exact;
namespace ex = gt3r::sim::exact;

Collision::Collision(const gt3r::course::Road& road, const std::vector<ex::Car>& cars) {
    course_ = std::make_unique<gt3r::sim::CourseCollision>(road);
    bodies_.resize(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) {
        const ex::Params& p = cars[i].p;
        // (its octagon's corners cut by 0x3E4CCCCC of the width (0x41C8E0), the later builds' constant in the engine's builder)
        bodies_[i].behaviour.variant[size_t(gt3r::sim::Rule::BodyBuild)] = gt3r::u8(gt3r::sim::Program::ConceptGenevaEu);
        cx::build(bodies_[i], p.body_front, p.body_rear, p.body_width, p.mass, p.yaw_inertia, int(i));
        bodies_[i].course = course_.get();
        bodies_[i].behaviour = cars[i].behaviour;
    }
    cx::pair_tables(bodies_);
}

void Collision::step(std::vector<ex::Car>& cars, std::vector<Contact>& contacts) {
    const size_t n = std::min(cars.size(), bodies_.size());
    for (size_t i = 0; i < n; ++i) {
        cx::Body& b = bodies_[i];
        ex::Car& c = cars[i];
        b.cx = c.pos.x, b.cy = c.pos.y, b.cz = c.pos.z;
        b.cvx = c.vel.x, b.cvy = c.vel.y, b.cvz = c.vel.z;
        b.hint = c.road_seg;
        b.ch = c.heading, b.cw = c.yaw_rate;
        b.dt_in = c.dt, b.steer = c.steer_pct, b.ghost = c.ghost != 0, b.control = c.control;
        b.rumble = &c.hit_rumble;
    }
    cx::step_all(bodies_);
    contacts.assign(n, {});
    for (size_t i = 0; i < n; ++i) {
        cx::Body& b = bodies_[i];
        ex::Car& c = cars[i];
        b.rumble = nullptr;
        c.pos = {b.cx, b.cy, b.cz};
        c.vel = {b.cvx, b.cvy, b.cvz};
        c.heading = b.ch, c.yaw_rate = b.cw;
        Contact& k = contacts[i];
        k.strength = b.car_hit || b.wall_hit ? std::min(b.strength.host(), 1.0f) : 0.0f;
        k.kind = b.car_hit ? 2 : b.wall_hit ? 1 : 0;
        if (b.car_hit && b.other >= 0 && size_t(b.other) < n && size_t(b.other) != i) k.car = b.other;
        k.sum = b.strength;
        if (k.kind == 1) k.part = b.wcorner && !b.wpoint ? b.we + 8 : b.wk;
        else if (k.kind == 2) {
            // the car whose side was met keeps the pair (its side + 8); the other its corner
            k.part = b.hit_edge + 8;
            for (size_t o = 0; !b.pair && o < n; ++o)
                if (o != i && bodies_[o].car_hit && bodies_[o].pair && bodies_[o].other == int(i)) k.part = bodies_[o].vertex, k.car = int(o);
        }
    }
}

} // namespace gt4r::sim::prologue
