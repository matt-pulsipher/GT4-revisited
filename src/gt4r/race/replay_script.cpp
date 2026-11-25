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
        r.dissolve = f32_at(d, o + 0x18);
        r.haze = f32_at(d, o + 0x0C) > 0, r.depth_of_field = f32_at(d, o + 0x10) > 0;
        for (size_t k = 0; k < 3; ++k) r.eye[k] = read_curve(d, rd32(d, o + 0x34 + 4 * k)), r.look[k] = read_curve(d, rd32(d, o + 0x40 + 4 * k));
        r.lens = read_curve(d, rd32(d, o + 0x4C));
        r.offset[0] = read_curve(d, rd32(d, o + 0x50)), r.offset[1] = read_curve(d, rd32(d, o + 0x54));
        r.roll = read_curve(d, rd32(d, o + 0x6C));
        r.follow[0] = read_curve(d, rd32(d, o + 0x64)), r.follow[1] = read_curve(d, rd32(d, o + 0x68));
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

float ReplayScript::distance(int crossings, float v) const {
    float d = float(crossings) * lap_length + v;
    const float p = period();
    if (d < 0 || d < end || !(p > 0)) return d;
    return d - p * float(int(d / p));
}

const ReplayRecord* ReplayScript::find(float d) const {
    // The record that started last at or before d, and only while it still covers d (an earlier
    // record that covers d too is not taken).
    const ReplayRecord* last = nullptr;
    for (const ReplayRecord& r : records) {
        if (r.start > d) break;
        last = &r;
    }
    return last && d < last->end ? last : nullptr;
}

const ReplayRecord* ReplayScript::choose(float d) const {
    if (records.empty() || d < records.front().start) return nullptr;
    size_t i = 0;
    for (size_t k = 0; k < records.size() && records[k].start <= d; ++k) i = k;
    if (d < records[i].end) return &records[i];
    // On past the last record: from record 0 a period back.
    if (i + 1 < records.size()) return nullptr;
    const float p = period();
    if (!(p > 0)) return nullptr;
    const float e = d - p;
    for (const ReplayRecord& r : records) {
        if (r.start > e) break;
        if (e < r.end) return &r;
    }
    return nullptr;
}

float CameraSpring::step(float target) {
    if (slew > 0) want += std::clamp(target - want, -slew / 60, slew / 60);
    else want = target;
    v += (k * (want - x) - c * v) / 60;
    v = std::clamp(v, vmin, vmax);
    x += v / 60;
    return x;
}

namespace {
float wrap180(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0) a += 360;
    return a - 180;
}
float wrap360(float a) {
    a = std::fmod(a, 360.0f);
    return a < 0 ? a + 360 : a;
}
} // namespace

float AimDamper::rnd() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / float(1u << 24);
}

void AimDamper::reset(float target, u32 seed) {
    seed_ = seed ? seed : 1;
    out_ = target, v_ = 0, prev_ = target, has_prev_ = false;
    noise_x_ = noise_target_ = 256 * rnd(), noise_v_ = 0;
    frame_ = 0;
}

float AimDamper::step(float target, float follow) {
    // The noise (0x384C48): every third frame, now and then a new random target; a spring toward it.
    if (follow > 1) noise_target_ = 256;
    else if (++frame_ % 3 == 0 && rnd() > 0.8358f) noise_target_ = 256 * rnd();
    noise_v_ += ((noise_target_ - noise_x_) * 60 - 10 * noise_v_) / 60;
    noise_x_ += noise_v_ / 60;
    const float noise = noise_x_ / 256;
    const float delta = has_prev_ ? wrap180(target - prev_) : 0.0f;
    prev_ = target, has_prev_ = true;
    const float error = wrap180(target - out_) + 5 * delta;
    const float g = std::clamp(follow, 0.0f, 1.0f);
    const float n = follow > 1 ? 1.0f : 0.6f + 0.9f * noise;
    v_ += (600 * n * error * g - 60 * v_) / 60;
    out_ = wrap360(out_ + v_ / 60);
    return out_;
}

float least_focus(float degrees) {
    if (degrees <= 2) return 16;
    if (degrees < 15) return std::clamp((15 - degrees) * 12 / 13 + 4, 4.0f, 16.0f);
    if (degrees < 30) return std::clamp((30 - degrees) * 3 / 15 + 1, 1.0f, 4.0f);
    if (degrees < 90) return std::clamp(1 - (degrees - 30) * 0.9f / 60, 0.1f, 1.0f);
    return 0.1f;
}

