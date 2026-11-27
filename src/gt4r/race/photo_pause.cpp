#include "gt4r/race/photo_pause.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race::photo {

namespace render = gt3r::render;

namespace {

constexpr float kPi = 3.14159274f;
constexpr float kDeg = 57.2958f;
constexpr float kWidest = 83.2f; // 0x3FFC68
constexpr int kMostShots = 64;   // 0x3FFD10: 0x1D00 bytes of records

// The default view of a camera `d` metres from the car (0x3FFC68): a stretch of 16 m across the picture's height, 2 .. 83.2 degrees.
float default_fov(float d) { return std::clamp(2 * std::atan(1 / ((d + d) * 0.0625f)) * kDeg, 2.0f, kWidest); }
constexpr float kScreenValue = 0; // the camera manager's +0xA04 (its screen +0x44)
// The closest view (0x37FD10): a stretch of 0.7 m.
float closest_fov(float d) { return std::max(2 * std::atan(1 / ((d + d) / 0.7f)) * kDeg, 2.0f); }
// The way a roadside camera first looks (0x36EBE8): above the car by a share of its view (the car below the middle).
float tilted(float pitch, float fov_max, float k) { return pitch - std::cos(pitch * kPi / 180.0f) * (fov_max * 0.5f) * k; }
// The view's top limit (0x3FFE50): straight up, turned from the first way; within the widest view.
float lowest_pitch(float pitch) {
    const float low = difference(0, wrap360(pitch)) - 90;
    return kWidest <= low ? -kWidest * 0.5f : low;
}

// The program's random numbers (0x578598) are not read: a generator of ours, 0 .. 1. 🔎
struct Random {
    u32 s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / float(1u << 24);
    }
};

Shot aimed_at(const Vec3& eye, const Vec3& car, float fov) {
    Shot s;
    s.eye = eye;
    const Vec3 d = car - eye;
    s.distance = std::max(render::length(d), 0.1f);
    s.focus = s.distance;
    s.fov = fov;
    return s;
}

// A view's edges [at - extent / 2, at + extent / 2] kept within [lo, hi] (0x37FEE0); both edges out: the middle.
float keep_within(float at, float extent, float lo, float hi, bool& low, bool& high) {
    low = high = false;
    const float half = extent * 0.5f;
    if (at - half <= lo) low = true, at = lo + half;
    if (hi <= at + half) {
        high = true, at = hi - half;
        if (at - half <= lo) low = true, at = (lo + hi) * 0.5f;
    }
    return at;
}

} // namespace

float shutter_seconds(int value) { return value < 0 ? 1.0f / float(-value) : value == 0 ? 1.0f : float(value) / 100.0f; }

float yaw_of(const Vec3& d) { return (kPi - std::atan2(d.x, d.z)) * kDeg; }
float pitch_of(const Vec3& d) { return -std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * kDeg; }
float wrap360(float a) {
    if (360 <= a) a -= std::trunc(a / 360) * 360;
    if (a < 0) a = 360 - (-a - std::trunc(-a / 360) * 360);
    return a;
}
float difference(float a, float b) {
    float d = a - b;
    if (180 < d) d -= 360;
    if (d < -180) d += 360;
    return d;
}

