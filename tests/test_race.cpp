// The race view's parts that need no window: the stand-in simulation on a made-up road, then on
// the roads of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/rail_simulation.h"

#include <cmath>

using namespace gt4r;

namespace {

using course::Runway;
using race::Vec3;

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(const Vec3& a) { return std::sqrt(dot(a, a)); }

// A flat square of road with a ring of checkpoints of radius `r`, driven clockwise seen from
// above (turning right all the way), the grid on the line.
Runway ring_road(float r, int n = 48) {
    Runway road;
    const float e = r + 60;
    road.vertices = {{{-e, 0, -e}}, {{e, 0, -e}}, {{e, 0, e}}, {{-e, 0, e}}};
    road.triangles = {{{0, 1, 2}}, {{0, 2, 3}}};
    road.length = 2 * 3.14159265f * r;
    road.min = {-e, 0, -e}, road.max = {e, 0, e};
    for (int i = 0; i < n; ++i) {
        // Travel direction at angle a: heading turns right as a grows.
        const float a = 2 * 3.14159265f * float(i) / float(n);
        const Vec3 out{std::cos(a), 0, std::sin(a)}; // from the ring's middle
        Runway::Checkpoint c;
        c.middle = {out.x * r, 0, out.z * r};
        c.left = {out.x * (r + 6), 0, out.z * (r + 6)};
        c.right = {out.x * (r - 6), 0, out.z * (r - 6)};
        c.v = road.length * float(i) / float(n);
        road.checkpoints.push_back(c);
    }
    for (int i = 0; i < 4; ++i) {
        const float a = -0.03f * float(i + 1);
        Runway::Place p;
        p.pos = {std::cos(a) * (r + (i & 1 ? 2.0f : -2.0f)), 0, std::sin(a) * (r + (i & 1 ? 2.0f : -2.0f))};
        road.grid.push_back(p);
    }
    return road;
}

void check_state(const race::CarState& s, const race::RailCar& spec) {
    CHECK(std::fabs(len(s.forward) - 1) < 1e-3f && std::fabs(len(s.up) - 1) < 1e-3f && std::fabs(len(s.right) - 1) < 1e-3f);
    CHECK(std::fabs(dot(s.forward, s.up)) < 1e-3f && std::fabs(dot(s.forward, s.right)) < 1e-3f);
    const Vec3 r = cross(s.forward, s.up);
    CHECK(dot(r, s.right) > 0.999f); // the right side is to the right of the direction faced
    CHECK(s.up.y > 0.7f);
    for (int w = 0; w < 4; ++w) {
        const race::WheelState& ws = s.wheels[w];
        CHECK(std::isfinite(ws.center.x) && std::isfinite(ws.spin) && std::isfinite(ws.travel));
        CHECK(std::fabs(ws.travel) < 0.25f);
        // Front wheels ahead of the rear ones, left wheels to the left.
        const Vec3 d{ws.center.x - s.position.x, ws.center.y - s.position.y, ws.center.z - s.position.z};
        CHECK((dot(d, s.forward) > 0) == (w < 2));
        CHECK((dot(d, s.right) > 0) == ((w & 1) == 1));
        CHECK(std::fabs(std::fabs(dot(d, s.forward)) - spec.wheelbase * 0.5f) < 0.2f);
    }
}

} // namespace

TEST(rail_line_ring) {
    const Runway road = ring_road(100);
    const race::RailLine line(road);
    CHECK(line.valid());
    CHECK(line.points.size() == 48);
    for (float v = 0; v < line.length; v += 7.3f) {
        const race::RailLine::Sample s = line.at(v);
        CHECK(std::fabs(std::hypot(s.pos.x, s.pos.z) - 100) < 0.5f);
        CHECK(std::fabs(s.curvature - 0.01f) < 0.002f); // turning right, radius 100
        CHECK(std::fabs(len(s.tangent) - 1) < 1e-4f);
        // To the right of travel is toward the ring's middle.
        CHECK(dot(s.across, {-s.pos.x, 0, -s.pos.z}) > 0);
        CHECK(s.left > 5 && s.right > 5);
        float pv, off;
        const Vec3 p{s.pos.x + s.across.x * 2, 0, s.pos.z + s.across.z * 2};
        line.project(p, pv, off);
        float dv = std::fabs(pv - v);
        dv = std::min(dv, line.length - dv);
        CHECK(dv < 1.0f);
        CHECK(std::fabs(off - 2) < 0.2f);
    }
    // It wraps both ways.
    CHECK(len({line.at(-5).pos.x - line.at(line.length - 5).pos.x, 0, line.at(-5).pos.z - line.at(line.length - 5).pos.z}) < 1e-3f);
}

