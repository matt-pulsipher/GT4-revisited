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
    c.gain[0] = gain * (1 - shift), c.gain[1] = gain, c.gain[2] = gain * (1 + shift);
    c.contrast = 1 - k * 0.1f;
    c.saturation = s;
    return c;
}

gt3r::render::DrawOptions::PostGrade PictureSettings::grade() const {
    const Correction c = correction();
    gt3r::render::DrawOptions::PostGrade g;
    // out = 0.5 + (in x gain - 0.5) / term = (in - black) / (white - black).
    const float gain = c.gain[1], term = std::max(c.contrast, 0.05f);
    g.black = (0.5f - 0.5f * term) / gain;
    g.white = g.black + term / gain;
    g.saturation = c.saturation;
    g.on = std::fabs(g.black) > 1e-4f || std::fabs(g.white - 1) > 1e-4f || std::fabs(g.saturation - 1) > 1e-4f;
    return g;
}

} // namespace gt4r::race
