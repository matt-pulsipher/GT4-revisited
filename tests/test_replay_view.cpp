// Watching a replay: the controls (race/replay_view.h), the passes' settings and the cameras' focus.
#include "race/sun_glare.h"

#include "base/config.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/car_visual.h"
#include "gt4r/race/dive_camera.h"
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

// The near plane: the on-board camera's 0.1 m; the others' 0.1 x tan(35) / tan(A / 2), 0.1 at 70 degrees, 0.80 at a 10-degree
// telephoto shot; the views a car's places give keep 0.1, a blend follows its ends.
TEST(camera_near_plane) {
    const float deg = 3.14159265f / 180;
    CHECK_NEAR(race::lens_near(70 * deg), 0.1, 1e-4);
    CHECK_NEAR(race::lens_near(10 * deg), 0.1 * 0.70020831 / std::tan(5 * deg), 1e-5);
    CHECK(race::lens_near(10 * deg) > 0.79f && race::lens_near(10 * deg) < 0.81f);
    race::DriveCamera cam;
    race::CarState car{};
    cam.reset(car);
    std::vector<race::CameraMount> mounts(7);
    mounts[1].pos = {0, 1.7f, 6}, mounts[1].flags = 2;
    const race::ViewPose chase = cam.pose(car, gt3r::render::Mat4::identity(), mounts, race::DriveCamera::kChase, false);
    CHECK(chase.on_board);
    CHECK_NEAR(chase.near, race::kScreenNear, 0);
    race::ViewPose a = chase, b = chase;
    a.near = 0.1f, b.near = 0.3f;
    CHECK_NEAR(race::blend_poses(a, b, 0.5f).near, 0.2, 1e-6);
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

TEST(dive_clock_and_markers) {
    race::MusicMarkers m;
    m.at = {1, 2.5f, 4}, m.length = 10;
    CHECK_NEAR(*m.next(0), 1 - 1.0 / 30, 1e-5); // (it looks 1 / 30 s ahead)
    CHECK_NEAR(*m.next(4), 10 + 1 - 4 - 1.0 / 30, 1e-5); // the piece goes round
    CHECK(m.passed(0.95f, 0.97f) && !m.passed(0.97f, 1.0f) && m.passed(9.9f, 0));
    CHECK(!race::MusicMarkers{}.next(0));
    // Cuts at the markers while the next is within 8 s, else every 8 s.
    race::DiveClock clock;
    int cuts = 0;
    float before = 0, now = 0;
    for (int i = 0; i < 600; ++i) {
        before = now, now += 1.0f / 60;
        if (now >= m.length) now -= m.length;
        cuts += clock.step(m.next(now), m.passed(before, now));
    }
    CHECK_EQ(cuts, 4); // the three markers and the piece's end
    race::DiveClock alone;
    cuts = 0;
    for (int i = 0; i < 1000; ++i) cuts += alone.step(std::nullopt, false);
    CHECK_EQ(cuts, 2);
    const race::EaseCurve line, slow(0.1f, 0, 1, 0);
    CHECK_NEAR(line.at(0.3f), 0.3, 1e-6);
    CHECK_NEAR(slow.at(0), 0, 1e-6);
    CHECK_NEAR(slow.at(1), 1, 1e-6);
    CHECK_NEAR(slow.at(0.5f), 0.5, 1e-6);
}

// The director over the courses: every step a view of a car, cut by its clock.
TEST(dive_director_disc) {
    if (test::reference_discs().empty()) SKIP("set GT4R_DISC");
    int courses = 0;
    for (const auto& disc : test::reference_discs()) {
        if (disc->info().game != Game::GT4) continue;
        race::DiveDirector d;
        const auto image = disc->program_image();
        CHECK(image && d.load(image->program, image->load_address));
        for (const char* name : {"crs/tsukuba", "crs/trialmt", "crs/newyork"}) {
            const auto* e = disc->volume().find(name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            const course::CourseFile file = course::CourseFile::parse(data);
            const race::ReplayScript script = race::ReplayScript::parse(file.section(data, course::kSlotReplay));
            CHECK(script.points.size() > 20);
            ++courses;
            std::vector<race::CarState> cars(3);
            race::DiveDirector::World w;
            w.cars = cars.size(), w.script = &script, w.lap_length = script.lap_length;
            w.car = [&](size_t i) {
                race::DiveDirector::Car c;
                c.state = &cars[i];
                c.body.at(0, 3) = cars[i].position.x, c.body.at(1, 3) = cars[i].position.y, c.body.at(2, 3) = cars[i].position.z;
                c.box_min = {-0.9f, 0, -2}, c.box_max = {0.9f, 1.3f, 2};
                return c;
            };
            d.reset(1234, 0);
            int cut = 0, seen = 0;
            for (int i = 0; i < 1800; ++i) {
                for (size_t k = 0; k < cars.size(); ++k) {
                    // (along the script's points, as a car goes round)
                    cars[k].course_v = std::fmod(float(i) * 0.6f + float(k) * 12, script.lap_length);
                    const race::ReplayPoint* at = &script.points[0];
                    for (const race::ReplayPoint& p : script.points)
                        if (std::fabs((p.start + p.end) / 2 - cars[k].course_v) < std::fabs((at->start + at->end) / 2 - cars[k].course_v)) at = &p;
                    cars[k].position = {at->at.x + 4, at->at.y, at->at.z + 3};
                    cars[k].velocity = {0, 0, 36}, cars[k].speed = 36;
                }
                race::ViewPose p;
                CHECK(d.step(w, i % 90 == 0, 1.5f, p));
                cut += d.is_cut();
                CHECK(std::isfinite(p.eye.x) && std::isfinite(p.view.at(0, 0)) && p.fov > 0.01f && p.fov < 3.0f);
                CHECK(d.trail() >= 0 && d.trail() <= 1.5f);
                seen |= 1 << int(d.kind());
            }
            CHECK_EQ(cut, 20);
            CHECK((seen & (1 << int(race::DiveDirector::Kind::Point))) != 0);
        }
    }
    if (!courses) SKIP("no Gran Turismo 4 disc");
}

// The cars' lamp glows: the directional power is the record's x the course's factor (Env0 +0x17A0), rounded as the EE does, at most 32.
TEST(car_lamp_power) {
    CHECK_EQ(race::lamp_power(16, 1), 16);
    CHECK_EQ(race::lamp_power(4, 4), 16);
    CHECK_EQ(race::lamp_power(16, 4), 32);
    CHECK_EQ(race::lamp_power(4, 0), 4);      // (0 reads as 1)
    CHECK_EQ(race::lamp_power(1, 0.49f), 0);  // (0.49 + 0.5 toward zero)
    CHECK_EQ(race::lamp_power(1, 0.5f), 1);
    CHECK_EQ(race::lamp_power(-3, 1), 0);
}

// Prologue's wheel level by its size on screen (0x412918): 0.09, 0.03 and 0.001.
TEST(prologue_wheel_detail_levels) {
    CHECK_EQ(race::prologue_wheel_detail(1, 0.1f, 1), 0);
    CHECK_EQ(race::prologue_wheel_detail(1, 0.05f, 1), 1);
    CHECK_EQ(race::prologue_wheel_detail(1, 0.02f, 1), 2);
    CHECK_EQ(race::prologue_wheel_detail(1, 0.002f, 1), 2);
    CHECK_EQ(race::prologue_wheel_detail(1, 0.0005f, 1), 3);
}
