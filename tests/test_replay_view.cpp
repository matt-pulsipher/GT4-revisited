// Watching a replay: the controls (race/replay_view.h), the passes' settings and the cameras' focus.
#include "race/sun_glare.h"

#include "base/config.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/replay_effects.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/replay_view.h"
#include "race/sun_glare.h"
#include "test_framework.h"

#include <cmath>

using namespace gt4r;

#define CHECK_NEAR(a, b, e) CHECK(std::fabs(double(a) - double(b)) <= (e))
#define CHECK_EQ(a, b) CHECK((a) == (b))

using race::ReplayPad;
using race::ReplayStyle;
using race::ReplayView;

TEST(replay_view_styles) {
    ReplayView v;
    v.begin(ReplayStyle::Plain);
    ReplayPad square;
    square.square = true;
    CHECK(v.step(square).style && v.style() == ReplayStyle::Dive);
    CHECK(v.step(square).style && v.style() == ReplayStyle::Display);
    CHECK(v.step(square).style && v.style() == ReplayStyle::Plain);
    CHECK(!v.step({}).style);
    // The music and the car are the director's in Dive.
    ReplayPad pad;
    pad.circle = pad.down = true;
    CHECK(v.step(pad).music);
    CHECK(v.step(pad).select_car == 1);
    v.step(square);
    CHECK(!v.step(pad).music && v.step(pad).select_car == 0);
    pad = {};
    pad.cross = true;
    CHECK(v.step(pad).director && !v.ride_on());
}

TEST(replay_view_ride_on) {
    ReplayView v;
    v.begin(ReplayStyle::Plain);
    ReplayPad cross, r1, l1, r2, l2, left;
    cross.cross = r1.r1 = l1.l1 = r2.r2 = l2.l2 = left.left_held = true;
    CHECK(!v.ride_on() && v.step(r1).view == false);
    CHECK(v.step(cross).view && v.ride_on() && v.mount() == 0);
    const int ahead[5] = {1, 20, 7, 5, 0};
    for (int m : ahead) {
        CHECK(v.step(r1).view);
        CHECK_EQ(v.mount(), m);
        CHECK(!v.look_back());
    }
    CHECK(v.step(l1).view && v.mount() == 0 && v.look_back());
    v.step(r1), v.step(r1), v.step(r1);
    CHECK(v.mount() == 8 && !v.look_back());
    // Held left: its camera as long as it is held, the other keys idle.
    CHECK(v.step(left).view && v.mount() == 10);
    left.r1 = true;
    CHECK(!v.step(left).view && v.mount() == 10);
    CHECK(v.step({}).view && v.mount() == 8);
    // L2 and R2 on and off, R1 back to the places.
    CHECK(v.step(r2).view && v.mount() == 4);
    CHECK(v.step(l2).view && v.mount() == 3);
    CHECK(v.step(l2).view && v.mount() == 8);
    v.step(r2);
    CHECK(v.step(r1).view && v.mount() == 8);
    // The place stays over a change to the course's cameras and back.
    v.step(cross), v.step(cross);
    CHECK(v.ride_on() && v.mount() == 8);
}

TEST(replay_view_zoom_and_speed) {
    ReplayView v;
    v.begin(ReplayStyle::Plain);
    ReplayPad hold;
    hold.triangle_held = true;
    for (int i = 0; i < 120; ++i) v.step(hold);
    CHECK_NEAR(v.zoom(), 4.0, 0.1);
    float last = v.zoom();
    for (int i = 0; i < 40; ++i) {
        v.step({});
        CHECK(v.zoom() <= last);
        last = v.zoom();
    }
    CHECK_NEAR(v.zoom(), 1.0, 0.01);
    ReplayPad l3;
    l3.l3 = true;
    CHECK(v.step(l3).speed && v.speed() == 4);
    CHECK(v.step(l3).speed && v.speed() == 1);
}

TEST(replay_cameras_least_focus) {
    CHECK_NEAR(race::least_focus(1), 16, 1e-5);
    CHECK_NEAR(race::least_focus(2.001f), 16, 1e-2);
    CHECK_NEAR(race::least_focus(15), 4, 1e-4);
    CHECK_NEAR(race::least_focus(30), 1, 1e-4);
    CHECK_NEAR(race::least_focus(90), 0.1, 1e-4);
    CHECK(race::least_focus(8) > 4 && race::least_focus(8) < 16);
}

