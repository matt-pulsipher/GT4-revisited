#include "gt4r/race/start_camera.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

namespace render = gt3r::render;
using RVec = render::Vec3;

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

void need(ByteSpan d, u64 at, u64 size) {
    if (at + size > d.size()) throw FormatError("Mot1: a table is outside the block");
}

std::string text_at(ByteSpan d, u32 at) {
    std::string s;
    for (size_t i = at; i < d.size() && d[i]; ++i) s += char(d[i]);
    return s;
}

RVec rv(const course::Vec3& v) { return {v.x, v.y, v.z}; }

} // namespace

float MotionChannel::at(float t) const {
    float t0 = 0, v0 = first;
    for (const Piece& p : pieces) {
        if (t <= p.end_time) {
            const float span = p.end_time - t0;
            const float u = span > 0 ? std::clamp((t - t0) / span, 0.0f, 1.0f) : 1.0f, w = 1 - u;
            return w * w * w * v0 + 3 * w * w * u * p.c1 + 3 * w * u * u * p.c2 + u * u * u * p.end_value;
        }
        t0 = p.end_time, v0 = p.end_value;
    }
    return v0;
}

CameraMotion CameraMotion::parse(ByteSpan d) {
    if (!is_motion(d)) throw FormatError("not a Mot1 motion");
    CameraMotion m;
    const u32 root = rd32(d, 0x10);
    need(d, root, 0x10);
    const u32 clips = rd16(d, root), sequences = rd16(d, root + 2), clip_at = rd32(d, root + 4), seq_at = rd32(d, root + 8);
    if (clips > 64) throw FormatError("Mot1: too many clips");
    need(d, clip_at, 0x20 * u64(clips));
    for (u32 i = 0; i < clips; ++i) {
        const size_t o = clip_at + 0x20 * size_t(i);
        MotionClip c;
        c.length = f32_at(d, o);
        c.name = text_at(d, rd32(d, o + 0x10));
        const u32 n = rd32(d, o + 0x14), ch_at = rd32(d, o + 0x18);
        if (n > 64) throw FormatError("Mot1: too many channels");
        need(d, ch_at, 4 * u64(n));
        for (u32 k = 0; k < n; ++k) {
            const u32 ch = rd32(d, ch_at + 4 * k);
            need(d, ch, 0x10);
            const u32 keys = rd32(d, ch + 0xC);
            need(d, keys, 0x10);
            const u32 count = rd32(d, keys);
            if (count > 4096) throw FormatError("Mot1: too many keys");
            MotionChannel mc;
            mc.first = f32_at(d, keys + 0xC);
            const u32 pieces = count >= 2 ? (count - 2) / 4 : 0;
            need(d, keys + 0x10, 0x10 * u64(pieces));
            for (u32 p = 0; p < pieces; ++p) {
                const size_t q = keys + 0x10 + 0x10 * size_t(p);
                mc.pieces.push_back({f32_at(d, q), f32_at(d, q + 4), f32_at(d, q + 8), f32_at(d, q + 12)});
            }
            c.channels.push_back(std::move(mc));
        }
        m.clips.push_back(std::move(c));
    }
    if (sequences > 0) {
        need(d, seq_at, 0x10);
        m.total = f32_at(d, seq_at + 8);
    }
    return m;
}

MotionPose motion_pose(const MotionClip& clip, float t) {
    MotionPose p;
    auto ch = [&](size_t i, float fallback) { return i < clip.channels.size() ? clip.channels[i].at(t) : fallback; };
    p.eye = {ch(0, 0), ch(1, 0), ch(2, 0)};
    p.look = {ch(3, 0), ch(4, 0), ch(5, 1)};
    p.up = {ch(6, 0), ch(7, 1), ch(8, 0)};
    p.fov = ch(9, 0.7f);
    return p;
}

int StartCamera::choose(float r) const {
    if (!motion_ || motion_->clips.empty()) return -1;
    const int n = int(motion_->clips.size());
    int c;
    if (!replay_) c = r >= 2 ? 0 : r >= 1 ? 1 : 2;
    else c = r >= 4 ? 0 : 1 + std::clamp(int(4 - r), 0, 3);
    return std::min(c, n - 1);
}

void StartCamera::begin(const CameraMotion* motion, bool replay, int hold_steps, const course::Runway::Place& grid, const CarState& car) {
    prologue_.begin(nullptr, false, 0);
    motion_ = motion && !motion->clips.empty() ? motion : nullptr;
    replay_ = replay;
    left_ = std::max(0, replay ? hold_steps + 60 : hold_steps - 60);
    clip_ = -1, t_ = 0;
    grid_pos_ = grid.pos, grid_heading_ = grid.heading;
    car_pos_ = car.position, car_heading_ = car_heading(car);
    if (motion_ && left_ > 0) clip_ = choose(float(left_) / 60);
}

bool StartCamera::step() {
    if (prologue_.step()) return true;
    if (!motion_ || left_ <= 0) return false;
    --left_;
    const int c = choose(float(left_) / 60);
    if (c != clip_) clip_ = c, t_ = 0;
    else t_ += 1.0f / 60;
    return left_ > 0;
}

bool StartCamera::pose(ViewPose& out, const gt3r::render::Mat4* car_body) const {
    if (prologue_.active()) return car_body && prologue_.pose(*car_body, out);
    if (!active() || clip_ < 0) return false;
    const MotionClip& clip = motion_->clips[size_t(clip_)];
    const MotionPose m = motion_pose(clip, t_);
    // The anchor: the long clip at the grid's first place, the others at the followed car.
    const bool at_grid = clip_ == 0;
    const RVec origin = rv(at_grid ? grid_pos_ : car_pos_);
    const float h = at_grid ? grid_heading_ : car_heading_;
    const RVec ahead{std::sin(h), 0, std::cos(h)}, up_axis{0, 1, 0};
    const RVec across = render::cross(ahead, up_axis); // the anchor's right
    // The motion's frame: x across, y up, z toward the back of the anchor (the program's own frame has
    // the course's z the other way round).
    auto place = [&](const course::Vec3& v) { return origin + across * v.x + up_axis * v.y - ahead * v.z; };
    auto turn = [&](const course::Vec3& v) { return across * v.x + up_axis * v.y - ahead * v.z; };
    const RVec eye = place(m.eye), look = place(m.look);
    RVec fwd = look - eye;
    if (!(render::length(fwd) > 1e-4f)) return false;
    fwd = render::normalize(fwd);
    RVec up = turn(m.up);
    RVec right = render::cross(fwd, up);
    if (!(render::length(right) > 1e-4f)) right = render::cross(fwd, up_axis);
    right = render::normalize(right);
    up = render::cross(right, fwd);
    out.eye = eye;
    out.view.m = {right.x, up.x, -fwd.x, 0, right.y, up.y, -fwd.y, 0, right.z, up.z, -fwd.z, 0,
                  -render::dot(right, eye), -render::dot(up, eye), render::dot(fwd, eye), 1};
    out.fov = std::clamp(m.fov, 0.05f, 2.5f);
    out.body_hidden = false;
    return true;
}

} // namespace gt4r::race
