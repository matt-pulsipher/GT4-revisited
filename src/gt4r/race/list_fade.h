#pragma once

// A view's scenery models over time, never more than Gran Turismo 4 draws. The program draws slot 1's models straight from
// the visibility list of the camera's place each picture (RacePS2Base::vf110 0x3403B8 -> 0x396E10: the list from 0x4580F0,
// then 0x396F00 draws its models in its order, nothing kept from one picture to the next): a model that joins the list
// appears at once and one that leaves it is gone at once. Ours keeps the list's choice in every picture: a model that
// leaves is gone at once, as in the game; a model that joins comes in dithered over kSeconds (DrawItem::fade), so the
// picture never shows a model the list leaves out and pops in less. The first picture and a camera cut (the eye moving
// kCut metres or more in a picture) take the list whole.

#include "base/bytes.h"
#include "render/math3d.h"

#include <vector>

namespace gt4r::race {

using gt3r::u16;
using gt3r::u8;

class ListFade {
public:
    static constexpr float kSeconds = 0.6f;
    static constexpr float kCut = 20.0f;
    // Steps towards `list` (the view's visibility list for `eye`; null: no list, every model, no fading) by `dt` seconds.
    // `list` must stay valid until the next update.
    void update(const std::vector<u16>* list, const gt3r::render::Vec3& eye, float dt);
    // The models drawn whole, in the list's order; null = every model.
    const std::vector<u16>* solid() const { return all_ ? nullptr : &solid_; }
    // The models coming in, in fifteen groups by weight (1/16 .. 15/16, some empty), each in the list's order. Every one
    // is in the list.
    static constexpr int kLevels = 16;
    struct Group {
        float weight = 0;
        std::vector<u16> nodes;
    };
    const std::vector<Group>& faded() const { return faded_; }
    // Back to the first picture's state (a course or race change).
    void reset() { *this = ListFade{}; }

private:
    std::vector<float> weight_; // per model: 0 out of the list
    std::vector<u8> listed_;    // scratch: per model, in the current list
    const std::vector<u16>* list_ = nullptr;
    gt3r::render::Vec3 eye_;
    bool started_ = false, all_ = true;
    std::vector<u16> solid_;
    std::vector<Group> faded_;
};

} // namespace gt4r::race
