#include "gt4r/race/drive_camera.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

namespace render = gt3r::render;
using render::Mat4;
using RVec = render::Vec3;

constexpr float kPi = 3.14159265f;
// The on-board camera's lens for the in-car and chase views (docs/formats/camera.md "Field of view"): the
// program's 90 degrees as the original's picture shows it, 61.2 degrees across a 4:3 picture's width. Two
// measures agree: the view's size factor K (0x8465E4) is 721.2 in the original's chase view, and for a picture
// 448 lines high K = 224 / (0.75 tan(A / 2)) x 10 / 7 (0x456918), so tan(A / 2) = 0.5916; and the Ka and the
// R390 are as wide in its chase pictures (20-race-chase, 25-sound-cars) as this lens draws them.
constexpr float kOnBoardAngle = 2 * 0.53418f; // 2 atan(0.5916)

float wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}
RVec v3(const course::Vec3& v) { return {v.x, v.y, v.z}; }

// The view matrix of an eye with these axes (unit, at right angles): right, up and the way it looks.
Mat4 view_of(const RVec& eye, const RVec& right, const RVec& up, const RVec& ahead) {
    Mat4 m;
    m.m = {right.x, up.x, -ahead.x, 0, right.y, up.y, -ahead.y, 0, right.z, up.z, -ahead.z, 0,
           -render::dot(right, eye), -render::dot(up, eye), render::dot(ahead, eye), 1};
    return m;
}
RVec turn(const RVec& v, const RVec& k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + render::cross(k, v) * s + k * (render::dot(k, v) * (1 - c));
}

} // namespace

float car_heading(const CarState& car) { return std::atan2(car.forward.x, car.forward.z); }
float car_pitch(const CarState& car) { return std::asin(std::clamp(car.forward.y, -1.0f, 1.0f)); }

void DriveCamera::reset(const CarState& car) {
    heading_ = car_heading(car), pitch_ = car_pitch(car);
    heading_rate_ = heading_accel_ = pitch_rate_ = 0;
    nod_ = nod_rate_ = 0;
    road_pitch_ = pitch_;
    side_ = look_ = 0;
    shake_pitch_ = {}, shake_yaw_ = {};
    for (Spring& j : jolt_) j = {};
    wobble_pitch_ = {}, wobble_yaw_ = {}, shake_side_ = {}, shake_heave_ = {};
}

float DriveCamera::draw() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / float(1u << 24);
}

void DriveCamera::Spring::step(float input) {
    v += ((input - x) * 750.0f + v * -17.0f) / 60.0f;
    x += v / 60.0f;
}

void DriveCamera::Shaker::step(float input) {
    v += ((input - u) - 12.0f * v) / 60.0f;
    x += v / 60.0f;
    u = input;
}

bool DriveCamera::aside() const { return std::fabs(side_) > 5.0f || std::fabs(look_) > 0.35f; }

