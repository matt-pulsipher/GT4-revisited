#include "gt4r/race/car_visual.h"

#include "base/log.h"
#include "formats/course/course_model.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

namespace scene = gt3r::scene;
using render::Mat4;

constexpr float kPi = 3.14159265f;
constexpr int kSkip = 99; // a callback value no switch has a case for: the construct is left out

render::Vec3 v3(const course::Vec3& v) { return {v.x, v.y, v.z}; }

float rdf(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

// The callback values of a car's body for the race view's draws: the wheels' constructs are left
// out (the wheels are items of their own), the spoiler is as built.
mdls::Scene body_scene(bool brake, int lamp_level) {
    mdls::Scene s;
    s.resolve_callbacks = true;
    s.callback_default = 0;
    s.callback_values[0] = brake ? 1 : 0;
    s.callback_values[7] = lamp_level;
    for (u16 id = 11; id <= 18; ++id) s.callback_values[id] = kSkip;
    for (u16 id = 20; id <= 39; ++id) s.callback_values[id] = kSkip;
    s.callback_values[47] = 0;
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

Mat4 basis(const render::Vec3& right, const render::Vec3& up, const render::Vec3& back, const render::Vec3& origin) {
    Mat4 m;
    m.m = {right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, back.x, back.y, back.z, 0, origin.x, origin.y, origin.z, 1};
    return m;
}

} // namespace

CarVisual::CarVisual(render::GpuContext& ctx, ByteSpan file, ByteSpan pat, const CarSpec& spec, const Options& opt, const std::string& name)
    : spec_(spec), ctx_(&ctx) {
    const car::CarFile c = car::CarFile::parse(file);
    if (c.body_paint && c.body_paint < file.size()) pat = file.subspan(c.body_paint);
    Bytes painted;
    ByteSpan body = file.subspan(c.body);
    if (car::paint_count(pat)) {
        painted = car::apply_paint(body, pat, std::min(opt.paint, car::paint_count(pat) - 1));
        body = painted;
    }
    const u32 body_models = mdls::read_header(body).models;

    // The body with its lamps unlit and lit. The far level draws its wheels as shapes of its own
    // with a picture of the wheel set, which this reader does not bind: the two nearer levels.
    constexpr int kLevels = 2;
    for (int brake = 0; brake < 2; ++brake) {
        scene::Model m = build_levels(body, 0, body_scene(brake != 0, opt.lamp_level), kLevels, 0, name);
        if (m.meshes.empty()) throw FormatError("The car's body has nothing to draw.");
        if (brake == 0) {
            m.compute_bounds();
            min_ = {m.bounds_min[0], m.bounds_min[1], m.bounds_min[2]};
            max_ = {m.bounds_max[0], m.bounds_max[1], m.bounds_max[2]};
            for (const scene::Mesh& mesh : m.meshes) lods_ = std::max(lods_, mesh.lod + 1);
        }
        body_[size_t(brake)] = std::make_unique<render::GpuModel>(ctx, m);
    }
    // The brake parts: the shapes the body's program draws in each wheel's frame.
    for (u16 w = 0; w < 4; ++w) {
        mdls::Scene sc = body_scene(false, opt.lamp_level);
        sc.callback_values[u16(11 + w)] = 0;
        sc.only_inside = 11 + w;
        scene::Model m = build_levels(body, 0, sc, 1, 0, name + "/brake");
        // The disc's glow (an additive shape whose strength is the brake's heat) is left out: cold brakes.
        bool any = false;
        for (scene::Mesh& mesh : m.meshes) {
            std::erase_if(mesh.primitives, [](const scene::Primitive& p) { return (p.flags & scene::kAdditive) != 0; });
            any |= !mesh.primitives.empty();
        }
        if (any) brakes_[w] = std::make_unique<render::GpuModel>(ctx, m);
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
                if (any) wheels_[w][size_t(turning)] = std::make_unique<render::GpuModel>(ctx, both);
            }
        }
    }
    // The shadow under the car: model 2, a flat picture at y = 0.
    if (body_models > 2) {
        mdls::Scene sc = body_scene(false, 0);
        gt3r::gtm1::Options o;
        o.node = 2;
        shadow_src_ = mdls::build_model(body, o, sc, name + "/shadow");
    }
    // The camera places (info +0x40: count, offset from the file's start; 0x20 bytes each).
    if (c.info && c.info + 0x48 <= file.size()) {
        const u32 n = rd32(file, c.info + 0x40), at = rd32(file, c.info + 0x44);
        for (u32 i = 0; i < n && i < 64 && at + 0x20 * size_t(i) + 0x20 <= file.size(); ++i) {
            const size_t o = at + 0x20 * size_t(i);
            CameraMount m;
            // Stored with y down and z forward: turned into the model's frame.
            m.pos = {rdf(file, o), -rdf(file, o + 4), -rdf(file, o + 8)};
            m.pitch = rdf(file, o + 12), m.yaw = rdf(file, o + 16), m.roll = rdf(file, o + 20);
            m.zoom = rdf(file, o + 24), m.flags = rd32(file, o + 28);
            mounts_.push_back(m);
        }
    }
}

