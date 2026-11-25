#include "gt4r/menu/model_motion.h"

#include <algorithm>
#include <array>
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
    return parse_object(d, rd32(d, 0x10));
}

std::vector<SkeletonMotion> SkeletonMotion::parse_all(ByteSpan d, std::map<std::string, Name>& names) {
    if (!is_motion(d)) throw FormatError("not a Mot1 motion");
    // +0x14 u16 objects (roots, 0x10 bytes each), u16 names; +0x1C -> names: u32 -> text, s16 object,
    // s16 clip (-1 none), s16 sequence (-1 none).
    const u32 root = rd32(d, 0x10), objects = rd16(d, 0x14), count = rd16(d, 0x16), table = rd32(d, 0x1C);
    std::vector<SkeletonMotion> out;
    for (u32 i = 0; i < objects && i < 64; ++i) out.push_back(parse_object(d, root + 0x10 * size_t(i)));
    need(d, table, 0x10 * size_t(count));
    for (u32 i = 0; i < count; ++i) {
        const size_t o = table + 0x10 * size_t(i);
        names[text_at(d, rd32(d, o))] = {s16(rd16(d, o + 4)), s16(rd16(d, o + 6)), s16(rd16(d, o + 8))};
    }
    return out;
}

SkeletonMotion SkeletonMotion::parse_object(ByteSpan d, size_t root) {
    SkeletonMotion m;
    // A root: u16 clips, u16 sequences, u32 -> clips, u32 -> sequences, u32 -> the scene program. A
    // clip (0x20 bytes): f32 length, f32 loop start, 8 bytes, u32 -> name, u16 channels, u16 events,
    // u32 -> channel pointers, u32 -> events. A channel: u32 segments (1), 8 bytes, u32 -> keys: u32
    // n, u32, f32 start, f32 first value, then (n - 2) / 4 pieces {f32 c1, c2, end time, end value}.
    // An event (0x10 bytes): u32 -> name, u16 n, u32 -> n x {f32 time, f32 value}.
    need(d, root, 0x10);
    const u32 count = rd16(d, root), clip_at = rd32(d, root + 4), scene = rd32(d, root + 0xC);
    if (count > 256) throw FormatError("Mot1: too many clips");
    need(d, clip_at, 0x20 * size_t(count));
    for (u32 i = 0; i < count; ++i) {
        const size_t o = clip_at + 0x20 * size_t(i);
        Clip c;
        c.length = f32_at(d, o), c.loop_start = f32_at(d, o + 4);
        c.name = text_at(d, rd32(d, o + 0x10));
        const u32 n = rd16(d, o + 0x14), n_events = rd16(d, o + 0x16), table = rd32(d, o + 0x18), event_at = rd32(d, o + 0x1C);
        if (n > 4096) throw FormatError("Mot1: too many channels");
        for (u32 k = 0; k < n_events && k < 64; ++k) {
            const size_t q = event_at + 0x10 * size_t(k);
            need(d, q, 0x10);
            Event e;
            e.name = text_at(d, rd32(d, q));
            const u32 times = rd16(d, q + 4), at = rd32(d, q + 8);
            need(d, at, 8 * size_t(times));
            for (u32 t = 0; t < times; ++t) e.at.emplace_back(f32_at(d, at + 8 * size_t(t)), f32_at(d, at + 8 * size_t(t) + 4));
            c.events.push_back(std::move(e));
        }
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
    // A sequence (0x20 bytes): u16 entries, u32 -> entries, f32 length. An entry (0x20 bytes): u32 ->
    // s16 clips[], f32 start, f32 offset into the clips, f32 duration, u16 clips, u8 reversed, u8 mode,
    // u8 held at its end, u32 -> name, s32 number.
    const u32 n_seq = rd16(d, root + 2), seq_at = rd32(d, root + 8);
    for (u32 i = 0; i < n_seq && i < 256; ++i) {
        const size_t o = seq_at + 0x20 * size_t(i);
        need(d, o, 0x20);
        Sequence q;
        const u32 n = rd16(d, o), at = rd32(d, o + 4);
        q.length = f32_at(d, o + 8);
        need(d, at, 0x20 * size_t(n));
        for (u32 k = 0; k < n; ++k) {
            const size_t e = at + 0x20 * size_t(k);
            Sequence::Entry x;
            x.start = f32_at(d, e + 4), x.offset = f32_at(d, e + 8), x.duration = f32_at(d, e + 12);
            const u32 nc = rd16(d, e + 16), list = rd32(d, e);
            need(d, list, 2 * size_t(nc));
            for (u32 c = 0; c < nc; ++c) x.clips.push_back(rd16(d, list + 2 * size_t(c)));
            x.reversed = d[e + 18] != 0, x.mode = d[e + 19], x.hold = d[e + 20] != 0;
            if (rd32(d, e + 24)) x.name = text_at(d, rd32(d, e + 24));
            x.number = s32(rd32(d, e + 28));
            q.entries.push_back(std::move(x));
        }
        m.sequences.push_back(std::move(q));
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

const SkeletonMotion::Sequence::Entry* SkeletonMotion::Sequence::at(float t) const {
    if (entries.empty()) return nullptr;
    size_t i = 0;
    while (i + 1 < entries.size() && entries[i + 1].start <= t) ++i;
    return &entries[i];
}

float SkeletonMotion::Sequence::start_of(int number) const {
    for (const Entry& e : entries)
        if (e.number >= number) return e.start;
    return 0;
}

namespace {

float step_sequence(const SkeletonMotion::Sequence& q, float t, float dt, int number) { // 0x429960
    using Entry = SkeletonMotion::Sequence::Entry;
    const Entry* e = q.at(t);
    if (!e) return q.length;
    const Entry* const end = q.entries.data() + q.entries.size();
    t += dt;
    if (e->number < number) {
        const Entry* const from = e;
        e = q.at(t);
        if (e->mode >= 2 && e->number < number) {
            if (++e == end) return q.length;
            t = e->start;
        }
        while (e != from && e->mode != 0 && e->number < number) {
            if (++e == end) return q.length;
            t = e->start;
        }
        if (e != from || e->number < number) return t;
    }
    // Held in this entry (0x42A290).
    if (e->hold) t = std::min(t, e->start + e->duration);
    else if (e->duration != 0) t = e->start + std::fmod(std::max(t - e->start, 0.0f), e->duration);
    if (e->start + e->duration <= t) t -= dt * 0.001f;
    return t;
}

// The turn about x, then y, then z (0x48B888), as a quaternion x, y, z, w.
std::array<float, 4> euler_turn(float x, float y, float z) {
    const float sx = std::sin(x / 2), cx = std::cos(x / 2), sy = std::sin(y / 2), cy = std::cos(y / 2), sz = std::sin(z / 2), cz = std::cos(z / 2);
    return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz};
}

std::array<float, 4> between(std::array<float, 4> a, std::array<float, 4> b, float t) {
    float d = 0;
    for (size_t i = 0; i < 4; ++i) d += a[i] * b[i];
    if (d < 0) {
        for (float& v : b) v = -v;
        d = -d;
    }
    float ka = 1 - t, kb = t;
    if (d < 0.9995f) {
        const float w = std::acos(d), s = std::sin(w);
        ka = std::sin(w * (1 - t)) / s, kb = std::sin(w * t) / s;
    }
    std::array<float, 4> q;
    float n = 0;
    for (size_t i = 0; i < 4; ++i) q[i] = a[i] * ka + b[i] * kb, n += q[i] * q[i];
    n = 1 / std::sqrt(n);
    for (float& v : q) v *= n;
    return q;
}

Mat4 turn_matrix(const std::array<float, 4>& q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m;
    m.m = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0, 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0, 0, 0, 0, 1};
    return m;
}

} // namespace

float SkeletonMotion::Sequence::advance(float t, float dt, int number) const {
    t = step_sequence(*this, t, dt, number);
    if (t < length || length <= 0) return t;
    while (t >= length) t -= length;
    return step_sequence(*this, t, 0, number);
}

SkeletonMotion::Pose SkeletonMotion::pose(int sequence, float t, float mix) const {
    Pose p;
    if (sequence < 0 || size_t(sequence) >= sequences.size()) return p;
    const Sequence::Entry* e = sequences[size_t(sequence)].at(t);
    if (!e || e->clips.empty()) return p;
    if (e->reversed) t = e->start + (e->start + e->duration) - t;
    const int last = int(e->clips.size()) - 1, i = std::clamp(int(mix), 0, last), j = std::clamp(int(mix) + 1, 0, last);
    p.a = e->clips[size_t(i)], p.b = e->clips[size_t(j)];
    p.mix = p.a == p.b ? 0.0f : mix - float(int(mix));
    p.time = (t - e->start) + e->offset;
    return p;
}

std::map<std::string, Mat4> SkeletonMotion::nodes(const Pose& p, const Mat4& start) const {
    std::map<std::string, Mat4> out;
    std::vector<Mat4> stack;
    Mat4 c = start;
    const auto of = [&](int clip, const Value& v) {
        if (v.channel < 0) return v.constant;
        if (clip < 0 || size_t(clip) >= clips.size() || size_t(v.channel) >= clips[size_t(clip)].channels.size()) return 0.0f;
        return clips[size_t(clip)].channels[size_t(v.channel)].at(p.time);
    };
    const auto v = [&](const Op& op, size_t i) {
        if (i >= op.values.size()) return 0.0f;
        const float a = of(p.a, op.values[i]);
        return p.mix > 0 && p.mix < 1 ? a * (1 - p.mix) + of(p.b, op.values[i]) * p.mix : a;
    };
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
        case 0x13: {
            std::array<float, 4> q = euler_turn(of(p.a, op.values[0]), of(p.a, op.values[1]), of(p.a, op.values[2]));
            if (p.mix > 0 && p.mix < 1) q = between(q, euler_turn(of(p.b, op.values[0]), of(p.b, op.values[1]), of(p.b, op.values[2])), p.mix);
            c = c * turn_matrix(q);
            break;
        }
        default: break;
        }
    }
    return out;
}

