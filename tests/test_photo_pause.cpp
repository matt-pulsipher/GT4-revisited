// The photo pause's camera (race/photo_pause.h): the pad as it reads it, the cameras it offers, a step's limits and settings.
// docs/formats/photo.md "The photo pause".

#include "test_framework.h"

#include "gt4r/race/photo_pause.h"

#include <cmath>

using namespace gt4r;
using namespace gt4r::race::photo;

namespace {

bool near(float a, float b, float by) { return std::fabs(a - b) <= by; }

ShotScene scene() {
    ShotScene s;
    s.car = {0, 0, 0};
    s.eye = {0, 2, 20};
    s.view = gt3r::render::look_at(s.eye, s.car, {0, 1, 0});
    s.lens = 30 * 3.14159274f / 180;
    s.points = {{4, {30, 1, 0}}, {0, {-25, 6, 5}}};
    s.ground = [](float, float, float, float& y) { return y = 0, true; };
    s.clear = [](const Vec3&, const Vec3& to) { return to; };
    s.seed = 7;
    return s;
}

} // namespace

TEST(photo_pause_angles) {
    // (0x36EA98, 0x36EA50: 0 along -z, 90 along +x; negative above the horizon)
    CHECK(near(wrap360(yaw_of({0, 0, -1})), 0, 1e-3f) || near(wrap360(yaw_of({0, 0, -1})), 360, 1e-3f));
    CHECK(near(yaw_of({1, 0, 0}), 90, 1e-3f));
    CHECK(near(pitch_of({0, 1, -1}), -45, 1e-3f));
    CHECK(near(wrap360(-30), 330, 1e-4f) && near(wrap360(725), 5, 1e-3f));
    CHECK(near(difference(10, 350), 20, 1e-4f) && near(difference(350, 10), -20, 1e-4f));
    CHECK(near(shutter_seconds(-125), 1.0f / 125, 1e-7f));
}

TEST(photo_pause_pad) {
    // Nothing held: Up / Down raise and lower the eye, Left / Right another camera (and nothing else that step).
    PadButtons b;
    b.up = true;
    CHECK(read_pad(b).climb == 1);
    b.horizontal_repeat = 1;
    Pad p = read_pad(b);
    CHECK(p.camera == 1 && p.climb == 0);
    // A button pressed counts only while the sticks are still: the first of Circle, Triangle, Square, Cross.
    b = {};
    b.cross = true, b.square = true;
    p = read_pad(b);
    CHECK(p.focus && !p.shoot);
    b.left_x = 0.5f;
    p = read_pad(b);
    CHECK(!p.focus && !p.shoot && p.left_x == 0.5f);
    // A shoulder button alone: its setting by Up / Down as they repeat; two together nothing.
    b = {};
    b.l1 = true, b.vertical_repeat = -1;
    p = read_pad(b);
    CHECK(p.exposure == 1 && p.held == 1);
    b.l1 = false, b.r2 = true;
    CHECK(read_pad(b).shutter == 1);
    b.l2 = true;
    p = read_pad(b);
    CHECK(p.shutter == 0 && p.balance == 0 && p.held == 0);
    CHECK(blinking(b) == -1);
    b.r2 = false;
    CHECK(blinking(b) == 0);
    // L1 and Cross: the shutter on the focused place.
    b = {};
    b.l1 = true, b.cross = true;
    CHECK(read_pad(b).shoot_on_point);
}

TEST(photo_pause_shots) {
    const std::vector<Shot> shots = make_shots(scene());
    CHECK(shots.size() == 64);
    // The replay's own camera first: its eye, its angle, looking at the car, held at its height.
    const Shot& s = shots[0];
    CHECK(near(s.eye.z, 20, 1e-4f) && near(s.fov, 30, 1e-3f) && s.y_low == s.y_high);
    CHECK(near(s.distance, std::sqrt(404.0f), 1e-3f));
    CHECK(s.aperture == 5 && s.shutter == 3 && s.exposure == 10 && s.balance == 6);
    // A roadside point of kind 4 may go 0.4 .. 3 m over the ground; its view the 16 m rule's (2 atan(8 / d)).
    CHECK(shots[1].kind == 4 && near(shots[1].y_low, 0.4f, 1e-4f) && near(shots[1].y_high, 3, 1e-4f));
    CHECK(near(shots[1].fov, 2 * std::atan(8.0f / std::hypot(30.0f, 1.0f)) * 57.2958f, 1e-2f));
    CHECK(shots[2].y_low == shots[2].y_high);
    // The rest round the car, within 3 m of the ground, looking at it.
    for (size_t i = 3; i < shots.size(); ++i) CHECK(shots[i].eye.y <= 3.0001f && shots[i].distance < 12);
}