CarVisual::~CarVisual() = default;

void CarVisual::set_shadow(const Shadow& s) {
    shadow_look_ = s;
    shadow_.reset();
    if (!s.on || shadow_src_.meshes.empty()) return;
    // The picture's own alpha shapes the shadow; its color is the course's.
    scene::Model m = shadow_src_;
    const u32 rgb = u32(std::clamp(s.rgb[0], 0.0f, 1.0f) * 255) | u32(std::clamp(s.rgb[1], 0.0f, 1.0f) * 255) << 8 | u32(std::clamp(s.rgb[2], 0.0f, 1.0f) * 255) << 16;
    for (scene::Mesh& mesh : m.meshes) {
        for (scene::Vertex& v : mesh.vertices) {
            const u32 a = u32(std::clamp(float(v.color >> 24) * s.alpha, 0.0f, 255.0f));
            v.color = rgb | a << 24;
        }
        for (scene::Primitive& p : mesh.primitives) {
            p.flags |= scene::kAlphaBlend | scene::kNoDepthWrite | scene::kUnlit | scene::kDoubleSided;
            p.flags &= ~u32(scene::kAdditive | scene::kEnvironmentMap | scene::kAlphaTest);
        }
    }
    shadow_ = std::make_unique<render::GpuModel>(*ctx_, m);
}

Mat4 CarVisual::body_matrix(const CarState& s) const {
    const render::Vec3 right = v3(s.right), up = v3(s.up), forward = v3(s.forward), pos = v3(s.position);
    // The model's origin is at the body's underside, the ride height above the road, with a hub
    // at y = R - q + its travel. Whichever point of the car the state's position is, the wheels
    // say how far along the body's up axis the origin lies from it.
    float lift = 0;
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        lift += render::dot(v3(s.wheels[w].center) - pos, up) - (spec_.tire_radius[axle] - spec_.ride_height[axle] + s.wheels[w].travel);
    }
    return basis(right, up, forward * -1.0f, pos + up * (lift * 0.25f));
}

void CarVisual::add_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, float step) const {
    const Mat4 body = body_matrix(s);
    const render::Vec3 pos = v3(s.position);
    const float dist = render::length(pos - eye);
    const int lod = std::min(dist < 30.0f ? 0 : 1, lods_ - 1);
    if (const render::GpuModel* m = body_[s.brake_lamps ? 1 : 0].get()) {
        render::DrawItem it{m, body, lod};
        it.far_whole = true;
        items.push_back(it);
    }
    const render::Vec3 right = v3(s.right), up = v3(s.up), forward = v3(s.forward);
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        const bool right_side = (w & 1) != 0;
        const WheelState& ws = s.wheels[w];
        // The wheel's frame: at the hub, steered about the body's up axis, moved out to the
        // tire's outer face; then turned about the axle and scaled by the rim's radius, the
        // right side's turned half round.
        const Mat4 hub = basis(right, up, forward * -1.0f, v3(ws.center)) * render::rotate_y(-ws.steer) *
                         render::translate({(right_side ? 0.5f : -0.5f) * spec_.tire_width[axle], 0, 0});
        if (lod == 0 && brakes_[size_t(w)]) items.push_back({brakes_[size_t(w)].get(), hub, 0});
        // The turning pictures show when the wheel turns more than 5 degrees in a step.
        const bool turning = std::fabs(ws.spin_rate * step) * (180.0f / kPi) > 5.0f;
        const render::GpuModel* wheel = wheels_[size_t(w)][turning ? 1 : 0] ? wheels_[size_t(w)][turning ? 1 : 0].get() : wheels_[size_t(w)][0].get();
        if (!wheel) continue;
        const float r = spec_.rim_radius[axle];
        Mat4 m = hub * render::rotate_x(-ws.spin) * render::scale({r, r, r});
        if (right_side) m = m * render::rotate_y(kPi);
        items.push_back({wheel, m, lod});
    }
}

void CarVisual::add_shadow_items(std::vector<render::DrawItem>& items, const CarState& s) const {
    if (!shadow_) return;
    // On the road under the car: the plane through the tires' contacts, lifted 2 cm.
    const render::Vec3 right = v3(s.right), forward = v3(s.forward);
    render::Vec3 contact{0, 0, 0};
    render::Vec3 c[4];
    for (int w = 0; w < 4; ++w) {
        c[w] = v3(s.wheels[w].center) - v3(s.up) * spec_.tire_radius[w / 2];
        contact = contact + c[w] * 0.25f;
    }
    const render::Vec3 ahead = render::normalize((c[0] + c[1]) - (c[2] + c[3]));
    render::Vec3 side = render::normalize((c[1] + c[3]) - (c[0] + c[2]));
    render::Vec3 normal = render::normalize(render::cross(side, ahead));
    if (!(render::length(ahead) > 0.5f) || !(render::length(side) > 0.5f) || !(normal.y > 0.2f)) return;
    side = render::cross(ahead, normal);
    (void)right, (void)forward;
    items.push_back({shadow_.get(), basis(side, normal, ahead * -1.0f, contact + normal * 0.02f), 0});
}

} // namespace gt4r::race
