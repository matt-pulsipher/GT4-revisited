#include "gt4r/race/car_visual.h"

#include "base/log.h"
#include "formats/course/course_model.h"
#include "gt4r/race/car_pose.h"
#include "gt4r/race/driver_figure.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

namespace scene = gt3r::scene;
using render::Mat4;

constexpr int kSkip = 99;       // a callback value no switch has a case for: the construct is left out
constexpr int kWheelLevels = 2; // the body's levels whose wheels are items of their own (the far level draws its own)

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
    return o;
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
            if (!m.meshes.empty()) to_gpu(wing_, m);
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
    for (int brake = 0; brake < 2; ++brake) {
        scene::Model m = build_levels(body, 0, body_scene(brake != 0, opt.lamp_level, spoiler), kLevels, 0, name);
        if (!far_body.empty()) {
            mdls::Scene sc = body_scene(brake != 0, opt.lamp_level, spoiler);
            sc.outside_sets[mdls::kWheelFaceSlot] = far_set;
            gt3r::gtm1::Options o = car_options();
            o.node = 0;
            o.run.lod = 2;
            scene::Model far = mdls::build_model(far_body, o, sc, name);
            for (scene::Mesh& mesh : far.meshes) mesh.lod = 2;
            gt3r::course::append_model(m, std::move(far));
        }
        if (m.meshes.empty()) throw FormatError("The car's body has nothing to draw.");
        if (brake == 0) {
            m.compute_bounds();
            min_ = {m.bounds_min[0], m.bounds_min[1], m.bounds_min[2]};
            max_ = {m.bounds_max[0], m.bounds_max[1], m.bounds_max[2]};
            for (const scene::Mesh& mesh : m.meshes) lods_ = std::max(lods_, mesh.lod + 1);
        }
        to_gpu(body_[size_t(brake)], m);
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
        if (any) to_gpu(brakes_[w], m);
        if (any_glow) to_gpu(glows_[w], glow);
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
                if (any) to_gpu(wheels_[w][size_t(turning)], both);
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
                r.exponent = 1;
                r.fade = (rd16(file, o + 0x64) & 1) != 0;
                if ((rd16(file, o + 0x64) & 2) && spoiler != 0) continue; // a lamp of the stock spoiler, which a wing replaces
                lists[l]->push_back(r);
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
        const CockpitPlaces places = read_cockpit(file);
        const std::vector<DriverPart> parts = driver_parts(set, opt.driver_skeleton);
        if (parts.empty()) return;
        const u32 models = mdls::read_header(set).models;
        std::vector<int> slot(models, -1);
        for (const DriverPart& p : parts)
            if (slot[p.model] < 0) slot[p.model] = int(driver_models_.size()), driver_models_.emplace_back();
        // (Reserved up front: the slots' places are kept for a later upload.)
        for (u32 i = 0; i < models; ++i) {
            if (slot[i] < 0) continue;
            gt3r::gtm1::Options o = car_options();
            o.node = int(i);
            scene::Model m = mdls::build_model(set, o, mdls::Scene{}, name + "/driver");
            bool any = false;
            for (const scene::Mesh& mesh : m.meshes) any |= !mesh.indices.empty();
            if (any) to_gpu(driver_models_[size_t(slot[i])], m);
        }
        for (int seat = 0; seat < (places.co_driver ? 2 : 1); ++seat) {
            const Mat4 root = driver_root(places, set, opt.driver_skeleton, seat == 1);
            for (const DriverPart& p : parts) driver_items_.push_back({size_t(slot[p.model]), p.skinned ? root : root * p.node});
        }
    } catch (const std::exception& e) {
        LOG_WARN("Car %s: driver: %s", name.c_str(), e.what());
        driver_items_.clear();
    }
}

void CarVisual::set_lamp_strengths(float head, float brake, float tail) {
    // The glow's color is the course's lamp color times the lamp's own: white ahead, red behind.
    for (gt3r::race::CarLampRecord& r : lamps_.head) r.rgb = {head, head, head};
    for (gt3r::race::CarLampRecord& r : lamps_.brake) r.rgb = {brake, brake * 0.12f, brake * 0.06f};
    for (gt3r::race::CarLampRecord& r : lamps_.tail) r.rgb = {tail, tail * 0.12f, tail * 0.06f};
}

