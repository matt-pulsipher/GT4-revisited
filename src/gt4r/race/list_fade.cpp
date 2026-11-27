#include "gt4r/race/list_fade.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

void ListFade::update(const std::vector<u16>* list, const gt3r::render::Vec3& eye, float dt) {
    const gt3r::render::Vec3 moved = eye - eye_;
    const bool cut = !started_ || all_ || std::sqrt(dot(moved, moved)) >= kCut;
    started_ = true;
    eye_ = eye;
    solid_.clear();
    faded_.resize(kLevels - 1);
    for (size_t k = 0; k < faded_.size(); ++k) faded_[k].weight = float(k + 1) / kLevels, faded_[k].nodes.clear();
    if (!list) {
        all_ = true;
        list_ = nullptr;
        weight_.clear(), listed_.clear();
        return;
    }
    all_ = false;
    size_t top = 0;
    for (u16 n : *list) top = std::max(top, size_t(n) + 1);
    if (weight_.size() < top) weight_.resize(top, 0.0f), listed_.resize(top, 0);
    // The models that left the list are gone at once.
    if (list_ && list_ != list) {
        for (u16 n : *list) listed_[n] = 1;
        for (u16 n : *list_)
            if (n < listed_.size() && !listed_[n]) weight_[n] = 0;
        for (u16 n : *list) listed_[n] = 0;
    }
    list_ = list;
    const float step = cut ? 1.0f : std::max(dt, 0.0f) / kSeconds;
    for (u16 n : *list) {
        float& w = weight_[n];
        w = std::min(1.0f, w + step);
        const int level = std::clamp(int(w * kLevels + 0.5f), 0, kLevels);
        if (level == kLevels) solid_.push_back(n);
        else if (level > 0) faded_[size_t(level - 1)].nodes.push_back(n);
    }
}

} // namespace gt4r::race
