#include "gt4r/race/course_scene.h"

#include "base/log.h"
#include "formats/model/coplanar_overlays.h"
#include "gt4r/formats/model/mdls.h"

namespace gt4r::race {

namespace scene = gt3r::scene;

std::unique_ptr<render::GpuModel> CourseScene::build(render::GpuContext& ctx, int slot, const std::string& label, bool keep,
                                                     bool sheen, scene::Model* out) {
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
    if (course::Runway::is_runway(file_.section(data_, course::kSlotRunway))) road_ = course::Runway::parse(file_.section(data_, course::kSlotRunway));

    scene::Model scenery;
    scenery_ = build(ctx, course::kSlotWorld, name + "/scenery", true, true, &scenery);
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
