#pragma once

// The light a car stands in (docs/formats/course.md "The light on a car"): the course's car lights
// by the road's two light values under it, or, on the courses whose road names lamps, the
// strongest four of the lamps near it.

#include "gt4r/formats/course/runway.h"
#include "race/sun_glare.h"
#include "render/view3d.h"

#include <vector>

namespace gt4r::race {

struct CarLight {
    struct Cast { // a shadow a lamp throws
        gt3r::render::Vec3 shear;
        float alpha = 1;
    };
    bool own = false;                // `lights`, not the course's scaled
    bool lamps = false;              // the lamps': `casts` in place of the course's shadow
    float scale[3] = {1, 1, 1};      // of the course's lights: ambient, diffuse, specular
    float shadow = 1;                // on the cast shadow's alpha (1: in the open)
    gt3r::render::ItemLights lights; // `own`
    std::vector<Cast> casts;         // `lamps`
};
// At `at` (a car's place), the road taken from `y_max` down.
CarLight car_light(const course::Runway& road, const gt3r::race::SunParams& env, const gt3r::render::Vec3& at, float y_max);

} // namespace gt4r::race
