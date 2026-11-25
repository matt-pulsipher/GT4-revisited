#include "gt4r/race/driver_figure.h"

#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls_skin.h"

#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

constexpr float kPi = 3.14159265358979f;

float f32(ByteSpan d, size_t o) {
    float v = 0;
    if (o + 4 <= d.size()) std::memcpy(&v, d.data() + o, 4);
    return v;
}
render::Vec3 vec(ByteSpan d, size_t o) { return {f32(d, o), f32(d, o + 4), f32(d, o + 8)}; }

// The bind (stored-pose) matrix of each bone, by name.
std::vector<std::pair<std::string, render::Mat4>> bind_nodes(ByteSpan set) {
    std::vector<std::pair<std::string, render::Mat4>> out;
    for (const mdls::Bone& b : mdls::read_bones(set)) out.emplace_back(b.name, render::inverse(b.inverse_bind));
    return out;
}

const render::Mat4* find_node(const std::vector<std::pair<std::string, render::Mat4>>& nodes, const std::string& name) {
    for (const auto& [n, m] : nodes)
        if (n == name) return &m;
    return nullptr;
}

std::vector<std::pair<std::string, render::Mat4>> skeleton_nodes(ByteSpan set, ByteSpan skeleton) {
    std::vector<std::pair<std::string, render::Mat4>> nodes = bind_nodes(set);
    if (nodes.empty() && !skeleton.empty()) nodes = bind_nodes(skeleton);
    return nodes;
}

} // namespace

CockpitPlaces read_cockpit(ByteSpan file) {
    const car::CarFile c = car::CarFile::parse(file);
    if (!c.info || c.info + 0x58 > file.size()) throw FormatError("car file without an info block");
    CockpitPlaces p;
    p.cockpit_kind = file[c.info + 2];
    p.co_driver = file[c.info + 3] != 0;
    const u32 parts = rd32(file, c.info + 0x50);
    if (!parts || parts + 0x100 > file.size()) throw FormatError("car file without a parts block");
    p.wheel = vec(file, parts + 0x00);
    p.column = vec(file, parts + 0x0C);
    p.wheel_radius = f32(file, parts + 0x18);
    p.pedals[0] = vec(file, parts + 0x1C);
    p.pedals[1] = vec(file, parts + 0x28);
    p.seat = vec(file, parts + 0x34);
    p.shifter = vec(file, parts + 0x4C);
    p.point_6c = vec(file, parts + 0x6C);
    p.shifter_kind = rd16(file, parts + 0x6A);
    const render::Vec3 co = vec(file, parts + 0xC4);
    if (co.x != 0 || co.y != 0 || co.z != 0) p.co_seat = co;
    return p;
}

render::Vec3 co_seat_offset(const CockpitPlaces& c) {
    if (c.co_seat) return *c.co_seat - c.seat;
    return {-2 * c.seat.x, 0, 0};
}

std::string character_driver_file(int kind, bool co_driver, int index) {
    const int i = ((index % 3) + 3) % 3;
    switch (kind) {
    case 1: {
        static const char* const kFiles[3] = {"character/driver2.mdl", "character/driver.mdl", "character/driver3.mdl"};
        return kFiles[i];
    }
    case 2: return "character/driver4.mdl";
    case 5:
    case 7: return "character/classic1.mdl";
    case 6: return "character/classic2.mdl";
    case 0xA: return "character/driver_nike.mdl";
    case 0xE: return "character/driver.mdl";
    case 0xF: return co_driver ? "character/driver_mtr.mdl" : "character/driver_nike.mdl";
    default: return {};
    }
}

std::vector<DriverPart> driver_parts(ByteSpan set, ByteSpan skeleton) {
    std::vector<DriverPart> out;
    const u32 models = mdls::read_header(set).models;
    const auto nodes = skeleton_nodes(set, skeleton);
    for (const mdls::Symbol& s : mdls::read_symbols(set)) {
        if (s.value >= models) continue;
        DriverPart p;
        p.model = s.value;
        if (s.name == "SKINNING" || s.name == "CRANIUM2") {
            // Drawn by the bones (CRANIUM2 by the CRANIUM bone's palette matrix, 0x3B0D18): in the
            // stored pose each palette matrix is the identity.
            p.skinned = true;
            out.push_back(p);
            continue;
        }
        const render::Mat4* n = find_node(nodes, s.name);
        if (!n) continue;
        p.node = *n;
        out.push_back(p);
    }
    return out;
}

render::Mat4 driver_root(const CockpitPlaces& c, ByteSpan set, ByteSpan skeleton, bool co_driver) {
    const auto nodes = skeleton_nodes(set, skeleton);
    render::Vec3 pelvis{0, 0, 0};
    if (const render::Mat4* p = find_node(nodes, "PELVIS")) pelvis = {p->at(0, 3), p->at(1, 3), p->at(2, 3)};
    const render::Vec3 seat = co_driver ? c.seat + co_seat_offset(c) : c.seat;
    return render::translate(seat) * render::rotate_y(kPi) * render::translate(pelvis * -1.0f);
}

} // namespace gt4r::race