void DriveCamera::step(const CarState& car, float h) {
    // The chase view's heading follows the car's through a spring of 120 / 15, its pitch through
    // one of 20 / 10.
    const float e = wrap(car_heading(car) - heading_);
    const float a = 120.0f * e - 15.0f * heading_rate_;
    heading_rate_ += (heading_accel_ + a) * 0.5f * h;
    heading_accel_ = a;
    heading_ = wrap(heading_ + heading_rate_ * h);
    const float ep = wrap(car_pitch(car) - pitch_);
    pitch_rate_ += (20.0f * ep - 10.0f * pitch_rate_) * h;
    pitch_ = wrap(pitch_ + pitch_rate_ * h);

    // The in-car view's nod: three times the body's pitch against the road under it (the line
    // from the rear wheels' contacts to the front ones'), at most 3.5 degrees of it, on a spring
    // of 100 / 20.
    const RVec front = (v3(car.wheels[0].center) + v3(car.wheels[1].center)) * 0.5f, rear = (v3(car.wheels[2].center) + v3(car.wheels[3].center)) * 0.5f;
    const RVec along = render::normalize(front - rear);
    if (render::length(along) > 0.5f) road_pitch_ = std::asin(std::clamp(along.y, -1.0f, 1.0f));
    const float limit = 3.5f * kPi / 180.0f;
    const float target = 3.0f * std::clamp(wrap(car_pitch(car) - road_pitch_), -limit, limit);
    nod_rate_ += (100.0f * (target - nod_) - 20.0f * nod_rate_) * h;
    nod_ += nod_rate_ * h;

    // The shake: A, 0.3 of each tire's slip ratio and its slip angle's share of a quarter turn; a draw under a half gives both
    // shakers an input of their own (the yaw's first).
    float slips = 0;
    for (const WheelState& w : car.wheels) slips += std::fabs(w.slip_ratio) * 0.3f + std::fabs(w.slip_angle) / (kPi / 2);
    const bool kick = draw() < 0.5f;
    const float yaw = kick ? (draw() - 0.5f) * slips * 250 : 0.0f;
    const float pitch = kick ? (draw() - 0.5f) * slips * 250 : 0.0f;
    shake_yaw_.step(yaw), shake_pitch_.step(pitch);

    // A hit's jolt (0x37A8E4 .. 0x37AAAC, the collision step's record of the step: CarState::jolt): with a the change along the
    // body and b across it, each signed and squared (a|a|, b|b|), and m the size sqrt(a^2 + b^2), the springs are kicked for
    // that step by: pitch, -(a's sign) x 10 (|a|a|| x (0.4 .. 0.8) + |b|b|| x (0.1 .. 0.5)); yaw, -10 b|b|; roll, 5 m x (0.1 ..
    // 0.8, either way); the shifts m x (0.4 .. 0.8), (0.05 .. 0.2), (0.03 .. 0.3), each either way (the program's random draws,
    // 0x578598, are a generator of ours). Each spring: v += (750 (input - x) - 17 v) / 60, x += v / 60.
    float input[6] = {};
    if (car.jolt) {
        const float a = car.jolt_forward, b = car.jolt_side;
        const float aa = a * std::fabs(a), bb = b * std::fabs(b), m = std::sqrt(std::fabs(aa) + std::fabs(bb));
        auto range = [](float r, float hi, float lo) { return (hi - lo) * r + lo; };                                 // 0x37A448
        auto either = [](float r, float hi, float lo) { r -= 0.5f; return (r < 0 ? -lo : lo) + 2 * r * (hi - lo); }; // 0x37A418
        const float k1 = range(draw(), 0.8f, 0.4f), k2 = range(draw(), 0.5f, 0.1f);
        const float tip = std::fabs(bb) * 10 * k2 + std::fabs(aa) * 10 * k1;
        input[0] = 0 < aa ? -tip : tip;
        input[1] = -bb * 10;
        input[2] = m * 5 * either(draw(), 0.8f, 0.1f);
        input[3] = m * either(draw(), 0.8f, 0.4f), input[4] = m * either(draw(), 0.2f, 0.05f), input[5] = m * either(draw(), 0.3f, 0.03f);
    }
    for (int k = 0; k < 6; ++k) jolt_[k].step(input[k]);

    // The places without bit 2: the wobbles' strength by the speed squared, (v^2 - 7.716) / 493.83 within 0 .. 1 (10 to 80 km/h
    // squared, 0x37A728); the shakers by the body's accelerations.
    const float strength = std::clamp((render::dot(v3(car.velocity), v3(car.velocity)) - 7.716f) / 493.83f, 0.0f, 1.0f);
    for (Wobble* w : {&wobble_pitch_, &wobble_yaw_}) {
        if (draw() < 0.3f) w->target = draw() * 2 - 1;
        w->x += (w->target - w->x) * strength * 0.4f;
    }
    shake_side_.step(car.accel_side * 0.5f), shake_heave_.step(car.accel_up * 2);
    const RVec vel = v3(car.velocity);
    shake_speed_ = std::clamp(render::length(vel), 2.7777778f, 33.333332f) / 33.333332f;
    // The sideways look eases by 0.2 of the rest a step; the stick's way round the car over about 0.2 s.
    side_ += (side_held_ - side_) * 0.2f;
    const float reach = std::sqrt(stick_x_ * stick_x_ + stick_y_ * stick_y_);
    float want = 0;
    if (reach > 0.2f) want = std::atan2(stick_x_, stick_y_) * std::min(1.0f, (reach - 0.2f) / 0.3f);
    look_ += (want - look_) * (1 - std::exp(-14.0f * h));
}

