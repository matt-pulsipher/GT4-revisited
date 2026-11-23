#include "gt4r/race/replay_script.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

namespace render = gt3r::render;
using RVec = render::Vec3;
constexpr float kPi = 3.14159265f;

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

// A curve at an offset of the block: u32 n, then n x {f32 t, f32 value}. (0: none.)
ReplayCurve read_curve(ByteSpan d, u32 at) {
    ReplayCurve c;
    if (!at) return c;
    if (at + 4 > d.size()) throw FormatError("REP4: a curve is outside the block");
    const u32 n = rd32(d, at);
    if (n > 4096 || at + 4 + 8 * u64(n) > d.size()) throw FormatError("REP4: a curve is outside the block");
    c.keys.resize(n);
    for (u32 i = 0; i < n; ++i) c.keys[i] = {f32_at(d, at + 4 + 8 * size_t(i)), f32_at(d, at + 8 + 8 * size_t(i))};
    return c;
}

RVec turn(const RVec& v, const RVec& k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + render::cross(k, v) * s + k * (render::dot(k, v) * (1 - c));
}

} // namespace

float ReplayCurve::at(float t) const {
    if (keys.empty()) return 0;
    if (t <= keys.front()[0]) return keys.front()[1];
    if (t >= keys.back()[0]) return keys.back()[1];
    const auto hi = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const std::array<float, 2>& k) { return x < k[0]; });
    const auto lo = hi - 1;
    const float span = (*hi)[0] - (*lo)[0];
    return span > 0 ? (*lo)[1] + ((*hi)[1] - (*lo)[1]) * (t - (*lo)[0]) / span : (*hi)[1];
}

ReplayScript ReplayScript::parse(ByteSpan d) {
    if (!is_script(d)) throw FormatError("not a REP4 replay script");
    ReplayScript s;
    const u32 size = rd32(d, 0x0C);
    if (size < 0x40 || size > d.size()) throw FormatError("REP4: bad size");
    d = d.first(size);
    s.lap_length = f32_at(d, 0x14), s.end = f32_at(d, 0x18), s.variants = rd32(d, 0x1C);
    const u32 n = rd32(d, 0x20), at = rd32(d, 0x24);
    if (n > 4096 || at + 0x7C * u64(n) > d.size()) throw FormatError("REP4: records outside the block");
    for (u32 i = 0; i < n; ++i) {
        const size_t o = at + 0x7C * size_t(i);
        ReplayRecord r;
        r.kind = rd32(d, o), r.start = f32_at(d, o + 4), r.end = f32_at(d, o + 8);
        const float aperture = f32_at(d, o + 0x1C);
        r.aperture = aperture < 1 ? 8.0f : aperture;
        for (size_t k = 0; k < 3; ++k) r.eye[k] = read_curve(d, rd32(d, o + 0x34 + 4 * k)), r.look[k] = read_curve(d, rd32(d, o + 0x40 + 4 * k));
        r.lens = read_curve(d, rd32(d, o + 0x4C));
        r.offset[0] = read_curve(d, rd32(d, o + 0x50)), r.offset[1] = read_curve(d, rd32(d, o + 0x54));
        r.roll = read_curve(d, rd32(d, o + 0x6C));
        r.fov_min = f32_at(d, o + 0x5C), r.fov_max = f32_at(d, o + 0x60);
        if (!(r.fov_max > 0)) r.fov_max = 100;
        s.end = std::max(s.end, r.end);
        s.records.push_back(std::move(r));
    }
    return s;
}

float ReplayScript::period() const {
    if (records.empty() || !(lap_length > 0)) return 0;
    return lap_length * float(int(end / lap_length) - 1 - int(records.front().start / lap_length));
}

int ReplayScript::variant_count() const {
    const float p = period();
    return lap_length > 0 && p > 0 ? std::max(1, int(p / lap_length + 0.5f)) : 1;
}

