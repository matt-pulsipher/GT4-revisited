#pragma once

// A car as the race view draws it: the painted body on its wheels' places, each wheel and tire
// in its own frame (steered, turning, the turning picture at speed), the brake parts in the
// wheel's frame, the lit brake lamps, the shadow under the car.
// docs/formats/model.md "Wheels as the game places them", "A car in a race".

#include "gt4r/base/types.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/simulation.h"
#include "race/lamp_glows.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::race {

namespace render = gt3r::render;

class CarVisual {
public:
    struct Options {
        u32 paint = 0;      // Pat0 paint
        int lamp_level = 0; // 0 lights off, 1 on (a night course)
    };
    // `car_file`: car/lod/<model>; `pat`: the paint file next to it when the car file has none
    // inside. Throws FormatError.
    CarVisual(render::GpuContext& ctx, ByteSpan car_file, ByteSpan pat, const CarSpec& spec, const Options& opt, const std::string& name);
    ~CarVisual();

    // The shadow's look on this course (the environment block's values).
    struct Shadow {
        bool on = false;
        float rgb[3] = {0, 0, 0};
        float alpha = 0.5f;
        float light_factor = 1;      // on the alpha of the shadow the light throws
        float headlamps = 0;         // the strength of the headlamps' light on the road (0: none)
        render::Vec3 shear{0, 0, 0}; // course-space shift per metre of height: where the light throws it
    };
    void set_shadow(const Shadow& s);

    // Appends the car's items for a state as seen from `eye`. `step`: the simulation's step in
    // seconds (the wheels show their turning picture by how far they turn in one).
    void add_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, float step = 1.0f / 60) const;
    // The shadow under the car: after the course, before any car.
    void add_shadow_items(std::vector<render::DrawItem>& items, const CarState& s) const;

    // The model's frame in course space for a state: the body as drawn.
    render::Mat4 body_matrix(const CarState& s) const;
    const std::vector<CameraMount>& mounts() const { return mounts_; }
    // The car's lamps for their glows at night (the file's three lists: lit with the lights, the
    // tail lamps, the brake lamps), in the model's frame. `head`, `brake`, `tail`: the course's
    // strengths for each list.
    const gt3r::race::CarLampSet& lamps() const { return lamps_; }
    void set_lamp_strengths(float head, float brake, float tail);
    const CarSpec& spec() const { return spec_; }
    int lods() const { return lods_; }
    // The body's box in the model's frame.
    const render::Vec3& bounds_min() const { return min_; }
    const render::Vec3& bounds_max() const { return max_; }

private:
    CarSpec spec_;
    std::array<std::unique_ptr<render::GpuModel>, 2> body_;        // [brake lamps lit]
    std::array<std::unique_ptr<render::GpuModel>, 4> brakes_;      // the brake parts per wheel
    std::array<std::array<std::unique_ptr<render::GpuModel>, 2>, 4> wheels_; // [wheel][turning]: tire and wheel
    // The body's shape thrown flat along the light (model 1) and the shadow under the car (model 2):
    // as read, to recolor, and as drawn.
    // ... and the headlamps' light on the road (model 3), drawn when the course asks for it.
    std::array<gt3r::scene::Model, 3> shadow_src_;
    std::array<std::unique_ptr<render::GpuModel>, 3> shadow_;
    gt3r::race::CarLampSet lamps_;
    render::GpuContext* ctx_ = nullptr;
    Shadow shadow_look_;
    std::vector<CameraMount> mounts_;
    render::Vec3 min_, max_;
    int lods_ = 1;
};

} // namespace gt4r::race