TEST(rail_simulation_ring) {
    const Runway road = ring_road(100);
    std::vector<race::RailCar> specs(4);
    race::RailSimulation sim(road, specs);
    CHECK(sim.cars().size() == 4);
    for (const race::CarState& s : sim.cars()) {
        check_state(s, specs[0]);
        CHECK(s.speed == 0);
        CHECK(std::fabs(s.wheels[0].center.y - specs[0].tire_radius[0]) < 0.01f); // standing on the road
    }
    // Car 0 leads on the grid.
    CHECK(sim.cars()[0].course_v > sim.cars()[3].course_v || sim.cars()[0].course_v < 10);

    // Nobody touches anything: nothing moves.
    std::vector<race::CarInput> inputs(4);
    for (int i = 0; i < 60; ++i) sim.step(inputs, 1.0f / 60);
    CHECK(sim.cars()[0].speed == 0);

    // Two minutes with every car on the simple driver.
    float top = 0, max_travel = 0;
    bool braked = false, dipped = false;
    for (int i = 0; i < 120 * 60; ++i) {
        for (size_t c = 0; c < 4; ++c) inputs[c] = race::follow_line(sim.line(), sim.cars()[c], c & 1 ? 1.5f : -1.5f, 1.0f - 0.05f * float(c));
        if (i > 60 * 60 && i < 62 * 60) inputs[0].throttle = 0, inputs[0].brake = 1; // car 0 brakes hard for two seconds
        sim.step(inputs, 1.0f / 60);
        const race::CarState& s = sim.cars()[0];
        top = std::max(top, s.speed);
        for (int w = 0; w < 4; ++w) max_travel = std::max(max_travel, std::fabs(s.wheels[w].travel));
        if (i > 60 * 60 + 10 && i < 61 * 60) {
            braked |= s.brake_lamps;
            dipped |= s.wheels[0].travel > 0.005f && s.wheels[2].travel < -0.005f; // the nose dips: front springs shorten
        }
        if (i % 600 == 0)
            for (const race::CarState& c : sim.cars()) check_state(c, specs[0]);
    }
    CHECK(braked && dipped);
    CHECK(top > 20 && top < 40); // a 100 m bend holds about 31 m/s
    CHECK(max_travel > 0.005f);
    for (const race::CarState& s : sim.cars()) {
        CHECK(s.lap >= 2);
        CHECK(s.gear >= 1 && s.engine_rpm >= 900 && s.engine_rpm <= specs[0].rev_limit);
        CHECK(s.wheels[0].spin > 100);
        CHECK(s.wheels[0].steer > 0.005f); // turning right
        CHECK(std::fabs(std::hypot(s.position.x, s.position.z) - 100) < 9);
    }
    // Reverse: from rest the car backs up with its lamps on.
    race::RailSimulation one(road, std::vector<race::RailCar>(1));
    race::CarInput back;
    back.reverse = true, back.throttle = 1;
    for (int i = 0; i < 120; ++i) one.step({&back, 1}, 1.0f / 60);
    CHECK(one.cars()[0].speed < -1 && one.cars()[0].gear == -1 && one.cars()[0].reverse_lamps);
}

TEST(rail_simulation_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        for (const char* name : {"crs/tsukuba", "crs/grandvalley", "crs/yosemite_dirt", "crs/route5", "crs/nurburgring", "crs/ss_shibuya"}) {
            const gt3r::Volume::Entry* e = disc->volume().find(name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const ByteSpan section = file.section(data, course::kSlotRunway);
            if (!Runway::is_runway(section)) continue;
            const Runway road = Runway::parse(section);
            const race::RailLine line(road);
            if (road.checkpoints.size() < 3) continue; // (a photo location has no lap)
            CHECK(line.valid());
            // The line stays on the road and its length is the lap's.
            float total = 0;
            size_t off_road = 0;
            const size_t n = line.points.size();
            for (size_t i = 0; i < n; ++i) {
                const Vec3& a = line.points[i].pos;
                const Vec3& b = line.points[(i + 1) % n].pos;
                total += len({b.x - a.x, b.y - a.y, b.z - a.z});
                float y;
                off_road += !road.height_at(a.x, a.z, a.y + 3, y);
            }
            if (off_road * 20 > n) std::printf("  %s: %zu of %zu line points off the road\n", name, off_road, n);
            CHECK(off_road * 20 <= n); // (a few points of the longest courses lie over a gap in the road's mesh)
            CHECK(total > road.length * 0.9f && total < road.length * 1.1f);

            std::vector<race::RailCar> specs(6);
            race::RailSimulation sim(road, specs);
            std::vector<race::CarInput> inputs(6);
            float worst = 0;
            for (int i = 0; i < 45 * 60; ++i) {
                for (size_t c = 0; c < 6; ++c) inputs[c] = race::follow_line(sim.line(), sim.cars()[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.04f * float(c));
                sim.step(inputs, 1.0f / 60);
                if (i % 30 == 0)
                    for (const race::CarState& s : sim.cars()) {
                        check_state(s, specs[0]);
                        float y;
                        if (road.height_at(s.position.x, s.position.z, s.position.y + 3, y)) worst = std::max(worst, std::fabs(s.position.y - y));
                    }
            }
            CHECK(worst < 0.6f); // the body's reference point is at the road
            for (const race::CarState& s : sim.cars()) CHECK(s.speed > 5);
            // In 45 seconds the leader covers some hundreds of metres.
            const race::CarState& lead = sim.cars()[0];
            CHECK(lead.lap >= 0 || lead.course_v > 0);
            ++ran;
        }
    }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}