float SkeletonMotion::named(const std::string& name, const Pose& p, float otherwise) const {
    for (const Op& op : program) {
        if (op.code != 0x10 || op.name != name || op.values.empty()) continue;
        const Value& v = op.values[0];
        if (v.channel < 0) return v.constant;
        if (p.a < 0 || size_t(p.a) >= clips.size() || size_t(v.channel) >= clips[size_t(p.a)].channels.size()) return otherwise;
        return clips[size_t(p.a)].channels[size_t(v.channel)].at(p.time);
    }
    return otherwise;
}

bool SkeletonMotion::camera(const Pose& p, Camera& out) const {
    const auto of = [&](const Value& v) {
        if (v.channel < 0) return v.constant;
        if (p.a < 0 || size_t(p.a) >= clips.size() || size_t(v.channel) >= clips[size_t(p.a)].channels.size()) return 0.0f;
        return clips[size_t(p.a)].channels[size_t(v.channel)].at(p.time);
    };
    bool placed = false, lens = false;
    for (const Op& op : program) {
        if (op.code == 0x0B && op.values.size() >= 9) {
            float v[9];
            for (size_t i = 0; i < 9; ++i) v[i] = of(op.values[i]);
            out.eye = {v[0], v[1], v[2]}, out.target = {v[3], v[4], v[5]}, out.up = {v[6], v[7], v[8]};
            placed = true;
        } else if (op.code == 0x0A && !op.values.empty()) {
            out.angle = of(op.values[0]);
            lens = true;
        }
    }
    return placed && lens;
}

float SkeletonMotion::events(int sequence, const std::string& name, float t0, float t1, float dt) const {
    if (sequence < 0 || size_t(sequence) >= sequences.size()) return 0;
    const Sequence& q = sequences[size_t(sequence)];
    const Sequence::Entry *a = q.at(t0), *b = q.at(t1);
    if (!a || !b) return 0;
    float sum = 0;
    const auto range = [&](const Sequence::Entry& e, float from, float to) {
        from = from - e.start + e.offset, to = to - e.start + e.offset;
        for (int clip : e.clips)
            if (size_t(clip) < clips.size())
                for (const Event& ev : clips[size_t(clip)].events) {
                    if (ev.name != name) continue;
                    for (const auto& [time, value] : ev.at) // (the first one in the span)
                        if (from <= time && time <= to) {
                            sum += value;
                            break;
                        }
                    break;
                }
    };
    if (a == b && t1 >= t0) {
        range(*a, t0, t1);
    } else if (a == b) {
        range(*a, t0, a->start + a->duration);
        range(*a, a->start, t1);
    } else {
        range(*a, t0, std::min(t0 + dt, a->start + a->duration));
        range(*b, std::max(t1 - dt, b->start), t1);
    }
    return sum;
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
