#pragma once

// A course's replay cameras ("REP4", course slot 44): shots by distance along the course, each
// a camera path, what it looks at and its lens. docs/formats/camera.md "Replay cameras".

#include "gt4r/base/types.h"
#include "gt4r/race/drive_camera.h"

#include <array>
#include <vector>

namespace gt4r::race {

// Keys of (t, value), t ascending in 0..1; straight lines between them.
struct ReplayCurve {
    std::vector<std::array<float, 2>> keys;
    bool empty() const { return keys.empty(); }
    float at(float t) const;
};

struct ReplayRecord {
    u32 kind = 0;
    float start = 0, end = 0; // distances along the script; seconds for a shot that runs on time
    float aperture = 8;
    std::array<ReplayCurve, 3> eye, look;
    ReplayCurve lens;                  // degrees, or with kind 0x100 the picture's width at the target (metres)
    std::array<ReplayCurve, 2> offset; // where the target sits in the picture, in half fields of view
    ReplayCurve roll;                  // degrees
    std::array<ReplayCurve, 2> follow; // +0x64 / +0x68: how closely the aim follows the car, pitch / yaw (over 1: exactly)
    float fov_min = 1, fov_max = 100;
    float dissolve = 0; // +0x18: seconds of the dissolve when the shot ends

    bool car_frame() const { return (kind & 3) == 1; }
    bool looks_at_car() const { return (kind & 0xC) != 0; }
    bool on_time() const { return (kind & 0x30) == 0x10; }
    bool lens_is_width() const { return (kind & 0x100) != 0; }
    bool full_roll() const { return (kind & 0x400) != 0; }
};

struct ReplayScript {
    float lap_length = 0;
    float end = 0;       // the largest record end
    u32 variants = 0;    // as the header states them
    std::vector<ReplayRecord> records; // by start

    static bool is_script(ByteSpan d) { return d.size() >= 0x40 && gt3r::has_magic(d, "REP4"); }
    // Throws FormatError.
    static ReplayScript parse(ByteSpan section);

    // The script's period: a distance past the last record wraps by it.
    float period() const;
    // The lap variants the period holds.
    int variant_count() const;
    // The script distance for a car `v` metres along the lap after `crossings` crossings of the line
    // (0 before the first; the camera's block +0x34 with +0x3C = 0): crossings x L + v, a distance at or
    // past the script's end brought back by whole periods (0x3DDFF0 -> 0x3DDF18; a negative one kept).
    float distance(int crossings, float v) const;
    // The record that started last at or before d when it still covers d (start <= d < end); null
    // otherwise. The records are in the order of their starts.
    const ReplayRecord* find(float d) const;
    // The program's choice (0x3DDFF0): the record `find` gives; else, when that search ran past the
    // last record, the first record from record 0 that covers d less the period.
    const ReplayRecord* choose(float d) const;
};

// A camera spring (0x385098): the wanted value moves at most `slew` a second (0: free);
// v += (k (want - x) - c v) / 60, v kept within [vmin, vmax], x += v / 60.
struct CameraSpring {
    float k = 50, c = 20, slew = 0, vmin = -1e9f, vmax = 1e9f;
    float x = 0, v = 0, want = 0;
    void reset(float value) { x = want = value, v = 0; }
    float step(float target);
};

// The replay camera's aim damper on an angle (degrees; 0x384E58 reset, 0x384EE8 step): it leads
// the target by five frames of its motion, follows it by the shot's follow gain, with a slow
// random hand (the noise) when the gain is under 1.
class AimDamper {
public:
    void reset(float target, u32 seed);
    float step(float target, float follow);
    float value() const { return out_; }

private:
    float rnd();
    float out_ = 0, v_ = 0, prev_ = 0;
    bool has_prev_ = false;
    float noise_x_ = 0, noise_v_ = 0, noise_target_ = 0;
    int frame_ = 0;
    u32 seed_ = 1;
};

// The camera that plays a script on a car, a step at a time (the program's designer camera,
// 0x37C138): a record found by the car's distance holds until the car leaves it; at a cut the shot
// starts exactly on its target; then for a car shot the yaw and pitch go through the aim dampers,
// the lens, the target's offsets and the roll through springs.
class ReplayCamera {
public:
    void reset() { record_ = nullptr; }
    // The shot for the car this step (call once a step). False when the script has no shot there
    // (`out` untouched).
    bool pose(const ReplayScript& script, const CarState& car, const gt3r::render::Mat4& body, ViewPose& out);
    const ReplayRecord* record() const { return record_; }
    // The step was a cut from one record to another: the ended record's dissolve, seconds (0 none).
    float cut_dissolve() const { return cut_dissolve_; }

private:
    const ReplayRecord* record_ = nullptr;
    bool cut_ = true;
    float cut_dissolve_ = 0;
    float roll_scale_ = 1;
    AimDamper yaw_, pitch_;
    CameraSpring lens_{100, 20, 0, -150, 100}, across_{70, 20, 2, -150, 150}, up_{70, 20, 2, -150, 150}, roll_{300, 40, 0, -150, 150};
    CameraSpring follow_[2] = {{50, 20, 1.5f, -100, 100}, {50, 20, 1.5f, -100, 100}};
    u32 seed_ = 0x2545F491u;
};

// A shot at its progress t (0..1): for a car shot `car_body` is the car's body frame; without one
// the shot's own curves are used throughout. `aimed`: the target's place in the picture (its offsets
// by half the lens angle each way) and the roll applied; else the view looks straight at the
// target. False when it has no view there.
bool shot_pose(const ReplayRecord& r, float t, const gt3r::render::Mat4* car_body, ViewPose& out, bool aimed = true);

// The scenic shots behind a race's menus (the course's slot 48, the program's camera mode 5): the
// script's shots on time in the file's order, each for its `end` seconds, then the next (after the
// last the first), cut with a dissolve of the ended shot's own seconds.
class ScenicCamera {
public:
    void reset(const ReplayScript* script);
    void step();
    bool pose(ViewPose& out) const;
    // The dissolve's part left (1 at the cut, 0 none).
    float dissolve() const;
    // The seconds of the dissolve the last cut began.
    float dissolve_seconds() const { return float(dissolve_length_) / 60; }
    size_t shot() const { return index_; }

private:
    const ReplayScript* script_ = nullptr;
    size_t index_ = 0;
    int frames_ = 0, dissolve_ = 0, dissolve_length_ = 0;
};

} // namespace gt4r::race
