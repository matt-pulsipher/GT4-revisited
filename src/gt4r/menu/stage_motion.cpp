#include "gt4r/menu/stage_motion.h"

#include <cstring>

namespace gt4r::menu {

namespace {

struct Reader {
    ByteSpan d;
    size_t at = 0;
    void need(size_t n) const {
        if (at + n > d.size()) throw FormatError("Mot1: the scene program runs past the file");
    }
    u8 byte() {
        need(1);
        return d[at++];
    }
    u16 half() {
        need(2);
        const u16 v = u16(d[at] | (d[at + 1] << 8));
        at += 2;
        return v;
    }
    float f32() {
        need(4);
        float f;
        std::memcpy(&f, d.data() + at, 4);
        at += 4;
        return f;
    }
    std::string text() {
        std::string s;
        while (at < d.size() && d[at]) s += char(d[at++]);
        ++at;
        return s;
    }
    // `n` values, those whose bit of `flags` is set a channel's number, the others constants.
    template <size_t N> std::array<StageMotion::Value, N> values(u32 flags) {
        std::array<StageMotion::Value, N> out{};
        for (size_t i = 0; i < N; ++i) {
            if (flags & (1u << i)) out[i].channel = half();
            else out[i].constant = f32();
        }
        return out;
    }
};

} // namespace

StageMotion StageMotion::parse(ByteSpan d) {
    if (!is_motion(d)) throw FormatError("not a Mot1 motion");
    StageMotion m;
    m.motion = race::CameraMotion::parse(d);
    const u32 root = rd32(d, 0x10);
    if (size_t(root) + 0x10 > d.size()) throw FormatError("Mot1: no root");
    if (rd16(d, root) > 0 && size_t(rd32(d, root + 4)) + 8 <= d.size()) { // (the clip: f32 length, f32 loop start)
        Reader clip{d, rd32(d, root + 4) + 4};
        m.loop_start_ = clip.f32();
    }
    Reader r{d, rd32(d, root + 0xC)};
    for (int guard = 0; guard < 4096; ++guard) {
        const u8 op = r.byte();
        if (op == 0) break;                     // the end
        if (op >= 1 && op <= 3) continue;       // the hierarchy's steps: nothing to read
        if (op == 4) {                          // a node's name
            r.text();
            continue;
        }
        if (op == 0x0A) { // the perspective: the field of view
            const u8 flags = r.byte();
            m.fov = r.values<1>(flags)[0];
        } else if (op == 0x0B) { // the camera: eye, point looked at, up
            const u16 flags = r.half();
            m.camera = r.values<9>(flags);
        } else if (op >= 0x0C && op <= 0x0F) { // a light's colours and its place
            const int index = r.byte();
            const u8 flags = r.byte();
            Light& l = m.lights[index];
            if (op == 0x0F) l.position = r.values<3>(flags), l.has_position = true;
            else (op == 0x0C ? l.diffuse : op == 0x0D ? l.ambient : l.specular) = r.values<4>(flags); // (its light parameters 1, 0 and 4: 0x42D024, 0x42D0F4, 0x42D1C4; a car face's `lN_amb` is parameter 0, 0x21A988)
        } else if (op == 0x10) { // a named value
            const u8 flags = r.byte();
            const std::string name = r.text();
            m.named[name] = r.values<1>(flags)[0];
        } else
            break; // ❓ the other kinds of node are not read
    }
    return m;
}

float StageMotion::length() const { return motion.clips.empty() ? 0 : motion.clips[0].length; }

float StageMotion::at(const Value& v, float t) const {
    if (v.channel < 0) return v.constant;
    if (motion.clips.empty() || size_t(v.channel) >= motion.clips[0].channels.size()) return 0;
    return motion.clips[0].channels[size_t(v.channel)].at(t);
}

float StageMotion::named_at(const std::string& name, float t, float otherwise) const {
    const auto it = named.find(name);
    return it == named.end() ? otherwise : at(it->second, t);
}

} // namespace gt4r::menu