std::vector<Shot> make_shots(const ShotScene& scene) {
    std::vector<Shot> shots;
    // The camera the replay shows: its eye fixed, its angle (no wider than the widest), no roll.
    {
        Shot s = aimed_at(scene.eye, scene.car, std::min(scene.lens * kDeg, kWidest));
        const Vec3 ahead{-scene.view.at(2, 0), -scene.view.at(2, 1), -scene.view.at(2, 2)};
        s.yaw = wrap360(yaw_of(ahead)), s.pitch = pitch_of(ahead);
        s.pitch_low = lowest_pitch(s.pitch), s.pitch_high = kWidest * 0.5f;
        s.y_low = s.y_high = s.eye.y;
        shots.push_back(s);
    }
    // The roadside points that serve the car's stretch: aimed at the car, a little above it.
    for (const auto& [kind, at] : scene.points) {
        if (shots.size() >= size_t(kMostShots)) break;
        Shot s = aimed_at(at, scene.car, default_fov(std::max(render::length(scene.car - at), 0.1f)));
        s.kind = kind;
        const Vec3 d = scene.car - at;
        s.pitch = tilted(pitch_of(d), s.fov, 0.45f), s.yaw = yaw_of(d);
        s.pitch_low = lowest_pitch(s.pitch), s.pitch_high = kWidest * 0.5f;
        s.y_low = s.y_high = at.y;
        float g = 0;
        if ((kind & 4) && scene.ground && scene.ground(at.x, at.z, at.y, g)) s.y_low = g + 0.4f, s.y_high = g + 3 < at.y ? at.y : g + 3;
        shots.push_back(s);
    }
    // The rest round the car, each at its own turn about it, a few metres off and 0.8 m up, out of what stands there (0x3FFA48).
    const int rest = kMostShots - int(shots.size());
    Random rnd{scene.seed ? scene.seed : 1};
    const float start = rnd.next() * 360, step = rest > 0 ? 360.0f / float(rest) : 0;
    const float corner = std::max({std::hypot(scene.front, scene.half_width), std::hypot(scene.rear, scene.half_width), scene.half_width});
    for (int k = 0; k < rest; ++k) {
        const float turn = wrap360(step * (float(k) + rnd.next() * 0.7f) + start) * kPi / 180.0f;
        const float r = rnd.next() * 8 + corner + 0.2f;
        // (in the car's frame: x right, z back)
        Vec3 p = scene.car + scene.right * (r * std::sin(turn)) + scene.back * (r * std::cos(turn)) + Vec3{0, 0.8f, 0};
        if (scene.clear) p = scene.clear(scene.car + Vec3{0, 0.8f, 0}, p);
        float g = 0;
        if (!scene.ground || !scene.ground(p.x, p.z, p.y + 1, g)) continue;
        if (g + 3 < p.y) p.y = g + 3;
        Shot s = aimed_at(p, scene.car, default_fov(std::max(render::length(scene.car - p), 0.1f)));
        const Vec3 d = scene.car - p;
        s.pitch = tilted(pitch_of(d), s.fov, 0.45f), s.yaw = yaw_of(d);
        s.pitch_low = lowest_pitch(s.pitch), s.pitch_high = kWidest * 0.5f;
        s.y_low = g + 0.4f, s.y_high = g + 3;
        shots.push_back(s);
    }
    return shots;
}

Pad read_pad(const PadButtons& b) {
    Pad p;
    const int which = (b.l1 ? 1 : 0) | (b.l2 ? 2 : 0) | (b.r1 ? 4 : 0) | (b.r2 ? 8 : 0);
    const s8 setting = s8(-b.vertical_repeat); // (up raises a setting)
    switch (which) {
    case 0:
        if (b.horizontal_repeat) {
            p.camera = s8(b.horizontal_repeat);
            break;
        }
        p.climb = s8((b.up ? 1 : 0) - (b.down ? 1 : 0));
        p.left_x = b.left_x, p.left_y = b.left_y, p.right_x = b.right_x, p.right_y = b.right_y;
        if (p.left_x == 0 && p.left_y == 0 && p.right_x == 0 && p.right_y == 0) {
            if (b.circle) p.portrait = true;
            else if (b.triangle) p.filter = true;
            else if (b.square) p.focus = true;
            else if (b.cross) p.shoot = true;
        }
        break;
    case 1:
        p.exposure = setting, p.held = 1;
        if (setting == 0 && b.cross) p.shoot_on_point = true;
        break;
    case 2: p.balance = setting, p.held = 2; break;
    case 4: p.aperture = setting, p.held = 3; break;
    case 8: p.shutter = setting, p.held = 4; break;
    default: break;
    }
    return p;
}

int blinking(const PadButtons& b) {
    int n = 0, v = -1;
    if (b.l2) v = 0, ++n;
    if (b.l1) v = 1, ++n;
    if (b.r2) v = 2, ++n;
    if (b.r1) v = 3, ++n;
    return n < 2 ? v : -1;
}

float ShotCamera::Ramp::step(bool on, float most, int sign) {
    if (!on) {
        count = 0;
        return 0;
    }
    if (count < limit) ++count;
    const float v = most * float(count) / float(limit);
    return sign < 0 ? -v : v;
}

