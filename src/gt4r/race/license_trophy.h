#pragma once

// The trophy of a passed license test (ResultLicense: race/trophy.mdl drawn in the results movie's
// "TropyPanel" picture, 0x3E0FF8): its place, turns and fade as the program moves them
// (0x3E18D0 sets them up, 0x3E1A80 steps them). docs/formats/strobe.md "The license test's results".

#include "render/math3d.h"

namespace gt4r::race {

struct LicenseTrophy {
    // A value that eases to its target: value += ((target - value) x gain + rate) x dt (0x3E2058).
    struct Channel {
        float value = 0, target = 0, gain = 0, rate = 0;
        void step(float dt) { value += ((target - value) * gain + rate) * dt; }
    };
    Channel place[3]; // x, y, z (the camera at the origin looking down -z)
    Channel turn[3];  // about x, y, z, degrees; each turns at its spin's value
    Channel spin[3];
    Channel fade;     // the picture's opacity

    // The program's kind (ResultLicense +0x570): 1, 2 gold, 3 silver, 4, 5 bronze; 0 none.
    void start(int kind);
    void step(float dt);
    // The model's matrix: translate(place), then turned about y, x, z.
    gt3r::render::Mat4 matrix() const;

    // The kind for a grade (0 gold, 1 silver, 2 bronze, 3 failed: 0). ❓ The program reads its kind
    // from the result (+0x88); which of 1 / 2 and 4 / 5 a run gets is not read.
    static int kind_for(int prize_level);
    // The trophy model's material set for a kind (program table 0x6A38F0: 0, 0, 0, 1, 2, 2), at most
    // `sets` - 1.
    static int material_for(int kind, int sets);
    // The lens the trophy is drawn with (0x49AC00): 50 degrees high, 0.1 .. 100 m, the 640 x 448
    // picture's aspect (the whole picture is then squeezed into the panel's box).
    static constexpr float kFovDegrees = 50, kNear = 0.1f, kFar = 100, kAspect = 640.0f / 448.0f;
};

} // namespace gt4r::race
