#include "gt4r/race/picture_settings.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

PictureSettings::Correction PictureSettings::correction() const {
    Correction c;
    const float b = float(std::clamp(v[0], -5, 5)) / 10.0f;
    const float k = float(std::clamp(v[1], -5, 5)) * 0.6f;
    const float s = (float(std::clamp(v[2], -5, 5)) + 10.0f) / 10.0f;
    const float t = 5500.0f + 200.0f * float(std::clamp(v[3], -5, 5));
    const float gain = b != 0 ? std::pow(2.0f, b) : 1.0f;
    const float shift = (t - 5500.0f) * 0.2f / 2000.0f;
    c.gamma[0] = gain * (1 - shift), c.gamma[1] = gain, c.gamma[2] = gain * (1 + shift);
    c.contrast = 1 - k * 0.1f;
    c.saturation = s;
    return c;
}

gt3r::render::DrawOptions::PostGrade PictureSettings::grade() const {
    const Correction c = correction();
    gt3r::render::DrawOptions::PostGrade g;
    g.black = 0;
    g.white = std::max(c.contrast, 0.05f);
    // out = t^(1 / gamma), as the pass's curve through the same middle.
    for (int i = 0; i < 3; ++i) g.curve[i] = 2 * std::pow(0.5f, 1 / std::max(c.gamma[i], 0.05f)) - 0.5f;
    g.saturation = c.saturation;
    g.on = std::fabs(g.white - 1) > 1e-4f || std::fabs(g.saturation - 1) > 1e-4f;
    for (float k : g.curve) g.on |= std::fabs(k - 0.5f) > 1e-4f;
    return g;
}

} // namespace gt4r::race