void ShotCamera::begin(Shot& shot, float aspect) {
    shot_ = &shot, aspect_ = aspect;
    focus_ = shot.focus;
    shot.manual_focus = false;
    if (!shot.fov_min_set) {
        shot.fov_min = closest_fov(shot.distance);
        shot.limits = 0;
        shot.fov_min_set = true;
    }
    turn_ = {}, roll_ = {}, zoom_ = {}, climb_ = {0, 30};
    roll_hold_ = 0, at_fov_limit_ = at_roll_limit_ = false, pushed_ = {}, clamp_again_ = true;
}

ShotCamera::Events ShotCamera::step(const Pad& pad, const World& world) {
    Events e;
    Shot& s = *shot_;
    if (pad.portrait) s.portrait = !s.portrait;
    // The eye up or down (Up / Down), faster for 30 steps up to 0.05 m a step.
    s.eye.y = std::clamp(s.eye.y + climb_.step(pad.climb != 0, 0.05f) * float(pad.climb), s.y_low, s.y_high);
    // The zoom (the right stick up and down), faster for 18 steps up to 5 % a step, within the closest and the widest view.
    float fov = s.fov;
    const float old = fov;
    const float z = pad.right_y * 0.05f, by = zoom_.step(z != 0, std::fabs(z)) + 1;
    fov = z < 0 ? fov / by : fov * by;
    bool hit = false;
    if (fov < s.fov_min) fov = s.fov_min, hit = true;
    if (s.fov_max < fov) fov = s.fov_max, hit = true;
    if (hit && !at_fov_limit_) e.refused = true;
    at_fov_limit_ = hit;
    // The scenery's check of the new view (0x380C50 -> 0x3FDD60): not with the eye in the followed car's box grown by
    // 0.5 m + 0.7 x the manager's screen value +0x44 / tan(half the angle) (❓ that value: its setter 0x5F53C8 is never called, so
    // 0 here), nor closer than a 0.7 m stretch at the car's distance. The first time it fails the view stays and becomes the
    // shot's closest (when wider than it was).
    if (world.in_car) {
        const float margin = kScreenValue * 0.7f / std::tan(fov * 0.5f * kPi / 180.0f) + 0.5f;
        const bool allowed = !world.in_car(s.eye, margin) && !(fov < 2 * std::atan(0.35f / s.distance) * kDeg);
        if (!allowed && !s.fov_min_found) {
            if (s.fov_min < fov) s.fov_min = fov;
            s.fov_min_found = true;
            fov = old;
        }
    }
    // The turn (the left stick), faster for 18 steps up to 2 / 60 of the view's angle a step; the roll (the right stick across),
    // up to 2 degrees a step, held at 0 for 30 steps when it crosses it, within 90 degrees each way.
    const float k = turn_.step(pad.left_x != 0 || pad.left_y != 0, 2) * fov / 60;
    const float turn_x = k * pad.left_x, turn_y = k * pad.left_y;
    float roll = s.turn_roll;
    if (pad.right_x != 0) {
        const float by_roll = roll_.step(true, 2) * pad.right_x;
        if (roll == 0) {
            if (roll_hold_ > 0) --roll_hold_;
            else roll += by_roll;
        } else {
            const float next = roll + by_roll;
            const bool crossed = next < 0 ? 0 < roll : 0 < next ? roll < 0 : true;
            if (crossed) roll = 0, roll_hold_ = 30;
            else roll = next, roll_hold_ = 0;
        }
    } else {
        roll_.step(false, 2);
        roll_hold_ = 0;
    }
    hit = false;
    if (roll < -90) roll = -90, hit = true;
    if (90 < roll) roll = 90, hit = true;
    if (hit && !at_roll_limit_) e.refused = true;
    at_roll_limit_ = hit;
    // The turn is the picture's way: turned by the roll.
    const float r = -roll * kPi / 180.0f, sr = std::sin(r), cr = std::cos(r);
    const float yaw_by = turn_x * cr + turn_y * sr, pitch_by = -turn_x * sr + turn_y * cr;
    const bool again = fov != old || clamp_again_;
    s.limits = 0;
    std::array<bool, 4> push{};
    if (yaw_by != 0 || again) {
        const float half = s.fov_max * aspect_ * 0.5f;
        bool lo = false, hi = false;
        s.turn_yaw = keep_within(s.turn_yaw + yaw_by, fov * aspect_, -half, half, lo, hi);
        if (lo) s.limits |= 1, push[0] = yaw_by < 0;
        if (hi) s.limits |= 2, push[1] = 0 < yaw_by;
    }
    if (pitch_by != 0 || again) {
        float p = wrap360(s.turn_pitch);
        if (180 < p) p -= 360;
        bool lo = false, hi = false;
        s.turn_pitch = keep_within(p + pitch_by, fov, s.pitch_low, s.pitch_high, lo, hi);
        if (lo) s.limits |= 4, push[2] = pitch_by < 0;
        if (hi) s.limits |= 8, push[3] = 0 < pitch_by;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (push[i] && !pushed_[i]) e.refused = true;
        pushed_[i] = push[i];
    }
    s.turn_roll = roll;
    // The settings, a step a press (with the shoulder button that names them held).
    s.aperture = std::clamp(s.aperture + pad.aperture, 0, int(kApertures.size()) - 1);
    s.shutter = std::clamp(s.shutter + pad.shutter, 0, int(kShutters.size()) - 1);
    s.exposure = std::clamp(s.exposure + pad.exposure, 0, int(kExposures.size()) - 1);
    s.balance = std::clamp(s.balance + pad.balance, 0, 10);
    s.fov = fov;
    const render::Mat4 v = view();
    // Square: the focus on what the finder's middle shows, kept on that place as the camera turns (0x37F640, 0x37F4E0 🔎).
    if (pad.focus) {
        const float d = std::max(world.middle_depth ? world.middle_depth(s.eye, v) : s.distance, 0.1f);
        focus_ = s.focus = d;
        s.manual_focus = true;
        const Vec3 ahead{-v.at(2, 0), -v.at(2, 1), -v.at(2, 2)};
        s.focus_point = s.eye + ahead * d, s.focus_point_found = true;
        e.focused = true;
    }
    e.shutter = pad.shoot || pad.shoot_on_point;
    e.on_point = pad.shoot_on_point;
    if (s.manual_focus) {
        if (s.focus_point_found) focus_ = std::max(-v.transform_point(s.focus_point).z, 0.1f);
    } else if (world.car_focus) {
        if (const std::optional<float> f = world.car_focus(v, f_number(), fov)) focus_ = s.focus = *f;
    }
    clamp_again_ = false;
    return e;
}

