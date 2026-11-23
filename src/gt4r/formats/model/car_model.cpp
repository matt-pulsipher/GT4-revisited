#include "gt4r/formats/model/car_model.h"

#include "formats/course/course_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::car {

CarFile CarFile::parse(ByteSpan d) {
    if (!is_car(d)) throw FormatError("not a CAR4 car file");
    CarFile c;
    c.info = rd32(d, 0x10), c.collision = rd32(d, 0x14);
    c.body = rd32(d, 0x18), c.body_paint = rd32(d, 0x1C);
    c.wheel = rd32(d, 0x20), c.wheel_paint = rd32(d, 0x24), c.wing = rd32(d, 0x28);
    c.tire[0] = rd32(d, 0x2C), c.tire[1] = rd32(d, 0x30), c.driver = rd32(d, 0x34);
    for (u32 off : {c.body, c.wheel, c.tire[0], c.tire[1]})
        if (off && (off >= d.size() || !mdls::is_mdls(d.subspan(off)))) throw FormatError("CAR4: a part is not a model set");
    if (!c.body) throw FormatError("CAR4: no body");
    return c;
}

u32 paint_count(ByteSpan pat) { return pat.size() >= 0x20 && gt3r::has_magic(pat, "Pat0") ? rd16(pat, 0x10) : 0; }

Bytes apply_paint(ByteSpan set, ByteSpan pat, u32 paint) {
    const u32 size = mdls::read_header(set).size;
    Bytes out(set.begin(), set.begin() + size);
    const u32 paints = paint_count(pat);
    if (paint >= paints) return out;
    const u32 patches = rd16(pat, 0x12);
    for (u32 k = 0; k < patches; ++k) {
        const u32 at = rd32(pat, 0x20 + 4 * (size_t(paint) * patches + k));
        const u32 target = rd32(pat, at), bytes = rd32(pat, at + 4);
        if (at + 8 + u64(bytes) > pat.size() || u64(target) + bytes > out.size()) throw FormatError("Pat0: patch out of range");
        std::memcpy(out.data() + target, pat.data() + at + 8, bytes);
    }
    return out;
}

namespace {

// A model's extent along x, and its largest distance from the x axis.
struct Extent {
    float x0 = 0, x1 = 0, radius = 0;
};
Extent extent(const gt3r::scene::Model& m) {
    Extent e;
    bool first = true;
    for (const gt3r::scene::Mesh& mesh : m.meshes)
        for (const gt3r::scene::Vertex& v : mesh.vertices) {
            if (first || v.pos[0] < e.x0) e.x0 = v.pos[0];
            if (first || v.pos[0] > e.x1) e.x1 = v.pos[0];
            e.radius = std::max(e.radius, std::hypot(v.pos[1], v.pos[2]));
            first = false;
        }
    return e;
}

// Puts a part in place: scaled along x and across it, turned half round about y for the
// right side, then moved (column-major matrix; mesh.transform is applied after the mesh's own).
void place(gt3r::scene::Model& m, float sx, float sr, bool right, float x, float y, float z) {
    for (gt3r::scene::Mesh& mesh : m.meshes) {
        for (gt3r::scene::Vertex& v : mesh.vertices) {
            float px = v.pos[0] * sx, pz = v.pos[2] * sr;
            float nx = v.normal[0], nz = v.normal[2];
            if (right) px = -px, pz = -pz, nx = -nx, nz = -nz;
            v.pos[0] = px + x, v.pos[1] = v.pos[1] * sr + y, v.pos[2] = pz + z;
            v.normal[0] = nx, v.normal[2] = nz;
        }
    }
}

} // namespace

gt3r::scene::Model build_car(ByteSpan file, ByteSpan pat, const Wheels* wheels, const Options& opt, const std::string& name) {
    const CarFile c = CarFile::parse(file);
    mdls::Options mo;
    mo.textures = opt.textures;
    mo.lod = opt.lod;
    // The body's models: 0 the body, 1 its light shadow, 2 its contact shadow, 3 the exhaust flame.
    mo.model = 0;
    if (c.body_paint && c.body_paint < file.size()) pat = file.subspan(c.body_paint);
    gt3r::scene::Model model;
    if (paint_count(pat)) model = mdls::build_model(apply_paint(file.subspan(c.body), pat, opt.paint), mo, name);
    else model = mdls::build_model(file.subspan(c.body), mo, name);
    model.name = name;
    if (!opt.wheels || !wheels || !wheels->valid() || !c.wheel) {
        model.compute_bounds();
        return model;
    }
    // The wheel and tire sets are in units of their own: the tire one unit wide from its outer
    // face at x = 0, the rim's radius 0.8 where the tire's outer radius is about 1.3. Both are
    // scaled across by the tire's radius over the tire model's, and along by the tire's width.
    // A set's models are a left wheel; the right side's is turned half round.
    for (int axle = 0; axle < 2; ++axle) {
        const u32 tire_at = c.tire[axle] ? c.tire[axle] : c.tire[0];
        if (!tire_at) continue;
        mdls::Options to = mo;
        to.model = 0;
        const gt3r::scene::Model tire = mdls::build_model(file.subspan(tire_at), to, "tire");
        const Extent te = extent(tire);
        if (te.radius <= 0 || te.x1 <= te.x0) continue;
        const float radius = wheels->tire_diameter[axle] * 0.5f, width = wheels->tire_width[axle];
        const float sr = radius / te.radius, sx = width / (te.x1 - te.x0);
        const float z = (axle == 0 ? 0.5f : -0.5f) * wheels->wheelbase; // the front is +z
        for (int side = 0; side < 2; ++side) {
            const float x = (side == 0 ? -0.5f : 0.5f) * wheels->outer_width[axle];
            gt3r::scene::Model t = tire;
            place(t, sx, sr, side == 1, x, radius, z);
            for (gt3r::scene::Mesh& mesh : t.meshes) mesh.name = "tire";
            gt3r::course::append_model(model, std::move(t));
            mdls::Options wo = mo;
            wo.model = axle * 2 + side;
            wo.scene.outside_sets[mdls::kWheelFaceSlot] = mdls::kWheelFaceSet;
            gt3r::scene::Model w = mdls::build_model(file.subspan(c.wheel), wo, "wheel");
            place(w, sx, sr, side == 1, x, radius, z);
            for (gt3r::scene::Mesh& mesh : w.meshes) mesh.name = "wheel";
            gt3r::course::append_model(model, std::move(w));
        }
    }
    model.compute_bounds();
    return model;
}

} // namespace gt4r::car
