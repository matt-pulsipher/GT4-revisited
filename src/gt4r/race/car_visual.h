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
#include "gt4r/race/mod_car_body.h"
#include "gt4r/race/simulation.h"
#include "race/lamp_glows.h"
#include "gt4r/formats/model/model_script.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::race {

namespace render = gt3r::render;

// A lamp's directional power: the record's (+0x60) x the course's factor (Env0 +0x17A0; 0 reads as 1), rounded as the EE does
// (+ 0.5, toward zero), at most 32; a negative one is none (0x3B8460).
int lamp_power(float power, float factor);
// A wheel's level of detail in Prologue (0x412918): by its size on screen, 0 .. 3.
int prologue_wheel_detail(float proj_x, float bound_radius, float depth);

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
        // A Gran Turismo 3 car file's brake parts (Prologue's cars): the disc's race/brake.bin. Empty: none drawn.
        ByteSpan brake_parts;
        // A mod car's body (mod_car_body.h): the model's stretched, or one of its own in its place. Null: the file's.
        std::shared_ptr<const ModCarBody> mod_body;
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
    // The moving parts of one car's body as its script sets them (a few cars': docs/formats/model.md "What a car's script
    // reads"): the script's state, run on with that car's values; the parts it turns (0x48 by a register) and the shapes
    // it blends (0x4C), as that car shows them.
    struct Motion {
        mdls::ScriptState state;
        u32 time_ms = 0;
        std::vector<render::Mat4> turns;                       // per turned part: its matrix in the model's frame
        std::vector<float> shown;                              // per blend weight: the one the shapes show
        std::array<std::unique_ptr<render::GpuModel>, 2> band; // those shapes (per brake lamp state), made at the first change
    };
    // The game's values the script reads (CarModel +0x40, +0x48, +0x74, +0x78): the brake, the engine's speed (RaceCarModel
    // vf22 0x451AB0: int(F +0x738); 0 in a showroom), the driver's two pedals (vf31; 0 when the cockpit gives none).
    struct MotionInput {
        float brake = 0, engine_rpm = 0, pedal_left = 0, pedal_right = 0;
        bool brake_lamps = false;
    };
    // Null for a body without moving parts.
    std::unique_ptr<Motion> make_motion() const;
    void restart_motion(Motion& m) const;
    // `seconds` on: the model's time (+0x34 in milliseconds, trunc(1000 x the step), 0x451110), the script's per-step and
    // draw entries with the values, the parts' matrices and the blended shapes from its registers.
    void step_motion(Motion& m, float seconds, const MotionInput& in) const;
    // `motion`: this car's moving parts (null: at rest, as a still draw).
    void add_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, float step = 1.0f / 60,
                   const LodView* view = nullptr, bool wheels = true, const Motion* motion = nullptr) const;
    // A wheel away from the car: in a hub's frame, made as a left one (its outer face at -x).
    void add_loose_wheel(std::vector<render::DrawItem>& items, int wheel, const render::Mat4& hub) const;
    // The shadow under the car: after the course, before any car.
    // The car mirrored in the road under it (a wet road's reflection, the course's flag): the plane
    // through the tires' contacts. False when the car is not on a road it can mirror in.
    bool add_mirror_items(std::vector<render::DrawItem>& items, const CarState& s, const render::Vec3& eye, const Motion* motion = nullptr) const;
    // That mirror (course space to course space).
    bool road_mirror(const CarState& s, render::Mat4& out) const;
    // `light`: the light it stands in (null: the course's, in full).
    void add_shadow_items(std::vector<render::DrawItem>& items, const CarState& s, const CarLight* light = nullptr) const;

    // The model's frame in course space for a state: the body as drawn.
    render::Mat4 body_matrix(const CarState& s) const;
    // The still figures' skeletons (the driver's, a co-driver's: in the figure's frame), and one set in place of a seat's
    // posture (a film's figure when it is developed).
    const posture::Skeleton* still_skeleton(size_t seat) const { return seat < still_.size() ? &still_[seat]->skeleton() : nullptr; }
    void set_still_skeleton(size_t seat, const posture::Skeleton& s);
    const std::vector<CameraMount>& mounts() const { return mounts_; }
    // The car's lamps for their glows at night (the file's three lists: lit with the lights, the
    // tail lamps, the brake lamps), in the model's frame. `head`, `brake`, `tail`: the course's
    // strengths for each list.
    const gt3r::race::CarLampSet& lamps() const { return lamps_; }
    void set_lamp_strengths(float head, float brake, float tail);
    // The course's factor on the lamps' directional power (Env0 +0x17A0).
    void set_lamp_type_factor(float factor);
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
    // The flashing lamps' patterns run on `seconds` (RaceCarModel::vf3, one for every car of the model).
    void animate(float seconds);
    void restart_animation();
    // The lamp level it was built with (0 off, 1 on: the course's night, or a car the race lights; RaceBase::vf38).
    int lamp_level() const { return lamp_level_; }
    const CarSpec& spec() const { return spec_; }
    bool gt3_file() const { return gt3_file_; } // a Gran Turismo 3 car file (Prologue's)
    int lods() const { return lods_; }
    // The body's box in the model's frame.
    const render::Vec3& bounds_min() const { return min_; }
    const render::Vec3& bounds_max() const { return max_; }

