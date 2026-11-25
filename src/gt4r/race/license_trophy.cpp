#include "gt4r/race/license_trophy.h"

#include <algorithm>

namespace gt4r::race {

void LicenseTrophy::start(int kind) {
    *this = LicenseTrophy{};
    place[2] = {-0.5f, -2.5f, 0.5f, 0}; // comes away from the camera
    spin[1] = {0, 60, 0.5f, 0};          // turns about y, up to 60 degrees a second
    fade = {0, 1, 1, 0};
    if (kind <= 0) return;
    if (kind < 3) {
        turn[1].value = -22.5f;
        place[2].gain = 0.35f;
    } else if (kind == 3) {
        turn[1].value = 180;
        spin[1] = {1000, 60, 1.2f, 0}; // spun fast, slowing
    } else {
        turn[1].value = 270;
        turn[0] = {-180, 0, 1.5f, 0}; // turned over, righting itself
        spin[1].gain = 3;
    }
}

void LicenseTrophy::step(float dt) {
    for (int k = 0; k < 3; ++k) {
        spin[k].step(dt);
        turn[k].rate = spin[k].value;
        place[k].step(dt);
        turn[k].step(dt);
    }
    fade.step(dt);
}

gt3r::render::Mat4 LicenseTrophy::matrix() const {
    constexpr float kDeg = 3.14159265358979f / 180;
    return gt3r::render::translate({place[0].value, place[1].value, place[2].value}) * gt3r::render::rotate_y(turn[1].value * kDeg) *
           gt3r::render::rotate_x(turn[0].value * kDeg) * gt3r::render::rotate_z(turn[2].value * kDeg);
}

int LicenseTrophy::kind_for(int prize_level) { return prize_level == 0 ? 1 : prize_level == 1 ? 3 : prize_level == 2 ? 4 : 0; }

int LicenseTrophy::material_for(int kind, int sets) {
    static const int kTable[6] = {0, 0, 0, 1, 2, 2};
    const int m = kind >= 0 && kind < 6 ? kTable[kind] : 0;
    return std::clamp(m, 0, std::max(sets - 1, 0));
}

} // namespace gt4r::race