TEST(photo_pause_camera_steps) {
    std::vector<Shot> shots = make_shots(scene());
    ShotCamera cam;
    cam.begin(shots[0]);
    ShotCamera::World w;
    // The closest view: a stretch of 0.7 m at the car's distance; the zoom stops there with the "disable" sound once.
    Pad p;
    p.right_y = -1;
    bool refused = false;
    for (int i = 0; i < 400; ++i) refused = cam.step(p, w).refused || refused;
    CHECK(refused && near(cam.fov(), std::max(2 * std::atan(0.35f / shots[0].distance) * 57.2958f, 2.0f), 1e-3f));
    CHECK(!cam.step(p, w).refused); // (held against it: said once)
    // The widest: 83.2 degrees.
    p.right_y = 1;
    for (int i = 0; i < 400; ++i) cam.step(p, w);
    CHECK(near(cam.fov(), 83.2f, 1e-3f));
    // The roll stops at 90 degrees.
    p = {};
    p.right_x = 1;
    for (int i = 0; i < 200; ++i) cam.step(p, w);
    CHECK(near(cam.roll(), 90, 1e-4f));
    // The settings a step a press within their tables; each camera keeps its own.
    p = {};
    p.aperture = -1;
    for (int i = 0; i < 20; ++i) cam.step(p, w);
    CHECK(near(cam.f_number(), 1.4f, 1e-5f));
    p = {};
    p.balance = 1;
    for (int i = 0; i < 20; ++i) cam.step(p, w);
    CHECK(cam.kelvin() == 7500);
    cam.begin(shots[1]);
    CHECK(near(cam.f_number(), 2.8f, 1e-5f) && cam.kelvin() == 5500);
    cam.begin(shots[0]);
    CHECK(near(cam.f_number(), 1.4f, 1e-5f));
    // Square: the focus on what the middle shows, and manual from then on (AF again on another camera).
    w.middle_depth = [](const Vec3&, const gt3r::render::Mat4&) { return 12.5f; };
    p = {};
    p.focus = true;
    CHECK(cam.step(p, w).focused && cam.manual_focus() && near(cam.focus(), 12.5f, 1e-3f));
    cam.begin(shots[2]);
    CHECK(!cam.manual_focus());
    // The focal length the movie shows (24 mm high film): 2 atan(12 / 50) is 50 mm.
    Shot f;
    f.fov = 2 * std::atan(12.0f / 50.0f) * 57.2958f;
    cam.begin(f);
    CHECK(cam.focal_mm() == 50);
}

TEST(photo_pause_zoom_scenery_check) {
    // A camera whose eye is in the followed car's box grown by 0.5 m (0x380C50): the first zoom in is refused, the view stays and
    // becomes the camera's closest; later steps are held there.
    Shot s;
    s.eye = {0, 1, 0}, s.distance = 2, s.fov = 40;
    ShotCamera cam;
    cam.begin(s);
    ShotCamera::World w;
    float margin_seen = -1;
    w.in_car = [&](const Vec3& e, float margin) {
        margin_seen = margin;
        return std::fabs(e.x) < 1 + margin;
    };
    Pad p;
    p.right_y = -1;
    cam.step(p, w);
    CHECK(near(cam.fov(), 40, 1e-4f) && near(cam.shot().fov_min, 40 / (1 + 0.05f / 18), 1e-3f));
    CHECK(near(margin_seen, 0.5f + 0.07f / std::tan(40 / (1 + 0.05f / 18) * 0.5f / 57.2958f), 1e-4f) && cam.shot().fov_min_found);
    for (int i = 0; i < 50; ++i) cam.step(p, w);
    CHECK(cam.fov() >= cam.shot().fov_min - 1e-4f && cam.fov() < 40);
    // Away from the car the zoom goes on to the 0.7 m stretch.
    Shot far;
    far.eye = {10, 1, 0}, far.distance = 10, far.fov = 40;
    cam.begin(far);
    for (int i = 0; i < 400; ++i) cam.step(p, w);
    CHECK(near(cam.fov(), 2 * std::atan(0.35f / 10) * 57.2958f, 1e-3f) && !cam.shot().fov_min_found);
}

TEST(photo_pause_recompose) {
    // L1 + Cross with the focus on a place: the eye and the place carried with the car, the camera aimed at the place again.
    std::vector<Shot> shots = make_shots(scene());
    ShotCamera cam;
    cam.begin(shots[0]);
    CHECK(!cam.recompose(gt3r::render::translate({1, 0, 0}))); // (no focused place yet)
    ShotCamera::World w;
    w.middle_depth = [](const Vec3&, const gt3r::render::Mat4&) { return 15.0f; };
    Pad p;
    p.focus = true;
    cam.step(p, w);
    const Vec3 eye = cam.eye();
    const gt3r::render::Mat4 v0 = cam.view();
    const Vec3 place = eye + Vec3{-v0.at(2, 0), -v0.at(2, 1), -v0.at(2, 2)} * 15.0f;
    CHECK(cam.recompose(gt3r::render::translate({2, 0, 1})));
    CHECK(near(cam.eye().x, eye.x + 2, 1e-4f) && near(cam.eye().z, eye.z + 1, 1e-4f));
    const Vec3 seen = cam.view().transform_point(place + Vec3{2, 0, 1});
    CHECK(near(seen.x, 0, 1e-3f) && near(seen.y, 0, 1e-3f) && near(seen.z, -15, 1e-3f));
}
