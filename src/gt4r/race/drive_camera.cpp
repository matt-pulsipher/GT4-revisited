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
// The on-board camera's angle for the in-car and chase views (docs/formats/camera.md "Field of
// view"): the program's own number; how it meets the picture has not been compared with the
// original running.
constexpr float kOnBoardAngle = 90.0f * kPi / 180.0f;

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
}

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
    if (mount >= 0 && size_t(mount) < mounts.size() && mounts[size_t(mount)].flags != 0) m = mounts[size_t(mount)];
    else if (mount == kChase) m.pos = {0, 1.7f, 6.0f}, m.flags = 2; // (a car without the record: the place most cars have)
    else m.pos = {0, 1.0f, 0.2f}, m.zoom = 2.0503f, m.flags = 5;

    if (mount == kChase) {
        // A level frame at the car's origin with the lagged heading and pitch; the eye at the
        // place in it, looking straight along it. Looking back turns the frame half round.
        const float heading = look_back ? heading_ + kPi : heading_, pitch = look_back ? -pitch_ : pitch_;
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
    p.eye = origin + right * m.pos.x + up * m.pos.y - ahead * m.pos.z;
    if (mount == kInCar) {
        ahead = turn(ahead, right, nod_), up = turn(up, right, nod_);
        p.fov = kOnBoardAngle;
        if (look_back) ahead = ahead * -1.0f, right = right * -1.0f;
    } else {
        // The place's own turn: yaw (to the right), then pitch (a positive one looks down: the
        // chase place's 8 degrees toward the car), then roll.
        const float yaw = (m.yaw + (look_back ? 180.0f : 0.0f)) * kPi / 180.0f, pitch = (look_back ? m.pitch : -m.pitch) * kPi / 180.0f;
        ahead = turn(ahead, up, -yaw), right = turn(right, up, -yaw);
        ahead = turn(ahead, right, pitch), up = turn(up, right, pitch);
        const float roll = (look_back ? -m.roll : m.roll) * kPi / 180.0f;
        right = turn(right, ahead, roll), up = turn(up, ahead, roll);
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
