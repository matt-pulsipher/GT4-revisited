#include "gt4r/formats/course/course_file.h"

#include "formats/course/course_model.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cstring>

namespace gt4r::course {

bool CourseFile::looks_like_course(ByteSpan data) {
    if (data.size() < kSlotCount * 4 + 4) return false;
    return rd32(data, 0) == 0 && rd32(data, 4) == kSlotCount * 4 && gt3r::has_magic(data.subspan(kSlotCount * 4), "MDLS");
}

CourseFile CourseFile::parse(ByteSpan data) {
    if (data.size() < kSlotCount * 4) throw FormatError("course: file too small");
    CourseFile f;
    for (int i = 0; i < kSlotCount; ++i) {
        const u32 off = rd32(data, size_t(i) * 4);
        if (!off) continue;
        if (off < kSlotCount * 4 || off > data.size()) throw FormatError("course: bad section offset");
        Section s;
        s.slot = i, s.offset = off;
        if (off + 4 <= data.size()) {
            bool printable = true;
            for (size_t k = 0; k < 4; ++k) printable &= data[off + k] >= 0x20 && data[off + k] <= 0x7E;
            if (printable) s.tag.assign(reinterpret_cast<const char*>(data.data()) + off, 4);
        }
        f.sections_.push_back(std::move(s));
    }
    // A section runs to the next one by offset (the slots are not always in file order).
    std::vector<u32> starts;
    for (const Section& s : f.sections_) starts.push_back(s.offset);
    std::sort(starts.begin(), starts.end());
    for (Section& s : f.sections_) {
        const auto next = std::upper_bound(starts.begin(), starts.end(), s.offset);
        s.size = (next == starts.end() ? u32(data.size()) : *next) - s.offset;
    }
    return f;
}

const Section* CourseFile::find(int slot) const {
    for (const Section& s : sections_)
        if (s.slot == slot) return &s;
    return nullptr;
}

ByteSpan CourseFile::section(ByteSpan data, int slot) const {
    const Section* s = find(slot);
    return s ? data.subspan(s->offset, s->size) : ByteSpan{};
}

float scenery_color_scale(ByteSpan env) {
    if (env.size() < 0x3C || !gt3r::has_magic(env, "Env0")) return 1.0f;
    float f;
    std::memcpy(&f, env.data() + 0x38, 4);
    return f >= 0.25f && f <= 8.0f ? f : 1.0f;
}

gt3r::scene::Model build_course_model(ByteSpan data, const ModelOptions& opt, const std::string& name) {
    const CourseFile file = CourseFile::parse(data);
    gt3r::scene::Model model;
    model.name = name;
    const float color_scale = scenery_color_scale(file.section(data, kSlotEnv));
    auto add = [&](int slot, const char* label) {
        const ByteSpan s = file.section(data, slot);
        if (!mdls::is_mdls(s)) return;
        // Scenery as the shared engine decodes Gran Turismo 3's: the programs' render state kept,
        // cut-out textures alpha tested, the road's second (sheen) pass left out.
        gt3r::gtm1::Options o;
        o.run.lod = opt.lod;
        o.textures = opt.textures;
        o.reflection = false;
        o.course_env_maps = true;
        o.alpha_test = true;
        o.gs_state = true;
        mdls::Scene scene;
        scene.color_scale = color_scale;
        gt3r::scene::Model m = mdls::build_model(s, o, scene, label);
        for (gt3r::scene::Mesh& mesh : m.meshes) mesh.name = std::string(label) + "/" + mesh.name;
        gt3r::course::append_model(model, std::move(m));
    };
    if (opt.world) add(kSlotWorld, "world");
    if (opt.environment) add(kSlotEnvironment, "environment");
    if (opt.course_line) add(kSlotCourseLine, "course_line");
    if (opt.after) add(kSlotAfter, "after");
    if (opt.far) add(kSlotFar, "far");
    if (opt.sky) add(kSlotSky, "sky");
    model.compute_bounds();
    return model;
}

} // namespace gt4r::course
