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
    return correction_grade(c.gamma, 0, c.contrast, c.saturation, 255);
}

std::array<std::array<u8, 3>, 256> correction_levels(const float gamma[3], float black, float white, float most) {
    std::array<std::array<u8, 3>, 256> out{};
    const float inv = white == black ? 0.0f : 1.0f / (white - black);
    float e[3];
    for (int c = 0; c < 3; ++c) e[c] = 1.0f / std::max(gamma[c], 0.0f);
    most = std::clamp(most, 0.0f, 255.0f);
    for (int i = 0; i < 256; ++i) {
        const float x = float(i) * (1.0f / 255.0f);
        if (!(black < x)) continue; // (0, 0, 0)
        const float ln_t = std::log(std::min((x - black) * inv, 1.0f));
        for (int c = 0; c < 3; ++c)
            out[size_t(i)][size_t(c)] = u8(int(std::clamp(std::exp(e[c] * ln_t) * 255.0f, 0.0f, most)));
    }
    return out;
}

gt3r::render::DrawOptions::PostGrade correction_grade(const float gamma[3], float black, float white, float saturation, float most) {
    gt3r::render::DrawOptions::PostGrade g;
    // (0x3EFD08: nothing to do when every gamma, the white level and the saturation are 1 and the black level 0; `most` under
    // 255 still holds the levels down.)
    g.on = black != 0 || white != 1 || saturation != 1 || most < 255;
    for (int c = 0; c < 3; ++c) g.on |= gamma[c] != 1;
    g.saturation = std::clamp(saturation, -1.0f, 1.0f);
    g.black = black;
    g.white = white;
    // The grade's own tables (DrawOptions::PostGrade::own_table): GT4's levels, in place of the engine's curve.
    g.own_table = true;
    const std::array<std::array<u8, 3>, 256> levels = correction_levels(gamma, black, white, most);
    for (size_t i = 0; i < 256; ++i)
        for (size_t c = 0; c < 3; ++c) g.table[i][c] = levels[i][c];
    return g;
}

} // namespace gt4r::race
