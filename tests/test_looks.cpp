// What players see, by Gran Turismo 4's (and Prologue's) own rules where they differ from the shared engine's.

#include "test_framework.h"

#include <cmath>
#include <cstring>

#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/display_mode_prologue.h"
#include "gt4r/race/glow_filter.h"
#include "gt4r/race/light_model.h"
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

// Prologue's Best and Last lines (RaceDisplay::vf36 0x38CA18 and the fader 0x392C78): shown until START, then 8 s from it (and
// from a lap), fading in and out over 9 steps. ✔ the original's Tsukuba race: shown at the count, gone at 0'20 (its saved
// state: both faders at 0, state 2, the hold's count just below 0).
TEST(prologue_best_last_lines_for_eight_seconds) {
    using gt4r::race::prologue_times_opacity;
    CHECK(prologue_times_opacity(-1) == 1.0f); // (before START)
    CHECK(prologue_times_opacity(0) == 1.0f);
    CHECK(std::fabs(prologue_times_opacity(1) - 8.0f / 9) < 1e-4f); // (the call's step fades out)
    CHECK(prologue_times_opacity(2) == 1.0f);
    CHECK(prologue_times_opacity(480) == 1.0f);
    CHECK(std::fabs(prologue_times_opacity(481) - 8.0f / 9) < 1e-4f);
    CHECK(prologue_times_opacity(489) <= 1e-4f && prologue_times_opacity(600) == 0.0f);
    CHECK(prologue_times_opacity(20 * 60) == 0.0f); // (0'20: gone)
    // A lap's call from nothing: in over nine steps, out after the hold.
    CHECK(prologue_times_opacity(1, 0) == 0.0f);
    CHECK(std::fabs(prologue_times_opacity(5, 0) - 4.0f / 9) < 1e-4f);
    CHECK(prologue_times_opacity(10, 0) == 1.0f && prologue_times_opacity(480, 0) == 1.0f && prologue_times_opacity(490, 0) == 0.0f);
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
// The road sheen's viewer: the camera itself (0x4A0080's +0xAB0, never moved), not the shared renderer's 2 m above it.
TEST(race_sheen_seen_from_the_camera) {
    gt3r::render::DrawOptions o;
    CHECK(o.sheen_viewer_up == 2.0f); // (the engine's default: Gran Turismo 3's)
    race::use_gt4_light_model(o);
    CHECK(o.sheen_viewer_up == 0.0f);
}

TEST(race_correction_levels) {
    const float plain[3] = {1, 1, 1};
    const auto id = race::correction_levels(plain, 0, 1, 255);
    // (A gamma of 1 through the program's log and exp series: each level as it is or one under it, 28 of them one under, the
    // program's own: gt4r_diff grade.)
    int same = 0, under = 0;
    for (int i = 0; i < 256; ++i) {
        same += id[size_t(i)][0] == i, under += id[size_t(i)][0] == i - 1;
        CHECK(id[size_t(i)][1] == id[size_t(i)][0] && id[size_t(i)][2] == id[size_t(i)][0]);
    }
    CHECK(same == 228 && under == 28 && id[35][0] == 34 && id[250][0] == 249);
    const race::PictureSettings settings; // (brightness +3: a gamma of 2^0.3)
    const race::PictureSettings::Correction c = settings.correction();
    const auto l = race::correction_levels(c.gamma, 0, c.contrast, 255);
    CHECK(l[0][0] == 0 && l[255][1] == 255);
    CHECK(l[128][1] == 145); // 255 x (128 / 255)^(1 / 1.2311)
    // The program's levels for brightness +3 every 16th level (0x4A8CB8 in the interpreter: gt4r_diff grade).
    const int every16[17] = {0, 26, 47, 65, 82, 99, 115, 130, 145, 160, 174, 188, 202, 216, 229, 242, 255};
    for (int k = 0; k < 17; ++k) CHECK(l[size_t(std::min(16 * k, 255))][1] == every16[k] && l[size_t(std::min(16 * k, 255))][0] == every16[k]);
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
    // The sum at a place: the picture around half a pixel right of and below it.
    CHECK(std::fabs(g->offset_x - 0.5f / 781.2196f) < 1e-7f && g->offset_y == g->offset_x);
    struct {
        struct {
            bool on = false, tent = false;
            float threshold = 0, radius = 0, sum = 0, gain = 0, offset[2] = {0, 0};
            int samples = 0;
        } glow;
    } o;
    race::apply_glow(o, g);
    CHECK(o.glow.on && o.glow.tent && o.glow.samples == 5 && o.glow.offset[1] == g->offset_y);
    put(0x1754, -0.1f);
    CHECK(!race::GlowFilter::from_env(env));
}

// Prologue's display places through the screen's projection (0x40C0D8, race::prologue_screen_x): rounded toward zero at each
// step on VU0, 614 comes out 613.9375 and a right-aligned line at it ends at 613 (✔ the original's GS dump: the race time's box
// ends at 613 for its row's 614, the in-car speed's at its computed 250, the rank's digit at 32).
TEST(prologue_display_places_through_the_projection) {
    using gt4r::race::prologue_screen_x;
    CHECK(prologue_screen_x(614) == 613.9375f && int(prologue_screen_x(614)) == 613);
    CHECK(prologue_screen_x(250) == 250.0f && prologue_screen_x(32) == 32.0f && prologue_screen_x(16) == 16.0f);
    CHECK(prologue_screen_x(615) == 615.0f && prologue_screen_x(457) == 456.9375f && prologue_screen_x(484.5f) == 484.4375f);
    int moved = 0; // (a whole place either kept or a 16th less)
    for (int x = 0; x <= 640; ++x) {
        const float got = prologue_screen_x(float(x));
        CHECK(got == float(x) || got == float(x) - 0.0625f);
        moved += got != float(x);
    }
    CHECK(moved > 0 && moved < 641);
}
