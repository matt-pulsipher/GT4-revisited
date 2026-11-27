#include "gt4r/race/car_visual.h"

#include "base/log.h"
#include "formats/course/course_model.h"
#include "formats/model/brake_model.h"
#include "gt4r/race/car_pose.h"
#include "gt4r/race/driver_figure.h"
#include "gt4r/race/picture_scale.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace gt4r::race {

namespace {

namespace scene = gt3r::scene;
using render::Mat4;

thread_local bool t_unuploaded = false; // make_unuploaded is building

constexpr int kSkip = 99;       // a callback value no switch has a case for: the construct is left out
constexpr int kWheelLevels = 2; // the body's levels whose wheels are items of their own (the far level draws its own)

// Row-vector 4 x 4 matrices (v' = v M), as the decoder places the shapes.
using RowMatrix = std::array<float, 16>;
RowMatrix row_mul(const RowMatrix& a, const RowMatrix& b) {
    RowMatrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r[size_t(i * 4 + j)] += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
    return r;
}
// The inverse of a matrix of turns, scales and a translation (row vectors).
RowMatrix inverse_affine(const RowMatrix& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    RowMatrix r{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (std::fabs(det) < 1e-12f) return r;
    r[0] = (e * i - f * h) / det, r[1] = (c * h - b * i) / det, r[2] = (b * f - c * e) / det;
    r[4] = (f * g - d * i) / det, r[5] = (a * i - c * g) / det, r[6] = (c * d - a * f) / det;
    r[8] = (d * h - e * g) / det, r[9] = (b * g - a * h) / det, r[10] = (a * e - b * d) / det;
    for (int j = 0; j < 3; ++j) r[size_t(12 + j)] = -(m[12] * r[size_t(j)] + m[13] * r[size_t(4 + j)] + m[14] * r[size_t(8 + j)]);
    return r;
}

render::Vec3 v3(const course::Vec3& v) { return {v.x, v.y, v.z}; }

// The callback values of a car's body for the race view's draws: the wheels' constructs are left
// out (the wheels are items of their own), the spoiler is as built.
mdls::Scene body_scene(bool brake, int lamp_level, int spoiler = 0) {
    mdls::Scene s;
    s.resolve_callbacks = true;
    s.callback_default = 0;
    s.callback_values[0] = brake ? 1 : 0;
    s.callback_values[7] = lamp_level;
    for (u16 id = 11; id <= 18; ++id) s.callback_values[id] = kSkip;
    for (u16 id = 20; id <= 39; ++id) s.callback_values[id] = kSkip;
    s.callback_values[47] = spoiler; // 0 as built, 1 a wing fitted, 2 the spoiler taken off
    return s;
}

// A car's sets as the shared engine decodes its own cars': the programs' alpha test cuts the wheel
// arches and grilles out, the reflection passes are weighted by the paint's own alpha, the
// pictures are sampled at full size.
gt3r::gtm1::Options car_options() {
    gt3r::gtm1::Options o;
    o.program_alpha_test = true;
    o.weighted_reflection = true;
    o.gs_mip_levels = true;
    use_node_fog(o);
    return o;
}

// The reflection passes' weights: those the program draws with its constant factors (0x57, marked C1 = -0 by
// mdls::to_gtm1) keep C0 = 1, C1 = 0; the others take the draw's (Options::reflection_by_draw, reflection_c0 / _c1).
void weigh_reflections(scene::Model& m, bool by_draw, float c0, float c1) {
    for (scene::Mesh& mesh : m.meshes)
        for (scene::Primitive& p : mesh.primitives) {
            if (!(p.flags & scene::kEnvironmentMap) || !p.env_rows || p.env_c0[0] != 1 || p.env_c1[0] != 0) continue;
            if (std::signbit(p.env_c1[0])) {
                for (float& c : p.env_c1) c = 0;
            } else if (c0 >= 0) {
                for (int i = 0; i < 3; ++i) p.env_c0[i] = c0, p.env_c1[i] = c1;
            } else if (by_draw) {
                p.env_rows = false;
            }
        }
}

// A set's model at every level of detail it has (at most `levels`) as one model, the meshes
// tagged with their level.
scene::Model build_levels(ByteSpan set, int model, const mdls::Scene& sc, int levels, int texture_list, const std::string& name) {
    scene::Model out;
    out.name = name;
    for (int lod = 0; lod < levels; ++lod) {
        gt3r::gtm1::Options o = car_options();
        o.node = model;
        o.run.lod = lod;
        o.run.texture_lod = texture_list;
        scene::Model m = mdls::build_model(set, o, sc, name);
        for (scene::Mesh& mesh : m.meshes) mesh.lod = lod;
        gt3r::course::append_model(out, std::move(m));
    }
    return out;
}

// The tire shape every car shares, made the car's size (in units of the rim's radius): as wide
// as the tire, the inside at 1 and the outside at R / r.
void reshape_tire(scene::Model& tire, float width, float rim_radius, float tire_radius) {
    float r_in = 1e9f, r_out = 0, x_max = 0;
    for (const scene::Mesh& mesh : tire.meshes)
        for (const scene::Vertex& v : mesh.vertices) {
            const float d = std::hypot(v.pos[1], v.pos[2]);
            r_in = std::min(r_in, d), r_out = std::max(r_out, d), x_max = std::max(x_max, std::fabs(v.pos[0]));
        }
    if (r_out <= r_in || x_max <= 0 || rim_radius <= 0) return;
    const float kx = (width / rim_radius) / x_max, kr = ((tire_radius - rim_radius) / rim_radius) / (r_out - r_in);
    for (scene::Mesh& mesh : tire.meshes)
        for (scene::Vertex& v : mesh.vertices) {
            const float d = std::hypot(v.pos[1], v.pos[2]);
            const float to = 1 + (d - r_in) * kr;
            v.pos[0] *= kx;
            if (d > 1e-6f) v.pos[1] *= to / d, v.pos[2] *= to / d;
        }
}

// A model put together from several decodes of a set (its levels of detail, its lit and unlit lamps) holds the set's
// pictures once for each: the pictures its shapes use, each kept once.
void share_textures(scene::Model& m) {
    const auto same = [](const scene::Texture& a, const scene::Texture& b) {
        return a.width == b.width && a.height == b.height && a.clamp_u == b.clamp_u && a.clamp_v == b.clamp_v && a.no_mips == b.no_mips && a.nearest == b.nearest &&
               a.livery_cover == b.livery_cover && a.rgba == b.rgba;
    };
    std::vector<int> to(m.textures.size(), -1);
    std::vector<scene::Texture> kept;
    for (scene::Mesh& mesh : m.meshes)
        for (scene::Primitive& p : mesh.primitives) {
            if (p.texture < 0 || size_t(p.texture) >= to.size()) continue;
            int& at = to[size_t(p.texture)];
            if (at < 0) {
                scene::Texture& t = m.textures[size_t(p.texture)];
                const auto it = std::find_if(kept.begin(), kept.end(), [&](const scene::Texture& k) { return same(k, t); });
                at = int(it - kept.begin());
                if (it == kept.end()) kept.push_back(std::move(t));
            }
            p.texture = at;
        }
    m.textures = std::move(kept);
}

// Mirrors a model front to back (a wheel whose right side is the left's mirror image).
void mirror_z(scene::Model& m) {
    for (scene::Mesh& mesh : m.meshes) {
        for (scene::Vertex& v : mesh.vertices) v.pos[2] = -v.pos[2], v.normal[2] = -v.normal[2];
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
    }
}

// Whether the wheel set's symbols name model `model` a mirror image ("SYM").
bool is_mirror_model(ByteSpan set, u32 model) {
    if (set.size() < 0x60) return false;
    const u32 n = rd16(set, 0x2A), table = rd32(set, 0x58);
    for (u32 i = 0; i < n && table + 8 * size_t(i) + 8 <= set.size(); ++i) {
        const u32 name = rd32(set, table + 8 * size_t(i)), value = rd32(set, table + 8 * size_t(i) + 4);
        if (value == model && name + 4 <= set.size() && std::memcmp(set.data() + name, "SYM", 4) == 0) return true;
    }
    return false;
}

// A model set with one more texture set in each of its lists, `tex1` (a Tex1 set of another file):
// the new set's number is the set's count of sets before. Its lists and the set go after the block.
Bytes with_texture_set(ByteSpan set, ByteSpan tex1, int& index) {
    const mdls::Header h = mdls::read_header(set);
    index = int(h.texture_sets);
    const u32 tex_size = tex1.size() >= 0x10 ? std::min<u32>(rd32(tex1, 0x0C), u32(tex1.size())) : 0;
    Bytes out(set.begin(), set.begin() + std::min<size_t>(h.size ? h.size : set.size(), set.size()));
    auto align = [&] { out.resize((out.size() + 15) & ~size_t(15), 0); };
    auto put32 = [&](size_t at, u32 v) { std::memcpy(out.data() + at, &v, 4); };
    align();
    const u32 tex_at = u32(out.size());
    out.insert(out.end(), tex1.begin(), tex1.begin() + tex_size);
    for (u32 l = 0; l < h.texture_lists; ++l) {
        align();
        const u32 list = u32(out.size());
        const u32 old_list = rd32(set, h.texture_table + 4 * l);
        for (u32 k = 0; k < h.texture_sets; ++k) {
            const u32 v = old_list + 4 * k + 4 <= set.size() ? rd32(set, old_list + 4 * k) : 0;
            out.resize(out.size() + 4), put32(out.size() - 4, v);
        }
        out.resize(out.size() + 4), put32(out.size() - 4, tex_at);
        put32(h.texture_table + 4 * l, list);
    }
    align();
    const u16 sets = u16(h.texture_sets + 1);
    std::memcpy(out.data() + 0x1C, &sets, 2);
    put32(0x10, u32(out.size()));
    return out;
}

Mat4 basis(const render::Vec3& right, const render::Vec3& up, const render::Vec3& back, const render::Vec3& origin) {
    Mat4 m;
    m.m = {right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, back.x, back.y, back.z, 0, origin.x, origin.y, origin.z, 1};
    return m;
}

} // namespace

CarVisual::CarVisual(render::GpuContext& ctx, ByteSpan file, ByteSpan pat, const CarSpec& spec, const Options& opt, const std::string& name)
    : spec_(spec), ctx_(&ctx) {
    if (!car::CarFile::is_car(file) && is_gt3_car(file)) {
        build_gt3(file, opt, name); // (Prologue's cars)
        return;
    }
    reflection_by_draw_ = opt.reflection_by_draw, reflection_c0_ = opt.reflection_c0, reflection_c1_ = opt.reflection_c1;
    lamp_level_ = opt.lamp_level;
    const car::CarFile c = car::CarFile::parse(file);
    if (c.body_paint && c.body_paint < file.size()) pat = file.subspan(c.body_paint);
    Bytes painted;
    ByteSpan body = file.subspan(c.body);
    if (car::paint_count(pat)) {
        painted = car::apply_paint(body, pat, std::min(opt.paint, car::paint_count(pat) - 1));
        body = painted;
    }
    const u32 body_models = mdls::read_header(body).models;

    // The level of detail's switch at the start of the body's program (0x07: centre, size), after the
    // render-state commands without operands that come before it (0x22 on the cars looked at).
    {
        const mdls::Header h = mdls::read_header(body);
        u32 pc = h.models && h.model_table + 0x1C <= body.size() ? rd32(body, h.model_table + 0x18) : 0;
        const auto plain = [](u8 op) {
            for (u8 o : {0x01, 0x0B, 0x0C, 0x0D, 0x17, 0x18, 0x1A, 0x1B, 0x1D, 0x1E, 0x22, 0x23, 0x25, 0x26, 0x2D, 0x2E, 0x30, 0x40, 0x4E, 0x4F, 0x56, 0x57, 0x58})
                if (op == o) return true;
            return false;
        };
        for (int k = 0; pc && k < 8 && pc < body.size() && plain(body[pc]); ++k) ++pc;
        if (pc && pc + 18 <= body.size() && body[pc] == 0x07) {
            float f[4];
            std::memcpy(f, body.data() + pc + 1, 16);
            lod_centre_ = {f[0], f[1], f[2]}, lod_size_ = f[3], lod_levels_ = body[pc + 17];
        }
    }
    // A fitted wing (wing/lod/<label>): the stock spoiler's case 1 of callback 47; the wing set's
    // model 0 is drawn before the body at the parts block's place (+0xD0), scaled by half its width
    // (+0xDC) (0x4533A8).
    const int spoiler = opt.wing.empty() ? 0 : 1;
    if (!opt.wing.empty() && c.info && c.info + 0x58 <= file.size()) {
        const u32 parts = rd32(file, c.info + 0x50);
        if (parts && parts + 0xE0 <= file.size()) {
            float f[4];
            std::memcpy(f, file.data() + parts + 0xD0, 16);
            wing_place_ = {f[0], f[1], f[2]}, wing_scale_ = f[3] * 0.5f;
            mdls::Scene ws;
            ws.resolve_callbacks = true;
            scene::Model m = build_levels(opt.wing, 0, ws, 3, 0, name + "/wing");
            for (const scene::Mesh& mesh : m.meshes) wing_lods_ = std::max(wing_lods_, mesh.lod + 1);
            if (!m.meshes.empty()) to_gpu(wing_, std::move(m));
        }
    }
    // The body with its lamps unlit and lit, at its three levels of detail. The far level draws its
    // wheels as shapes of its own with the picture callback 46 binds to slot 2: the wheel set's last
    // texture set (its far level's turning face), given to the body as a set of its own here.
    constexpr int kLevels = kWheelLevels;
    Bytes far_body;
    int far_set = -1;
    if (c.wheel) {
        const ByteSpan wheel_set = file.subspan(c.wheel);
        const mdls::Header wh = mdls::read_header(wheel_set);
        const u32 list = wh.texture_lists ? rd32(wheel_set, wh.texture_table) : 0;
        const u32 last = wh.texture_sets && list + 4 * wh.texture_sets <= wheel_set.size() ? rd32(wheel_set, list + 4 * (wh.texture_sets - 1)) : 0;
        if (lod_levels_ >= 3 && last && last + 0x20 <= wheel_set.size()) far_body = with_texture_set(body, wheel_set.subspan(last), far_set);
    }
    // A scripted body (docs/formats/model.md "What a car's script reads"): the brake lamps by script, and on a few cars
    // moving parts, the shapes under a turn by a register (0x48: the 1886 Benz's flywheel and belts) and those blended by
    // a register's weight (0x4C: a pace car's light bar).
    mdls::Script script;
    try {
        script = mdls::Script::parse(body);
    } catch (const std::exception&) {
    }
    // A scripted body's registers for its still draws, the brake off and on (the brake lamps by script: their lenses under
    // 0x54's `gtala`, 1 while `brake` is above 0).
    std::array<std::vector<u32>, 2> still_registers;
    for (int brake = 0; brake < 2; ++brake) {
        const std::pair<std::string, float> hosts[] = {{"brake", brake ? 1.0f : 0.0f}, {"taillamp", brake ? 1.0f : 0.0f}, {"timezone", float(opt.lamp_level)}};
        still_registers[size_t(brake)] = mdls::still_registers(script, hosts);
    }
    mdls::RegisterReads reads;
    if (!script.empty()) {
        try {
            reads = mdls::register_reads(body, 0);
        } catch (const std::exception&) {
        }
    }
    if (!reads.turns.empty() || !reads.weights.empty()) {
        moving_ = std::make_unique<Moving>();
        Moving& mv = *moving_;
        mv.script = script;
        mv.time_cell = script.host_cell("time"), mv.rpm_cell = script.host_cell("engineRPM"), mv.brake_cell = script.host_cell("brake");
        mv.left_cell = script.host_cell("pedalLeft"), mv.right_cell = script.host_cell("pedalRight");
        mv.taillamp_cell = script.host_cell("taillamp"), mv.timezone_cell = script.host_cell("timezone");
        mv.weights = reads.weights;
        for (const mdls::RegisterTurn& t : reads.turns) {
            Moving::Part part;
            part.op = t.op, part.cell = t.cell, part.pivot = t.pivot;
            part.pivot_inverse = inverse_affine(t.pivot);
            mv.parts.push_back(std::move(part));
        }
    }
    const auto body_levels = [&](int brake, float morph_weight, int turned = -2, const std::map<u16, float>& weights = {}) {
        mdls::Scene base = body_scene(brake != 0, opt.lamp_level, spoiler);
        base.morph_weight = morph_weight;
        base.morph_weights = weights;
        base.registers = still_registers[size_t(brake)];
        base.turned = turned;
        scene::Model m = build_levels(body, 0, base, kLevels, 0, name);
        if (!far_body.empty()) {
            mdls::Scene sc = base;
            sc.outside_sets[mdls::kWheelFaceSlot] = far_set;
            gt3r::gtm1::Options o = car_options();
            o.node = 0;
            o.run.lod = 2;
            scene::Model far = mdls::build_model(far_body, o, sc, name);
            for (scene::Mesh& mesh : far.meshes) mesh.lod = 2;
            gt3r::course::append_model(m, std::move(far));
        }
        return m;
    };
    for (int brake = 0; brake < 2; ++brake) {
        // With turned parts: the body without them, and each part alone.
        const int turned = moving_ && !moving_->parts.empty() ? -1 : -2;
        scene::Model m = body_levels(brake, -1, turned);
        if (moving_ && !moving_->weights.empty()) {
            std::vector<scene::Model> ends;
            for (u16 w : moving_->weights) ends.push_back(body_levels(brake, -1, turned, {{w, 1.0f}}));
            split_band(m, ends, moving_->band[size_t(brake)]);
        }
        if (moving_)
            for (Moving::Part& part : moving_->parts) to_gpu(part.gpu[size_t(brake)], body_levels(brake, -1, int(part.cell)));
        if (m.meshes.empty()) throw FormatError("The car's body has nothing to draw.");
        if (brake == 0) {
            m.compute_bounds();
            min_ = {m.bounds_min[0], m.bounds_min[1], m.bounds_min[2]};
            max_ = {m.bounds_max[0], m.bounds_max[1], m.bounds_max[2]};
            for (const scene::Mesh& mesh : m.meshes) lods_ = std::max(lods_, mesh.lod + 1);
        }
        // The band: a model of its own (the body's pictures it draws with), drawn at rest without a Motion.
        if (moving_ && !moving_->band[size_t(brake)].meshes.empty()) {
            Moving::Band& band = moving_->band[size_t(brake)];
            scene::Model bm;
            bm.name = name + "/band";
            bm.textures = m.textures;
            bm.meshes = band.meshes;
            weigh_reflections(bm, reflection_by_draw_, reflection_c0_, reflection_c1_);
            share_textures(bm);
            band.meshes = bm.meshes;
            to_gpu(band.gpu, std::move(bm), true);
        }
        to_gpu(body_[size_t(brake)], std::move(m));
    }
    if (moving_) {
        // The parts at rest: the still registers' turns.
        mdls::ScriptState st(moving_->script);
        st.registers = still_registers[0];
        moving_->still_turns = part_matrices(st);
    }
    // The brake parts: the shapes the body's program draws in each wheel's frame.
    for (u16 w = 0; w < 4; ++w) {
        mdls::Scene sc = body_scene(false, opt.lamp_level);
        sc.callback_values[u16(11 + w)] = 0;
        sc.only_inside = 11 + w;
        scene::Model m = build_levels(body, 0, sc, kLevels, 0, name + "/brake");
        // The disc's glow: the added shapes among them, which the program draws under the colour scale
        // (1, 1, 1, glow) callback 11 + w sets (0x453798); an item of its own whose alpha is the glow.
        scene::Model glow = m;
        bool any = false, any_glow = false;
        for (scene::Mesh& mesh : m.meshes) {
            std::erase_if(mesh.primitives, [](const scene::Primitive& p) { return (p.flags & scene::kAdditive) != 0; });
            any |= !mesh.primitives.empty();
        }
        for (scene::Mesh& mesh : glow.meshes) {
            std::erase_if(mesh.primitives, [](const scene::Primitive& p) { return (p.flags & scene::kAdditive) == 0; });
            any_glow |= !mesh.primitives.empty();
        }
        if (any) to_gpu(brakes_[w], std::move(m));
        if (any_glow) to_gpu(glows_[w], std::move(glow));
    }
    // Wheels and tires, still and turning. Both sets are in units of the rim's radius, modelled
    // as a left wheel with the outer face at x = 0.
    if (c.wheel) {
        const ByteSpan wheel_set = file.subspan(c.wheel);
        const u32 wheel_models = mdls::read_header(wheel_set).models;
        for (u32 w = 0; w < 4 && w < wheel_models; ++w) {
            const int axle = int(w / 2), side = int(w & 1);
            const u32 tire_at = c.tire[axle] ? c.tire[axle] : c.tire[0];
            for (int turning = 0; turning < 2; ++turning) {
                scene::Model both;
                both.name = name + "/wheel";
                if (tire_at) {
                    const ByteSpan tire_set = file.subspan(tire_at);
                    const mdls::Header th = mdls::read_header(tire_set);
                    mdls::Scene ts;
                    ts.resolve_callbacks = true;
                    // The turning tire is the set's second list of pictures.
                    scene::Model tire = build_levels(tire_set, th.models > u32(side) ? side : 0, ts, kLevels, th.texture_lists > 1 ? turning : 0, "tire");
                    reshape_tire(tire, spec.tire_width[axle], spec.rim_radius[axle], spec.tire_radius[axle]);
                    gt3r::course::append_model(both, std::move(tire));
                }
                // The face is drawn through outside slot 2: the level's still picture or the one
                // after it, the turning one (sets 3 / 4 for the nearest level, 5 / 6, 7 / 8).
                for (int lod = 0; lod < kLevels; ++lod) {
                    mdls::Scene ws;
                    ws.resolve_callbacks = true;
                    ws.outside_sets[mdls::kWheelFaceSlot] = mdls::kWheelFaceSet + 2 * lod + turning;
                    gt3r::gtm1::Options o = car_options();
                    o.node = int(w);
                    o.run.lod = lod;
                    scene::Model wheel = mdls::build_model(wheel_set, o, ws, "wheel");
                    for (scene::Mesh& mesh : wheel.meshes) mesh.lod = lod;
                    if (is_mirror_model(wheel_set, w)) mirror_z(wheel);
                    gt3r::course::append_model(both, std::move(wheel));
                }
                bool any = false;
                for (const scene::Mesh& mesh : both.meshes) any |= !mesh.indices.empty();
                if (any) to_gpu(wheels_[w][size_t(turning)], std::move(both));
            }
        }
    }
    // The shadows: model 1, the body's shape, which the race throws flat along the light, and
    // model 2, the shadow under the car (flat, at y = 0). Black shapes whose vertex alpha is
    // their strength.
    for (u32 k = 0; k < 3 && body_models > k + 1; ++k) {
        mdls::Scene sc = body_scene(false, 1);
        gt3r::gtm1::Options o;
        o.node = int(k + 1);
        shadow_src_[k] = mdls::build_model(body, o, sc, name + "/shadow");
    }
    mounts_ = read_camera_mounts(file);
    build_driver(file, opt, name);
    // The lamps (info +0x18, +0x20, +0x28: count and offset from the file's start; 0x70 bytes
    // each): a matrix whose rows are the lamp's axes and place (the third axis points into the
    // car, against the light), its half sizes, the point tested for being in sight, and flags.
    if (c.info && c.info + 0x30 <= file.size()) {
        auto f = [&](size_t o) {
            float v;
            std::memcpy(&v, file.data() + o, 4);
            return v;
        };
        std::vector<gt3r::race::CarLampRecord>* const lists[3] = {&lamps_.head, &lamps_.tail, &lamps_.brake};
        for (size_t l = 0; l < 3; ++l) {
            const u32 n = rd32(file, c.info + 0x18 + 8 * l), at = rd32(file, c.info + 0x1C + 8 * l);
            for (u32 i = 0; i < n && i < 32 && at + 0x70 * size_t(i) + 0x70 <= file.size(); ++i) {
                const size_t o = at + 0x70 * size_t(i);
                gt3r::race::CarLampRecord r;
                r.xaxis = {f(o), f(o + 4), f(o + 8)};
                r.view_axis = {f(o + 0x20), f(o + 0x24), f(o + 0x28)};
                r.pos = {f(o + 0x30), f(o + 0x34), f(o + 0x38)};
                r.a = f(o + 0x40), r.b = f(o + 0x44);
                r.test = {f(o + 0x48), f(o + 0x4C), f(o + 0x50)};
                // The lamp's own color (+0x54: white ahead, red behind), which the course's lamp color and the list's strength
                // multiply (0x3B8A7C); the power of its directional factor (+0x60, by the course's factor: set_lamp_type_factor).
                r.rgb = {f(o + 0x54), f(o + 0x58), f(o + 0x5C)};
                r.exponent = int(std::min(rd32(file, o + 0x60), 32u));
                r.fade = (rd16(file, o + 0x64) & 1) != 0;
                if ((rd16(file, o + 0x64) & 2) && spoiler != 0) continue; // a lamp of the stock spoiler, which a wing replaces
                lists[l]->push_back(r);
                lamp_colors_[l].push_back(r.rgb);
                lamp_powers_[l].push_back(float(rd32(file, o + 0x60)));
                // A lamp that flashes (+0x66: its pattern 1 or 2, +0x68: its phase in seconds; the lit list's, RaceCarModel::vf36).
                if (l == 0) lamp_flash_.push_back({int(rd16(file, o + 0x66)), f(o + 0x68)});
            }
        }
    }
}

void CarVisual::build_driver(ByteSpan file, const Options& opt, const std::string& name) {
    const car::CarFile c = car::CarFile::parse(file);
    if (!opt.driver) return;
    const ByteSpan set = c.driver ? file.subspan(c.driver) : opt.driver_set;
    if (set.empty() || !mdls::is_mdls(set)) return;
    try {
        const CockpitBytes cockpit = read_cockpit_bytes(file);
        const posture::FigureClass cls = posture::figure_class(cockpit.kind);
        if (cls == posture::FigureClass::None) return;
        for (int seat = 0; seat < (cockpit.co_driver ? 2 : 1); ++seat) cockpits_.push_back(posture::read_posture_cockpit(file, seat == 1));
        driver_models_ = std::make_unique<DriverModels>(*ctx_, set, name, t_unuploaded ? &unuploaded_ : nullptr);
        moving_driver_ = cls != posture::FigureClass::Board;
        if (moving_driver_) return;
        for (const posture::Cockpit& k : cockpits_) {
            still_.push_back(std::make_unique<DriverFigure>(*driver_models_));
            still_.back()->pose(posture::still_posture(k));
        }
    } catch (const std::exception& e) {
        LOG_WARN("Car %s: driver: %s", name.c_str(), e.what());
        still_.clear();
        driver_models_.reset();
        moving_driver_ = false;
    }
}

bool CarVisual::driver_shows(const CarState& s, const render::Vec3& eye, const LodView* view) const {
    const render::Vec3 at = v3(s.position) - eye;
    const float depth = std::max(view && view->k > 0 ? render::dot(at, view->forward) : render::length(at), 0.0001f);
    const float k = view && view->k > 0 ? view->k : 721.2f; // (the chase view's)
    return k / depth / 420.0f >= 0.025f;
}

namespace {
// The flashing patterns (0x391778 fills them once into 0x844348): a period, flashes in it, the time from one to the next, a
// flash's length (seconds). Pattern 1 one flash of 0.25 s every 0.4 s; pattern 2 five of 0.05 s, 0.05 s apart, every 0.9 s.
struct FlashPattern {
    float period;
    int count;
    float interval, length;
};
constexpr FlashPattern kFlashPatterns[2] = {{0.4f, 1, 0.5f, 0.25f}, {0.9f, 5, 0.05f, 0.05f}};
// A pattern's strength at its time `t` and a lamp's `phase` (0x3916E0): wrapped once by the period, 0 after the pattern's
// flashes or between them, inside a flash 1 - x^2 for x from -1 to 1 over its length.
float flash_strength(const FlashPattern& p, float t, float phase) {
    t += phase;
    if (p.period < t) t -= p.period;
    const int k = int(t / p.interval);
    if (k >= p.count) return 0;
    t -= float(k) * p.interval;
    if (p.length < t) return 0;
    const float x = t / (p.length * 0.5f) - 1;
    return 1 - x * x;
}
} // namespace

void CarVisual::set_lamp_strengths(float head, float brake, float tail) {
    // The glow's color is the course's lamp color times the lamp's own (record +0x54) times the list's strength (RaceCarModel::vf36
    // 0x3911D0: Env0 +0x1794 the lights, +0x1798 the brake lamps, +0x179C the tail lamps).
    std::vector<gt3r::race::CarLampRecord>* const lists[3] = {&lamps_.head, &lamps_.tail, &lamps_.brake};
    const float k[3] = {head, tail, brake};
    for (size_t l = 0; l < 3; ++l)
        for (size_t i = 0; i < lists[l]->size() && i < lamp_colors_[l].size(); ++i)
            for (int c = 0; c < 3; ++c) (*lists[l])[i].rgb[size_t(c)] = lamp_colors_[l][i][size_t(c)] * k[l];
}

void CarVisual::set_lamp_type_factor(float factor) {
    // The directional factor's power: the record's (+0x60) x the course's (Env0 +0x17A0, 0 read as 1: 0x397C38), rounded as the EE
    // does (+ 0.5, toward zero), at most 32; a negative one is no directional factor (0x3B8460).
    std::vector<gt3r::race::CarLampRecord>* const lists[3] = {&lamps_.head, &lamps_.tail, &lamps_.brake};
    for (size_t l = 0; l < 3; ++l)
        for (size_t i = 0; i < lists[l]->size() && i < lamp_powers_[l].size(); ++i) (*lists[l])[i].exponent = lamp_power(lamp_powers_[l][i], factor);
}

int lamp_power(float power, float factor) {
    if (factor == 0) factor = 1;
    const float e = power * factor;
    return e < 0 ? 0 : std::min(int(e + 0.5f), 32);
}

CarVisual::~CarVisual() = default;

std::unique_ptr<CarVisual> CarVisual::make_unuploaded(render::GpuContext& ctx, ByteSpan file, ByteSpan pat, const CarSpec& spec, const Options& opt,
                                                      const std::string& name) {
    struct Flag {
        Flag() { t_unuploaded = true; }
        ~Flag() { t_unuploaded = false; }
    } flag;
    return std::make_unique<CarVisual>(ctx, file, pat, spec, opt, name);
}

void CarVisual::to_gpu(std::unique_ptr<render::GpuModel>& slot, scene::Model m, bool prepared) {
    if (!prepared) {
        weigh_reflections(m, reflection_by_draw_, reflection_c0_, reflection_c1_);
        share_textures(m);
    }
    if (t_unuploaded) unuploaded_.emplace_back(&slot, std::move(m));
    else slot = std::make_unique<render::GpuModel>(*ctx_, m);
}

void CarVisual::restart_animation() { flash_time_ = {}; }

void CarVisual::animate(float seconds) {
    // The flashing lamps' patterns (RaceCarModel::vf3 0x390DD8: each pattern's time a frame on, wrapped by its period), and
    // their lamps' strengths (vf36: the pattern's, in place of the course's for the lit list).
    if (lamp_flash_.empty()) return;
    for (size_t k = 0; k < 2; ++k) {
        flash_time_[k] += seconds;
        if (kFlashPatterns[k].period < flash_time_[k]) flash_time_[k] -= kFlashPatterns[k].period;
    }
    for (size_t i = 0; i < lamp_flash_.size() && i < lamps_.head.size() && i < lamp_colors_[0].size(); ++i) {
        const auto [pattern, phase] = lamp_flash_[i];
        if (pattern < 1 || pattern > 2) continue;
        const float k = flash_strength(kFlashPatterns[pattern - 1], flash_time_[size_t(pattern - 1)], phase);
        for (int c = 0; c < 3; ++c) lamps_.head[i].rgb[size_t(c)] = lamp_colors_[0][i][size_t(c)] * k;
    }
}

std::unique_ptr<CarVisual::Motion> CarVisual::make_motion() const {
    if (!moving_) return nullptr;
    auto m = std::make_unique<Motion>();
    restart_motion(*m);
    return m;
}

void CarVisual::restart_motion(Motion& m) const {
    if (!moving_) return;
    m.state = mdls::ScriptState(moving_->script);
    m.time_ms = 0;
    for (const mdls::Script::Entries& e : moving_->script.models)
        if (e.setup >= 0) mdls::run_script(moving_->script, e.setup, m.state);
    m.turns = moving_->still_turns;
    m.shown.assign(moving_->weights.size(), 0.0f);
    for (auto& band : m.band) band.reset();
}

// Each turned part's matrix in the model's frame from the registers: the shapes as decoded stand where the program's matrix
// put them without the turn; taken back into the frame the turn is made in (its matrix there), turned, and put back.
std::vector<render::Mat4> CarVisual::part_matrices(const mdls::ScriptState& st) const {
    std::vector<render::Mat4> out;
    if (!moving_) return out;
    const auto f = [&](u32 cell) { return cell < st.registers.size() ? st.register_f(cell) : 0.0f; };
    for (const Moving::Part& part : moving_->parts) {
        const u32 c = part.cell;
        std::array<float, 16> r{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        switch (part.op) {
        case 0x48: r = mdls::register_turn(f(c), f(c + 1), f(c + 2), f(c + 3)); break;
        case 0x49: r = mdls::register_turn(f(c), 1, 0, 0); break;
        case 0x4A: r = mdls::register_turn(f(c), 0, 1, 0); break;
        case 0x4B: r = mdls::register_turn(f(c), 0, 0, 1); break;
        case 0x45: r[12] = f(c), r[13] = f(c + 1), r[14] = f(c + 2); break;
        case 0x46: r[0] = f(c), r[5] = f(c + 1), r[10] = f(c + 2); break;
        case 0x44:
            for (u32 k = 0; k < 16; ++k) r[k] = f(c + k);
            break;
        default: break;
        }
        // (Row vectors: v' = v P^-1 R P; a row-vector matrix is the column-vector one in Mat4's column-major order.)
        out.push_back(Mat4::from(row_mul(row_mul(part.pivot_inverse, r), part.pivot)));
    }
    return out;
}

void CarVisual::step_motion(Motion& m, float seconds, const MotionInput& in) const {
    if (!moving_) return;
    const Moving& mv = *moving_;
    // The model's time in milliseconds, trunc(1000 x the step) a step (0x451110); the game's values; the per-step and draw
    // scripts.
    m.time_ms += u32(std::max(0.0f, seconds * 1000.0f));
    m.state.set_host(mv.time_cell, m.time_ms);
    m.state.set_host(mv.timezone_cell, u32(lamp_level_));
    m.state.set_host_f(mv.rpm_cell, in.engine_rpm);
    m.state.set_host_f(mv.brake_cell, in.brake);
    m.state.set_host_f(mv.taillamp_cell, in.brake_lamps ? 1.0f : 0.0f);
    m.state.set_host_f(mv.left_cell, in.pedal_left);
    m.state.set_host_f(mv.right_cell, in.pedal_right);
    for (const mdls::Script::Entries& e : mv.script.models)
        if (e.tick >= 0) mdls::run_script(mv.script, e.tick, m.state);
    for (const mdls::Script::Entries& e : mv.script.models)
        if (e.draw >= 0) mdls::run_script(mv.script, e.draw, m.state);
    if (!mv.parts.empty()) m.turns = part_matrices(m.state);
    if (mv.weights.empty()) return;
    // The blended shapes, in a small model of this car's own drawn with the band's pictures, when a weight changes.
    bool changed = false;
    for (size_t k = 0; k < mv.weights.size(); ++k) {
        const float w = std::clamp(m.state.register_f(mv.weights[k]), 0.0f, 1.0f);
        if (std::isfinite(w) && w != m.shown[k]) m.shown[k] = w, changed = true;
    }
    if (!changed) return;
    for (size_t b = 0; b < 2; ++b) {
        const Moving::Band& band = mv.band[b];
        if (band.meshes.empty() || !band.gpu) continue;
        if (!m.band[b]) m.band[b] = std::make_unique<render::GpuModel>(*ctx_, *band.gpu);
        std::vector<scene::Mesh> meshes = band.meshes;
        for (size_t k = 0; k < mv.weights.size(); ++k) {
            const float w = m.shown[k];
            if (w == 0) continue;
            for (size_t i = 0; i < meshes.size() && i < band.ends[k].size(); ++i)
                for (size_t v = 0; v < meshes[i].vertices.size() && v < band.ends[k][i].size(); ++v) {
                    scene::Vertex& x = meshes[i].vertices[v];
                    const scene::Vertex &from = band.meshes[i].vertices[v], &e = band.ends[k][i][v];
                    for (int j = 0; j < 3; ++j) x.pos[j] += (e.pos[j] - from.pos[j]) * w, x.normal[j] += (e.normal[j] - from.normal[j]) * w;
                    for (int j = 0; j < 2; ++j) x.uv[j] += (e.uv[j] - from.uv[j]) * w;
                }
        }
        m.band[b]->update_geometry(meshes);
    }
}

// The shapes of `m` that the blend weights move (their vertices differ in an end: the body at a weight's 1): their triangles
// taken out of the body into `band`, with each end's vertices beside them.
void CarVisual::split_band(scene::Model& m, const std::vector<scene::Model>& ends, Moving::Band& band) {
    band.ends.assign(ends.size(), {});
    for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
        scene::Mesh& mesh = m.meshes[mi];
        std::vector<bool> moved(mesh.vertices.size(), false);
        bool any = false;
        for (const scene::Model& other : ends) {
            if (mi >= other.meshes.size() || other.meshes[mi].vertices.size() != mesh.vertices.size()) continue;
            const scene::Mesh& end = other.meshes[mi];
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                const scene::Vertex &a = mesh.vertices[i], &b = end.vertices[i];
                for (int k = 0; k < 3; ++k) moved[i] = moved[i] || std::fabs(a.pos[k] - b.pos[k]) > 1e-6f;
                for (int k = 0; k < 2; ++k) moved[i] = moved[i] || std::fabs(a.uv[k] - b.uv[k]) > 1e-6f;
                any = any || moved[i];
            }
        }
        if (!any) continue;
        scene::Mesh part;
        part.name = mesh.name + "/band", part.lod = mesh.lod, part.node = mesh.node, part.transform = mesh.transform;
        std::vector<u32> taken; // the band's vertices: their numbers in the mesh
        std::map<u32, u32> renumber;
        const auto take = [&](u32 v) {
            const auto [at, fresh] = renumber.try_emplace(v, u32(part.vertices.size()));
            if (fresh) part.vertices.push_back(mesh.vertices[v]), taken.push_back(v);
            return at->second;
        };
        std::vector<u32> kept;
        for (scene::Primitive& p : mesh.primitives) {
            scene::Primitive q = p;
            q.first_index = u32(part.indices.size()), q.index_count = 0;
            const u32 first = u32(kept.size());
            for (u32 t = p.first_index; t + 2 < p.first_index + p.index_count; t += 3) {
                const u32 a = mesh.indices[t], b = mesh.indices[t + 1], c = mesh.indices[t + 2];
                if (moved[a] || moved[b] || moved[c]) {
                    part.indices.push_back(take(a)), part.indices.push_back(take(b)), part.indices.push_back(take(c));
                    q.index_count += 3;
                } else kept.push_back(a), kept.push_back(b), kept.push_back(c);
            }
            p.first_index = first, p.index_count = u32(kept.size()) - first;
            if (q.index_count) part.primitives.push_back(q);
        }
        mesh.indices = std::move(kept);
        std::erase_if(mesh.primitives, [](const scene::Primitive& p) { return p.index_count == 0; });
        for (size_t k = 0; k < ends.size(); ++k) {
            std::vector<scene::Vertex> e;
            const bool same = mi < ends[k].meshes.size() && ends[k].meshes[mi].vertices.size() == mesh.vertices.size();
            for (u32 v : taken) e.push_back(same ? ends[k].meshes[mi].vertices[v] : mesh.vertices[v]);
            band.ends[k].push_back(std::move(e));
        }
        band.meshes.push_back(std::move(part));
    }
}

