#pragma once

// The car-selection stage's reflection picture (menu/select/env.mdl): what the car page's car reflects. The program draws the
// set's models into three faces and warps them into a sphere map each picture (0x44EB10); the camera and the place stand
// still, so here each model's map is made once and the maps are added by their weights. docs/formats/menu.md "Cars".

#include "gt4r/base/types.h"

#include "scene/model.h"

#include <array>
#include <vector>

namespace gt4r::menu {

// A sphere map of size x size, RGB 0..255: texel (s, t) the colour seen along the direction the reflection kernel looks up
// there (s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m, m = |R + (0, 0, 1)|), R turned up 15 degrees about the view's x axis (the
// program's environment camera, 0x44EF18) and into the world by the view's rotation (rows right, up, back). Black outside
// 0.9 of the disc's radius, which the program's faces do not reach.
std::vector<std::array<float, 3>> sphere_map(const gt3r::scene::Model& model, const std::array<float, 3>& eye,
                                             const std::array<std::array<float, 3>, 3>& view_rows, int size);

// The direction (in the world) of a texel, and whether it lies in the picture's disc.
std::array<float, 3> sphere_map_direction(float s, float t, const std::array<std::array<float, 3>, 3>& view_rows, bool& inside);

} // namespace gt4r::menu
