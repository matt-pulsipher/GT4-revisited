#pragma once

// The license test's staff (pit/license_001; the program's LicenseConcourse, 0x408120 .. 0x4098A0, kept by RaceLicense at
// +0x24E5C): three men in GT shirts, the master and two of his staff, at the test's stopping place after a run that ends with a
// time. Their motions say whether it was passed (license_success_*) or ended slower than every target (license_failed_*); a run
// failed by the rules (off the course, a wall, a cone) brings no one out. The scene's own cameras show it, the race's picture
// fading out and in round the change. docs/formats/pit_scene.md "The license scene".

#include "gt4r/base/types.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/menu/model_motion.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::race {

class LicenseScene {
public:
    static constexpr int kMen = 3; // 0 the master, 1 and 2 his staff

    // What the men are placed by: the test car's seat and gear lever (the car file's parts block +0x34, +0x4C: NormalCarGeometry
    // virtuals 2, 4 and 18), its front wheels' places in its frame (x across, z along: front is -z; 0x344AA8, 0x344C08).
    struct CarSizes {
        float seat_x = 0.35f, seat_z = 0, lever_x = 0;
        float wheel_x[2] = {-0.7f, 0.7f}, wheel_z[2] = {-1.2f, -1.2f}; // front left, front right
    };
    // The seat and the lever of a car file (car/lod). Throws FormatError.
    static void read_car_file(ByteSpan car_file, CarSizes& s);

    // Without a GPU the men are stepped, not drawn. Throws FormatError.
    LicenseScene(gt3r::render::GpuContext* ctx, ByteSpan file);
    // Where the light throws a shadow (course-space shift per metre of height); without it no shadows.
    void set_shadow_shear(const gt3r::render::Vec3& shear) { shear_ = shear, shadows_ = true; }

    // The test's start (0x408F90): the road section's places for the staff (none: no scene), the car's sizes, and the road's
    // height where a man stands. With two places or more the men stand at place 1 through the run (drawn), else at place 0
    // (not drawn until the end), each in his standing motion.
    // (The road's height under a point and, when asked, its plane's normal there; false off the road.)
    using Height = std::function<bool(float x, float z, float& y, gt3r::render::Vec3* normal)>;
    void start(const std::vector<course::Runway::Place>& places, const CarSizes& car, Height height);
    bool on() const { return on_; }

    // The run's result, once (0x409138 from 0x3BC6E0): passed (the test's result word set), or ended with a time slower than
    // every target (event 10 with no result).
    void set_result(bool passed);
    int result() const { return result_; } // 0 none yet, 1 passed, 2 failed

    // A step of `dt` (0x409168). True in the step the men come to the stopping place (state 2 begun): the test car is then
    // put there (car_place, 0x3FAB10).
    bool step(float dt);
    int state() const { return state_; } // 0 the 2 s after the result, 1 the 2 s of the fade out, 2 the men there, 3 a second on
    float time() const { return clock_; }
    // The place and heading the car is put at (the place's own heading + 3.14).
    const gt3r::render::Vec3& place() const { return place_; }
    float heading() const { return heading_; }

    // The race's picture brightness (race +0xCF54, the fade's rate: -1/2 a second from state 1, +1 a second from state 2).
    float brightness() const { return brightness_; }

    // The scene's camera (0x4094C8): from state 2 on it is the race's (0x4096F8), now or as it was before the last step.
    struct CameraShot {
        gt3r::render::Vec3 eye{0, 0, 0};
        gt3r::render::Mat4 view;
        float angle = 1; // the program's, radians
    };
    bool camera(CameraShot& out, bool before = false) const;

    // The men, in course space (drawn while the scene is on and they have been placed: 0x4096C0).
    void add_items(std::vector<gt3r::render::DrawItem>& items);

    // A man as he stands (for checks).
    struct Man {
        int sequence = -1, object = 0;
        float time = 0;
        gt3r::render::Vec3 pos{0, 0, 0}; // course space
        float heading = 0;               // course space (the place's heading added)
        gt3r::render::Vec3 ground{0, 1, 0}; // the road's plane under him (0x407EF0; up off the road)
    };
    // How far a shadow rises on a plane of normal `n` for a metre of the man's height thrown `shift` across the ground (course
    // space; 0x407FA0): -(n.x sx + n.z sz) / n.y, 0 on a plane standing on its edge.
    static float shadow_rise(const gt3r::render::Vec3& n, const gt3r::render::Vec3& shift);
    const Man& man(int k) const { return men_[size_t(k)]; }
    int camera_sequence() const { return camera_sequence_; }
    float camera_time() const { return camera_time_; }

private:
    void place_men(int place);
    void set_motion(int k, const char* name);
    float advance(const menu::SkeletonMotion::Sequence& q, float t, float dt) const;
    void step_camera(float dt);

    gt3r::render::GpuContext* ctx_ = nullptr;
    std::vector<menu::SkeletonMotion> motions_, cameras_;
    std::map<std::string, menu::SkeletonMotion::Name> names_, camera_names_;
    struct Set {
        std::vector<mdls::Bone> bones;
        std::vector<mdls::Symbol> symbols;
        mdls::SkinTable skin;
        int skinned = -1;
        std::vector<std::unique_ptr<gt3r::render::GpuModel>> models;
        gt3r::scene::Model stored;
        std::array<std::unique_ptr<gt3r::render::GpuModel>, kMen> posed;
    };
    Set crew_, shadow_;
    void build_set(ByteSpan set, Set& out, const char* name);
    gt3r::render::Vec3 shear_{0, 0, 0};
    bool shadows_ = false;

    std::vector<course::Runway::Place> places_;
    CarSizes car_;
    Height height_;
    bool on_ = false, drawn_ = false;
    int result_ = 0, state_ = 0, was_ = 0;
    float clock_ = 0;
    gt3r::render::Vec3 place_{0, 0, 0};
    float heading_ = 0;
    std::array<Man, kMen> men_;
    float brightness_ = 1, fade_rate_ = 0;
    int cameras_shown_ = 0; // +0x74: which of the three camera sequences comes next
    int camera_sequence_ = -1;
    float camera_time_ = 0;
    CameraShot shot_, shot_before_;
    bool shot_on_ = false, shot_before_on_ = false;
};

} // namespace gt4r::race