void CarVisual::upload() {
    while (upload_one()) {
    }
}

bool CarVisual::upload_one() {
    if (unuploaded_.empty()) return false;
    auto& [slot, m] = unuploaded_.front();
    *slot = std::make_unique<render::GpuModel>(*ctx_, m);
    unuploaded_.erase(unuploaded_.begin());
    return !unuploaded_.empty();
}

void CarVisual::set_shadow(const Shadow& s) {
    shadow_look_ = s;
    const u32 rgb = u32(std::clamp(s.rgb[0], 0.0f, 1.0f) * 255) | u32(std::clamp(s.rgb[1], 0.0f, 1.0f) * 255) << 8 | u32(std::clamp(s.rgb[2], 0.0f, 1.0f) * 255) << 16;
    for (size_t k = 0; k < 3; ++k) {
        shadow_[k].reset();
        if (!s.on || shadow_src_[k].meshes.empty()) continue;
        if (k == 2) {
            // The headlamps' light on the road: its own colors, added, at the course's strength.
            if (s.headlamps <= 0) continue;
            scene::Model m = shadow_src_[k];
            for (scene::Mesh& mesh : m.meshes) {
                for (scene::Vertex& v : mesh.vertices) {
                    const u32 a = u32(std::clamp(float(v.color >> 24) * s.headlamps, 0.0f, 255.0f));
                    v.color = (v.color & 0x00FFFFFFu) | a << 24;
                }
                for (scene::Primitive& p : mesh.primitives) {
                    p.flags |= scene::kAdditive | scene::kNoDepthWrite | scene::kUnlit | scene::kDoubleSided;
                    p.flags &= ~u32(scene::kAlphaBlend | scene::kEnvironmentMap | scene::kAlphaTest);
                }
            }
            shadow_[k] = std::make_unique<render::GpuModel>(*ctx_, m);
            continue;
        }
        // The shape's own alpha is the shadow's strength; its color is the course's.
        scene::Model m = shadow_src_[k];
        const float factor = s.alpha * (k == 0 ? s.light_factor : 1.0f);
        for (scene::Mesh& mesh : m.meshes) {
            for (scene::Vertex& v : mesh.vertices) {
                const u32 a = u32(std::clamp(float(v.color >> 24) * factor, 0.0f, 255.0f));
                v.color = rgb | a << 24;
            }
            for (scene::Primitive& p : mesh.primitives) {
                p.flags |= scene::kAlphaBlend | scene::kNoDepthWrite | scene::kUnlit | scene::kDoubleSided;
                p.flags &= ~u32(scene::kAdditive | scene::kEnvironmentMap | scene::kAlphaTest);
            }
        }
        shadow_[k] = std::make_unique<render::GpuModel>(*ctx_, m);
    }
}

