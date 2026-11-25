// The pit crew (race/pit_scene.h): the motions of pit/pit_001, and a stop of an R34 as the original's saved states have it
// (the men's places round the car in its box). On the discs of GT4R_DISC.
#include "test_framework.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/race/pit_scene.h"

#include <cmath>

using namespace gt4r;

namespace {
bool near(float a, float b, float tol = 0.003f) { return std::fabs(a - b) < tol; }
} // namespace

TEST(pit_motion_sequences_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        const gt3r::Volume::Entry* e = disc->volume().find("pit/pit_001");
        if (!e || disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        ++ran;
        const Bytes file = disc->volume().read(*e);
        std::map<std::string, menu::SkeletonMotion::Name> names;
        const auto motions = menu::SkeletonMotion::parse_all(ByteSpan(file).subspan(rd32(file, 4)), names);
        CHECK(motions.size() == 3);
        CHECK(names.count("tiremanLF_1seq") && names.count("tiremanRR_1seq") && names.count("fuelman_1seq") && names.count("lollipopman_1seq"));
        const auto& n = names["tiremanLF_1seq"];
        const auto& m = motions[size_t(n.object)];
        const auto& q = m.sequences[size_t(n.sequence)];
        // Held at its wait until the number comes past it, and at the work until the next.
        float t = 0;
        for (int i = 0; i < 1200; ++i) t = q.advance(t, 1 / 60.0f, 0);
        CHECK(q.at(t)->number == 0);
        for (int i = 0; i < 600; ++i) t = q.advance(t, 1 / 60.0f, 1);
        CHECK(q.at(t)->number == 1);
        float gun = 0, nut = 0, jack = 0;
        for (int i = 0; i < 1500; ++i) {
            const float was = t;
            t = q.advance(t, 1 / 60.0f, 2);
            gun += m.events(n.sequence, "wheel_gun", was, t, 1 / 60.0f);
            nut += m.events(n.sequence, "wheel_nut", was, t, 1 / 60.0f);
            jack += m.events(n.sequence, "jack_up", was, t, 1 / 60.0f);
        }
        CHECK(gun > 0 && nut > 0);
        CHECK(t <= q.length + 0.001f);
        const auto nodes = m.nodes(m.pose(n.sequence, 0, 0.5f), gt3r::render::Mat4::identity());
        CHECK(nodes.count("gun") && nodes.count("tire0") && nodes.count("tire1"));
    }
    if (!ran) SKIP("the American disc is not among GT4R_DISC");
}

TEST(pit_scene_stop_disc) {
    using race::PitScene;
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        const gt3r::Volume::Entry* e = disc->volume().find("pit/pit_001");
        if (!e || disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        ++ran;
        PitScene scene(nullptr, disc->volume().read(*e));
        PitScene::CarSizes s;
        s.width = 1.7798f, s.front = 2.262f, s.rear = 2.338f;
        s.tire_width[0] = s.tire_width[1] = 0.2f;
        s.tire_radius[0] = s.tire_radius[1] = 0.32f;
        s.filler = {0.85f, 0.684f, 1.832f}, s.filler_axis = {-1, 0, 0}; // (its filler's lid looks to the right)
        scene.set_car(0, s);
        PitScene::Car c;
        c.body = gt3r::render::Mat4::identity();
        c.body.m[12] = 100, c.body.m[13] = 0.3f;
        c.hub[0] = {-0.762f, 0.02f, -1.332f}, c.hub[1] = {0.762f, 0.02f, -1.332f};
        c.hub[2] = {-0.767f, 0.02f, 1.332f}, c.hub[3] = {0.767f, 0.02f, 1.332f};
        c.box = {0, 0, 0}, c.box_heading = -1.5708f, c.has_box = true;
        c.height = 0.5f;
        std::vector<std::string> sounds;
        const auto sound = [&](const char* n) { sounds.push_back(n); };
        const auto run = [&](int steps) {
            for (int i = 0; i < steps; ++i) scene.step({&c, 1}, 0, 1 / 60.0f, sound);
        };
        run(1);
        CHECK(!scene.active(0) || !scene.wheels_off(0));
        c.stage = 1, c.kind = 2, c.service = 0x1C; // tires and fuel
        run(600);
        // (The places as the saved state has them, the random parts of them aside.)
        CHECK(near(scene.man(0, 0).pos.x, -1.04f, 0.1f) && near(scene.man(0, 0).pos.z, -1.332f));
        CHECK(near(scene.man(0, 4).pos.x, 2.24f, 0.1f));
        CHECK(near(scene.man(0, 5).pos.x, 1.708f) && near(scene.man(0, 5).pos.z, -3.262f));
        c.body.m[12] = 0, c.stage = 2, c.kind = 3, c.service = 0x1C0;
        run(60);
        CHECK(near(scene.man(0, 0).pos.x, -0.862f) && near(scene.man(0, 2).pos.x, -0.867f));
        CHECK(near(scene.man(0, 4).pos.x, 0.9899f) && near(scene.man(0, 4).pos.z, 1.832f) && near(scene.man(0, 4).heading, 4.71225f));
        c.stage = 3, c.refuelling = true;
        run(400);
        CHECK(scene.wheels_off(0) && scene.hose());
        c.refuelling = false;
        run(700);
        bool gun = false, nut = false, tank = false;
        for (const std::string& n : sounds) gun |= n == "wheelgun", nut |= n == "dropnut", tank |= n == "oiltank";
        CHECK(gun && nut && tank);
        for (int k = 0; k < 4; ++k) CHECK(scene.man(0, k).done);
        c.stage = 5, c.kind = 4;
        run(60);
        c.stage = 0, c.kind = 5;
        run(2);
        CHECK(!scene.active(0));
        // No tires wanted: nobody at the wheels.
        c.stage = 1, c.kind = 2, c.service = 0x18;
        run(10);
        c.stage = 2, c.kind = 3;
        run(10);
        CHECK(scene.man(0, 0).idle && scene.man(0, 3).idle && !scene.man(0, 4).idle);
    }
    if (!ran) SKIP("the American disc is not among GT4R_DISC");
}

TEST(pit_car_files_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0, cars = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        ++ran;
        gt3r::Volume& vol = disc->volume();
        for (const std::string& name : vol.list_dir("car/lod")) {
            const gt3r::Volume::Entry* e = vol.find("car/lod/" + name);
            if (!e) continue;
            const Bytes file = vol.read(*e);
            if (!car::CarFile::is_car(file)) continue;
            race::PitScene::CarSizes s;
            race::PitScene::read_car_file(file, s);
            ++cars;
            // A filler on the car, its direction a unit vector (or none).
            CHECK(std::fabs(s.filler.x) < 1.3f && s.filler.y > -0.3f && s.filler.y < 1.6f && std::fabs(s.filler.z) < 3.5f);
        }
        CHECK(cars == 807);
    }
    if (!ran) SKIP("the American disc is not among GT4R_DISC");
}