CarVisual::~CarVisual() = default;

namespace {
thread_local bool t_unuploaded = false; // make_unuploaded is building
} // namespace

std::unique_ptr<CarVisual> CarVisual::make_unuploaded(render::GpuContext& ctx, ByteSpan file, ByteSpan pat, const CarSpec& spec, const Options& opt,
                                                      const std::string& name) {
    struct Flag {
        Flag() { t_unuploaded = true; }
        ~Flag() { t_unuploaded = false; }
    } flag;
    return std::make_unique<CarVisual>(ctx, file, pat, spec, opt, name);
}

void CarVisual::to_gpu(std::unique_ptr<render::GpuModel>& slot, const scene::Model& m) {
    if (t_unuploaded) unuploaded_.emplace_back(&slot, m);
    else slot = std::make_unique<render::GpuModel>(*ctx_, m);
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

void CarVisual::add_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, float step, const LodView* view) const {
    const Mat4 body = body_matrix(s);
    const int lod = level(s, eye, view);
    if (wing_) {
        render::DrawItem it{wing_.get(), body * render::translate(wing_place_) * render::scale({wing_scale_, wing_scale_, wing_scale_}), std::min(lod, wing_lods_ - 1)};
        it.far_whole = true;
        items.push_back(it);
    }
    // The driver (in the car's frame), before the body and its glass.
    if (lod < 2)
        for (const DriverItem& d : driver_items_)
            if (const render::GpuModel* m = driver_models_[d.model].get()) items.push_back({m, body * d.local, 0});
    if (const render::GpuModel* m = body_[s.brake_lamps ? 1 : 0].get()) {
        render::DrawItem it{m, body, lod};
        it.far_whole = true;
        items.push_back(it);
    }
    for (int w = 0; w < 4; ++w) {
        // The brake parts in the wheel's frame; the tire and the wheel turned and scaled in it, with
        // their turning pictures when the wheel turns fast enough.
        if (lod >= kWheelLevels) break; // the far level draws its wheels itself
        if (brakes_[size_t(w)]) items.push_back({brakes_[size_t(w)].get(), wheel_frame(s, spec_, w), lod});
        if (const float glow = brake_glow(s.wheels[w].brake_heat); glow > 0 && glows_[size_t(w)]) {
            render::DrawItem it{glows_[size_t(w)].get(), wheel_frame(s, spec_, w), lod};
            it.alpha = glow;
            items.push_back(it);
        }
        const bool turning = wheel_turning(s.wheels[w], step);
        const render::GpuModel* wheel = wheels_[size_t(w)][turning ? 1 : 0] ? wheels_[size_t(w)][turning ? 1 : 0].get() : wheels_[size_t(w)][0].get();
        if (wheel) items.push_back({wheel, wheel_matrix(s, spec_, w), lod});
    }
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

bool CarVisual::add_mirror_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye) const {
    Mat4 m;
    if (!road_mirror(s, m)) return false;
    const size_t first = items.size();
    add_items(items, s, eye);
    for (size_t i = first; i < items.size(); ++i) items[i].transform = m * items[i].transform;
    return true;
}

void CarVisual::add_shadow_items(std::vector<render::DrawItem>& items, const CarState& s, const CarLights::Light* light) const {
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
            for (const CarLights::Cast& k : light->casts) cast(k.shear, shadow_look_.alpha * shadow_look_.light_factor > 0 ? k.alpha / (shadow_look_.alpha * shadow_look_.light_factor) : k.alpha);
        } else cast(shadow_look_.shear, light ? light->shadow : 1.0f);
    }
    if (shadow_[1]) items.push_back({shadow_[1].get(), ground, 0});
    if (shadow_[2]) items.push_back({shadow_[2].get(), ground, 0});
}

} // namespace gt4r::race
