#pragma once

// How a mod car's body differs from its body car's model (formats/db/mod_cars.h): the borrowed model stretched to the car's
// dimensions ([car] fit_body), or a body of the mod's own (binary glTF, [model]) drawn in its place in that model, with the
// body car's wheels, lamps, shadows, driver and camera places (CarVisual::Options::mod_body).

#include "gt4r/base/types.h"
#include "formats/model/car_model.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

namespace gt4r::db {
class SpecDb;
}

namespace gt4r::race {

struct ModCarBody {
    std::optional<gt3r::BodyFit> fit;             // the borrowed body (and its shadow, lamps, camera places) stretched
    std::shared_ptr<const Bytes> glb;             // a body of its own: the model's body at every level of detail
    std::string glb_path;
    std::optional<std::array<float, 3>> paint;    // its paint materials' color (0..1): the paint chosen of [paints]
    float lower = 0;                              // m: [model] lower_mm
    // What tells two bodies apart (the race keeps one model of each).
    std::string key() const;
};

// A mod car's body for the menus' model (`menu`) or a race's at a time of day (0 day, 1 evening, 2 night), in paint `paint`
// (the menus' number); none for a disc car or a mod car drawn as its body car is. Reads the .glb.
std::optional<ModCarBody> mod_car_body(db::SpecDb& db, const std::string& label, bool menu, int time_of_day, int paint);

} // namespace gt4r::race