TEST(replay_effect_settings) {
    gt3r::IniDocument d;
    const race::ReplayEffectSettings s = race::ReplayEffectSettings::load(d);
    CHECK(!s.haze && s.vignette && s.trail && s.depth_of_field == race::ReplayEffectSettings::DepthOfField::High);
    d.set("video", "heat_haze", "on"), d.set("video", "replay_vignette", "off"), d.set("video", "depth_of_field", "standard");
    const race::ReplayEffectSettings t = race::ReplayEffectSettings::load(d);
    CHECK(t.haze && !t.vignette && t.depth_of_field == race::ReplayEffectSettings::DepthOfField::Standard);
}

// A frame's passes: what the camera asks for, in the views the player does not drive in.
TEST(replay_effects_gates) {
    gt3r::race::SunParams env;
    env.has_env = true, env.vignette = true, env.dof_min_focus = 4, env.vignette_outer = 420, env.vignette_inner = 200;
    race::ReplayEffects fx;
    fx.setup(env);
    race::ViewPose pose;
    pose.depth_of_field = true, pose.focus = 2, pose.f_number = 2.8f;
    gt3r::render::Mat4 proj;
    gt3r::render::DrawOptions live, replay, plain;
    fx.apply(live, pose, proj, {}, {}, 5000);
    CHECK(!live.dof.on && !live.vignette);
    race::EffectView v;
    v.replay = true;
    fx.apply(replay, pose, proj, v, {}, 5000);
    CHECK(replay.dof.on && replay.vignette);
    CHECK_NEAR(replay.dof.focus, 4, 1e-6);
    CHECK_NEAR(replay.dof.focal_mm, 24, 1e-6);
    CHECK_NEAR(replay.dof.f_number, 2.8, 1e-6);
    pose.depth_of_field = false;
    fx.apply(plain, pose, proj, v, {}, 5000);
    CHECK(!plain.dof.on && plain.vignette);
}

// Looking aside and the in-car view's shake.
TEST(drive_camera_look_and_shake) {
    race::DriveCamera cam;
    race::CarState car;
    car.velocity = {0, 0, 30};
    gt3r::render::Mat4 body;
    std::vector<race::CameraMount> mounts(8);
    mounts[0] = {{0, 1, 0}, 0, 0, 0, 2.05f, 5};
    mounts[1] = {{0, 1.7f, 6}, 8, 0, 0, 2.7f, 2};
    cam.reset(car);
    const auto ahead = [&](int mount) {
        const race::ViewPose p = cam.pose(car, body, mounts, mount, false);
        return gt3r::render::Vec3{-p.view.at(2, 0), -p.view.at(2, 1), -p.view.at(2, 2)};
    };
    for (int i = 0; i < 60; ++i) cam.step(car, 1.0f / 60);
    CHECK_NEAR(ahead(0).x, 0, 1e-4); // gripping tires: still
    cam.set_sideways(true, false);
    for (int i = 0; i < 60; ++i) cam.step(car, 1.0f / 60);
    CHECK(cam.aside());
    CHECK_NEAR(ahead(0).x, std::sin(60 * 0.0174532925), 1e-3); // (ahead +z, the right -x: left is +x)
    cam.set_sideways(false, false), cam.set_stick(1, 0);
    for (int i = 0; i < 120; ++i) cam.step(car, 1.0f / 60);
    CHECK_NEAR(ahead(0).x, -1, 1e-3);
    CHECK_NEAR(ahead(1).x, -1, 1e-3); // (the chase view goes round the car)
    cam.set_stick(0, 0);
    for (int i = 0; i < 120; ++i) cam.step(car, 1.0f / 60);
    CHECK(!cam.aside());
    for (race::WheelState& w : car.wheels) w.slip_ratio = 1;
    float most = 0, steady = 0;
    for (int i = 0; i < 120; ++i) {
        cam.step(car, 1.0f / 60);
        most = std::max(most, std::fabs(ahead(0).x));
        cam.set_shake(false);
        steady = std::max(steady, std::fabs(ahead(0).x));
        cam.set_shake(true);
    }
    CHECK(most > 1e-3 && most < std::sin(2.01 * 0.0174532925));
    CHECK_NEAR(steady, 0, 1e-4);
}
