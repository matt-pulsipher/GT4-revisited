// The driver in a race car: the cockpit's places, the character files by cockpit kind, the driver
// sets' parts placed at the skeleton's nodes (race/driver_figure.h), on the cars of GT4R_DISC.
#include "test_framework.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/driver_figure.h"

#include <cmath>

using namespace gt4r;
namespace render = gt3r::render;

TEST(driver_character_files) {
    CHECK(race::character_driver_file(0x20, false, 0).empty()); // the cars with a set of their own
    CHECK(race::character_driver_file(1, false, 0) == "character/driver2.mdl");
    CHECK(race::character_driver_file(1, false, 1) == "character/driver.mdl");
    CHECK(race::character_driver_file(1, false, 5) == "character/driver3.mdl");
    CHECK(race::character_driver_file(2, false, 0) == "character/driver4.mdl");
    CHECK(race::character_driver_file(0xF, true, 0) == "character/driver_mtr.mdl");
    CHECK(race::character_driver_file(0xF, false, 0) == "character/driver_nike.mdl");
    race::CockpitPlaces c;
    c.seat = {0.4f, 0.3f, 0.1f};
    const auto off = race::co_seat_offset(c);
    CHECK(std::fabs(off.x + 0.8f) < 1e-6f && off.y == 0 && off.z == 0); // mirrored
}

TEST(driver_figure_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        gt3r::Volume& vol = disc->volume();
        const gt3r::Volume::Entry* sk = vol.find("character/driver.mdl");
        CHECK(sk != nullptr);
        if (!sk) continue;
        const Bytes skeleton = vol.read(*sk);
        size_t cars = 0, with_set = 0, whole = 0;
        for (const std::string& name : vol.list_dir("car/lod")) {
            const gt3r::Volume::Entry* e = vol.find("car/lod/" + name);
            if (!e) continue;
            const Bytes file = vol.read(*e);
            if (!car::CarFile::is_car(file)) continue;
            ++cars;
            const race::CockpitPlaces c = race::read_cockpit(file);
            // The pedals ahead of the seat and below the wheel; the column's axis a unit vector.
            CHECK(c.pedals[0].z < c.seat.z && c.wheel.z < c.seat.z && c.wheel.y > c.pedals[0].y);
            CHECK(std::fabs(c.column.x * c.column.x + c.column.y * c.column.y + c.column.z * c.column.z - 1) < 0.01f);
            const car::CarFile cf = car::CarFile::parse(file);
            if (!cf.driver) continue;
            ++with_set;
            const ByteSpan set = ByteSpan(file).subspan(cf.driver);
            const std::vector<race::DriverPart> parts = race::driver_parts(set, skeleton);
            CHECK(!parts.empty());
            bool skinned = false;
            for (const race::DriverPart& p : parts) skinned |= p.skinned;
            whole += skinned;
            // The figure's PELVIS at the seat.
            const render::Mat4 root = race::driver_root(c, set, skeleton, false);
            const render::Vec3 pelvis = root.transform_point({0, 0, -0.017f});
            CHECK(std::fabs(pelvis.x - c.seat.x) < 2e-3f && std::fabs(pelvis.y - c.seat.y) < 2e-3f && std::fabs(pelvis.z - c.seat.z) < 2e-3f);
            if (name == "niss0021") {
                // The R34: a silhouette bound to CRANIUM, the head 0.507 above the pelvis in the stored pose.
                CHECK(parts.size() == 1 && !parts[0].skinned);
                const render::Vec3 head = (root * parts[0].node).transform_point({0, 0, 0});
                CHECK(std::fabs(head.y - (c.seat.y + 0.507f)) < 0.01f);
                CHECK(std::fabs(c.seat.x - 0.369f) < 0.001f && std::fabs(c.wheel.z + 0.241f) < 0.001f);
            }
        }
        CHECK(cars > 800 && with_set > 790 && whole >= 10);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}
