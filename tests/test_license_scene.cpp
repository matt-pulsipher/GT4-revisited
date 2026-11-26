// A license test's road section: its own cones and gates; the staff that come out at its end (race/license_scene.h).
#include "test_framework.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/race/gadgets.h"
#include "gt4r/race/license_scene.h"
#include "gt4r/race/license_trophy.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/sections.h"
#include "gt4r/sim/ai/run.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

TEST(license_section_gadgets_disc) {
    // A race on a road section adds the section's own cones and gates to the road's (0x395B60): B-3's (Deep Forest's
    // section 2) are its start and finish gates (two posts and a banner each) and a row of cones; the road has none.
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("crs/deepforest");
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const course::CourseFile f = course::CourseFile::parse(data);
        const ByteSpan road = f.section(data, course::kSlotRunway);
        const race::Gadgets none = race::Gadgets::parse(f.section(data, course::kSlotGadgets), road);
        CHECK(none.placements.empty() && none.kinds.size() == 7);
        const race::Gadgets b3 = race::Gadgets::parse(f.section(data, course::kSlotGadgets), road, 2);
        CHECK(b3.section_from == 0 && b3.placements.size() == 25);
        if (b3.placements.size() < 6) continue;
        // The finish gate: posts of kind 3 either side of the road, the banner (kind 1) between them 4 m up.
        const auto& banner = b3.placements[5];
        CHECK(banner.kind == 1 && std::fabs(banner.position.x + 273.34f) < 0.01f && std::fabs(banner.position.z + 135.15f) < 0.01f);
        CHECK(b3.placements[3].kind == 3 && b3.placements[4].kind == 3);
        // Tsukuba's road keeps its own 18 and adds a section's after them.
        const gt3r::Volume::Entry* t = disc->volume().find("crs/tsukuba");
        if (!t) continue;
        const Bytes td = disc->volume().read(*t);
        const course::CourseFile tf = course::CourseFile::parse(td);
        const race::Gadgets ts = race::Gadgets::parse(tf.section(td, course::kSlotGadgets), tf.section(td, course::kSlotRunway), 0);
        CHECK(ts.section_from == 18 && ts.placements.size() == 32);
    }
}


TEST(license_staff_places_disc) {
    // The road sections' places for the staff (+0x0E, +0x10): the 100 m test course's B-1 section has the stopping box and a
    // second place; Deep Forest's sections one place twice.
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("crs/testcourse_100m");
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const course::CourseFile f = course::CourseFile::parse(data);
        const std::vector<race::RoadSection> sections = race::road_sections(f.section(data, course::kSlotRunway));
        CHECK(!sections.empty() && sections[0].staff.size() == 2);
        if (sections.empty() || sections[0].staff.size() < 2) continue;
        const auto& box = sections[0].staff[0];
        CHECK(std::fabs(box.pos.x + 964.126f) < 0.01f && std::fabs(box.pos.z + 1498.05f) < 0.01f && std::fabs(box.heading - 1.0579f) < 0.001f);
    }
}

