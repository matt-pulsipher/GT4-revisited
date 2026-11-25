#pragma once

// Dive, the replay's playback mode cut to the music (the camera manager's mode 8, 0x376AF0): a director that cuts at the
// piece's markers, chooses the car and the camera at random (0x376638), and now and then colors the picture (GammaEffector
// 0x4004A8) and lets it trail. docs/formats/camera.md "Dive".

#include "gt4r/race/drive_camera.h"
#include "gt4r/race/replay_script.h"
#include "render/view3d.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace gt4r::race {

// The program's random numbers' role here: draws in 0 .. 1.
class Draws {
public:
    explicit Draws(u32 seed = 0x2545F491u) : seed_(seed ? seed : 1) {}
    float next();
    int below(int n);

private:
    u32 seed_;
};

// A cubic from 0 to 1 with the slopes b / a at its start and d / c at its end (0x36F710, 0x36F778).
struct EaseCurve {
    float k3 = 0, k2 = 0, k1 = 1;
    EaseCurve() = default;
    EaseCurve(float a, float b, float c, float d);
    float at(float t) const;
};

// When the director cuts (DiveController 0x400A18): at the music's markers while the next is within 8 s, else every 8 s.
class DiveClock {
public:
    // A step: the seconds to the piece's next marker (none: no piece) and whether one was passed. True: a cut.
    bool step(std::optional<float> next, bool passed);
    float length() const { return timer_; } // how long the shot to come is
    void reset() { timer_ = 8; }

private:
    float timer_ = 8;
};

// A piece's markers (bgm's .adm, track 0), the seconds of each and the piece's length; played in a loop.
struct MusicMarkers {
    std::vector<float> at;
    float length = 0;
    // From `before` to `now` seconds of playing (0x33D170, which looks 1 / 30 s ahead).
    std::optional<float> next(float now) const;
    bool passed(float before, float now) const;
};

class DiveDirector {
public:
    // The color presets (0x622700, 0x6228F8) and the cameras' shares (0x6210A8 .. 0x621130) are the program's. False for another
    // program: no colors, the cameras by equal shares.
    bool load(ByteSpan program, u32 load_address);

    struct Car {
        const CarState* state = nullptr;
        gt3r::render::Mat4 body;
        gt3r::render::Vec3 box_min, box_max;
        std::span<const CameraMount> mounts;
    };
    // What the director asks of its caller.
    struct World {
        size_t cars = 0;
        std::function<Car(size_t)> car;
        const ReplayScript* script = nullptr;
        float lap_length = 0;
        std::function<bool(const gt3r::render::Vec3& at, float y_max, float& y)> ground;
        std::function<bool(const gt3r::render::Vec3& a, const gt3r::render::Vec3& b)> blocked;
    };
    void reset(u32 seed, size_t car);
    // One step: `cut` the clock's (or asked for), `length` the shot's if it is one. The pose of this step.
    bool step(const World& w, bool cut, float length, ViewPose& out);
    size_t car() const { return car_; }
    bool is_cut() const { return cut_; }
    // The trail's weight (a circled car behind a wall trails too: CameraRound's vf27).
    float trail() const { return (kind_ == Kind::Round || kind_ == Kind::Above) && wall_ > 0 ? wall_ : trail_; }
    gt3r::render::DrawOptions::PostGrade grade(const gt3r::render::DrawOptions::PostGrade& plain) const;
    enum class Kind { None, OnBoard, Round, Point, Above };
    Kind kind() const { return kind_; }

private:
    struct Point;
    struct Round;
    bool begin_shot(const World& w, float length);
    bool begin_point(const World& w, float length);
    void begin_round(const World& w, float length, int way);
    void begin_on_board(const World& w, float length);
    bool point_pose(const World& w, ViewPose& out);
    bool round_pose(const World& w, ViewPose& out);
    void begin_above();
    bool above_pose(const World& w, ViewPose& out);
    bool on_board_pose(const World& w, ViewPose& out);
    gt3r::render::Vec3 clear_eye(const World& w, const gt3r::render::Vec3& target, const gt3r::render::Vec3& eye);
    void color(bool cut);

    Draws r_;
    Kind kind_ = Kind::None;
    size_t car_ = 0;
    int frames_ = 0, since_car_ = 0;
    bool cut_ = false, first_ = true;
    float length_ = 8, trail_ = 0;

    // A roll that moves from one angle to another (0x36F948 .. 0x36F980).
    struct Roll {
        EaseCurve ease;
        float from = 0, to = 0, duration = 0.5f;
        float at(float t) const;
    };
    Roll roll_;

    // The roadside point's shot (CameraPoint 0x37E4D8, 0x37EA78).
    ReplayPoint point_;
    EaseCurve rise_;
    float rise_from_ = 0, rise_to_ = 0, let_go_ = 1, let_gone_ = 1.05f, pull_from_ = 1, opening_ = 8;
    bool zoom_in_ = false, flat_lens_ = false;
    float spin_ = 0, middle_ = 0, middle_lens_ = 60;
    std::array<std::array<float, 2>, 3> lens_keys_{};
    int lens_key_count_ = 2;
    AimDamper yaw_, pitch_;
    CameraSpring lead_{20, 20, 2, -120, 120}, gain_yaw_{50, 20, 1.5f, -100, 100}, gain_pitch_{50, 20, 1.5f, -100, 100}, pull_{15, 15, 2, -20, 20};
    float lens_ = 60, lens_rate_ = 0, spin_rate_ = 0;

    // The shot circling the car (CameraRound 0x380E48, 0x3814A0).
    int way_ = 0;
    bool eased_ = false;
    float still_ = 0, start_angle_ = 0, sweep_ = 0, base_angle_ = 0, round_lens_ = 45, roll_from_ = 0, roll_to_ = 0;
    EaseCurve round_ease_;
    std::array<float, 4> ramp_{};
    gt3r::render::Vec3 look_from_, look_to_, eye_from_, eye_to_;
    float noise_[2][3] = {}; // value, target, rate
    float wall_ = 0;

    // On board.
    int mount_ = 0;
    bool back_ = false, rolls_ = false, wobble_ = false;
    float board_lens_ = 0;
    DriveCamera board_;

    // The picture's colors: the state, the values now and those they go to.
    int color_state_ = 0;
    bool color_moves_ = false, color_on_ = false;
    std::array<float, 6> color_now_{1, 1, 1, 0, 1, 1}, color_to_{1, 1, 1, 0, 1, 1};
    struct Tables {
        std::vector<std::array<float, 6>> tints, hues;
        std::array<float, 3> points{0.7f, 0.95f, 2};     // which of the best points
        std::array<std::array<float, 9>, 3> ways{};      // the cameras of a short, a middling and a long shot
    } tables_;
};

} // namespace gt4r::race