private:
    void build_gt3(ByteSpan car_file, const Options& opt, const std::string& name); // car_visual_prologue.cpp
    // A model to the GPU now, or kept for upload() when made by make_unuploaded. `prepared`: its reflections weighed and
    // its textures shared already.
    void to_gpu(std::unique_ptr<render::GpuModel>& slot, gt3r::scene::Model m, bool prepared = false);
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
    // The lamps' own colors and powers as the file has them, per list (head, tail, brake).
    std::vector<std::array<float, 3>> lamp_colors_[3];
    std::vector<float> lamp_powers_[3];
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
    // The body's moving parts: its script, the shapes under a turn by a register (each a model of its own, drawn turned
    // about the program's matrix there) and the shapes its blend weights move (taken out of the body into a band: their
    // vertices at every weight 0 and at each weight's 1), per brake lamp state.
    struct Moving {
        mdls::Script script;
        int time_cell = -1, rpm_cell = -1, left_cell = -1, right_cell = -1, brake_cell = -1, taillamp_cell = -1, timezone_cell = -1;
        std::vector<u16> weights; // the registers of the band's blends
        struct Band {
            std::vector<gt3r::scene::Mesh> meshes;                       // at every weight 0 (textures of `gpu`)
            std::vector<std::vector<std::vector<gt3r::scene::Vertex>>> ends; // [weight][mesh][vertex] at that weight's 1
            std::unique_ptr<render::GpuModel> gpu;                       // as built: the still draw's
        };
        std::array<Band, 2> band;
        struct Part {
            u8 op = 0; // the command (0x48: a turn by an angle and an axis) and its register
            u16 cell = 0;
            std::array<float, 16> pivot{}, pivot_inverse{}; // the program's matrix there (row vectors)
            std::array<std::unique_ptr<render::GpuModel>, 2> gpu;
        };
        std::vector<Part> parts;
        std::vector<render::Mat4> still_turns; // the parts at rest (the still registers')
    };
    void split_band(gt3r::scene::Model& m, const std::vector<gt3r::scene::Model>& ends, Moving::Band& band);
    std::vector<render::Mat4> part_matrices(const mdls::ScriptState& st) const;
    std::unique_ptr<Moving> moving_;
    int lamp_level_ = 0;
    std::vector<std::pair<int, float>> lamp_flash_; // per lit-list lamp: its flashing pattern (0 none) and phase
    std::array<float, 2> flash_time_{};             // the two patterns' times
    // A Gran Turismo 3 car file's brake parts drawn by its program's rule (car_visual_prologue.cpp): on a wheel's two
    // nearest levels of detail, seen from outside its outer face; their glow on those levels.
    bool gt3_brakes_shown(const CarState& s, int w, const render::Vec3& eye, const LodView* view, bool& parts) const;
};

} // namespace gt4r::race
