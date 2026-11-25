#include "gt4r/menu/model_motion.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::menu {

using gt3r::render::Mat4;
namespace render = gt3r::render;

namespace {

float f32_at(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

void need(ByteSpan d, size_t at, size_t n) {
    if (at > d.size() || d.size() - at < n) throw FormatError("Mot1: data out of range");
}

std::string text_at(ByteSpan d, size_t o) {
    std::string s;
    while (o < d.size() && d[o]) s += char(d[o++]);
    return s;
}

struct Reader {
    ByteSpan d;
    size_t at = 0;
    u8 byte() {
        need(d, at, 1);
        return d[at++];
    }
    u16 half() {
        need(d, at, 2);
        const u16 v = rd16(d, at);
        at += 2;
        return v;
    }
    std::string text() {
        std::string s;
        for (;;) {
            const u8 c = byte();
            if (!c) return s;
            s += char(c);
        }
    }
    // `n` values: those whose bit of `flags` is set a channel's number, the others constants.
    std::vector<SkeletonMotion::Value> values(u32 flags, int n) {
        std::vector<SkeletonMotion::Value> out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            if (flags & (1u << i)) out[size_t(i)].channel = half();
            else {
                need(d, at, 4);
                out[size_t(i)].constant = f32_at(d, at);
                at += 4;
            }
        }
        return out;
    }
};

} // namespace

