// What players see, by Gran Turismo 4's (and Prologue's) own rules where they differ from the shared engine's.

#include "test_framework.h"

#include <cmath>
#include <cstring>

#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/display_mode_prologue.h"
#include "gt4r/race/glow_filter.h"
#include "gt4r/race/picture_settings.h"

using gt3r::race::HudMode;
using gt3r::race::HudPlay;
using gt3r::race::HudRun;
using gt4r::race::prologue_display_mode;
using namespace gt4r;

TEST(prologue_display_mode_by_its_race_classes) {
    CHECK(prologue_display_mode(HudRun::Race, HudPlay::Live) == HudMode::Race);
    CHECK(prologue_display_mode(HudRun::License, HudPlay::Live) == HudMode::License);
    CHECK(prologue_display_mode(HudRun::License, HudPlay::Replay) == HudMode::Replay);
    CHECK(prologue_display_mode(HudRun::Race, HudPlay::Demo) == HudMode::Demo);
    // No free run or time trial class: an arcade race's display.
    CHECK(prologue_display_mode(HudRun::TimeTrial, HudPlay::Live) == HudMode::Race);
    // The shared engine's rule makes a run of 10 laps the endurance display; Prologue keeps the test's (lesson 35, ten laps).
    gt3r::race::HudModeSelect gt3;
    gt3.run = HudRun::License, gt3.event_laps = 10;
    CHECK(gt3r::race::hud_mode(gt3) == HudMode::Endurance);
    CHECK(prologue_display_mode(HudRun::License, HudPlay::Live) == HudMode::License);
}

// The sheen's exponent (kind 4, 0x49EB90): 8 below a shininess of 12, never 4. The converted sets give the engine (which picks
// 4 / 8 / 16 at 6 / 12) a shininess of at least 6 on every sheen; the lit shapes keep theirs (4 / 8 / 16 there too).
TEST(disc_sheen_exponent_is_this_games) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (Disc* disc : test::reference_discs()) { // (the US disc's files)
        if (disc->info().game != gt4r::Game::GT4) continue;
        size_t sheen = 0, lit_low = 0;
        for (const char* path : {"crs/20r60r", "car/lod/acra0008"}) {
            const gt3r::Volume::Entry* e = disc->volume().find(path);
            CHECK(e != nullptr);
            if (!e) continue;
            const gt4r::Bytes data = disc->volume().read(*e);
            for (size_t off : gt4r::mdls::find_model_sets(data)) {
                const gt3r::scene::Model m = gt4r::mdls::build_model(gt4r::ByteSpan(data).subspan(off), {});
                for (const gt3r::scene::Mesh& mesh : m.meshes)
                    for (const gt3r::scene::Primitive& p : mesh.primitives) {
                        if (!p.material.valid) continue;
                        if (p.material.specular_local) {
                            ++sheen;
                            CHECK(p.material.shininess >= 6);
                        }
                        else lit_low += p.material.shininess < 6;
                    }
            }
        }
        CHECK(sheen > 0);
        CHECK(lit_low > 0); // (the lit shapes' low shininess is left alone)
    }
}

// The correction's levels (0x4A8CB8): t^(1 / gamma) over the levels, held to `most`; the racing views' default brightness +3.
TEST(race_correction_levels) {
    const float plain[3] = {1, 1, 1};
    const auto id = race::correction_levels(plain, 0, 1, 255);
    // (i x (1 / 255 in float) x 255 truncates to i or one under it, as the program's does)
    for (int i = 0; i < 256; ++i) CHECK((id[size_t(i)][0] == i || id[size_t(i)][0] == i - 1) && id[size_t(i)][2] == id[size_t(i)][0]);
    const race::PictureSettings settings; // (brightness +3: a gamma of 2^0.3)
    const race::PictureSettings::Correction c = settings.correction();
    const auto l = race::correction_levels(c.gamma, 0, c.contrast, 255);
    CHECK(l[0][0] == 0 && l[255][1] == 255);
    CHECK(l[128][1] == 145); // 255 x (128 / 255)^(1 / 1.2311)
    // A Dive camera's colors stop at 220, a course's at 235.
    CHECK(race::correction_levels(plain, 0, 1, 220)[255][0] == 220);
    // The black level: nothing at or under it; the white level: everything over it full.
    const auto lw = race::correction_levels(plain, 0.25f, 0.5f, 255);
    CHECK(lw[63][0] == 0 && lw[64][0] < 5 && lw[128][0] == 255);
    // The grade: off for the plain correction, the saturation held to 1.
    CHECK(!race::correction_grade(plain, 0, 1, 1, 255).on);
    CHECK(race::correction_grade(plain, 0, 1, 1.5f, 255).saturation == 1.0f);
}

// The glow filter's values (Env0 +0x1750; 0x33F1C8 / 0x3BADF8 / 0x4A2808's fixed weights).
TEST(race_glow_filter_values) {
    Bytes env(0x1800, 0);
    auto put = [&](size_t at, float v) { std::memcpy(env.data() + at, &v, 4); };
    CHECK(!race::GlowFilter::from_env(env)); // (switch off)
    const u32 on = 1, n = 5;
    std::memcpy(env.data() + 0x1750, &on, 4), std::memcpy(env.data() + 0x1764, &n, 4);
    put(0x1754, 0.73f), put(0x1758, 1.9f), put(0x175C, 1.9f), put(0x1760, 30.0f); // (ss_shibuya's)
    const std::optional<race::GlowFilter> g = race::GlowFilter::from_env(env);
    CHECK(g.has_value());
    if (!g) return;
    CHECK(g->samples == 5 && std::fabs(g->threshold - 0.73f) < 1e-6f);
    CHECK(std::fabs(g->radius - 30.0f / 781.2196f) < 1e-6f);
    CHECK(std::fabs(g->sum - 25.0f * 36.0f / 128.0f) < 1e-5f); // trunc(128 x 1.9 / (0.27 x 25)) = 36
    CHECK(std::fabs(g->gain - 65.0f / 128.0f) < 1e-5f);        // trunc(128 x 0.27 x 1.9) = 65
    put(0x1754, -0.1f);
    CHECK(!race::GlowFilter::from_env(env));
}