bool ReplayCamera::pose(const ReplayScript& script, const CarState& car, const render::Mat4& body, ViewPose& out, const std::function<float(const render::Mat4&)>& nearest) {
    cut_dissolve_ = 0, first_ = false;
    if (script.records.empty()) return false;
    // (`lap` is the laps completed, -1 before the line: the crossings less one.)
    float d = script.distance(car.lap + 1, car.course_v);
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
        const ReplayRecord* ended = record_;
        record_ = script.choose(d);
        if (!record_ || !within(*record_, at)) {
            record_ = nullptr;
            cut_ = true;
            return false;
        }
        // The ended shot's dissolve (CameraDesigner::vf25 0x37C9C8).
        if (ended && ended != record_) cut_dissolve_ = std::max(0.0f, ended->dissolve);
        cut_ = true;
    }
    const ReplayRecord& r = *record_;
    const float t = r.end > r.start ? std::clamp((at - r.start) / (r.end - r.start), 0.0f, 1.0f) : 0.0f;
    // The shot's own pose (raw), then its angles.
    ViewPose raw;
    if (!shot_pose(r, t, &body, raw, false)) return false;
    const bool car_shot = r.looks_at_car();
    const RVec ahead{-raw.view.at(2, 0), -raw.view.at(2, 1), -raw.view.at(2, 2)};
    float pitch = -std::atan2(ahead.y, std::sqrt(ahead.x * ahead.x + ahead.z * ahead.z)) * 180 / kPi;
    float yaw = 180 - std::atan2(ahead.x, ahead.z) * 180 / kPi;
    const float lens = raw.fov * 180 / kPi;
    const float roll_raw = r.roll.empty() ? 0.0f : r.roll.at(t);
    const bool fresh = cut_;
    first_ = fresh;
    if (cut_) {
        // A cut: everything starts on its value; a random roll share (1 with kind 0x400).
        cut_ = false;
        seed_ = seed_ * 1664525u + 1013904223u;
        roll_scale_ = r.full_roll() ? 1.0f : float(seed_ >> 8) / float(1u << 24);
        lens_.reset(lens), across_.reset(r.offset[0].at(t)), up_.reset(r.offset[1].at(t)), roll_.reset(roll_raw * roll_scale_);
        // The focus found: at once, or on 16 cuts of 100 from under half of it (0x37C650).
        auto draw = [&] { return seed_ = seed_ * 1664525u + 1013904223u, float(seed_ >> 8) / float(1u << 24); };
        pull_.reset(draw() < 0.16f ? draw() - 0.5f : 1.0f);
        for (int k = 0; k < 2; ++k) follow_[k].reset(r.follow[size_t(k)].empty() ? 2.0f : r.follow[size_t(k)].at(t));
    }
    const float zoom = car_shot ? zoom_ : 1.0f;
    const float shown = lens_.step(lens) / zoom;  // (+0xC)
    const float f = std::max(1.0f, lens / zoom); // (what the aim's shares are of: the lens itself, not its spring)
    const float across = across_.step(r.offset[0].at(t)), up = up_.step(r.offset[1].at(t));
    yaw -= across * f / 2;
    if (car_shot) pitch -= 0.15f * f * std::cos(pitch * kPi / 180);
    pitch += up * f / 2;
    float roll = roll_raw * roll_scale_;
    if (car_shot) {
        roll = roll_.step(roll);
        const float fp = follow_[0].step(r.follow[0].empty() ? 2.0f : r.follow[0].at(t));
        const float fy = follow_[1].step(r.follow[1].empty() ? 2.0f : r.follow[1].at(t));
        if (fresh) yaw_.reset(wrap360(yaw), seed_), pitch_.reset(wrap360(pitch), seed_ ^ 0x9E3779B9u);
        yaw = yaw_.step(wrap360(yaw), fy);
        pitch = pitch_.step(wrap360(pitch), fp);
    }
    // The view: the eye, then yaw about y, pitch about x and roll about z.
    const float ya = (180 - yaw) * kPi / 180, pa = pitch * kPi / 180;
    RVec dir{std::sin(ya) * std::cos(pa), -std::sin(pa), std::cos(ya) * std::cos(pa)};
    RVec right = render::cross(dir, {0, 1, 0});
    if (render::length(right) < 1e-4f) right = {1, 0, 0};
    right = render::normalize(right);
    RVec upv = render::cross(right, dir);
    if (roll != 0) {
        const float a = roll * kPi / 180;
        right = turn(right, dir, a), upv = turn(upv, dir, a);
    }
    out.eye = raw.eye;
    out.view.m = {right.x, upv.x, -dir.x, 0, right.y, upv.y, -dir.y, 0, right.z, upv.z, -dir.z, 0,
                  -render::dot(right, raw.eye), -render::dot(upv, raw.eye), render::dot(dir, raw.eye), 1};
    out.fov = shown * kPi / 180;
    out.body_hidden = false;
    // Its focus: the car's nearest point and 0.7 m, as far as it is found, no nearer than the lens allows (0x37C800).
    out.haze = r.haze, out.depth_of_field = r.depth_of_field, out.f_number = r.aperture;
    const float sharp = (nearest ? nearest(out.view) : render::length(RVec{body.at(0, 3), body.at(1, 3), body.at(2, 3)} - raw.eye)) + 0.7f;
    out.focus = std::max(std::clamp(sharp * std::clamp(pull_.step(2), 0.0f, 1.0f), 0.001f, 100000.0f), least_focus(lens_.x));
    return true;
}

