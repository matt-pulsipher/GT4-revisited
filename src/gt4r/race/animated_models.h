#pragma once

// A course's animated models: the flags, banners and people whose draw programs pick a key shape
// and a blend weight from registers that the set's script sets (docs/formats/course.md "Scripts
// of the model sets"). Each such model is decoded once per key shape, at both ends of the blend;
// every frame the script runs and the shown shape is the blend the registers ask for.
//
// Models whose programs move or turn by registers (the walking people of one course) are not
// taken: they stay still in the scenery.

#include "formats/model/gtm1.h"
#include "gt4r/formats/model/model_script.h"
#include "gt4r/race/simulation.h"
#include "scene/model.h"

#include <span>
#include <vector>

namespace gt4r::race {

class AnimatedModels {
public:
    // `set`: the model set (a course's slot 1); `decode`: the options its still models are decoded
    // with; `color_scale`: the factor on its stored colors.
    AnimatedModels(ByteSpan set, float color_scale, const gt3r::gtm1::Options& decode);
    AnimatedModels() = default;

    bool empty() const { return models_.empty(); }
    // The set's models that are animated here, ascending: to leave out of the still scenery.
    std::vector<u32> models() const;
    // A model holding the pictures the animated models draw with (no shapes): theirs are indices
    // into its list.
    const gt3r::scene::Model& textures() const { return textures_; }

    // The start of a race: time 0, the set-up scripts run.
    void restart();
    // A step of the race: the time moves on and the per-step scripts run. `cars`: what the
    // people of a course ask about (the nearest car, how soon one arrives).
    void tick(float seconds, std::span<const CarState> cars);
    // The models as they are now, a mesh each (Mesh::node its number in the set): the draw
    // scripts run, then each model's key shape blended by its weight.
    std::vector<gt3r::scene::Mesh> meshes(std::span<const CarState> cars);

    const mdls::ScriptState& state() const { return state_; }

private:
    struct Key {
        gt3r::scene::Mesh first, second; // the ends of the blend (the same shape when it has one end)
    };
    struct Animated {
        u32 model = 0;
        int switch_cell = -1, weight_cell = -1; // the registers its program reads; -1: none
        std::vector<Key> keys;                  // one per case of the switch (one without a switch)
    };
    float ask(u32 id, std::span<const u32> cells, std::span<const CarState> cars) const;

    mdls::Script script_;
    mdls::ScriptState state_;
    std::vector<Animated> models_;
    gt3r::scene::Model textures_;
    int time_cell_ = -1, step_cell_ = -1;
    double time_ = 0;
};

} // namespace gt4r::race
