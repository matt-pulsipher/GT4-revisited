#include "gt4r/menu/model_stream_prologue.h"

#include "base/bytes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::menu {

namespace {
float rdf(ByteSpan d, size_t at) {
    float f = 0;
    std::memcpy(&f, d.data() + at, 4);
    return f;
}
constexpr size_t kLight = 0x50, kFrame = 0x30;
} // namespace

std::optional<ModelStream> read_model_stream(ByteSpan d) {
    if (d.size() < 0x20 + 2 * kLight || std::memcmp(d.data(), "LMOV", 4) != 0 || rd32(d, 0x0C) != 2) return std::nullopt;
    const size_t count = rd32(d, 0x10), at = rd32(d, 0x14);
    if (at > d.size() || count > (d.size() - at) / kFrame) return std::nullopt;
    ModelStream s;
    for (size_t k = 0; k < 2; ++k) {
        const size_t o = 0x20 + k * kLight;
        for (size_t i = 0; i < 4; ++i) {
            s.lights[k].pos[i] = rdf(d, o + i * 4);
            s.lights[k].shadow[i] = rdf(d, o + 0x10 + i * 4);
            s.lights[k].diffuse[i] = rdf(d, o + 0x20 + i * 4);
            s.lights[k].ambient[i] = rdf(d, o + 0x30 + i * 4);
            s.lights[k].specular[i] = rdf(d, o + 0x40 + i * 4);
        }
    }
    s.frames.resize(count);
    for (size_t f = 0; f < count; ++f) {
        const size_t o = at + f * kFrame;
        for (size_t i = 0; i < 3; ++i) s.frames[f].eye[i] = rdf(d, o + i * 4), s.frames[f].rotate[i] = rdf(d, o + 0x0C + i * 4);
        s.frames[f].view_angle = rdf(d, o + 0x18);
        s.frames[f].light_scale[0] = rdf(d, o + 0x1C), s.frames[f].light_scale[1] = rdf(d, o + 0x20);
    }
    return s;
}

std::string model_stream_path(const std::string& root, const std::string& name) { return "mpeg/" + root + "/" + name + ".lmv"; }

size_t ModelStream::frame_at(double seconds) const {
    if (frames.empty()) return 0;
    return std::min(size_t(std::max(seconds, 0.0) * kRate), frames.size() - 1);
}

void ModelStream::apply(size_t frame, FaceView& v) const {
    if (frame >= frames.size()) return;
    const Frame& f = frames[frame];
    for (int i = 0; i < 3; ++i) v.camera_trans[i] = f.eye[i], v.camera_rotate[i] = f.rotate[i], v.world_ambient[i] = 0;
    v.view_angle = f.view_angle;
    v.perfect_dark = false;
    for (int k = 0; k < 2; ++k) {
        FaceLight& L = v.lights[k];
        const float scale = f.light_scale[k];
        L.on = true;
        for (int i = 0; i < 4; ++i) L.pos[i] = lights[k].pos[i];
        for (int i = 0; i < 3; ++i) {
            L.amb[i] = lights[k].ambient[i] * scale;
            L.dif[i] = lights[k].diffuse[i] * scale;
            L.spe[i] = lights[k].specular[i] * scale;
            L.sdw[i] = lights[k].shadow[i];
        }
        L.sdw[3] = lights[k].shadow[3] * scale;
    }
    v.lights[2].on = false;
}

} // namespace gt4r::menu