bool ShotCamera::recompose(const render::Mat4& moved) {
    Shot& s = *shot_;
    if (!s.manual_focus || !s.focus_point_found) return false;
    s.eye = moved.transform_point(s.eye);
    s.focus_point = moved.transform_point(s.focus_point);
    const Vec3 d = s.focus_point - s.eye;
    s.turn_yaw = difference(yaw_of(d), s.yaw), s.turn_pitch = pitch_of(d) - s.pitch;
    focus_ = s.focus = std::max(render::length(d), 0.1f);
    return true;
}

render::Mat4 ShotCamera::view() const {
    const float d = kPi / 180.0f;
    return render::rotate_z(roll() * d) * render::rotate_x(pitch() * d) * render::rotate_y(yaw() * d) * render::translate(-eye());
}

float ShotCamera::f_number() const { return float(kApertures[size_t(std::clamp(shot_->aperture, 0, 15))]) / 100.0f; }
float ShotCamera::shutter_seconds() const { return photo::shutter_seconds(kShutters[size_t(std::clamp(shot_->shutter, 0, 18))]); }
float ShotCamera::exposure() const { return kExposures[size_t(std::clamp(shot_->exposure, 0, 20))]; }
float ShotCamera::kelvin() const { return kBalances[size_t(std::clamp(shot_->balance, 0, 10))]; }
int ShotCamera::focal_mm() const {
    const int tenths = int(240.0f / (2 * std::tan(shot_->fov * 0.5f * kPi / 180.0f)));
    return int(float(tenths) / 10.0f + 0.5f);
}

} // namespace gt4r::race::photo