bool shot_pose(const ReplayRecord& r, float t, const render::Mat4* car_body, ViewPose& out, bool aimed) {
    const render::Mat4 body = car_body ? *car_body : render::Mat4{};
    const bool car_frame = car_body && r.car_frame();
    const RVec origin{body.at(0, 3), body.at(1, 3), body.at(2, 3)};
    const RVec up_axis{body.at(0, 1), body.at(1, 1), body.at(2, 1)};
    RVec eye{r.eye[0].at(t), r.eye[1].at(t), r.eye[2].at(t)};
    if (car_frame) eye = body.transform_point(eye);
    // The target: the car (a little above its origin), or the shot's own point.
    RVec target = origin + up_axis * 0.4f;
    if ((!r.looks_at_car() || !car_body) && !r.look[0].empty()) {
        target = {r.look[0].at(t), r.look[1].at(t), r.look[2].at(t)};
        if (car_frame) target = body.transform_point(target);
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
    const float across = aimed ? r.offset[0].at(t) * half : 0.0f, upward = aimed ? r.offset[1].at(t) * half : 0.0f;
    ahead = turn(ahead, up, across), right = turn(right, up, across);
    ahead = turn(ahead, right, -upward), up = turn(up, right, -upward);
    if (aimed && !r.roll.empty()) {
        const float roll = r.roll.at(t) * kPi / 180;
        right = turn(right, ahead, roll), up = turn(up, ahead, roll);
    }
    out.eye = eye;
    out.view.m = {right.x, up.x, -ahead.x, 0, right.y, up.y, -ahead.y, 0, right.z, up.z, -ahead.z, 0,
                  -render::dot(right, eye), -render::dot(up, eye), render::dot(ahead, eye), 1};
    out.fov = fov * kPi / 180;
    out.body_hidden = false;
    out.haze = r.haze, out.depth_of_field = r.depth_of_field, out.f_number = r.aperture, out.focus = std::max(distance, least_focus(fov));
    return true;
}

} // namespace gt4r::race

namespace gt4r::race {

void ScenicCamera::reset(const ReplayScript* script) {
    script_ = script && !script->records.empty() ? script : nullptr;
    index_ = 0, frames_ = 0, dissolve_ = 0, dissolve_length_ = 0;
}

void ScenicCamera::step() {
    if (!script_) return;
    const ReplayRecord& r = script_->records[index_];
    if (dissolve_ > 0) --dissolve_;
    // A shot on time runs `end` seconds; the next follows (after the last, the first), with a
    // dissolve of the ended shot's own length.
    if (++frames_ >= int(std::max(r.end, 1.0f / 60) * 60)) {
        index_ = (index_ + 1) % script_->records.size();
        frames_ = 0;
        dissolve_length_ = dissolve_ = int(std::max(0.0f, r.dissolve) * 60 + 0.5f);
    }
}

bool ScenicCamera::pose(ViewPose& out) const {
    if (!script_) return false;
    const ReplayRecord& r = script_->records[index_];
    const float t = std::clamp(float(frames_) / std::max(1.0f, r.end * 60), 0.0f, 1.0f);
    return shot_pose(r, t, nullptr, out);
}

float ScenicCamera::dissolve() const { return dissolve_length_ > 0 ? float(dissolve_) / float(dissolve_length_) : 0.0f; }

} // namespace gt4r::race