float ReplayScript::distance(int variant, float v) const {
    const int n = variant_count();
    float d = float(((variant % n) + n) % n) * lap_length + v;
    const float p = period();
    if (p > 0 && !records.empty())
        while (d >= records.back().end && d - p >= records.front().start) d -= p;
    return d;
}

const ReplayRecord* ReplayScript::find(float d) const {
    for (const ReplayRecord& r : records)
        if (r.start <= d && d < r.end) return &r;
    return nullptr;
}

bool ReplayCamera::pose(const ReplayScript& script, const CarState& car, const render::Mat4& body, ViewPose& out) {
    if (script.records.empty()) return false;
    float d = script.distance(std::max(car.lap, 0), car.course_v);
    // The shot holds while the car is in its stretch (brought into it by whole laps); then a cut.
    auto within = [&](const ReplayRecord& r, float& at) {
        at = d;
        if (script.lap_length > 0) {
            while (at < r.start) at += script.lap_length;
            while (at >= r.end && at - script.lap_length >= r.start) at -= script.lap_length;
        }
        return r.start <= at && at < r.end;
    };
    float at = d;
    if (!record_ || !within(*record_, at)) {
        record_ = script.find(d);
        // (The standing start lies before distance 0: the first records begin a little behind it.)
        if (!record_ && script.lap_length > 0) record_ = script.find(d - script.lap_length);
        if (!record_ || !within(*record_, at)) {
            record_ = nullptr;
            return false;
        }
    }
    const ReplayRecord& r = *record_;
    const float t = r.end > r.start ? std::clamp((at - r.start) / (r.end - r.start), 0.0f, 1.0f) : 0.0f;
    const RVec origin{body.at(0, 3), body.at(1, 3), body.at(2, 3)};
    const RVec up_axis{body.at(0, 1), body.at(1, 1), body.at(2, 1)};
    RVec eye{r.eye[0].at(t), r.eye[1].at(t), r.eye[2].at(t)};
    if (r.car_frame()) eye = body.transform_point(eye);
    // The target: the car (a little above its origin), or the shot's own point.
    RVec target = origin + up_axis * 0.4f;
    if (!r.looks_at_car() && !r.look[0].empty()) {
        target = {r.look[0].at(t), r.look[1].at(t), r.look[2].at(t)};
        if (r.car_frame()) target = body.transform_point(target);
    }
    const RVec to = target - eye;
    const float distance = render::length(to);
    if (!(distance > 1e-3f)) return false;
    // The lens: an angle in degrees, or the picture's width at the target; between the shot's limits.
    float fov = r.lens.empty() ? 30.0f : r.lens.at(t);
    if (r.lens_is_width()) fov = 2 * std::atan(fov / (2 * distance)) * 180 / kPi;
    fov = std::clamp(fov, std::max(r.fov_min, 0.1f), std::max(r.fov_max, r.fov_min));
    RVec ahead = to * (1.0f / distance);
    RVec right = render::cross(ahead, {0, 1, 0});
    if (render::length(right) < 1e-4f) right = {1, 0, 0};
    right = render::normalize(right);
    RVec up = render::cross(right, ahead);
    // The target's place in the picture: the aim moves off it by that share of the half view.
    const float half = fov * 0.5f * kPi / 180;
    const float across = r.offset[0].at(t) * half, upward = r.offset[1].at(t) * half * 0.75f;
    ahead = turn(ahead, up, across), right = turn(right, up, across);
    ahead = turn(ahead, right, -upward), up = turn(up, right, -upward);
    if (!r.roll.empty()) {
        const float roll = r.roll.at(t) * kPi / 180;
        right = turn(right, ahead, roll), up = turn(up, ahead, roll);
    }
    out.eye = eye;
    out.view.m = {right.x, up.x, -ahead.x, 0, right.y, up.y, -ahead.y, 0, right.z, up.z, -ahead.z, 0,
                  -render::dot(right, eye), -render::dot(up, eye), render::dot(ahead, eye), 1};
    out.fov = fov * kPi / 180;
    out.body_hidden = false;
    return true;
}

} // namespace gt4r::race
