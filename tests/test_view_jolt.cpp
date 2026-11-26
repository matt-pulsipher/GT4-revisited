// A hit's jolt of the in-car view: the collision step's record (sim::rules::view_jolt, 0x3F4C18) from the program's limits
// (0x34A4B8), and the camera's springs (race::DriveCamera, 0x37A8E4 ..). docs/formats/camera.md "In-car view".

#include "test_framework.h"

#include "gt4r/race/drive_camera.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_feel.h"

#include <cmath>

using namespace gt4r;
using gt3r::ee::F;

namespace {

race::CarState level_car() {
    race::CarState s;
    s.position = {0, 0, 0};
    s.forward = {1, 0, 0}, s.up = {0, 1, 0}, s.right = {0, 0, 1};
    return s;
}

} // namespace

TEST(view_jolt_record) {
    sim::rules::FeelTables t;
    t.jolt_bytes = {1, 10, 200, 1, 50, 0, 20}; // (as the disc's program has them)
    const sim::rules::JoltLimits l = sim::rules::jolt_limits(t, 0);
    CHECK(l.on && std::fabs(l.floor.host() - 2.7778f) < 1e-3f && std::fabs(l.cap.host() - 55.556f) < 1e-2f);
    CHECK(std::fabs(l.base.host() - 0.27778f) < 1e-4f && std::fabs(l.gain.host() - 0.25789f) < 1e-4f);
    // A dirt course raises both speeds by 20 km/h.
    CHECK(std::fabs(sim::rules::jolt_limits(t, 2).floor.host() - 30 / 3.6f) < 1e-3f);

    sim::rules::ViewState v;
    gt3r::sim::exact::Car c{};
    c.fwd = {F(1.0f), F(), F()}, c.lat = {F(), F(), F(1.0f)};
    c.vel = {F(20.0f), F(), F()};
    // Under the floor: nothing kept.
    sim::rules::view_jolt(c, {F(18.0f), F(), F()}, v, l);
    CHECK(v.jolt == 0);
    // 12 m/s lost along the body: kept as 0.278 + 0.258 (12 - 2.78) back, for this step only.
    sim::rules::view_jolt(c, {F(8.0f), F(), F()}, v, l);
    CHECK(v.jolt == 1 && std::fabs(v.jolt_forward.host() + (0.27778f + 0.25789f * (12 - 2.7778f))) < 1e-3f && std::fabs(v.jolt_side.host()) < 1e-6f);
    sim::rules::view_jolt(c, {F(8.0f), F(), F()}, v, l);
    CHECK(v.jolt == 0);
}

TEST(view_jolt_program) {
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        const sim::Program program = sim::Program::load(*disc);
        if (!program.known() || disc->info().game != Game::GT4) continue;
        const sim::rules::FeelTables t = sim::rules::feel_tables(program);
        const sim::rules::JoltLimits l = sim::rules::jolt_limits(t, 0);
        // The words the emulator's race states hold at 0x6202E8, 0x6202EC, 0x6202F0, 0x6202F8 (03-race/state_at_start), to the
        // last bit of the EE's multiplier (1 x 0x3E8E38E3 is one below on the EE; the emulator's is the host's).
        CHECK(l.on && l.floor.bits == 0x4031C71Bu && l.cap.bits == 0x425E38E2u);
        CHECK(l.base.bits + 1 >= 0x3E8E38E3u && l.base.bits <= 0x3E8E38E3u && l.gain.bits + 1 >= 0x3E840AC8u && l.gain.bits <= 0x3E840AC8u);
        ++ran;
    }
    if (!ran) SKIP("set GT4R_DISC (Gran Turismo 4)");
}

TEST(view_jolt_camera) {
    race::CarSpec spec;
    race::CarState s = level_car();
    std::vector<race::CameraMount> mounts(7);
    mounts[0] = {{0, 1.0f, 0.2f}, 0, 0, 0, 2.0503f, 5};
    const gt3r::render::Mat4 body = gt3r::render::Mat4::identity();
    race::DriveCamera cam;
    cam.set_shake(false);
    cam.reset(s);
    cam.step(s, 1.0f / 60);
    const race::ViewPose still = cam.pose(s, body, mounts, race::DriveCamera::kInCar, false);
    // A head-on hit: the view pitches down at once, then swings back past level and settles within a second.
    s.jolt = true, s.jolt_forward = -3.0f, s.jolt_side = 0;
    cam.step(s, 1.0f / 60);
    s.jolt = false;
    float lowest = 0, highest = 0;
    for (int i = 0; i < 60; ++i) {
        const race::ViewPose p = cam.pose(s, body, mounts, race::DriveCamera::kInCar, false);
        const gt3r::render::Vec3 ahead = p.view.transform_point({p.eye.x + 10, p.eye.y, p.eye.z}); // 10 m ahead, level
        lowest = std::min(lowest, ahead.y), highest = std::max(highest, ahead.y);
        if (i < 6) CHECK(ahead.y >= -1e-4f); // looking down: the level point is above the middle
        cam.step(s, 1.0f / 60);
    }
    CHECK(highest > 1.0f && lowest < -0.2f);
    const race::ViewPose p = cam.pose(s, body, mounts, race::DriveCamera::kInCar, false);
    const gt3r::render::Vec3 a = p.view.transform_point({p.eye.x + 10, p.eye.y, p.eye.z});
    CHECK(std::fabs(a.y) < 0.05f && std::fabs(a.x) < 0.05f);
    CHECK(std::fabs(p.eye.y - still.eye.y) < 0.01f);
    // The chase view has none.
    s.jolt = true;
    cam.step(s, 1.0f / 60);
    s.jolt = false;
    cam.step(s, 1.0f / 60);
    const race::ViewPose chase = cam.pose(s, body, mounts, race::DriveCamera::kChase, false);
    CHECK(std::fabs(chase.view.transform_point({chase.eye.x + 10, chase.eye.y, chase.eye.z}).y) < 1e-3f);
}
