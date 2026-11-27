// The driver in a race car: the cockpit's places, the character files by cockpit kind, the driver
// sets' parts placed at the skeleton's nodes (race/driver_figure.h), on the cars of GT4R_DISC.
#include "test_framework.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/driver_figure.h"
#include "gt4r/race/driver_state.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/ai/run.h"

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

// The hands go round with the wheel and take new grips when their turn ends; let go, they come back.
TEST(driver_hands) {
    namespace rp = race::posture;
    rp::Cockpit c;
    c.kind = 0x11;
    rp::DriverState s;
    s.reset(c);
    rp::HandSpeeds speeds;
    for (rp::F& v : speeds) v = rp::f(1);
    const rp::F dt(1 / 60.0f);
    // A little to the left and back: the grips go along.
    s.step(c, rp::f(0.5f), dt, speeds);
    CHECK(std::fabs(s.after.at(0x34).host() + 0.2f) < 1e-5f);
    CHECK(std::fabs(s.after.at(0x48).host() - (-1.09955728f - 0.2f)) < 1e-4f);
    // Far round: a new grip, and the hands never past their ends.
    bool moved = false;
    for (int i = 0; i < 400; ++i) {
        s.step(c, rp::f(20.0f * std::sin(float(i) * 0.02f)), dt, speeds);
        moved |= s.state[0] != 0 || s.state[1] != 0;
        for (u32 h = 0; h < 2; ++h) CHECK(std::fabs(s.hand[h].host()) < 3.2f);
        CHECK(std::fabs(s.after.at(0x34).host()) <= 8.3776f);
    }
    CHECK(moved);
}

// Where the driver looks (driver_gaze.h): the aim on the road within 60 degrees of straight ahead, the place 2.5 m from the
// eye toward it, reached by its springs. gt4r_diff posture with GT4R_POSTURE_GAZE checks it against the program bit for bit.
TEST(driver_gaze) {
    namespace rp = race::posture;
    const auto near = [](rp::F a, float b, float tol = 1e-3f) { return std::fabs(a.host() - b) < tol; };
    rp::Cockpit c;
    c.seat = {rp::f(0.3f), rp::f(0.1f), rp::f(0.2f)};
    // An aim ahead and a little right is kept; one straight to the right is turned onto the 60 degree edge; one behind is none.
    const auto ahead = rp::road_target(c, {rp::f(3), rp::f(0.9f), rp::f(-28)});
    CHECK(ahead && near(ahead->x, 3) && near(ahead->z, -28) && near(ahead->y, 0.9f));
    const auto side = rp::road_target(c, {rp::f(10), rp::f(0.9f), rp::f(-1)});
    CHECK(side && near(rp::atan2(side->x, -side->z), 1.0471976f));
    CHECK(!rp::road_target(c, {rp::f(0.5f), rp::f(0.9f), rp::f(10)}));
    // Straight ahead, turned left by the front wheels.
    const rp::V3 left = rp::ahead_of_seat(c, rp::f(0.2f));
    CHECK(left.x < rp::f(0) && near(left.y, 0.1f));
    // The first step puts the place 2.5 m out at once; then it follows a target to the side, at most 60 degrees round.
    rp::DriverGaze g;
    rp::DriverRecord r;
    rp::DriverGaze::Inputs in;
    in.dt = rp::f(1.0f / 60), in.first = true, in.aim = rp::V3{rp::f(0), rp::f(0.9f), rp::f(-28)};
    g.step(c, in, r);
    CHECK(near(r.at(0x58), 0.3f - 2.5f * 0.3f / std::sqrt(0.09f + 28.2f * 28.2f)) && near(r.at(0x5C), 0.9f) && near(r.at(0x60), 0.2f - 2.5f, 0.01f));
    in.first = false, in.aim = rp::V3{rp::f(-30), rp::f(0.9f), rp::f(-2)};
    for (int k = 0; k < 120; ++k) g.step(c, in, r);
    const float dx = r.at(0x58).host() - 0.3f, dz = r.at(0x60).host() - 0.2f;
    CHECK(near(rp::f(std::sqrt(dx * dx + dz * dz)), 2.5f, 0.01f) && near(rp::f(std::atan2(-dx, -dz)), 1.0471976f, 0.01f));
}

