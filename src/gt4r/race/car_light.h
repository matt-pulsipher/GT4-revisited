#pragma once

// The light a car stands in (docs/formats/course.md "The light on a car"): the course's car lights
// by the road's two light values under it, or, on the courses whose road names lamps, the
// strongest four of the lamps near it.

#include "gt4r/formats/course/runway.h"
#include "race/sun_glare.h"
#include "render/view3d.h"

#include <vector>

namespace gt4r::race {

class CarLights {
public:
    // `section`: the course's road (RNW4, slot 55).
    void load(ByteSpan section);
    bool lamps() const { return !lamps_.empty(); }

    struct Cast { // a shadow a light throws
        gt3r::render::Vec3 shear;
        float alpha = 1; // of the shadow's
    };
    struct Light {
        bool own = false;                  // `lights`, not the course's scaled
        bool lamps = false;                // the lamps': `casts` in place of the course's shadow
        float scale[3] = {1, 1, 1};        // ambient, diffuse, specular
        float shadow = 1;                  // on the cast shadow's alpha (1: in full light)
        gt3r::render::ItemLights lights;   // `own`
        std::vector<Cast> casts;           // `lamps`
    };
    // At `at` (a car's place), the road taken from `y_max` down.
    Light at(const course::Runway& road, const gt3r::race::SunParams& env, const gt3r::render::Vec3& at, float y_max) const;

private:
    struct Lamp {
        u32 type = 0;
        gt3r::render::Vec3 axis, pos;
        float kind = 0; // 0: `pos` is a direction
    };
    std::vector<Lamp> lamps_;
    std::vector<u16> near_; // lamp numbers, `per_place_` from where a triangle's first vertex names
    size_t per_place_ = 0;
};

} // namespace gt4r::race