Mat4 CarVisual::body_matrix(const CarState& s) const { return race::body_matrix(s, spec_); }

int CarVisual::level(const CarState& s, const render::Vec3& eye, const LodView* view) const {
    const Mat4 body = body_matrix(s);
    if (!view || lod_size_ <= 0) return std::min(render::length(v3(s.position) - eye) < 30.0f ? 0 : 1, lods_ - 1);
    // The program's choice (0x07 with the car's callback, 0x4539A8): v = K x size / depth of the switch's
    // centre; v > T: 0, v > 0.33 T: 1, else 2, T = 200.
    const render::Vec3 c = body.transform_point(lod_centre_);
    const float depth = std::max(render::dot(c - eye, view->forward), 0.0001f);
    const float v = view->k * lod_size_ / depth;
    constexpr float kT = 200;
    const int level = v > kT ? 0 : v > kT * 0.33f ? 1 : 2;
    return std::min(level, lods_ - 1);
}

void CarVisual::add_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, float step, const LodView* view, bool wheels,
                          const Motion* motion) const {
    const Mat4 body = body_matrix(s);
    const int lod = level(s, eye, view);
    if (wing_) {
        render::DrawItem it{wing_.get(), body * render::translate(wing_place_) * render::scale({wing_scale_, wing_scale_, wing_scale_}), std::min(lod, wing_lods_ - 1)};
        it.far_whole = true;
        items.push_back(it);
    }
    // The driver (in the car's frame), before the body and its glass.
    if (!still_.empty() && driver_shows(s, eye, view))
        for (const std::unique_ptr<DriverFigure>& d : still_) d->add_items(items, body);
    if (const render::GpuModel* m = body_[s.brake_lamps ? 1 : 0].get()) {
        render::DrawItem it{m, body, lod};
        it.far_whole = true;
        items.push_back(it);
        // (The shapes the body's script moves: a pace car's light bar; the 1886 Benz's flywheel and belts.)
        if (moving_) {
            const size_t b = s.brake_lamps ? 1 : 0;
            const render::GpuModel* band = motion && motion->band[b] ? motion->band[b].get() : moving_->band[b].gpu.get();
            if (band && band->valid()) {
                render::DrawItem it{band, body, lod};
                it.far_whole = true;
                items.push_back(it);
            }
            const std::vector<Mat4>& turns = motion ? motion->turns : moving_->still_turns;
            for (size_t k = 0; k < moving_->parts.size() && k < turns.size(); ++k)
                if (const render::GpuModel* part = moving_->parts[k].gpu[b].get(); part && part->valid()) {
                    render::DrawItem it{part, body * turns[k], lod};
                    it.far_whole = true;
                    items.push_back(it);
                }
        }
    }
    for (int w = 0; w < 4; ++w) {
        // The brake parts in the wheel's frame; the tire and the wheel turned and scaled in it, with
        // their turning pictures when the wheel turns fast enough.
        if (lod >= kWheelLevels) break; // the far level draws its wheels itself
        if (gt3_file_) {
            bool parts = false;
            if (gt3_brakes_shown(s, w, eye, view, parts)) {
                if (parts && brakes_[size_t(w)]) {
                    render::DrawItem it{brakes_[size_t(w)].get(), wheel_frame(s, spec_, w), 0};
                    it.in_order = true;
                    items.push_back(it);
                }
                if (const float fix = gt3r::brake_glow_fix(s.wheels[w].brake_heat, glow_gain_); fix > 0 && glows_[size_t(w)]) {
                    render::DrawItem it{glows_[size_t(w)].get(), wheel_frame(s, spec_, w) * render::rotate_x(-s.wheels[w].spin), 0};
                    it.tint = fix, it.no_fog = true;
                    items.push_back(it);
                }
            }
        } else if (brakes_[size_t(w)]) items.push_back({brakes_[size_t(w)].get(), wheel_frame(s, spec_, w), lod});
        if (const float glow = brake_glow(s.wheels[w].brake_heat); !gt3_file_ && glow > 0 && glows_[size_t(w)]) {
            render::DrawItem it{glows_[size_t(w)].get(), wheel_frame(s, spec_, w), lod};
            it.alpha = glow;
            items.push_back(it);
        }
        if (!wheels) continue;
        const bool turning = wheel_turning(s.wheels[w], step);
        const render::GpuModel* wheel = wheels_[size_t(w)][turning ? 1 : 0] ? wheels_[size_t(w)][turning ? 1 : 0].get() : wheels_[size_t(w)][0].get();
        if (wheel) items.push_back({wheel, wheel_matrix(s, spec_, w), lod});
    }
}