// The staff of a passed B-1 against the original's pass scene (15-states state_license_b1_pass_result.p2s: the scene's state 3,
// its men 20.466 s into their motions, the camera 20.483 s into license_cam01_seq).
TEST(license_scene_b1_pass_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        gt3r::Volume& vol = disc->volume();
        const gt3r::Volume::Entry* sf = vol.find("pit/license_001");
        const gt3r::Volume::Entry* cf = vol.find("crs/testcourse_100m");
        db::SpecDb sdb = sim::ai::open_specdb(*disc);
        std::string model;
        for (const db::CarEntry& c : sdb.cars())
            if (c.label == "demio_glx_99") model = c.model;
        const gt3r::Volume::Entry* car = vol.find("car/lod/" + model);
        CHECK(sf && cf && car);
        if (!sf || !cf || !car) continue;
        race::LicenseScene scene(nullptr, vol.read(*sf));
        race::LicenseScene::CarSizes sizes;
        race::LicenseScene::read_car_file(vol.read(*car), sizes);
        CHECK(sizes.seat_x >= sizes.lever_x && std::fabs(sizes.seat_z - 0.0028f) < 0.01f); // (the Demio is driven from the right)
        // Its front wheels as the original's state has them (the hubs across and along).
        sizes.wheel_x[0] = -0.7095f, sizes.wheel_x[1] = 0.7095f, sizes.wheel_z[0] = sizes.wheel_z[1] = -1.1978f;
        const Bytes course = vol.read(*cf);
        const course::CourseFile f = course::CourseFile::parse(course);
        const std::vector<race::RoadSection> sections = race::road_sections(f.section(course, course::kSlotRunway));
        if (sections.empty()) continue;
        scene.start(sections[0].staff, sizes, [](float, float, float& y, gt3r::render::Vec3*) { return y = 0, true; });
        CHECK(scene.on());
        // (the men wait at the second place through the run)
        CHECK(std::fabs(scene.man(0).pos.x - sections[0].staff[1].pos.x) < 2.0f);
        constexpr float dt = 1.0f / 60;
        for (int i = 0; i < 10; ++i) scene.step(dt);
        scene.set_result(true);
        int arrived = -1;
        for (int i = 0; i < 400 && arrived < 0; ++i)
            if (scene.step(dt)) arrived = i;
        CHECK(arrived >= 238 && arrived <= 241); // 2 s, then 2 s of fading out
        CHECK(scene.state() == 2 && scene.brightness() < 0.02f);
        CHECK(std::fabs(scene.heading() - 4.1979f) < 0.001f);
        int steps = 0;
        while (scene.man(0).time < 20.466f - dt / 2 && steps < 2000) scene.step(dt), ++steps;
        CHECK(scene.state() == 3 && scene.brightness() == 1.0f);
        // Each man where the state has him, turned to the seat; the staff's last entry gone round.
        const float want[3][3] = {{-964.7218f, -1496.9967f, 2.6279f}, {-965.7154f, -1497.2711f, 1.9001f}, {-962.3677f, -1498.7329f, 5.1816f}};
        for (int k = 0; k < 3; ++k) {
            const auto& m = scene.man(k);
            CHECK(std::fabs(m.pos.x - want[k][0]) < 0.02f && std::fabs(m.pos.z - want[k][1]) < 0.02f);
            CHECK(std::fabs(std::remainder(m.heading - want[k][2], 6.2831853f)) < 0.02f);
            std::printf("  man %d at %.4f %.4f heading %.4f, %.3f s\n", k, double(m.pos.x), double(m.pos.z), double(m.heading), double(m.time));
        }
        CHECK(std::fabs(scene.man(1).time - 14.6664f) < 0.01f && std::fabs(scene.camera_time() - 20.4829f) < 0.01f);
        // (the state's eye is the one its last picture was drawn with, a step before its clock: the shot before the last step)
        race::LicenseScene::CameraShot shot;
        CHECK(scene.camera(shot, true));
        std::printf("  eye %.4f %.4f %.4f, lens %.4f\n", double(shot.eye.x), double(shot.eye.y), double(shot.eye.z), double(shot.angle));
        CHECK(std::fabs(shot.eye.x + 966.9601f) < 0.005f && std::fabs(shot.eye.y - 2.6518f) < 0.005f && std::fabs(shot.eye.z + 1502.2668f) < 0.005f);
        CHECK(std::fabs(shot.angle - 0.6436f) < 0.005f);
    }
}

// The result page's trophy (race/trophy.mdl): its one picture, a sphere map, in three palettes chosen by the paint (Tex1 +0x20):
// gold, silver, bronze for the program's material index 0, 1, 2 (table 0x6A38F0 by the result's kind).
TEST(license_trophy_palettes_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("race/trophy.mdl");
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes f = disc->volume().read(*e);
        double mean[3][3] = {};
        for (int paint = 0; paint < 3; ++paint) {
            gt3r::gtm1::Options o;
            o.static_env_maps = true;
            o.paint = race::LicenseTrophy::material_for(race::LicenseTrophy::kind_for(paint), int(mdls::read_header(f).variations));
            const gt3r::scene::Model m = mdls::build_model(f, o, mdls::Scene{}, "trophy");
            CHECK(!m.textures.empty());
            if (m.textures.empty()) continue;
            const auto& t = m.textures[0];
            double n = 0;
            for (u32 c : t.rgba) {
                const double w = double(c >> 24) / 255.0 * (double((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF)) / 765.0);
                for (int k = 0; k < 3; ++k) mean[paint][k] += w * double((c >> (8 * k)) & 0xFF);
                n += w;
            }
            for (double& v : mean[paint]) v /= std::max(n, 1e-9);
            std::printf("  paint %d: mean %.0f %.0f %.0f\n", o.paint, mean[paint][0], mean[paint][1], mean[paint][2]);
        }
        // Gold: red over blue by far; silver: grey; bronze: red over green over blue, darker than gold.
        CHECK(mean[0][0] > mean[0][2] * 1.5 && mean[0][1] > mean[0][2] * 1.2);
        CHECK(std::fabs(mean[1][0] - mean[1][2]) < 25 && std::fabs(mean[1][1] - mean[1][2]) < 25);
        CHECK(mean[2][0] > mean[2][1] && mean[2][1] > mean[2][2] && mean[2][0] > mean[2][2] * 1.2);
    }
}

TEST(license_staff_shadow_rise) {
    // A shadow thrown 1 m across a level road stays on it; on a road rising 10 % along the shadow it rises 0.1 m (0x407FA0).
    CHECK(race::LicenseScene::shadow_rise({0, 1, 0}, {1, 0, 0}) == 0);
    const float l = std::sqrt(1.01f);
    CHECK(std::fabs(race::LicenseScene::shadow_rise({-0.1f / l, 1 / l, 0}, {1, 0, 0}) - 0.1f) < 1e-6f);
    CHECK(std::fabs(race::LicenseScene::shadow_rise({0, 1 / l, 0.1f / l}, {0, 0, 1}) + 0.1f) < 1e-6f);
}
