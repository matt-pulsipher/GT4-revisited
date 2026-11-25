// The driver in a race car: the cockpit's places, the character files by cockpit kind, the driver
// sets' parts placed at the skeleton's nodes (race/driver_figure.h), on the cars of GT4R_DISC.
#include "test_framework.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/driver_figure.h"
#include "gt4r/race/driver_state.h"

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
}

// The posture's arithmetic against the host's.
TEST(driver_posture_math) {
    namespace rp = race::posture;
    const auto near = [](rp::F a, float b, float tol = 1e-4f) { return std::fabs(a.host() - b) < tol; };
    for (float x = -1; x <= 1; x += 0.0625f) CHECK(near(rp::asin(rp::f(x)), std::asin(x)));
    const rp::V3 a = rp::normalized({rp::f(0.3f), rp::f(-0.5f), rp::f(0.8f)}), b = rp::normalized({rp::f(-0.7f), rp::f(0.1f), rp::f(0.4f)});
    const rp::V3 c = rp::rotate(rp::between(a, b), a);
    CHECK(near(c.x, b.x.host()) && near(c.y, b.y.host()) && near(c.z, b.z.host()));
    const rp::V3 d = rp::rotate(rp::align(rp::axis_y(), rp::axis_z(), a, b), rp::axis_y());
    CHECK(near(d.x, a.x.host()) && near(d.y, a.y.host()) && near(d.z, a.z.host()));
    CHECK(near(rp::wrap(rp::f(7), rp::f(-rp::kPi)), 7 - 6.2831853f) && near(rp::wrap(rp::f(-4), rp::f(-rp::kPi)), -4 + 6.2831853f));
    CHECK(near(rp::triangle_angle(rp::f(3), rp::f(4), rp::f(5)), 1.5707963f));
}

// A record that went round and a hand that took a new grip.
TEST(driver_record_blend) {
    namespace rp = race::posture;
    rp::DriverRecord a, b;
    a.at(0x0C) = rp::f(1), b.at(0x0C) = rp::f(3);
    a.at(0xE0) = rp::f(2.9f), b.at(0xE0) = rp::f(0.1f); // past its end: back at its start
    a.at(0x48) = a.at(0x40) = rp::f(-1), b.at(0x48) = rp::f(-2), b.at(0x40) = rp::f(-1), b.at(0x50) = rp::f(0.5f);
    const rp::DriverRecord r = rp::blend_records(a, b, rp::f(0.5f));
    CHECK(std::fabs(r.at(0x0C).host() - 2) < 1e-6f);
    CHECK(std::fabs(r.at(0x48).host() + 2) < 1e-6f && std::fabs(r.at(0x40).host() + 1) < 1e-6f && std::fabs(r.at(0x50).host() - 0.75f) < 1e-6f);
}

TEST(driver_figure_disc) {
    namespace rp = race::posture;
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        gt3r::Volume& vol = disc->volume();
        size_t cars = 0, with_set = 0, boards = 0, torsos = 0, reach = 0;
        for (const std::string& name : vol.list_dir("car/lod")) {
            const gt3r::Volume::Entry* e = vol.find("car/lod/" + name);
            if (!e) continue;
            const Bytes file = vol.read(*e);
            if (!car::CarFile::is_car(file)) continue;
            ++cars;
            const rp::Cockpit c = rp::read_posture_cockpit(file, false);
            const rp::FigureClass cls = rp::figure_class(c.kind);
            boards += cls == rp::FigureClass::Board;
            torsos += cls == rp::FigureClass::Torso;
            const car::CarFile cf = car::CarFile::parse(file);
            if (!cf.driver) continue;
            ++with_set;
            const ByteSpan set = ByteSpan(file).subspan(cf.driver);
            const std::vector<race::DriverPart> parts = race::driver_parts(set);
            CHECK(!parts.empty());
            rp::Skeleton s = race::driver_skeleton(set);
            CHECK(std::fabs(s[rp::kLThigh].len.host() - 0.4513f) < 1e-3f && std::fabs(s[rp::kLHand].len.host() - 0.0974f) < 1e-3f);
            rp::PostureInput in;
            if (cls == rp::FigureClass::Board) {
                in = rp::still_posture(c);
            } else {
                rp::DriverState state;
                state.reset(c);
                in = rp::driving_posture(c, state.after, {}, {}, false);
            }
            rp::solve(in.controls, s, in.places, in.has_gaze ? &in.gaze : nullptr);
            const auto v = [](rp::V3 p) { return render::Vec3{p.x.host(), p.y.host(), p.z.host()}; };
            // The pelvis at the seat, the head above it, no farther behind it than half a meter, and the
            // hands within the rim's reach of the wheel.
            const render::Vec3 seat = v(c.seat), pelvis = v(s[rp::kPelvis].pos), head = v(s[rp::kCranium].pos), wheel = v(c.wheel);
            CHECK(render::length(pelvis - seat) < 2e-3f || cls == rp::FigureClass::Whole);
            CHECK(head.y - pelvis.y > 0.3f && head.y - pelvis.y < 0.75f && head.z - pelvis.z > -0.3f && head.z - pelvis.z < 0.55f);
            bool on_rim = true;
            for (int n : {rp::kLHand, rp::kRHand}) on_rim &= render::length(v(rp::bone_end(s[n])) - wheel) < c.wheel_radius.host() + 0.06f;
            reach += on_rim;
            if (name == "niss0021") {
                // The R34: a head and shoulders bound to CRANIUM.
                CHECK(parts.size() == 1 && !parts[0].skinned && parts[0].node == rp::kCranium && cls == rp::FigureClass::Board);
                CHECK(std::fabs(seat.x - 0.369f) < 0.001f && std::fabs(wheel.z + 0.241f) < 0.001f);
            }
        }
        std::printf("    %zu cars, %zu sets, %zu boards, %zu single-seaters, %zu with both hands at the wheel\n", cars, with_set, boards, torsos, reach);
        CHECK(cars > 800 && with_set > 790 && boards > 780 && torsos == 10 && reach > with_set * 95 / 100);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}