void CarVisual::add_loose_wheel(std::vector<render::DrawItem>& items, int wheel, const Mat4& hub) const {
    const size_t w = size_t(wheel) & 3;
    if (!wheels_[w][0]) return;
    const float r = spec_.rim_radius[w / 2];
    items.push_back({wheels_[w][0].get(), hub * render::translate({-0.5f * spec_.tire_width[w / 2], 0, 0}) * render::scale({r, r, r}), 0});
}

bool CarVisual::road_mirror(const CarState& s, Mat4& out) const {
    render::Vec3 c[4], p{0, 0, 0};
    for (int w = 0; w < 4; ++w) {
        c[w] = v3(s.wheels[w].center) - v3(s.up) * spec_.tire_radius[w / 2];
        p = p + c[w] * 0.25f;
    }
    const render::Vec3 ahead = (c[0] + c[1]) - (c[2] + c[3]), side = (c[1] + c[3]) - (c[0] + c[2]);
    if (!(render::length(ahead) > 0.5f) || !(render::length(side) > 0.5f)) return false;
    const render::Vec3 n = render::normalize(render::cross(side, ahead));
    if (!(n.y > 0.3f)) return false;
    const float pn = render::dot(p, n), N[3] = {n.x, n.y, n.z};
    out = Mat4{};
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) out.at(r, k) = (r == k ? 1.0f : 0.0f) - 2.0f * N[r] * N[k];
        out.at(r, 3) = 2.0f * pn * N[r];
    }
    return true;
}