std::vector<CameraMount> read_camera_mounts(ByteSpan file) {
    std::vector<CameraMount> out;
    if (file.size() < 0x40 || !gt3r::has_magic(file, "CAR4")) return out;
    const u32 info = rd32(file, 0x10);
    if (!info || info + 0x48 > file.size()) return out;
    // Info +0x40: count, offset from the file's start; 0x20 bytes each.
    const u32 n = rd32(file, info + 0x40), at = rd32(file, info + 0x44);
    auto f = [&](size_t o) {
        float v;
        std::memcpy(&v, file.data() + o, 4);
        return v;
    };
    for (u32 i = 0; i < n && i < 64 && at + 0x20 * size_t(i) + 0x20 <= file.size(); ++i) {
        const size_t o = at + 0x20 * size_t(i);
        CameraMount m;
        // Stored with y down and z forward: turned into the model's frame.
        m.pos = {f(o), -f(o + 4), -f(o + 8)};
        m.pitch = f(o + 12), m.yaw = f(o + 16), m.roll = f(o + 20);
        m.zoom = f(o + 24), m.flags = rd32(file, o + 28);
        out.push_back(m);
    }
    return out;
}

ViewPose DriveCamera::pose(const CarState& car, const Mat4& body, std::span<const CameraMount> mounts, int mount, bool look_back) const {
    ViewPose p;
    const RVec origin{body.at(0, 3), body.at(1, 3), body.at(2, 3)};
    CameraMount m;
    const bool far_chase = mount == kFarChase, in_car_view = in_car(mount);
    if (far_chase) mount = kChase;
    if (in_car_view) mount = kInCar;
    if (mount >= 0 && size_t(mount) < mounts.size() && mounts[size_t(mount)].flags != 0) m = mounts[size_t(mount)];
    else if (mount == kChase) m.pos = {0, 1.7f, 6.0f}, m.flags = 2; // (a car without the record: the place most cars have)
    else m.pos = {0, 1.0f, 0.2f}, m.zoom = 2.0503f, m.flags = 5;

    if (mount == kChase) {
        // A level frame at the car's origin with the lagged heading and pitch; the eye at the
        // place in it, looking straight along it. Looking back turns the frame half round.
        // (The stick's look goes round the car.)
        const float heading = look_back ? heading_ + kPi : heading_ - look_, pitch = look_back ? -pitch_ : pitch_ * std::cos(look_);
        if (far_chase) m.pos = m.pos * 1.45f;
        const RVec level{std::sin(heading), 0, std::cos(heading)};
        const RVec right = render::cross(level, {0, 1, 0});
        const RVec ahead = turn(level, right, pitch), up = render::cross(right, ahead);
        // (The place is in the model's frame: x right, y up, the front at -z.)
        p.eye = origin + right * m.pos.x + up * m.pos.y - ahead * m.pos.z;
        p.view = view_of(p.eye, right, up, ahead);
        p.fov = kOnBoardAngle;
        p.body_hidden = false;
        return p;
    }
    // The other views ride the body: its heading, pitch and roll.
    RVec right = v3(car.right), up = v3(car.up), ahead = v3(car.forward);
    if (mount != kInCar && (m.flags & 4) == 0) // (the side and heave shakers move a place without bit 2: 0x37B6BC)
        m.pos.x += 2 * std::clamp(shake_side_.x, -1.0f, 1.0f), m.pos.y -= 2 * std::clamp(shake_heave_.x, -1.0f, 1.0f);
    p.eye = origin + right * m.pos.x + up * m.pos.y - ahead * m.pos.z;
    if (mount == kInCar) {
        const float nod = car.nod_given ? car.nod : nod_; // (the simulation's sway, read at 0x34A760)
        ahead = turn(ahead, right, nod), up = turn(up, right, nod);
        p.fov = kOnBoardAngle;
        if (look_back) ahead = ahead * -1.0f, right = right * -1.0f;
        // Looking aside, then a hit's jolt, then the shake nearest the eye: 2 and 8 degrees by the speed.
        const float aside = side_ * kPi / 180.0f + (look_back ? 0.0f : look_);
        ahead = turn(ahead, up, -aside), right = turn(right, up, -aside);
        // The jolt (0x37B13C ..): the shifts held within 0.6, 0.1 and 0.3 m, the turns within 90 degrees; turned as a camera
        // place is (its yaw, pitch, roll about the shifted eye).
        const float deg = kPi / 180.0f;
        const float sx = std::clamp(jolt_[3].x, -0.6f, 0.6f), sy = std::clamp(jolt_[4].x, -0.1f, 0.1f), sz = std::clamp(jolt_[5].x, -0.3f, 0.3f);
        p.eye = p.eye + right * sx - up * sy + ahead * sz;
        const float jyaw = std::clamp(jolt_[1].x, -90.0f, 90.0f) * deg, jpitch = std::clamp(jolt_[0].x, -90.0f, 90.0f) * deg, jroll = std::clamp(jolt_[2].x, -90.0f, 90.0f) * deg;
        ahead = turn(ahead, up, -jyaw), right = turn(right, up, -jyaw);
        ahead = turn(ahead, right, -jpitch), up = turn(up, right, -jpitch);
        right = turn(right, ahead, jroll), up = turn(up, ahead, jroll);
        if (shake_on_) {
            const float yaw = 2 * shake_speed_ * std::clamp(shake_yaw_.x, -1.0f, 1.0f) * kPi / 180.0f, tip = 8 * shake_speed_ * std::clamp(shake_pitch_.x, -1.0f, 1.0f) * kPi / 180.0f;
            ahead = turn(ahead, up, -yaw), right = turn(right, up, -yaw);
            ahead = turn(ahead, right, -tip), up = turn(up, right, -tip);
        }
    } else {
        // The place's own turn: yaw (to the right), then pitch (a positive one looks down: the
        // chase place's 8 degrees toward the car), then roll.
        const float yaw = (m.yaw + (look_back ? 180.0f : 0.0f)) * kPi / 180.0f, pitch = (look_back ? m.pitch : -m.pitch) * kPi / 180.0f;
        ahead = turn(ahead, up, -yaw), right = turn(right, up, -yaw);
        ahead = turn(ahead, right, pitch), up = turn(up, right, pitch);
        const float roll = (look_back ? -m.roll : m.roll) * kPi / 180.0f;
        right = turn(right, ahead, roll), up = turn(up, ahead, roll);
        if ((m.flags & 4) == 0) {
            // A place without bit 2 (0x37B690): nearest the eye the wobbles, 0.7 degrees each at their full -1 .. 1, in pitch
            // and yaw; the eye moved across and up by twice the side and heave shakers' outputs (each within -1 .. 1).
            const float wyaw = 0.7f * std::clamp(wobble_yaw_.x, -1.0f, 1.0f) * kPi / 180.0f, wpitch = 0.7f * std::clamp(wobble_pitch_.x, -1.0f, 1.0f) * kPi / 180.0f;
            ahead = turn(ahead, up, -wyaw), right = turn(right, up, -wyaw);
            ahead = turn(ahead, right, -wpitch), up = turn(up, right, -wpitch);
        }
        p.fov = m.zoom > 0.1f && std::isfinite(m.zoom) ? 2 * std::atan(1.0f / m.zoom) : kOnBoardAngle;
    }
    p.view = view_of(p.eye, right, up, ahead);
    p.body_hidden = (m.flags & 2) == 0;
    return p;
}

