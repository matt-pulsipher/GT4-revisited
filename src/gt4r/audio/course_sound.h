#pragma once

// A course's own sounds in a race (docs/formats/sound.md "A course's sounds"): the ambience of
// its "CRSS" section (five lists of emitters: the air, the city, the crowds, the cheering and the
// sea), the two tunnels' loops and the reverb's depth by the road under the camera.
//
//   air, city       not placed: both sides at the level of the list's first emitter's first point
//   crowds          placed: each point's weight, past 32 m x 32 / distance, panned, with Doppler
//   cheer, sea      placed the same, without Doppler
//
// An emitter with two gates sounds by the cars between them (1 for the first such car, 0.8 for
// the next, 0.64 ..., at most 1), its gain rising by 1/120 and falling by 1/180 a step; one
// without gates sounds at once. Under the camera the road's flags & 0x18 choose: 0x08 the first
// tunnel's pair and a reverb of 0.5; 0x10 the second's and 0.25; 0x18 a reverb of 0.25; else
// 0.125. The reverb moves toward its target by 1/36 a step. Heard from more than one camera,
// both sides take the larger level, the pitch is 1 and the reverb 0.125.

#include "formats/sound/crss.h"
#include "gt4r/race/simulation.h"

#include <array>
#include <functional>
#include <vector>

namespace gt4r::audio {

class CourseSound {
public:
    enum Channel : int { Air = 0, City, Crowds, Cheer, Sea, kChannels };
    // The effect each list plays (sound/gt4se.inf). Prologue's (0x3A2728): air, city, none, crowds, sea.
    static const char* effect_name(int channel, bool prologue = false);
    // Prologue's rules (0x4104C8): a list of air or city that holds an emitter plays at 1, the placed lists (crowds is the
    // fourth) each carry their own pitch, a gated emitter's gain moves by 1/60 a step.
    void set_prologue(bool on) { prologue_ = on; }

    struct Listener {
        race::Vec3 position, velocity, right{1, 0, 0};
        u8 road_flags = 0; // the flags byte of the road's triangle under the camera
    };
    struct Out {
        float left = 0, right = 0, pitch = 1;
    };

    // `distance(p, &d)`: the course distance of a point, false off the road; `length`: a lap.
    void load(const gt3r::sound::CrssData& data, const std::function<bool(const race::Vec3&, float*)>& distance, float length);
    void clear();
    bool empty() const;

    // One step: the cameras and each car's course distance (in grid order).
    void update(const std::vector<Listener>& listeners, const std::vector<float>& car_distances);

    const Out& channel(int c) const { return out_[size_t(c)]; }
    float tunnel1() const { return tunnel1_; }
    float tunnel2() const { return tunnel2_; }
    float reverb() const { return reverb_; }        // the depth now, 0..1
    float reverb_target() const { return target_; }
    size_t emitters(int c) const { return lists_[size_t(c)].size(); }
    float emitter_gain(int c, size_t i) const { return lists_[size_t(c)][i].gain; }
    // An emitter's gates as course distances (equal: not gated) and its points.
    std::pair<float, float> emitter_gates(int c, size_t i) const { return {lists_[size_t(c)][i].lo, lists_[size_t(c)][i].hi}; }
    const gt3r::sound::CrssEmitter& emitter(int c, size_t i) const { return lists_[size_t(c)][i].src; }

private:
    struct Emitter {
        gt3r::sound::CrssEmitter src;
        float lo = 0, hi = 0; // the gates' course distances (equal: not gated)
        float gain = 0;
    };
    std::array<std::vector<Emitter>, kChannels> lists_;
    std::array<Out, kChannels> out_{};
    bool prologue_ = false;
    float tunnel1_ = 0, tunnel2_ = 0;
    float reverb_ = 0.125f, target_ = 0.125f;
};

} // namespace gt4r::audio
