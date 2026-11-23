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
#include "race/occluder.h"
#include "race/scenery_fade.h"
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
    // Builds the models from a course file's bytes. Throws FormatError.
    CourseScene(render::GpuContext& ctx, ByteSpan course_file, const std::string& name);
    ~CourseScene();

    enum class View { Race, Mirror };
    // Appends the draw items for a camera at `eye`, in the game's order: sky, backdrop (the depth
    // is reset after it), then the scenery's models named by the view's list for `eye` (with
    // `fade`: those drawn whole and, dithered, those coming or going).
    void add_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, View view = View::Race,
                   const gt3r::race::SceneryFade* fade = nullptr) const;
    // The models drawn after the cars (slot 17), all of them.
    void add_after_items(std::vector<render::DrawItem>& items) const;
    // The cars' reflection picture: its sky (slot 23) turned by `sky_rotation`, then slot 5 by
    // slot 6's list for `eye`. False when the course has neither.
    bool add_env_items(std::vector<render::DrawItem>& items, const render::Vec3& eye, const render::Mat4& sky_rotation) const;

    // The scenery models the view draws from `eye` (null: all of them). Valid while the scene lives.
    const std::vector<u16>* visible_nodes(const render::Vec3& eye, View view = View::Race) const {
        return vis_[size_t(view)].visible(eye.x, eye.y, eye.z);
    }
    // The environment block's values (fog, lights, sun, shadows), as the shared engine reads them.
    const gt3r::race::SunParams& env() const { return env_; }
    // The picture's scale (Env0 +0x38): the factor already put on the models' stored colors.
    float color_scale() const { return color_scale_; }
    const course::Runway& road() const { return road_; }
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
    std::unique_ptr<render::GpuModel> build(render::GpuContext& ctx, int slot, const std::string& label, bool keep, bool sheen,
                                            gt3r::scene::Model* out = nullptr);

    Bytes data_;
    course::CourseFile file_;
    course::Runway road_;
    gt3r::race::SunParams env_;
    float color_scale_ = 1;
    std::unique_ptr<render::GpuModel> scenery_, after_, backdrop_, sky_, mirror_sky_, env_scenery_, env_sky_;
    std::array<gt3r::course::Visibility, 2> vis_; // slot 1's lists per View
    gt3r::course::Visibility env_vis_;            // slot 5's
    gt3r::race::Occluder occluder_;
    render::ItemLights lights_;
    bool has_lights_ = false;
    Stats stats_;
    float horizon_[3] = {0.62f, 0.70f, 0.80f}, sky_rgb_[3] = {0.35f, 0.55f, 0.85f}, ground_[3] = {0.18f, 0.17f, 0.16f};
};

} // namespace gt4r::race
