#include "gt4r/race/course_scene.h"

#include "gt4r/formats/course/runway_gtrw.h"

#include "base/log.h"
#include "formats/model/coplanar_overlays.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cstring>

namespace gt4r::race {

namespace scene = gt3r::scene;

std::unique_ptr<render::GpuModel> CourseScene::build(render::GpuContext& ctx, int slot, const std::string& label, bool keep,
                                                     bool sheen, scene::Model* out, bool animated) {
    const ByteSpan s = file_.section(data_, slot);
    if (!mdls::is_mdls(s)) return nullptr;
    // Scenery as the shared engine decodes its own: the programs' render state kept, cut-out
    // textures alpha tested, each packet's own texture for its environment pass, and the second
    // pass over the road and the fences (the sheen) weighted by the frame's alpha.
    mdls::Scene sc;
    sc.color_scale = color_scale_;
    const Bytes block = mdls::to_gtm1(s, nullptr, sc);
    const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
    gt3r::gtm1::Options o;
    o.reflection = sheen;
    o.road_sheen = sheen;
    o.frame_alpha = sheen;
    o.course_env_maps = true;
    o.alpha_test = true;
    o.gs_state = true;
    scene::Model m = gt3r::gtm1::build_model(g, o, label);
    for (scene::Mesh& mesh : m.meshes) gt3r::gtm1::mark_coplanar_overlays(mesh);
    if (animated) {
        // The set's animated models are drawn apart, posed each frame; a set whose scripts do not
        // read stays whole and still.
        try {
            animated_ = AnimatedModels(s, color_scale_, o);
        } catch (const std::exception& e) {
            LOG_WARN("Course %s: its animated models stay still (%s)", label.c_str(), e.what());
            animated_ = {};
        }
        const std::vector<u32> moving = animated_.models();
        std::erase_if(m.meshes, [&](const scene::Mesh& mesh) { return mesh.node >= 0 && std::binary_search(moving.begin(), moving.end(), u32(mesh.node)); });
        if (!animated_.empty()) {
            scene::Model holder = animated_.textures();
            holder.name = label + "/animated";
            animated_textures_ = std::make_unique<render::GpuModel>(ctx, holder);
            animated_gpu_ = std::make_unique<render::GpuModel>(ctx, *animated_textures_);
            animated_gpu_->keep_geometry();
        }
    }
    if (m.meshes.empty()) return nullptr;
    auto gpu = std::make_unique<render::GpuModel>(ctx, m, keep);
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
    scenery_ = build(ctx, course::kSlotWorld, name + "/scenery", true, true, &scenery, true);
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
    sky_ = build(ctx, course::kSlotSky, name + "/sky", false, false, &sky);
    if (sky_) {
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
        has_lights_ = true;
    }
    LOG_INFO("Course %s: %zu scenery models, %zu triangles, %zu textures; far %.0f m, fog %.0f / %.2f, color scale %.2f", name.c_str(),
             stats_.models, stats_.triangles, stats_.textures, env_.far_clip, env_.fog_a, env_.fog_b, color_scale_);
}

CourseScene::~CourseScene() = default;

ByteSpan CourseScene::section(int slot) const { return file_.section(data_, slot); }

void CourseScene::restart_animations() { animated_.restart(); }
void CourseScene::animate(float seconds, std::span<const CarState> cars) { animated_.tick(seconds, cars); }
void CourseScene::upload_animations(std::span<const CarState> cars) {
    if (animated_gpu_) animated_gpu_->update_geometry(animated_.meshes(cars));
}

void CourseScene::add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, View view,
                            const gt3r::race::SceneryFade* fade) const {
    const render::ItemLights* lights = has_lights_ ? &lights_ : nullptr;
    const bool mirror = view == View::Mirror;
    // The sky is drawn round the camera with the view's translation taken off.
    const render::GpuModel* sky = mirror && mirror_sky_ ? mirror_sky_.get() : sky_.get();
    if (sky) {
        // With its own depth (the dome's flat top lies behind its upper ring and must not cover
        // it), cleared after it; no fog.
        render::DrawItem it{sky, render::translate(eye), 0};
        it.in_order = true;
        it.depth_reset = true;
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
        if (animated_gpu_ && animated_gpu_->valid()) {
            it.model = animated_gpu_.get();
            it.in_order_join = true;
            items.push_back(it);
        }
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
