#include "gt4r/race/driver_figure.h"

#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"

#include <cstring>

namespace gt4r::race {

namespace {

// A set's extension record (header +0x68: u8 its size in dwords, u24 its id, the data); empty: none.
ByteSpan extension(ByteSpan set, u32 id) {
    if (set.size() < 0x6C) return {};
    for (size_t at = rd32(set, 0x68); at && at + 4 <= set.size();) {
        const u32 head = rd32(set, at), words = head & 0xFF;
        if (!words || !(head >> 8)) break;
        const size_t bytes = size_t(words) * 4;
        if (head >> 8 == id && at + bytes <= set.size()) return set.subspan(at + 4, bytes - 4);
        at += bytes;
    }
    return {};
}

} // namespace

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

CockpitBytes read_cockpit_bytes(ByteSpan file) {
    const car::CarFile c = car::CarFile::parse(file);
    if (!c.info || c.info + 4 > file.size()) throw FormatError("car file without an info block");
    return {file[c.info + 2], file[c.info + 3] != 0};
}

posture::Skeleton driver_skeleton(ByteSpan set) {
    const ByteSpan d = extension(set, 0x800000);
    std::array<posture::F, 15> sizes;
    if (d.size() < 4 * sizes.size()) throw FormatError("driver set without its skeleton's sizes");
    for (size_t i = 0; i < sizes.size(); ++i) sizes[i] = posture::F::raw(rd32(d, 4 * i));
    return posture::build_skeleton(sizes);
}

render::Mat4 node_matrix(const posture::Bone& b) {
    const posture::V3 x = posture::x_axis(b.q), y = posture::y_axis(b.q), z = posture::z_axis(b.q);
    render::Mat4 m;
    m.m = {x.x.host(), x.y.host(), x.z.host(), 0, y.x.host(), y.y.host(), y.z.host(), 0, z.x.host(), z.y.host(), z.z.host(), 0,
           b.pos.x.host(), b.pos.y.host(), b.pos.z.host(), 1};
    return m;
}

std::vector<DriverPart> driver_parts(ByteSpan set) {
    std::vector<DriverPart> out;
    const u32 models = mdls::read_header(set).models;
    for (const mdls::Symbol& s : mdls::read_symbols(set)) {
        if (s.value >= models) continue;
        DriverPart p;
        p.model = s.value;
        if (s.name == "SKINNING" || s.name == "CRANIUM2") {
            p.skinned = true;
            if (s.name == "CRANIUM2") p.node = posture::kCranium;
        } else {
            for (int n = 0; n < posture::kNodes; ++n)
                if (s.name == posture::kNodeNames[n]) p.node = n;
            if (p.node < 0) continue;
        }
        out.push_back(p);
    }
    return out;
}

DriverModels::DriverModels(render::GpuContext& ctx, ByteSpan set, const std::string& name, Keep* keep)
    : ctx_(ctx), parts_(driver_parts(set)), skeleton_(driver_skeleton(set)), bones_(mdls::read_bones(set)) {
    if (const ByteSpan d = extension(set, 0x800001); d.size() >= 8)
        for (size_t k = 0; k < 2; ++k) std::memcpy(&level_size_[k], d.data() + 4 * k, 4);
    models_.resize(parts_.size());
    stored_.resize(parts_.size());
    if (!bones_.empty()) skin_ = mdls::SkinTable::of(set);
    for (size_t i = 0; i < parts_.size(); ++i) {
        gt3r::gtm1::Options o;
        o.node = int(parts_[i].model);
        o.program_alpha_test = true;
        o.weighted_reflection = true;
        o.gs_mip_levels = true;
        gt3r::scene::Model m = mdls::build_model(set, o, mdls::Scene{}, name + "/driver");
        bool any = false;
        for (const gt3r::scene::Mesh& mesh : m.meshes) any |= !mesh.indices.empty();
        if (!any) continue;
        if (parts_[i].skinned) stored_[i] = m;
        if (keep) keep->emplace_back(&models_[i], std::move(m));
        else models_[i] = std::make_unique<render::GpuModel>(ctx, m);
    }
}

void DriverFigure::pose(const posture::PostureInput& in) {
    posture::Controls c = in.controls;
    posture::solve(c, skeleton_, in.places, in.has_gaze ? &in.gaze : nullptr);
    skin();
}

void DriverFigure::set_skeleton(const posture::Skeleton& s) {
    skeleton_ = s;
    skin();
}

void DriverFigure::skin() {
    if (models_.bones_.empty()) return;
    // Each bone by the node of its name.
    u32 slots = 0;
    for (const mdls::Bone& b : models_.bones_) slots = std::max(slots, b.index + 1);
    mdls::Palette palette(slots, render::Mat4::identity());
    for (const mdls::Bone& b : models_.bones_)
        for (int n = 0; n < posture::kNodes; ++n)
            if (b.name == posture::kNodeNames[n]) palette[b.index] = node_matrix(skeleton_[n]) * b.inverse_bind;
    posed_.resize(models_.parts_.size());
    for (size_t i = 0; i < models_.parts_.size(); ++i) {
        const DriverPart& p = models_.parts_[i];
        if (!p.skinned || p.node >= 0 || !models_.models_[i]) continue;
        std::vector<gt3r::scene::Mesh> meshes;
        mdls::pose(models_.stored_[i].meshes, models_.skin_, palette, meshes);
        if (!posed_[i]) posed_[i] = std::make_unique<render::GpuModel>(models_.ctx_, *models_.models_[i]);
        if (!posed_[i]->update_geometry(meshes)) posed_[i].reset();
    }
}

void DriverFigure::add_items(std::vector<render::DrawItem>& items, const render::Mat4& body) const {
    for (size_t i = 0; i < models_.parts_.size(); ++i) {
        const DriverPart& p = models_.parts_[i];
        if (!models_.models_[i]) continue;
        if (p.skinned && p.node < 0) {
            if (i < posed_.size() && posed_[i]) items.push_back({posed_[i].get(), body, 0});
            continue;
        }
        render::Mat4 at = node_matrix(skeleton_[p.node]);
        if (p.skinned) // by the bone's palette matrix: its vertices are the stored pose's
            for (const mdls::Bone& b : models_.bones_)
                if (b.name == posture::kNodeNames[p.node]) at = at * b.inverse_bind;
        items.push_back({models_.models_[i].get(), body * at, 0});
    }
}

} // namespace gt4r::race