float SkeletonMotion::Channel::at(float t) const {
    float t0 = start, v0 = first;
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

bool SkeletonMotion::is_motion(ByteSpan d) { return d.size() >= 0x20 && gt3r::has_magic(d, "Mot1"); }

SkeletonMotion SkeletonMotion::parse(ByteSpan d) {
    if (!is_motion(d)) throw FormatError("not a Mot1 motion");
    SkeletonMotion m;
    // The root (+0x10): u16 clips, u16 sequences, u32 -> clips, u32 -> sequences, u32 -> the scene
    // program. A clip (0x20 bytes): f32 length, f32 loop start, 8 bytes, u32 -> name, u32 channels,
    // u32 -> channel pointers. A channel: u32 segments (1), 8 bytes, u32 -> keys: u32 n, u32,
    // f32 start, f32 first value, then (n - 2) / 4 pieces {f32 c1, c2, end time, end value}.
    const u32 root = rd32(d, 0x10);
    need(d, root, 0x10);
    const u32 count = rd16(d, root), clip_at = rd32(d, root + 4), scene = rd32(d, root + 0xC);
    if (count > 256) throw FormatError("Mot1: too many clips");
    need(d, clip_at, 0x20 * size_t(count));
    for (u32 i = 0; i < count; ++i) {
        const size_t o = clip_at + 0x20 * size_t(i);
        Clip c;
        c.length = f32_at(d, o), c.loop_start = f32_at(d, o + 4);
        c.name = text_at(d, rd32(d, o + 0x10));
        const u32 n = rd32(d, o + 0x14), table = rd32(d, o + 0x18);
        if (n > 4096) throw FormatError("Mot1: too many channels");
        need(d, table, 4 * size_t(n));
        for (u32 k = 0; k < n; ++k) {
            const u32 ch = rd32(d, table + 4 * size_t(k));
            need(d, ch, 0x10);
            const u32 keys = rd32(d, ch + 0xC);
            need(d, keys, 0x10);
            const u32 nk = rd32(d, keys);
            if (nk > 65536) throw FormatError("Mot1: too many keys");
            Channel cc;
            cc.start = f32_at(d, keys + 8), cc.first = f32_at(d, keys + 0xC);
            const u32 pieces = nk >= 2 ? (nk - 2) / 4 : 0;
            need(d, keys + 0x10, 0x10 * size_t(pieces));
            for (u32 p = 0; p < pieces; ++p) {
                const size_t q = keys + 0x10 + 0x10 * size_t(p);
                cc.pieces.push_back({f32_at(d, q), f32_at(d, q + 4), f32_at(d, q + 8), f32_at(d, q + 12)});
            }
            c.channels.push_back(std::move(cc));
        }
        m.clips.push_back(std::move(c));
    }
    // The scene program (the interpreter at 0x42B440): one byte an operation, then its operands.
    Reader r{d, scene};
    for (int guard = 0; guard < 65536; ++guard) {
        Op op;
        op.code = r.byte();
        if (op.code <= 1) break; // the end
        switch (op.code) {
        case 2: case 3: break;                                      // push, pop
        case 4: op.name = r.text(); break;                          // a node
        case 5: case 9: case 0x13: { const u8 f = r.byte(); op.values = r.values(f, 3); break; } // translate, scale, rotate XYZ
        case 6: case 7: case 8: case 0x0A: { const u8 f = r.byte(); op.values = r.values(f, 1); break; } // rotate, perspective
        case 0x0B: { const u16 f = r.half(); op.values = r.values(f, 9); break; } // camera
        case 0x0C: case 0x0D: case 0x0E: { r.byte(); const u8 f = r.byte(); op.values = r.values(f, 4); break; } // a light's colours
        case 0x0F: { r.byte(); const u8 f = r.byte(); op.values = r.values(f, 3); break; }               // a light's place
        case 0x10: { const u8 f = r.byte(); op.name = r.text(); op.values = r.values(f, 1); break; }   // a named value
        case 0x11: case 0x12: { const u8 f = r.byte(); op.name = r.text(); r.text(); op.values = r.values(f, op.code == 0x11 ? 2 : 3); break; }
        default: throw FormatError("Mot1: unknown scene operation");
        }
        m.program.push_back(std::move(op));
    }
    return m;
}

float SkeletonMotion::at(const Value& v, float t) const {
    if (v.channel < 0) return v.constant;
    if (clips.empty() || size_t(v.channel) >= clips[0].channels.size()) return 0;
    return clips[0].channels[size_t(v.channel)].at(t);
}

float SkeletonMotion::named(const std::string& name, float t, float otherwise) const {
    for (const Op& op : program)
        if (op.code == 0x10 && op.name == name && !op.values.empty()) return at(op.values[0], t);
    return otherwise;
}

std::map<std::string, Mat4> SkeletonMotion::nodes(float t, const Mat4& start) const {
    std::map<std::string, Mat4> out;
    std::vector<Mat4> stack;
    Mat4 c = start;
    auto v = [&](const Op& op, size_t i) { return i < op.values.size() ? at(op.values[i], t) : 0.0f; };
    for (const Op& op : program) {
        switch (op.code) {
        case 2: stack.push_back(c); break;
        case 3:
            if (!stack.empty()) c = stack.back(), stack.pop_back();
            break;
        case 4: out[op.name] = c; break;
        case 5: c = c * render::translate({v(op, 0), v(op, 1), v(op, 2)}); break;
        case 6: c = c * render::rotate_x(v(op, 0)); break;
        case 7: c = c * render::rotate_y(v(op, 0)); break;
        case 8: c = c * render::rotate_z(v(op, 0)); break;
        case 9: c = c * render::scale({v(op, 0), v(op, 1), v(op, 2)}); break;
        // X turned first, then Y, then Z (the program's Euler quaternion, 0x48B888).
        case 0x13: c = c * render::rotate_z(v(op, 2)) * render::rotate_y(v(op, 1)) * render::rotate_x(v(op, 0)); break;
        default: break;
        }
    }
    return out;
}

mdls::Palette palette(const std::vector<mdls::Bone>& bones, const std::map<std::string, Mat4>& nodes) {
    u32 size = 0;
    for (const mdls::Bone& b : bones) size = std::max(size, b.index + 1);
    mdls::Palette p(size, Mat4::identity());
    for (const mdls::Bone& b : bones)
        if (const auto it = nodes.find(b.name); it != nodes.end()) p[b.index] = it->second * b.inverse_bind;
    return p;
}

void MotionClock::play(u64 frame) {
    if (running) return;
    running = true, since = frame;
}

void MotionClock::stop(u64 frame) {
    base = steps(frame);
    running = false;
}

float MotionClock::time(u64 frame, float length, float loop_start, bool* playing) const {
    float t = float(steps(frame)) / 60.0f;
    bool on = running;
    if (t > length) {
        if (loop && length - loop_start > 1e-4f) {
            t = loop_start + std::fmod(t - loop_start, length - loop_start);
        } else if (!loop) {
            // It stops the first picture its time passes the length, and holds that time.
            t = std::floor(length * 60.0f + 1.0f) / 60.0f;
            on = false;
        } else
            t = length;
    }
    if (playing) *playing = on;
    return t;
}

} // namespace gt4r::menu