// The hand on the gear lever (driver_gear.h): gt4r_diff <disc> gear and posture (GT4R_POSTURE_GEAR) check it against the
// program bit for bit; here what it does.
TEST(driver_gear_change) {
    namespace rp = race::posture;
    const auto near = [](rp::F a, float b) { return std::fabs(a.host() - b) < 1e-6f; };
    // A five-speed gate from slot 1, reverse at 6: first up left, second down left, ..., fifth up right, reverse down right.
    const rp::GearLayout gate{1, 1, 6, 5};
    rp::F x, y;
    const float places[7][2] = {{0.5f, -1}, {-0.5f, 1}, {-0.5f, -1}, {0, 1}, {0, -1}, {0.5f, 1}, {0.5f, 1}};
    for (int g = 0; g <= 6; ++g) {
        rp::gate_place(gate, g, x, y);
        CHECK(near(x, places[g][0]) && near(y, places[g][1]));
    }
    rp::gate_place({1, 1, -1, 5}, 0, x, y); // reverse left of first
    CHECK(near(x, -1) && near(y, 1));
    rp::gate_place({4, 0, 0, 3}, 0, x, y); // a selector: forward or not
    CHECK(near(x, 0) && near(y, 1));
    rp::gate_place({4, 0, 0, 3}, 2, x, y);
    CHECK(near(y, -1));
    rp::gate_place({0, 0, 0, 5}, 3, x, y);
    CHECK(near(x, 0) && near(y, 0));
    // The reach: near the up-shift on the throttle, near the down-shift off it.
    rp::ShiftInputs si;
    si.top = 5, si.gear = 3, si.up_rpm = 7000, si.down_rpm = 4000, si.suggested = 3;
    si.rpm = 6800, si.throttle = rp::f(0.9f);
    CHECK(rp::shift_reach(si).up && !rp::shift_reach(si).down);
    si.rpm = 6600;
    CHECK(!rp::shift_reach(si).any());
    si.rpm = 4400, si.throttle = rp::f(0), si.brake = rp::f(0.5f), si.suggested = 2;
    CHECK(rp::shift_reach(si).down && rp::shift_reach(si).far);
    // A shift from second to third: the hand to the lever, across the gate's middle row and up, back to the wheel; the left
    // foot down while it is off the wheel.
    rp::Cockpit c;
    c.kind = 1;
    c.seat = {rp::f(0.35f), rp::f(0.2f), rp::f(0.1f)}, c.lever = {rp::f(0.05f), rp::f(0.15f), rp::f(-0.2f)};
    rp::DriverState s;
    s.reset(c, gate, 2);
    CHECK(near(s.after.at(0x20), -0.5f) && near(s.after.at(0x24), -1));
    rp::HandSpeeds speeds;
    for (rp::F& v : speeds) v = rp::f(1);
    rp::GearInput in;
    in.layout = gate, in.gear = 3;
    rp::DriverMotion m;
    m.turn = rp::identity(), m.gear = &in;
    const rp::F dt(1 / 60.0f);
    int phases = 0, steps = 0;
    float foot = 0, hand = 0;
    for (; steps < 600; ++steps) {
        s.step(c, rp::f(0), dt, speeds, &m);
        phases |= 1 << s.phase;
        foot = std::max(foot, s.after.at(0x30).host());
        hand = std::max(hand, s.after.at(0x1C).host());
        if (steps > 10 && s.phase == 0) break;
    }
    std::printf("    the shift took %d steps (phases %03X)\n", steps, phases);
    CHECK(s.held_gear == 3 && s.phase == 0 && steps < 200);
    CHECK((phases & 0x3D0) == 0x3D0); // 4, 6, 7, 8, 9 (2 goes on to 4 in its step)
    CHECK(near(s.after.at(0x20), 0) && near(s.after.at(0x24), 1));
    CHECK(foot > 0.99f && hand > 0.99f && near(s.after.at(0x1C), 0));
    // A reach with no shift: to the lever and back, the gear as it was.
    in.reach.up = true;
    s.step(c, rp::f(0), dt, speeds, &m);
    CHECK(s.phase == 1 || s.phase == 3);
    in.reach.up = false;
    for (int i = 0; i < 300 && s.phase != 0; ++i) s.step(c, rp::f(0), dt, speeds, &m);
    CHECK(s.phase == 0 && s.held_gear == 3);
}

// Which cars move the lever: the open cars' figures (not the head-and-shoulders boards) with a gearbox of lever kind 1 or 4.
TEST(driver_gear_layouts_disc) {
    namespace rp = race::posture;
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        db::SpecDb db = sim::ai::open_specdb(*disc);
        gt3r::Volume& vol = disc->volume();
        int kinds[6] = {}, moving[6] = {};
        std::string shown;
        for (const db::CarEntry& car : db.cars()) {
            const gt3r::Volume::Entry* e = vol.find("car/lod/" + car.model);
            if (!e) continue;
            const Bytes file = vol.read(*e);
            if (!car::CarFile::is_car(file)) continue;
            const std::string& name = car.model;
            const std::optional<db::CarParts> parts = db::car_parts(db, car.label);
            if (!parts) continue;
            const rp::GearLayout g = rp::gear_layout(db::part_row(db, parts->part(db::PartTable::Gear)));
            CHECK(g.kind >= 0 && g.kind <= 5);
            ++kinds[std::min(g.kind, 5)];
            const rp::Cockpit c = rp::read_posture_cockpit(file, false);
            if (rp::figure_class(c.kind) == rp::FigureClass::Board || c.kind == 0) continue;
            ++moving[std::min(g.kind, 5)];
            if ((g.kind == 1 || g.kind == 4) && shown.size() < 400) shown += " " + name + "(" + std::to_string(g.kind) + ")";
            if (g.kind == 1) CHECK(g.count >= 1 && g.count <= 7);
        }
        std::printf("    lever kinds 0..5: %d %d %d %d %d %d; of the moving drivers' cars %d %d %d %d %d %d:%s\n", kinds[0], kinds[1], kinds[2], kinds[3], kinds[4],
                    kinds[5], moving[0], moving[1], moving[2], moving[3], moving[4], moving[5], shown.c_str());
        CHECK(kinds[1] > 0 && moving[1] + moving[4] > 0);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}
