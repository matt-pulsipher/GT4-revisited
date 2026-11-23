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
    float fov_min = 1, fov_max = 100;
    float dissolve = 0; // +0x18: seconds of the dissolve when the shot ends

    bool car_frame() const { return (kind & 3) == 1; }
    bool looks_at_car() const { return (kind & 0xC) != 0; }
    bool on_time() const { return (kind & 0x30) == 0x10; }
    bool lens_is_width() const { return (kind & 0x100) != 0; }
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
    // A script distance for a car `v` metres along the lap on variant `k`, wrapped into the script.
    float distance(int variant, float v) const;
    // The record that started last at or before d when it still covers d (start <= d < end); null
    // otherwise. The records are in the order of their starts.
    const ReplayRecord* find(float d) const;
};

// The camera that plays a script on a car.
class ReplayCamera {
public:
    void reset() { record_ = nullptr; }
    // The shot for the car now. False when the script has no shot there (`out` untouched).
    bool pose(const ReplayScript& script, const CarState& car, const gt3r::render::Mat4& body, ViewPose& out);
    const ReplayRecord* record() const { return record_; }

private:
    const ReplayRecord* record_ = nullptr;
};

// A shot at its progress t (0..1): for a car shot `car_body` is the car's body frame; without one
// the shot's own curves are used throughout. False when it has no view there.
bool shot_pose(const ReplayRecord& r, float t, const gt3r::render::Mat4* car_body, ViewPose& out);

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
    size_t shot() const { return index_; }

private:
    const ReplayScript* script_ = nullptr;
    size_t index_ = 0;
    int frames_ = 0, dissolve_ = 0, dissolve_length_ = 0;
};

} // namespace gt4r::race
