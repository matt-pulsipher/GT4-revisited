#include "gt4r/race/course_scene.h"
#include "gt4r/race/gadget_bodies.h"

#include "gt4r/formats/course/runway_gtrw.h"

#include "base/log.h"
#include "formats/model/coplanar_overlays.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cstring>

namespace gt4r::race {

namespace scene = gt3r::scene;

namespace {
float rd_float(ByteSpan d, size_t o) {
    const u32 v = gt3r::rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
} // namespace

std::unique_ptr<render::GpuModel> CourseScene::build(render::GpuContext& ctx, int slot, const std::string& label, bool keep,
                                                     bool sheen, scene::Model* out, AnimatedSet* animated) {
    const ByteSpan s = file_.section(data_, slot);
    const bool gtm1 = gt3r::has_magic(s, "GTM1"); // (Prologue's courses in Gran Turismo 3's table)
    if (!mdls::is_mdls(s) && !gtm1) return nullptr;
    // Scenery as the shared engine decodes its own: the programs' render state kept, cut-out
    // textures alpha tested, each packet's own texture for its environment pass, and the second
    // pass over the road and the fences (the sheen) weighted by the frame's alpha.
    mdls::Scene sc;
    sc.color_scale = color_scale_;
    const Bytes block = gtm1 ? Bytes(s.begin(), s.end()) : mdls::to_gtm1(s, nullptr, sc);
    if (gtm1) animated = nullptr;
    const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
    gt3r::gtm1::Options o;
    o.reflection = sheen;
    o.road_sheen = sheen;
    o.frame_alpha = sheen;
    o.course_env_maps = true;
    o.alpha_test = true;
    o.gs_state = true;
    scene::Model m = gt3r::gtm1::build_model(g, o, label);
    if (!gtm1) {
        // The reflecting shapes' strength: the course's (Env0 +0x34, and by the angle +0x34 x +0x30); no course's
        // program sets its own.
        const ByteSpan env = file_.section(data_, course::kSlotEnv);
        if (env.size() >= 0x38) {
            const float k[2] = {rd_float(env, 0x30), rd_float(env, 0x34)};
            for (scene::Mesh& mesh : m.meshes)
                for (scene::Primitive& p : mesh.primitives)
                    if (p.env_kind)
                        for (int i = 0; i < 3; ++i) p.env_c0[i] = k[1], p.env_c1[i] = k[1] * k[0];
        }
    }
    for (scene::Mesh& mesh : m.meshes) gt3r::gtm1::mark_coplanar_overlays(mesh);
    if (animated) {
        // The set's animated models are drawn apart, posed each frame; a set whose scripts do not
        // read stays whole and still.
        try {
            animated->models = AnimatedModels(s, color_scale_, o);
        } catch (const std::exception& e) {
            LOG_WARN("Course %s: its animated models stay still (%s)", label.c_str(), e.what());
            animated->models = {};
        }
        const std::vector<u32> moving = animated->models.models();
        std::erase_if(m.meshes, [&](const scene::Mesh& mesh) { return mesh.node >= 0 && std::binary_search(moving.begin(), moving.end(), u32(mesh.node)); });
        if (!animated->models.empty()) {
            scene::Model holder = animated->models.textures();
            holder.name = label + "/animated";
            animated->textures = std::make_unique<render::GpuModel>(ctx, holder);
            animated->gpu = std::make_unique<render::GpuModel>(ctx, *animated->textures);
            animated->gpu->keep_geometry();
        }
    }
    std::unique_ptr<render::GpuModel> gpu;
    if (!m.meshes.empty()) gpu = std::make_unique<render::GpuModel>(ctx, m, keep);
    if (out) *out = std::move(m);
    return gpu;
}

CourseScene::CourseScene(render::GpuContext& ctx, ByteSpan course_file, const std::string& name)
    : data_(course_file.begin(), course_file.end()), file_(course::CourseFile::parse(data_)) {
    const ByteSpan env = file_.section(data_, course::kSlotEnv);
    env_ = gt3r::race::SunParams::from_env0(env);
    color_scale_ = course::scenery_color_scale(env);
    if (std::optional<course::Runway> road = course::course_road(file_, data_)) road_ = std::move(*road); // (RNW4, or Prologue's GTRW)

    scene::Model scenery;
    scenery_ = build(ctx, course::kSlotWorld, name + "/scenery", true, true, &scenery, &animated_);
    if (!scenery_) throw FormatError("The course has no scenery.");
    stats_.models = scenery.meshes.size();
    stats_.textures = scenery.textures.size();
    {
        // What a car's sides reflect without a reflection picture: the scenery's mean color.
        double acc[3] = {}, n = 0;
        for (const scene::Mesh& m : scenery.meshes)
            for (const scene::Primitive& p : m.primitives) {
                stats_.triangles += p.index_count / 3;
                if (p.texture < 0 || p.index_count == 0) continue;
                const scene::Texture& t = scenery.textures[size_t(p.texture)];
                if (t.rgba.empty()) continue;
                const scene::Vertex& v = m.vertices[m.indices[p.first_index]];
                const u32 tc = t.rgba[t.rgba.size() / 2];
                const double vs = (p.flags & scene::kVertexColor2x) ? 1.0 / 128.0 : 1.0 / 255.0;
                for (int k = 0; k < 3; ++k)
                    acc[k] += double(p.index_count) * (double((tc >> (8 * k)) & 0xFF) / 255.0) * std::min(1.0, double((v.color >> (8 * k)) & 0xFF) * vs);
                n += double(p.index_count);
            }
        if (n > 0)
            for (int k = 0; k < 3; ++k) ground_[k] = float(acc[k] / n);
    }
    occluder_.add(scenery);
    scenery = {};

    scene::Model backdrop;
    backdrop_ = build(ctx, course::kSlotFar, name + "/backdrop", true, true, &backdrop);
    occluder_.add(backdrop);
    occluder_.build();

    scene::Model sky;
    sky_ = build(ctx, course::kSlotSky, name + "/sky", false, false, &sky, &animated_sky_);
    if (sky_ || animated_sky_.gpu) {
        auto mean_rows = [&](float out[3], bool lower) {
            double acc[3] = {}, n = 0;
            for (const scene::Texture& t : sky.textures)
                for (u32 y = lower ? t.height / 2 : 0; y < (lower ? t.height : t.height / 2); ++y)
                    for (u32 x = 0; x < t.width; ++x) {
                        const u32 c = t.rgba[size_t(y) * t.width + x];
                        for (int k = 0; k < 3; ++k) acc[k] += double((c >> (8 * k)) & 0xFF) / 255.0;
                        n += 1;
                    }
            if (n > 0)
                for (int k = 0; k < 3; ++k) out[k] = float(acc[k] / n);
        };
        mean_rows(horizon_, true);
        mean_rows(sky_rgb_, false);
    }
    mirror_sky_ = build(ctx, course::kSlotMirrorSky, name + "/mirror_sky", false, false);
    after_ = build(ctx, course::kSlotAfter, name + "/after", false, false);
    // The cones and boards: their kinds' shapes, drawn where the road file puts them.
    try {
        ByteSpan road = file_.section(data_, course::kSlotRunway);
        if (road.empty()) road = file_.section(data_, course::kSlotRoadGtrw); // (Prologue's)
        gadgets_ = Gadgets::parse(file_.section(data_, course::kSlotGadgets), road);
        if (!gadgets_.models.empty() && !gadgets_.placements.empty() && mdls::is_mdls(gadgets_.models)) {
            mdls::Scene sc;
            sc.color_scale = color_scale_;
            const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(mdls::to_gtm1(gadgets_.models, nullptr, sc));
            gt3r::gtm1::Options o;
            o.alpha_test = true;
            o.gs_state = true;
            scene::Model m = gt3r::gtm1::build_model(g, o, name + "/gadgets");
            std::string nodes;
            for (const scene::Mesh& mesh : m.meshes) nodes += " " + std::to_string(mesh.node);
            LOG_INFO("Course %s: %zu cones and boards of %zu kinds; shapes of models%s", name.c_str(), gadgets_.placements.size(), gadgets_.kinds.size(), nodes.c_str());
            if (!m.meshes.empty()) gadget_model_ = std::make_unique<render::GpuModel>(ctx, m);
            for (const Gadgets::Kind& k : gadgets_.kinds) gadget_nodes_.push_back({u16(2 * k.index)}), gadget_shadow_nodes_.push_back({u16(2 * k.index + 1)});
        }
    } catch (const std::exception& e) {
        LOG_WARN("Course %s: no cones and boards (%s)", name.c_str(), e.what());
        gadgets_ = {};
    }
    env_scenery_ = build(ctx, course::kSlotEnvironment, name + "/env", false, false);
    env_sky_ = build(ctx, course::kSlotEnvSky, name + "/env_sky", false, false);

    vis_[size_t(View::Race)] = gt3r::course::Visibility::parse(file_.section(data_, course::kSlotWorldVis));
    vis_[size_t(View::Mirror)] = gt3r::course::Visibility::parse(file_.section(data_, course::kSlotWorldVis + 1));
    env_vis_ = gt3r::course::Visibility::parse(file_.section(data_, course::kSlotEnvironment + 1));

    // The road reflection: the scenery stored mirrored below the road (slot 9) and the road as its
    // mask (slot 13), handed to the shared engine's pass as a course file of its own layout (48
    // offsets; the sets converted, the lists as they are).
    if (mdls::is_mdls(file_.section(data_, course::kSlotReflection))) {
        try {
            mdls::Scene sc;
            sc.color_scale = color_scale_;
            Bytes out(48 * 4, 0);
            auto put = [&](int slot, ByteSpan bytes) {
                if (bytes.empty()) return;
                while (out.size() & 15) out.push_back(0);
                const u32 at = u32(out.size());
                std::memcpy(out.data() + size_t(slot) * 4, &at, 4);
                out.insert(out.end(), bytes.begin(), bytes.end());
            };
            put(course::kSlotReflection, mdls::to_gtm1(file_.section(data_, course::kSlotReflection), nullptr, sc));
            put(course::kSlotReflection + 1, file_.section(data_, course::kSlotReflection + 1));
            if (mdls::is_mdls(file_.section(data_, course::kSlotReflectionMask))) {
                put(course::kSlotReflectionMask, mdls::to_gtm1(file_.section(data_, course::kSlotReflectionMask), nullptr, sc));
                put(course::kSlotReflectionMask + 1, file_.section(data_, course::kSlotReflectionMask + 1));
            }
            reflection_course_ = std::move(out);
            reflection_sky_ = build(ctx, course::kSlotSkyReflection, name + "/reflection_sky", false, false);
            if (env.size() >= 0xC20) {
                float v[4];
                std::memcpy(v, env.data() + 0xC10, 16);
                // (One strength for the three channels: the colors are near white.)
                const float gain = (v[0] + v[1] + v[2]) / 3 * v[3];
                reflection_gain_ = gain > 0 && gain <= 4 ? gain : 1.0f;
            }
        } catch (const std::exception& e) {
            LOG_WARN("Course %s: no road reflection (%s)", name.c_str(), e.what());
            reflection_course_.clear();
        }
    }

    // The lights of the course's own lit shapes: the environment's scene lights.
    if (env_.has_env) {
        env_.course_lights(lights_);
        // (A light color a hair below 0 (Togakushi Kogen's blue, -9e-7) is 0 in the program's whole numbers, rounded
        // toward 0; the shared renderer's, rounded down, would be 255.)
        for (auto& color : lights_.specular)
            for (float& c : color)
                if (c < 0 && c > -1e-3f) c = 0;
        has_lights_ = true;
    }
    LOG_INFO("Course %s: %zu scenery models, %zu triangles, %zu textures; far %.0f m, fog %.0f / %.2f, color scale %.2f", name.c_str(),
             stats_.models, stats_.triangles, stats_.textures, env_.far_clip, env_.fog_a, env_.fog_b, color_scale_);
}

CourseScene::~CourseScene() = default;

ByteSpan CourseScene::section(int slot) const { return file_.section(data_, slot); }

void CourseScene::restart_animations() { animated_.models.restart(), animated_sky_.models.restart(); }
void CourseScene::animate(float seconds, std::span<const CarState> cars) { animated_.models.tick(seconds, cars), animated_sky_.models.tick(seconds, cars); }
void CourseScene::upload_animations(std::span<const CarState> cars) {
    if (animated_.gpu) animated_.gpu->update_geometry(animated_.models.meshes(cars));
    if (animated_sky_.gpu) animated_sky_.gpu->update_geometry(animated_sky_.models.meshes(cars));
}

void CourseScene::add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, View view,
                            const gt3r::race::SceneryFade* fade) const {
    const render::ItemLights* lights = has_lights_ ? &lights_ : nullptr;
    const bool mirror = view == View::Mirror;
    // The sky is drawn round the camera with the view's translation taken off.
    const render::GpuModel* sky = mirror && mirror_sky_ ? mirror_sky_.get() : sky_.get();
    const render::GpuModel* planes = !(mirror && mirror_sky_) && animated_sky_.gpu && animated_sky_.gpu->valid() ? animated_sky_.gpu.get() : nullptr;
    for (const render::GpuModel* model : {sky, planes}) {
        if (!model) continue;
        // With its own depth (the dome's flat top lies behind its upper ring and must not cover
        // it), cleared after it; no fog.
        render::DrawItem it{model, render::translate(eye), 0};
        it.in_order = true;
        it.in_order_join = model == planes && sky;
        it.depth_reset = model == planes || !planes;
        it.no_fog = true;
        it.draw_distance = 0;
        it.lights = lights;
        items.push_back(it);
    }
    if (backdrop_ && !mirror) {
        render::DrawItem it{backdrop_.get(), {}, 0};
        it.draw_distance = 0;
        it.lights = lights;
        it.sphere_map = true;
        // Its depth is that of a point a hundred times nearer, so its far plane lies a hundred
        // times as far as the scenery's; the depth is cleared after it.
        it.modelview_scale = 0.01f;
        it.frame_alpha = true;
        it.guard_band = true;
        it.in_order = true;
        it.depth_reset = true;
        items.push_back(it);
    }
    const std::vector<u16>* nodes = fade ? fade->solid() : visible_nodes(eye, view);
    {
        render::DrawItem it{scenery_.get(), {}, 0};
        it.nodes = nodes;   // the list's models in its order
        it.in_order = true; // (in file order without a list)
        it.frame_alpha = true;
        it.lights = lights;
        it.sphere_map = true;
        it.guard_band = true;
        items.push_back(it);
        // The animated models with the scenery they were taken from.
        if (animated_.gpu && animated_.gpu->valid()) {
            it.model = animated_.gpu.get();
            it.in_order_join = true;
            items.push_back(it);
        }
    }
    // The cones and boards near the camera (lit by the course's lights, 🔎 scene light 0 in the
    // program), each kind's shape at its place and turn.
    if (gadget_model_ && !mirror)
        for (size_t g = 0; g < gadgets_.placements.size(); ++g) {
            const Gadgets::Placement& p = gadgets_.placements[g];
            if (p.kind < 0 || size_t(p.kind) >= gadget_nodes_.size()) continue;
            // Where the cars have left it, or at its place.
            const GadgetBodies::Body* body = gadget_bodies_ && g < gadget_bodies_->bodies().size() ? &gadget_bodies_->bodies()[g] : nullptr;
            if (body && body->removed) continue;
            const course::Vec3 pos = body ? body->pos : p.position;
            const float dx = pos.x - eye.x, dy = pos.y - eye.y, dz = pos.z - eye.z;
            if (dx * dx + dy * dy + dz * dz > 400.0f * 400.0f) continue;
            const std::array<float, 9> r = body ? body->rot : Gadgets::rotation(p.angles);
            render::Mat4 at;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) at.at(i, j) = r[size_t(i * 3 + j)];
            at.at(0, 3) = pos.x, at.at(1, 3) = pos.y, at.at(2, 3) = pos.z;
            // The light on the road under it, and the road's plane there (0x3E29F8).
            if (gadget_light_.size() != gadgets_.placements.size()) gadget_light_.assign(gadgets_.placements.size(), {});
            GadgetLight& l = gadget_light_[g];
            if (!l.known || l.at.x != pos.x || l.at.y != pos.y || l.at.z != pos.z) {
                l.known = true, l.at = pos;
                road_.light_at(pos.x, pos.z, pos.y + 1.0f, l.sun, l.sky);
                float y;
                int tri = -1;
                l.plane = {0, 1, 0, -pos.y};
                if (road_.height_at(pos.x, pos.z, pos.y + 1.0f, y, &tri) && tri >= 0) {
                    const auto v3 = [&](int k) { const auto& q = road_.vertices[road_.triangles[size_t(tri)].v[size_t(k)]].pos; return render::Vec3{q.x, q.y, q.z}; };
                    const render::Vec3 a = v3(0);
                    render::Vec3 n = render::normalize(render::cross(v3(1) - a, v3(2) - a));
                    if (n.y < 0) n = n * -1.0f;
                    if (n.y > 0.2f) l.plane = {n.x, n.y, n.z, -render::dot(n, a)};
                }
                // (A cone that stands above the road file's surface, Tsukuba's pit lane: on the level it stands on.)
                float low = 0;
                for (const Gadgets::Kind& k : gadgets_.kinds)
                    if (k.index == p.kind)
                        for (const course::Vec3& h : k.hull) low = std::min(low, h.y);
                if (l.plane[0] * pos.x + l.plane[1] * (pos.y + low) + l.plane[2] * pos.z + l.plane[3] > 0.3f) l.plane = {0, 1, 0, -(pos.y + low)};
            }
            if (env_.has_shadow && size_t(p.kind) < gadget_shadow_nodes_.size()) {
                // Its shadow: the kind's second model thrown onto the road's plane along the light (0x3E4968), as strong as the
                // light there makes it (0x3983E0).
                constexpr float kRad = 3.14159265f / 180;
                const float el = -env_.shadow_elev * kRad, az = -env_.shadow_az * kRad;
                const render::Vec3 L{std::sin(az) * std::cos(el) * env_.shadow_len, -1, std::cos(az) * std::cos(el) * env_.shadow_len};
                const float nl = l.plane[0] * L.x + l.plane[1] * L.y + l.plane[2] * L.z;
                if (nl < -0.05f) {
                    // p - L (n . p + d) / (n . L)
                    render::Mat4 flat;
                    const float Lv[3] = {L.x, L.y, L.z};
                    for (int i = 0; i < 3; ++i) {
                        for (int j = 0; j < 3; ++j) flat.at(i, j) = (i == j ? 1.0f : 0.0f) - Lv[i] * l.plane[size_t(j)] / nl;
                        flat.at(i, 3) = -Lv[i] * l.plane[3] / nl;
                    }
                    flat.at(1, 3) += 0.005f;
                    render::DrawItem sh{gadget_model_.get(), flat * at, 0};
                    sh.nodes = &gadget_shadow_nodes_[size_t(p.kind)];
                    sh.alpha = std::min(env_.shadow_factor(l.sun, l.sky) / std::max(color_scale_, 1.0f), 1.0f);
                    items.push_back(sh);
                }
            }
            render::DrawItem it{gadget_model_.get(), at, 0};
            it.nodes = &gadget_nodes_[size_t(p.kind)];
            it.lights = lights;
            if (env_.car_light_mask)
                for (int k = 0; k < 3; ++k) it.light_scale[k] = env_.light_scale(l.sun, l.sky)[size_t(k)];
            items.push_back(it);
        }
    if (fade)
        for (const gt3r::race::SceneryFade::Group& g : fade->faded()) {
            if (g.nodes.empty()) continue;
            render::DrawItem it{scenery_.get(), {}, 0};
            it.nodes = &g.nodes;
            it.fade = g.weight;
            it.in_order = it.in_order_join = true;
            it.frame_alpha = true;
            it.lights = lights;
            it.sphere_map = true;
            it.guard_band = true;
            items.push_back(it);
        }
}

void CourseScene::add_reflection_sky_items(std::vector<render::DrawItem>& items, const render::Vec3& eye) const {
    if (!reflection_sky_) return;
    render::DrawItem it{reflection_sky_.get(), render::translate(eye), 0};
    it.background = true;
    items.push_back(it);
}

void CourseScene::add_after_items(std::vector<render::DrawItem>& items) const {
    if (!after_) return;
    render::DrawItem it{after_.get(), {}, 0};
    it.lights = has_lights_ ? &lights_ : nullptr;
    it.sphere_map = true;
    items.push_back(it);
}

bool CourseScene::add_env_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Mat4& sky_rotation) const {
    if (!env_scenery_ && !env_sky_) return false;
    if (env_sky_) {
        render::DrawItem it{env_sky_.get(), render::translate(eye) * sky_rotation, 0};
        it.background = true;
        items.push_back(it);
    }
    if (env_scenery_) {
        render::DrawItem it{env_scenery_.get(), {}, 0};
        it.nodes = env_vis_.visible(eye.x, eye.y, eye.z);
        it.in_order = true;
        it.sphere_map = true;
        items.push_back(it);
    }
    return true;
}

} // namespace gt4r::race
