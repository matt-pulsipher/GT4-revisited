#pragma once

// The pit crew (pit/pit_001; the program's scene at 0x3CEB90 .. 0x3D2C48): for the car watched, four men with the
// tires, one with the fuel and one with the sign, round its box from the time it is on the pit lane until it has left
// it, their motions by the stop's stage, the wheels they carry and the wheel guns' and hose's sounds.
// docs/formats/race_flow.md "The pit crew".

#include "gt4r/base/types.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/menu/model_motion.h"
#include "gt4r/race/car_visual.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::race {

class PitScene {
public:
    static constexpr int kMen = 6; // 0..3 the tires (front left, front right, rear left, rear right), 4 the fuel, 5 the sign

    // What the scene reads of a car at a step.
    struct Car {
        int stage = 0;          // 0x3C9F38: 0 not in the pits, 1 on the lane, 2 in the box, 3 the work, 4 the last second, 5 leaving
        u8 kind = 0;            // the driver's kind (F +0x467)
        u16 service = 0;        // F +0x4B8
        bool refuelling = false; // 0x35BEB0
        render::Mat4 body;      // the car's model frame
        render::Vec3 position{0, 0, 0}, right{1, 0, 0}, up{0, 1, 0}, back{0, 0, 1}; // the car's position and axes, course space
        render::Vec3 velocity{0, 0, 0}; // metres per second
        render::Vec3 hub[4];    // the wheels' middles in it
        float steer[4] = {0, 0, 0, 0}, spin[4] = {0, 0, 0, 0}; // about the up axis (+ right), rolled (forward +)
        render::Vec3 box{0, 0, 0}; // the car's box on the road, and its heading about the up axis (0: facing -z)
        float box_heading = 0;
        bool has_box = false;
        render::Vec3 gantry{0, 0, 0}; // the pit's stand the air hoses hang from (the course's pit stop nearest the box)
        float gantry_heading = 0;
    };
    // What it knows of a car for the whole race.
    struct CarSizes {
        float front = 2.2f, rear = 2.2f, width = 1.75f; // from the model's origin; across
        float tire_width[2] = {0.2f, 0.2f}, tire_radius[2] = {0.31f, 0.31f};
        render::Vec3 filler{0, 0.6f, 1.5f}, filler_axis{-1, 0, 0}; // parts block +0xAC, +0xB8
        render::Vec3 seat{0, 0.3f, 0}; // parts block +0x34
        u16 flags = 0;          // info +0x00
        bool known = false;     // a car file was read
    };

    // The filler and the flags of a car file (car/lod); the rest as given. Throws FormatError.
    static void read_car_file(ByteSpan car_file, CarSizes& s);
    // A car's frame and hubs as it stands (the body as drawn).
    static void place_car(Car& out, const CarVisual& visual, const CarState& s);

    // Without a GPU the crew is stepped, not drawn. Throws FormatError.
    PitScene(render::GpuContext* ctx, ByteSpan file);
    void set_car(size_t i, const CarSizes& s);
    // Where the light throws a shadow (course-space shift per metre of height); without it no shadows.
    void set_shadow_shear(const render::Vec3& shear) { shear_ = shear, shadows_ = true; }
    void reset();

    // A simulation step of `dt` for all the cars; `shown` the car watched (-1 none): its crew's sounds are told to
    // `sound` (the names of the race's effects).
    void step(std::span<const Car> cars, int shown, float dt, const std::function<void(const char*)>& sound);
    // A hose runs (the held refuelling sound).
    bool hose() const { return hose_; }

    // The crew of a car and the wheels they have, in course space.
    struct Wheel {
        int wheel = 0;
        render::Mat4 frame; // the hub's frame (the model as a left wheel, its outer face at -x)
    };
    void add_items(std::vector<render::DrawItem>& items, std::vector<Wheel>& wheels, size_t car);
    // The car's own wheels are off (the scene draws all of them).
    bool wheels_off(size_t car) const { return active(car) && cars_[car].stage == 3; }
    // Whether the car's crew is out: while the pit's camera is on it (0x3D25B0).
    bool active(size_t car) const { return car < cars_.size() && cars_[car].stage > 0 && cars_[car].camera_number >= 5; }

    // The pit's own camera for a car (the race's view gives way to it, 0x3FD620 -> CameraSys::CameraPitIn), as it was at the
    // step `before` the last one or at the last; false: none now.
    struct CameraShot {
        render::Vec3 eye{0, 0, 0};
        render::Mat4 view;
        float angle = 1; // the program's, radians
        int shot = 0;    // counts the cuts
    };
    bool camera(size_t car, CameraShot& out, bool before = false) const;

