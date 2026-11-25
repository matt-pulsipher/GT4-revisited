#pragma once

// A car as the race view draws it: the painted body on its wheels' places, each wheel and tire
// in its own frame (steered, turning, the turning picture at speed), the brake parts in the
// wheel's frame, the lit brake lamps, the shadow under the car.
// docs/formats/model.md "Wheels as the game places them", "A car in a race".

#include "gt4r/base/types.h"
#include "gt4r/race/car_light.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/driver_figure.h"
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
        ByteSpan wing;      // a fitted wing's model set (wing/lod/<label>); empty: none
        // The driver: a character file's set for a car without its own (driver_figure.h
        // character_driver_file). Empty: the car's own set alone / no figure.
        ByteSpan driver_set;
        // The reflection passes' weight from the draw's own factors (DrawOptions::reflection_strength and _fresnel: the
        // program's globals [0x6238A0] and [0x62389C], op 0x56), not the constant 1 the shapes get without them.
        bool reflection_by_draw = false;
        // C0 >= 0: the draw's factors written on those passes instead (a race: the course's Env0 +0x34 and
        // +0x34 x +0x30, 0x397CE0), for a draw whose own factors stay C0 = 1, C1 = 0.
        float reflection_c0 = -1, reflection_c1 = 0;
        bool driver = false; // draw the driver
    };
    // `car_file`: car/lod/<model>; `pat`: the paint file next to it when the car file has none
    // inside. Throws FormatError.
    CarVisual(render::GpuContext& ctx, ByteSpan car_file, ByteSpan pat, const CarSpec& spec, const Options& opt, const std::string& name);
    // A car file in Gran Turismo 3's layout (Gran Turismo 4 Prologue's cars), which the
    // constructor reads too (car_visual_prologue.cpp): `paint` is then its body's paint variant.
    static bool is_gt3_car(ByteSpan car_file);
    ~CarVisual();
    // The same made away from the thread that owns the GPU (the menus load a car while they go
    // on drawing): the models are built and kept; upload() sends them to the GPU on its thread
    // before the car is drawn. Throws FormatError.
    static std::unique_ptr<CarVisual> make_unuploaded(render::GpuContext& ctx, ByteSpan car_file, ByteSpan pat, const CarSpec& spec,
                                                      const Options& opt, const std::string& name);
    void upload();
    // One model of them; true while more are left (the menus send a few a picture).
    bool upload_one();

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
    // Where the light throws the shadow (Shadow::shear), without making the shadows anew (a light
    // that moves: the menus' car stage).
    void set_shadow_shear(const render::Vec3& shear) { shadow_look_.shear = shear; }

    // The view the level of detail is chosen for: its forward axis (unit, course space) and K, the
    // program's size factor of the view (0x456918: one unit's height on screen at depth 1 x 10 / 7).
    struct LodView {
        render::Vec3 forward{0, 0, -1};
        float k = 0;
    };
    // The body's level of detail for a state seen from `eye`: the program's rule with a view, else by
    // the distance alone (30 m).
    int level(const CarState& s, const render::Vec3& eye, const LodView* view = nullptr) const;
    // Appends the car's items for a state as seen from `eye`. `step`: the simulation's step in
    // seconds (the wheels show their turning picture by how far they turn in one).
    // `wheels` false: its wheels are off (the pit crew has them).
    void add_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, float step = 1.0f / 60,
                   const LodView* view = nullptr, bool wheels = true) const;
    // A wheel away from the car: in a hub's frame, made as a left one (its outer face at -x).
    void add_loose_wheel(std::vector<render::DrawItem>& items, int wheel, const render::Mat4& hub) const;
    // The shadow under the car: after the course, before any car.
    // The car mirrored in the road under it (a wet road's reflection, the course's flag): the plane
    // through the tires' contacts. False when the car is not on a road it can mirror in.
    bool add_mirror_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye) const;
    // That mirror (course space to course space).
    bool road_mirror(const CarState& s, render::Mat4& out) const;
    // `light`: the light it stands in (null: the course's, in full).
    void add_shadow_items(std::vector<render::DrawItem>& items, const CarState& s, const CarLight* light = nullptr) const;

    // The model's frame in course space for a state: the body as drawn.
    render::Mat4 body_matrix(const CarState& s) const;
    const std::vector<CameraMount>& mounts() const { return mounts_; }
    // The car's lamps for their glows at night (the file's three lists: lit with the lights, the
    // tail lamps, the brake lamps), in the model's frame. `head`, `brake`, `tail`: the course's
    // strengths for each list.
    const gt3r::race::CarLampSet& lamps() const { return lamps_; }
    void set_lamp_strengths(float head, float brake, float tail);
    // The brake discs' glow on this course: heat^2 x this (Env0 +0x4C over the picture's scale +0x38).
    void set_brake_glow_gain(float gain) { glow_gain_ = gain; }
    // A wheel's glow strength for its brake heat, as the race's sponsor gives it (0x3D75F0).
    float brake_glow(float heat) const { return heat * heat * glow_gain_; }
    // A car whose driver moves (an open racer): its models and its seats' cockpits, for a figure of each
    // car of its own (the caller's: DriverFigure). Null for the others, whose figures sit still.
    const DriverModels* moving_driver() const { return moving_driver_ ? driver_models_.get() : nullptr; }
    const std::vector<posture::Cockpit>& cockpits() const { return cockpits_; }
    // Whether the figures show from here (0x3B1750: not below 0.025 of the view's size factor / 420 at a metre).
    bool driver_shows(const CarState& s, const render::Vec3& eye, const LodView* view = nullptr) const;
    const CarSpec& spec() const { return spec_; }
    int lods() const { return lods_; }
    // The body's box in the model's frame.
    const render::Vec3& bounds_min() const { return min_; }
    const render::Vec3& bounds_max() const { return max_; }

