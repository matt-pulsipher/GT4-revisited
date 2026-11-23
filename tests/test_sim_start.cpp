// The simulation's start and what a step publishes besides the cars' poses: a rolling start, the
// contacts of the collision step and the wheels' landings, on the Gran Turismo 4 disc of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/rules_start.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>
#include <memory>

using namespace gt4r;

namespace {

db::SpecDb open_db(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

std::shared_ptr<const course::Runway> open_road(Disc& disc, const std::string& name) {
    const gt3r::Volume::Entry* e = disc.volume().find("crs/" + name);
    if (!e) return nullptr;
    const Bytes data = disc.volume().read(*e);
    const course::CourseFile file = course::CourseFile::parse(data);
    return std::make_shared<const course::Runway>(course::Runway::parse(file.section(data, course::kSlotRunway)));
}

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(sim_rolling_start_places) {
    // The arcade race's rolling start at Tsukuba (the rule block's -400 m, 20 m apart, six cars): the last
    // place 400 m before the line, the first 300 m.
    CHECK(sim::rules::rolling_start_distance(-400, 20, 6, 5) == -400);
    CHECK(sim::rules::rolling_start_distance(-400, 20, 6, 0) == -300);
}

TEST(sim_disc_rolling_start) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "tsukuba");
    CHECK(road != nullptr);
    if (!road) return;
    // The field of the original's arcade race at Tsukuba, rolling at 80 km/h in single file.
    const char* labels[] = {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01"};
    std::vector<sim::Entry> e(6);
    for (int i = 0; i < 6; ++i) {
        CHECK(sim::stock_car(sdb, program, labels[i], e[size_t(i)].equipment));
        e[size_t(i)].grid_place = i;
        e[size_t(i)].start_speed = 80.0f / 3.6f;
        e[size_t(i)].start_distance = float(sim::rules::rolling_start_distance(-400, 20, 6, i));
    }
    sim::Session s(sdb, program, road, e);
    for (int i = 0; i < 6; ++i) {
        const race::CarState& c = s.cars()[size_t(i)];
        const auto& r = s.record(size_t(i));
        // Moving at the start's speed in a forward gear, the clutch locked, the engine turning with the wheels.
        CHECK(std::fabs(c.speed - 80.0f / 3.6f) < 0.01f);
        CHECK(c.gear >= 1 && r.clutch == 1 && c.engine_rpm > 1000);
        CHECK(std::fabs(r.w[0].rim_speed.host() - c.speed) < 0.01f);
        float v = 0;
        CHECK(s.ground().distance_along(c.position, v));
        float want = std::fmod(float(sim::rules::rolling_start_distance(-400, 20, 6, i)) + road->length, road->length);
        CHECK(std::fabs(v - want) < 6.0f);
        CHECK(c.contact == 0 && c.contact_kind == 0);
    }
    // Each car 20 m behind the one ahead, all facing the same way along the course.
    for (int i = 1; i < 6; ++i) {
        const course::Vec3 a = s.cars()[size_t(i - 1)].position, b = s.cars()[size_t(i)].position;
        CHECK(std::fabs(std::hypot(a.x - b.x, a.z - b.z) - 20.0f) < 3.0f);
        CHECK(s.cars()[size_t(i - 1)].forward.x * s.cars()[size_t(i)].forward.x + s.cars()[size_t(i - 1)].forward.z * s.cars()[size_t(i)].forward.z > 0.9f);
    }
    // Driven on at full throttle, the cars keep their speed.
    std::vector<race::CarInput> in(6);
    for (race::CarInput& i : in) i.throttle = 1;
    for (int k = 0; k < 60; ++k) s.step_once(in);
    for (const race::CarState& c : s.cars()) CHECK(c.speed > 20.0f && c.up.y > 0.9f);
}

TEST(sim_disc_contacts) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "testcourse_1000m");
    CHECK(road != nullptr);
    if (!road) return;
    // A car driving into one that stands: both see the other's contact; then on into the wall.
    sim::Entry e[2];
    CHECK(sim::stock_car(sdb, program, "viper_gts_99", e[0].equipment) && sim::stock_car(sdb, program, "nsx_r_02", e[1].equipment));
    e[0].grid_place = 0, e[1].grid_place = 1;
    sim::Session s(sdb, program, road, e);
    const course::Vec3 a0 = s.cars()[0].position, b0 = s.cars()[1].position;
    const size_t behind = (a0.x - b0.x) * s.cars()[1].forward.x + (a0.z - b0.z) * s.cars()[1].forward.z > 0 ? 1 : 0, ahead = 1 - behind;
    bool met = false;
    for (int i = 0; i < 600 && !met; ++i) {
        race::CarInput in[2];
        in[behind].throttle = 1;
        const race::CarState &c = s.cars()[behind], &t = s.cars()[ahead];
        const float dx = t.position.x - c.position.x, dz = t.position.z - c.position.z;
        in[behind].steer = std::clamp(std::atan2(dx * c.right.x + dz * c.right.z, std::max(dx * c.forward.x + dz * c.forward.z, 1.0f)) * 3.0f, -1.0f, 1.0f);
        s.step_once(in);
        const race::CarState &p = s.cars()[behind], &q = s.cars()[ahead];
        if (p.contact_kind == 2) {
            met = true;
            CHECK(p.contact > 0 && p.contact <= 1);
            CHECK(p.contact_car == int(ahead));
            CHECK(q.contact_kind == 2 && q.contact_car == int(behind) && q.contact > 0);
        }
    }
    CHECK(met);
}

TEST(sim_disc_wall_contact) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Straight on at full throttle into Tsukuba's first corner: the wall's contact, with no car named.
    const std::string cars[] = {"nsx_r_02"};
    const std::unique_ptr<sim::Session> s = sim::Session::open(*disc, "tsukuba", cars);
    race::CarInput in;
    in.throttle = 1;
    bool wall = false;
    for (int i = 0; i < 30 * 60 && !wall; ++i) {
        s->step_once({&in, 1});
        const race::CarState& c = s->cars()[0];
        if (c.contact_kind == 1) {
            wall = true;
            CHECK(c.contact > 0 && c.contact <= 1 && c.contact_car == -1);
        }
    }
    CHECK(wall);
}
