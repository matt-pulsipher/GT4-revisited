#pragma once

// A course as the race view draws it: the sky, the backdrop, the scenery by the visibility list
// of the camera's place, the models drawn after the cars, and the scenery of the cars'
// reflection picture. docs/formats/course.md "The race view's frame".
//
// The model sets go through the shared engine's decoder (formats/model/mdls.h) and its renderer;
// the environment block is the shared engine's layout and is read by its own reader.

#include "formats/course/visibility.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/animated_models.h"
#include "gt4r/race/gadgets.h"
#include "race/occluder.h"
#include "gt4r/race/list_fade.h"
#include "race/sun_glare.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::race {

namespace render = gt3r::render;

class CourseScene {
public:
    // Builds the models from a course file's bytes; `section` the race's road section (a license test's, a mission's: its own
    // cones and gates are placed too), -1 none. Throws FormatError.
    CourseScene(render::GpuContext& ctx, ByteSpan course_file, const std::string& name, int section = -1);
    ~CourseScene();

    // A race's view, its mirror; a split screen's half and its mirror (their own lists: slots 4 and 7); the race view with
    // the race object's list switch off (race +0xE334 = 0: a photo's exposure 0x334730 and the race's photo finder 0x3C5430;
    // RacePS2Base::vf110 0x3403B8 then draws by slot 8's lists, every model on a course without them: 0x397048).
    enum class View { Race, Mirror, Half, HalfMirror, Photo };
    // Appends the draw items for a camera at `eye`, in the game's order: sky, backdrop (the depth
    // is reset after it), then the scenery's models named by the view's list for `eye` (with
    // `fade`: those drawn whole and, dithered, those coming in).
    void add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, View view = View::Race,
                   const ListFade* fade = nullptr) const;
    // The models drawn after the cars (slot 17), all of them.
    void add_after_items(std::vector<render::DrawItem>& items) const;
    // The cars' reflection picture: its sky (slot 23) turned by `sky_rotation`, then slot 5 by
    // slot 6's list for `eye`. False when the course has neither.
    bool add_env_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Mat4& sky_rotation) const;

    // The course's animated models (flags, people who step back from the road): the start of a
    // race, a step of it, and once per drawn frame their shapes as they are now.
    void restart_animations();
    void animate(float seconds, std::span<const CarState> cars);
    void upload_animations(std::span<const CarState> cars);

    // The road reflection's sets (slots 9 and 13 with their lists 10 and 14) as a course file in
    // the shared engine's layout, for its own reflection pass; empty when the course has no slot 9.
    ByteSpan reflection_course() const { return reflection_course_; }
    // The reflection buffer's sky (slot 50), round the camera.
    void add_reflection_sky_items(std::vector<render::DrawItem>& items, const render::Vec3& eye) const;
    // The reflection's strength on the road: the environment's color and gain (Env0 +0xC10..+0xC1C).
    float reflection_gain() const { return reflection_gain_; }

    // The scenery models the view draws from `eye` (null: all of them). Valid while the scene lives.
    const std::vector<u16>* visible_nodes(const render::Vec3& eye, View view = View::Race) const {
        return vis_[size_t(view)].visible(eye.x, eye.y, eye.z);
    }
    // The environment block's values (fog, lights, sun, shadows), as the shared engine reads them.
    const gt3r::race::SunParams& env() const { return env_; }
    // The race view's far plane (m): Env0 +0x20, which the one-view draw puts on the screen every picture (0x33EB9C); every
    // course of the discs has one (test course_far_planes_disc). Without it the screen keeps the far plane it starts with,
    // 750 m (0x3FB430).
    float far_clip() const { return env_.has_env && env_.far_clip > 0 ? env_.far_clip : kScreenFar; }
    static constexpr float kScreenFar = 750;
    // The picture's scale (Env0 +0x38). The models' stored colors carry race::stored_color_scale of it.
    float color_scale() const { return color_scale_; }
    const course::Runway& road() const { return road_; }
    // The cones and boards where the road file puts them (still: ❓ the program knocks them about).
    const Gadgets& gadgets() const { return gadgets_; }
    // The cones and boards where the cars have left them (null: at their places); kept by the caller.
    void set_gadget_bodies(const class GadgetBodies* bodies) { gadget_bodies_ = bodies; }
    // Opaque scenery and backdrop triangles, for sight tests (the sun's glare, the lamp glows).
    const gt3r::race::Occluder& occluder() const { return occluder_; }
    // The sections other parts of the race view read (copies of the file's bytes; empty: none).
    ByteSpan section(int slot) const;
    // Mean colors of the sky's upper half and of the scenery: what a car's paint reflects where no
    // reflection picture is drawn.
    const float* sky_color() const { return sky_rgb_; }
    const float* horizon_color() const { return horizon_; }
    const float* ground_color() const { return ground_; }
    struct Stats {
        size_t models = 0, triangles = 0, textures = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    // A set's animated models: a model holding their pictures, and their shapes of the frame.
    struct AnimatedSet {
        AnimatedModels models;
        std::unique_ptr<render::GpuModel> textures, gpu;
    };
    std::unique_ptr<render::GpuModel> build(render::GpuContext& ctx, int slot, const std::string& label, bool keep, bool sheen,
                                            gt3r::scene::Model* out = nullptr, AnimatedSet* animated = nullptr);

    Bytes data_;
    course::CourseFile file_;
    course::Runway road_;
    gt3r::race::SunParams env_;
    float color_scale_ = 1;
    AnimatedSet animated_, animated_sky_; // the scenery's, the sky's (aeroplanes and their trails)
    Bytes reflection_course_;
    float reflection_gain_ = 1;
    std::unique_ptr<render::GpuModel> reflection_sky_;
    std::unique_ptr<render::GpuModel> scenery_, after_, backdrop_, sky_, mirror_sky_, env_scenery_, env_sky_;
    Gadgets gadgets_;
    const class GadgetBodies* gadget_bodies_ = nullptr;
    std::unique_ptr<render::GpuModel> gadget_model_;
    std::vector<std::vector<u16>> gadget_nodes_, gadget_shadow_nodes_; // per kind: its shape's model, its shadow's
    struct GadgetLight {
        bool known = false;
        course::Vec3 at;
        float sun = 1, sky = 1;
        std::array<float, 4> plane{0, 1, 0, 0};
    };
    mutable std::vector<GadgetLight> gadget_light_;
    std::array<gt3r::course::Visibility, 5> vis_; // slot 1's lists per View
    gt3r::course::Visibility env_vis_;            // slot 5's
    gt3r::race::Occluder occluder_;
    render::ItemLights lights_;
    bool has_lights_ = false;
    Stats stats_;
    float horizon_[3] = {0.62f, 0.70f, 0.80f}, sky_rgb_[3] = {0.35f, 0.55f, 0.85f}, ground_[3] = {0.18f, 0.17f, 0.16f};
};

} // namespace gt4r::race