    // A crew member of a car as it stands (for checks).
    struct Man {
        float time = 0;
        render::Vec3 pos{0, 0, 0};
        float heading = 0;
        bool idle_next = false, idle = false; // +0x16C, +0x170: no work for him at this stop
        bool done = false;                    // the tires' men: past their "tire_end"
    };
    const Man& man(size_t car, int k) const { return cars_[car].men[size_t(k)]; }

private:
    struct Stop {
        CarSizes sizes;
        Car now;
        int stage = 0, was = 0;
        int phase = 0;                 // the scene's own state of the box (0x3D0DF0): 0 none, 2 waiting, 4 at work, 5 left
        render::Vec3 place{0, 0, 0};   // the crew's origin
        float heading = 0;
        std::array<Man, kMen> men;
        bool fuelled = false, was_refuelling = false;
        float spin[4] = {0, 0, 0, 0};
        render::Mat4 parted;           // the car's frame as it was when the work ended
        render::Vec3 parted_hub[4];
        // The camera (0x3CBB28, 0x3CC010): its place in the car's sequence, the number it is sent to, the clip picked.
        int camera_sequence = -1, camera_number = -1, camera_pick = 0, camera_clip = -1;
        float camera_time = 0, camera_entry = -1;
        bool stopping = false;         // on the stop line while on the lane
        bool camera_set = false;
        float cut_yaw = 0;             // the car's heading and place at the last cut
        render::Vec3 cut_at{0, 0, 0};
        render::Vec3 hubs[4];          // the hubs about the car's position as the race began
        float shake[2] = {0, 0}, shake_to[2] = {0, 0};
        CameraShot shot, shot_before;
        bool shot_on = false, shot_before_on = false;
        struct Sway {
            float x = 0, vx = 0, z = 0, vz = 0;
        } sway[4]; // the air hoses'
        float turn_out[4] = {1, 1, 1, 1}, turn_in[4] = {0, 0, 0, 0}; // how far the wheels taken off / put on follow the hub's turn
    };
    int number(const Stop& c, int k) const; // the motion's number a man is sent to (-1: not there)
    void waiting_places(Stop& c);
    void working_places(Stop& c);
    void fuel_place(Stop& c, bool waiting);
    void place(Stop& c, int stage);
    float random(float lo, float hi);
    bool wheel_frame(const Stop& c, int k, int which, render::Mat4& out) const;
    void step_camera(Stop& c, float dt);
    int camera_number(const Stop& c, bool& cut) const;
    bool camera_point(const Stop& c, int kind, const render::Vec3& v, bool direction, render::Vec3& out) const;
    menu::SkeletonMotion::Pose pose_of(const Stop& c, int k) const;

    render::GpuContext* ctx_ = nullptr;
    std::vector<menu::SkeletonMotion> motions_, cameras_;
    std::map<std::string, menu::SkeletonMotion::Name> camera_names_;
    struct Seq {
        int object = 0, sequence = -1;
    };
    Seq seq_[kMen];
    float release_[2][4] = {}, fix_[2][4] = {}; // when a wheel comes off and goes on (0x3CEB90)
    float tire1_z_[4] = {1, 1, 1, 1};           // the new wheel's place along its axis at a motion's start
    std::vector<mdls::Bone> bones_;
    std::vector<mdls::Symbol> symbols_;
    mdls::SkinTable skin_;
    int skinned_ = -1;
    std::vector<std::unique_ptr<render::GpuModel>> models_;
    gt3r::scene::Model stored_;
    std::array<std::unique_ptr<render::GpuModel>, kMen> posed_;
    // The shadow's figure, and the air hoses.
    std::vector<mdls::Bone> shadow_bones_;
    std::vector<mdls::Symbol> shadow_symbols_;
    mdls::SkinTable shadow_skin_;
    int shadow_skinned_ = -1;
    std::vector<std::unique_ptr<render::GpuModel>> shadow_models_;
    gt3r::scene::Model shadow_stored_;
    std::array<std::unique_ptr<render::GpuModel>, kMen> shadow_posed_;
    render::Vec3 shear_{0, 0, 0};
    bool shadows_ = false;
    std::unique_ptr<render::GpuModel> hose_model_, hose_lines_;
    void add_hoses(std::vector<render::DrawItem>& items, const Stop& c, const std::array<render::Vec3, 4>& guns, const std::array<bool, 4>& with);
    void build_set(ByteSpan set, std::vector<mdls::Bone>& bones, std::vector<mdls::Symbol>& symbols, mdls::SkinTable& skin, int& skinned,
                   std::vector<std::unique_ptr<render::GpuModel>>& models, gt3r::scene::Model& stored, const char* name);
    std::vector<Stop> cars_;
    u32 seed_ = 1;
    bool hose_ = false;
};

} // namespace gt4r::race