private:
    void build_gt3(ByteSpan car_file, const Options& opt, const std::string& name); // car_visual_prologue.cpp
    // A model to the GPU now, or kept for upload() when made by make_unuploaded.
    void to_gpu(std::unique_ptr<render::GpuModel>& slot, gt3r::scene::Model m);
    std::vector<std::pair<std::unique_ptr<render::GpuModel>*, gt3r::scene::Model>> unuploaded_;
    CarSpec spec_;
    std::array<std::unique_ptr<render::GpuModel>, 2> body_;        // [brake lamps lit]
    std::array<std::unique_ptr<render::GpuModel>, 4> brakes_;      // the brake parts per wheel
    std::array<std::unique_ptr<render::GpuModel>, 4> glows_;       // their glow: the added shapes, drawn at the heat's strength
    float glow_gain_ = 0;
    std::array<std::array<std::unique_ptr<render::GpuModel>, 2>, 4> wheels_; // [wheel][turning]: tire and wheel
    // The body's shape thrown flat along the light (model 1) and the shadow under the car (model 2):
    // as read, to recolor, and as drawn.
    // ... and the headlamps' light on the road (model 3), drawn when the course asks for it.
    std::array<gt3r::scene::Model, 3> shadow_src_;
    std::array<std::unique_ptr<render::GpuModel>, 3> shadow_;
    gt3r::race::CarLampSet lamps_;
    render::GpuContext* ctx_ = nullptr;
    // How a Gran Turismo 4 car's reflection passes are weighted (Options' fields).
    bool reflection_by_draw_ = false;
    float reflection_c0_ = -1, reflection_c1_ = 0;
    Shadow shadow_look_;
    std::vector<CameraMount> mounts_;
    render::Vec3 min_, max_;
    render::Vec3 lod_centre_{0, 0, 0}; // the body program's level switch (0x07): its centre and size
    float lod_size_ = 0;
    // The driver's models, and the figures (a co-driver's too) of a car whose driver sits still.
    std::unique_ptr<DriverModels> driver_models_;
    std::vector<std::unique_ptr<DriverFigure>> still_;
    std::vector<posture::Cockpit> cockpits_; // the driver's, a co-driver's
    bool moving_driver_ = false;
    void build_driver(ByteSpan file, const Options& opt, const std::string& name);
    std::unique_ptr<render::GpuModel> wing_; // a fitted wing, at the parts block's place and scale
    render::Vec3 wing_place_{0, 0, 0};
    float wing_scale_ = 1;
    int wing_lods_ = 1;
    int lod_levels_ = 0;
    int lods_ = 1;
    bool gt3_file_ = false; // a Gran Turismo 3 car file (Prologue's): its models' y = 0 is the road
};

} // namespace gt4r::race