ViewPose blend_poses(const ViewPose& a, const ViewPose& b, float t, float cut) {
    if (!(t < 1) || a.body_hidden != b.body_hidden) return b;
    t = std::max(t, 0.0f);
    const float dx = b.eye.x - a.eye.x, dy = b.eye.y - a.eye.y, dz = b.eye.z - a.eye.z;
    if (dx * dx + dy * dy + dz * dz > cut * cut || std::fabs(a.fov - b.fov) > 0.2f) return b;
    ViewPose p = b;
    p.eye = {a.eye.x + dx * t, a.eye.y + dy * t, a.eye.z + dz * t};
    p.fov = a.fov + (b.fov - a.fov) * t;
    p.focus = a.focus + (b.focus - a.focus) * t;
    // The turn's rows (the view's axes in course space): blended, then made square: the backward
    // axis, the up axis square to it, the right axis from both.
    float row[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) row[r][c] = a.view.at(r, c) + (b.view.at(r, c) - a.view.at(r, c)) * t;
    auto norm = [](float* v) {
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n > 1e-9f) v[0] /= n, v[1] /= n, v[2] /= n;
    };
    norm(row[2]);
    const float k = row[1][0] * row[2][0] + row[1][1] * row[2][1] + row[1][2] * row[2][2];
    for (int c = 0; c < 3; ++c) row[1][c] -= row[2][c] * k;
    norm(row[1]);
    const float rx = row[1][1] * row[2][2] - row[1][2] * row[2][1], ry = row[1][2] * row[2][0] - row[1][0] * row[2][2], rz = row[1][0] * row[2][1] - row[1][1] * row[2][0];
    const float sign = rx * row[0][0] + ry * row[0][1] + rz * row[0][2] >= 0 ? 1.0f : -1.0f;
    row[0][0] = rx * sign, row[0][1] = ry * sign, row[0][2] = rz * sign;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) p.view.at(r, c) = row[r][c];
        p.view.at(r, 3) = -(row[r][0] * p.eye.x + row[r][1] * p.eye.y + row[r][2] * p.eye.z);
    }
    return p;
}

} // namespace gt4r::race