bool CarVisual::add_mirror_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, const Motion* motion) const {
    Mat4 m;
    if (!road_mirror(s, m)) return false;
    const size_t first = items.size();
    add_items(items, s, eye, 1.0f / 60, nullptr, true, motion);
    for (size_t i = first; i < items.size(); ++i) items[i].transform = m * items[i].transform;
    return true;
}

void CarVisual::add_shadow_items(std::vector<render::DrawItem>& items, const CarState& s, const CarLight* light) const {
    if (!shadow_[0] && !shadow_[1] && !shadow_[2]) return;
    // On the road under the car: the plane through the tires' contacts, lifted 2 cm.
    render::Vec3 contact{0, 0, 0};
    render::Vec3 c[4];
    for (int w = 0; w < 4; ++w) {
        c[w] = v3(s.wheels[w].center) - v3(s.up) * spec_.tire_radius[w / 2];
        contact = contact + c[w] * 0.25f;
    }
    const render::Vec3 ahead = render::normalize((c[0] + c[1]) - (c[2] + c[3]));
    render::Vec3 side = render::normalize((c[1] + c[3]) - (c[0] + c[2]));
    const render::Vec3 normal = render::normalize(render::cross(side, ahead));
    if (!(render::length(ahead) > 0.5f) || !(render::length(side) > 0.5f) || !(normal.y > 0.2f)) return;
    side = render::cross(ahead, normal);
    const render::Vec3 back = ahead * -1.0f;
    const Mat4 ground = basis(side, normal, back, contact + normal * 0.02f);
    if (shadow_[0]) {
        // The body's shape flattened onto the road along the light: a point y above the road
        // lands `shear` x y away. (The model's y = 0 is the ride height above the road.)
        const float q = gt3_file_ ? 0.0f : (spec_.ride_height[0] + spec_.ride_height[1]) * 0.5f;
        auto cast = [&](const render::Vec3& shear, float alpha) {
            if (alpha < 0.01f) return;
            const float sx = render::dot(shear, side), sz = render::dot(shear, back);
            Mat4 flat;
            flat.m = {1, 0, 0, 0, sx, 0, sz, 0, 0, 0, 1, 0, sx * q, 0, sz * q, 1};
            render::DrawItem it{shadow_[0].get(), ground * flat, 0};
            it.alpha = std::min(alpha, 1.0f);
            items.push_back(it);
        };
        if (light && light->lamps) {
            // The lamps' shadows, one away from each.
            for (const CarLight::Cast& k : light->casts) cast(k.shear, shadow_look_.alpha * shadow_look_.light_factor > 0 ? k.alpha / (shadow_look_.alpha * shadow_look_.light_factor) : k.alpha);
        } else cast(shadow_look_.shear, light ? light->shadow : 1.0f);
    }
    if (shadow_[1]) items.push_back({shadow_[1].get(), ground, 0});
    if (shadow_[2]) items.push_back({shadow_[2].get(), ground, 0});
}

} // namespace gt4r::race
