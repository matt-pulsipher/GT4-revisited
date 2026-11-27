#pragma once

// A mod car's pictures on the game's pages (formats/db/mod_cars.h ModCars::picture): the mod's own PNG made the size of the
// body car's picture of the disc it stands for, else that picture.

#include "formats/tex1.h"

#include <functional>
#include <optional>
#include <string>

namespace gt4r {

class App;

// The picture for `path` ("piece/car_mini/<label>.img") of a mod car; none for any other path. `disc` decodes a picture of the
// disc by its path.
std::optional<gt3r::Tex1::Image> mod_car_picture(App& app, const std::string& path,
                                                 const std::function<std::optional<gt3r::Tex1::Image>(const std::string&)>& disc);

} // namespace gt4r
