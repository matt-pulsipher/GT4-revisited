// The simulation: the road query and the bridge to the engine's vehicle step on made-up data, then
// cars' specs and cars driving on the discs of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/line_driver.h"
#include "gt4r/sim/session.h"
#include "gt4r/sim/ai/run.h"

#include <cmath>
#include <memory>

using namespace gt4r;

namespace {

// A level road 200 m long and 20 m wide at height 3 with a strip of sand along one side, a bridge
// across it at height 9 and walls along both sides; checkpoints every 50 m, the lap's end inside the list.
course::Runway made_road() {
    course::Runway r;
    r.length = 200;
    r.min = {-20, 0, -10}, r.max = {20, 12, 210};
    auto quad = [&](float x0, float x1, float z0, float z1, float y, u8 material, u8 flags) {
        const u16 b = u16(r.vertices.size());
        for (const auto& [x, z] : {std::pair{x0, z0}, std::pair{x1, z0}, std::pair{x1, z1}, std::pair{x0, z1}}) {
            course::Runway::Vertex v;
            v.pos = {x, y, z}, v.b = 255, v.c = 127;
            r.vertices.push_back(v);
        }
        // Wound to face up: (b - a) x (c - a) has y > 0.
        r.triangles.push_back({{b, u16(b + 2), u16(b + 1)}, material, flags, 0});
        r.triangles.push_back({{b, u16(b + 3), u16(b + 2)}, material, flags, 0});
    };
    quad(-10, 10, 0, 200, 3, 0, 0);    // the road
    quad(10, 14, 0, 200, 3, 2, 0);     // sand
    quad(-14, -10, 0, 200, 3, 0, 1);   // the pit lane
    quad(-20, 20, 90, 110, 9, 1, 2);   // the bridge, wet
    course::Runway::Cluster all;
    for (u16 t = 0; t < r.triangles.size(); ++t) all.triangles.push_back(t);
    r.clusters.push_back(all);
    // The gates at z = 0, 50, 100, 150; the course's distance starts at the second (the first is 150 m along:
    // the lap's end lies between them). Facing +z, left is +x.
    for (int i = 0; i < 4; ++i) {
        const float z = float(i) * 50.0f;
        r.checkpoints.push_back({{18, 0, z}, {0, 0, z}, {-18, 0, z}, float((i + 3) % 4) * 50.0f});
    }
    r.grid.push_back({{0, 3, 10}, 0.0f}); // facing +z
    r.grid.push_back({{4, 3, 2}, 0.0f});
    // Two walls, each a chain of two points and back.
    for (float x : {-15.0f, 15.0f}) {
        r.wall_vertices.push_back({{x, 3, -5}, 2});
        r.wall_vertices.push_back({{x, 3, 205}, u32(u16(-1))});
    }
    return r;
}

db::SpecDb open_db(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

std::shared_ptr<const course::Runway> open_road(Disc& disc, const std::string& name) {
    const gt3r::Volume::Entry* e = disc.volume().find("crs/" + name);
    if (!e) return nullptr;
    const Bytes data = disc.volume().read(*e);
    const course::CourseFile file = course::CourseFile::parse(data);
    return std::make_shared<const course::Runway>(course::Runway::parse(file.section(data, course::kSlotRunway)));
}

// The Gran Turismo 4 disc of GT4R_DISC whose program's tables are mapped; null without one.
Disc* full_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d;
    return nullptr;
}

struct Run {
    double driven = 0, top = 0, quarter = -1, quarter_speed = 0;
    int top_gear = 0;
    bool on_road = true;
    float along = 0;
};
// Drives car 0 of a session for `seconds` at full throttle: straight, or along the road.
Run drive(sim::Session& s, double seconds, bool steer, bool careful = false) {
    Run r;
    sim::LineDriver driver(s, 0);
    if (!steer) { // a standing start: the engine revved through the countdown
        race::CarInput rev;
        rev.throttle = 1;
        for (int i = 0; i < 90; ++i) s.hold_once(std::span<const race::CarInput>(&rev, 1));
    }
    const float start = s.cars()[0].course_v + float(s.cars()[0].lap) * s.ground().road().length;
    for (int i = 0; i < int(seconds * 60); ++i) {
        race::CarInput in;
        in.throttle = 1;
        if (steer) in = driver.drive(1.0f, careful);
        s.step_once(std::span<const race::CarInput>(&in, 1));
        const race::CarState& c = s.cars()[0];
        r.driven += double(std::fabs(c.speed)) / 60.0;
        r.top = std::max(r.top, double(c.speed));
        r.top_gear = std::max(r.top_gear, c.gear);
        if (r.quarter < 0 && r.driven >= 402.336) r.quarter = s.time(), r.quarter_speed = double(c.speed);
        sim::GroundHit h;
        if (!s.ground().under(c.position, h)) r.on_road = false;
        r.along = c.course_v + float(c.lap) * s.ground().road().length - start;
    }
    return r;
}

} // namespace

TEST(sim_surface_rows) {
    // The wet variants (the road's flags bit 1).
    CHECK(sim::surface_index(0, 0) == 0 && sim::surface_index(0, 2) == 20 && sim::surface_index(7, 2) == 20);
    CHECK(sim::surface_index(1, 2) == 21 && sim::surface_index(11, 3) == 21 && sim::surface_index(9, 2) == 22);
    CHECK(sim::surface_index(2, 2) == 2 && sim::surface_index(5, 1) == 5);
    // Every surface the courses use has a row of its own in the core's tables.
    for (int s : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 20, 21, 22}) CHECK(sim::surface_of_core_row(sim::core_surface(s)) == s);
    for (int row = 0; row < 16; ++row) CHECK(sim::core_surface(sim::surface_of_core_row(row)) == row);
}

TEST(sim_road_query) {
    const course::Runway road = made_road();
    const sim::RoadGround g(road);
    sim::GroundHit h;
    // The road, the sand, the pit lane.
    CHECK(g.under({0, 3.4f, 50}, h) && std::fabs(h.point.y - 3) < 1e-4f && h.material == 0 && h.flags == 0 && h.normal.y > 0.999f);
    CHECK(std::fabs(h.attr_a - 1) < 1e-5f && std::fabs(h.attr_b - 1) < 1e-5f);
    CHECK(g.under({12, 3.4f, 50}, h) && h.material == 2);
    CHECK(g.under({-12, 3.4f, 50}, h) && h.flags == 1);
    // Under the bridge the road; on it the bridge (the query starts 5 m above the point).
    CHECK(g.under({0, 3.3f, 100}, h) && std::fabs(h.point.y - 3) < 1e-4f);
    CHECK(g.under({0, 9.3f, 100}, h) && std::fabs(h.point.y - 9) < 1e-4f && h.material == 1 && sim::surface_index(h.material, h.flags) == 21);
    // From high above: the bridge first.
    CHECK(g.cast_down({0, 50, 100}, h) && std::fabs(h.point.y - 9) < 1e-4f);
    // Off the road, and below it.
    CHECK(!g.under({30, 3, 50}, h));
    CHECK(!g.cast_down({0, -1, 50}, h));
    // An edge counts.
    CHECK(g.under({10, 3.2f, 50}, h));
}

TEST(sim_road_distance) {
    const course::Runway road = made_road();
    const sim::RoadGround g(road);
    float v = -1;
    int hint = -1;
    CHECK(g.distance_along({3, 3, 60}, v, &hint) && std::fabs(v - 10) < 1e-3f);
    CHECK(g.distance_along({-5, 3, 120}, v, &hint) && std::fabs(v - 70) < 1e-3f);
    CHECK(g.distance_along({0, 3, 20}, v) && std::fabs(v - 170) < 1e-2f); // between the last gate and the first
    CHECK(!g.distance_along({0, 3, -20}, v));
    course::Vec3 p, d;
    g.middle_at(25, p, d);
    CHECK(std::fabs(p.z - 75) < 1e-3f && std::fabs(p.x) < 1e-3f && d.z > 0.99f);
    g.middle_at(225, p, d); // a lap on
    CHECK(std::fabs(p.z - 75) < 1e-3f);
    g.middle_at(170, p, d); // across the lap's end
    CHECK(std::fabs(p.z - 20) < 1e-3f);
    // The road's middle in the gate: the paved stretch (the road and the pit lane beside it), not the sand.
    course::Vec3 m;
    float width = 0;
    CHECK(g.road_middle(10, 0, {0, 3, 60}, m, &width) && std::fabs(m.x - (-2)) < 1.0f && std::fabs(width - 24) < 2.0f && std::fabs(m.z - 60) < 1e-3f);
    // The walls.
    sim::RoadGround::WallHit w;
    CHECK(g.nearest_wall({14.5f, 3, 50}, 2, w) && std::fabs(w.distance - 0.5f) < 1e-3f && w.normal.x < -0.99f);
    CHECK(!g.nearest_wall({0, 3, 50}, 2, w));
}

TEST(sim_program_unknown) {
    // A program that is not a mapped build serves no tables, and the core takes none of Gran Turismo 3's values.
    const sim::Program p = sim::Program::from_image(Bytes(0x1000, 0), 0x100000);
    CHECK(!p.known() && !p.issue().empty());
    CHECK(p.b(0x100010) == 0 && p.b(0x50) == 0 && p.w(sim::Program::kStatic) == 0);
    const gt3r::sim::exact::ProgramData d = sim::core_program(p);
    using T = gt3r::sim::exact::ProgramData::Table;
    for (u32 i = 0; i < gt3r::sim::exact::ProgramData::size(T::Static); ++i) CHECK(d.b(T::Static, i) == 0);
}

TEST(sim_core_spec_fields) {
    sim::Spec s;
    s.set_h(0x20, 1000), s.set_b(0x08, 90), s.set_b(0x05, 20); // 1000 kg at 90 percent, 20 kg of ballast
    s.set_h(0x1A, 2500), s.set_b(0x22, 55), s.set_b(0x24, 22), s.set_b(0x25, 16), s.set_b(0x26, 2);
    s.set_h(0x9A, 300), s.set_h(0x9C, 120);
    // An engine curve of 20 points on a line with one peak: the thinned curve keeps the ends and the peak.
    s.set_b(0xBF, 20);
    for (u32 i = 0; i < 20; ++i) s.set_b(0xC0 + i, 10 + 4 * i), s.set_h(0xD8 + 2 * i, i == 9 ? 3000 : 1000 + 10 * i);
    s.set_h(0x108, 1000), s.set_b(0x07, 50);
    s.set_b(0x155, 9), s.set_h(0x150, 4100);
    for (u32 i = 0; i < 12; ++i) s.set_h(0x138 + 2 * i, 3000 - 200 * i);
    s.set_b(0x1CA, 155), s.set_b(0x1CB, 17); // rims: 15.5 in, and 17 as whole inches
    s.set_b(sim::Spec::tire_block(0) + 0x00, 135), s.set_b(sim::Spec::tire_block(0) + 0x02, 90), s.set_b(sim::Spec::tire_block(0) + 0x78, 80);
    s.set_b(sim::Spec::tire_block(3) + 0x64 + 12, 77);
    s.set_b(0x1F2, 1), s.set_b(0x204, 2);
    gt3r::sim::exact::Spec c;
    sim::core_spec(s, c);
    auto h = [&](u32 o) { return u32(c[o] | (c[o + 1] << 8)); };
    CHECK(h(0x2A) == 920 && h(0x24) == 2500 && c[0x2C] == 55);
    CHECK(c[0x2E] == 22 && c[0x2F] == 101 && c[0x30] == 13); // the pitch and roll bytes x 2 pi
    CHECK(c[0x5C] == 255 && c[0x5D] == 120);
    CHECK(c[0x71] == 16 && c[0x72] == 10 && c[0x72 + 15] == 86 && h(0xA2) == 500);
    bool peak = false;
    for (u32 i = 0; i < 16; ++i) peak = peak || (c[0x72 + i] == 46 && h(0x82 + 2 * i) == 3000);
    CHECK(peak);
    CHECK(c[0xC8] == 7 && h(0xB6) == 3000 && h(0xB6 + 14) == 1600 && h(0xC6) == 4100);
    CHECK(c[0x11C] == 16 && c[0x11D] == 17);
    CHECK(c[0x142] == 135 && c[0x143] == 90 && c[0x142 + 0x4B + 12] == 80); // the wet variant of surface 0 is the core's row 12
    CHECK(c[0x292 + 0x4B + 15] == 77);                                      // surface 12 is the core's row 15
    CHECK(c[0x132 + 2] == 1 && c[0x132 + 12] == 2 && c[0x132] == 0);
}

TEST(sim_gt4_params) {
    // Gran Turismo 4's rules over a parameter block: 1,000 kg, the axles 1.2 m and 1.3 m from the center of gravity.
    namespace ex = gt3r::sim::exact;
    using gt3r::ee::F;
    ex::Params p;
    p.mass = F(1000.0f), p.dist_front = F(1.2f), p.dist_rear = F(1.3f), p.rear_weight = F(0.48f);
    p.half_track = {F(0.75f), F(0.76f)}, p.tire_radius = {F(0.30f), F(0.32f)};
    p.cog_height = F(0.10f), p.lat_transfer = F(1.0f), p.long_transfer = F(2.0f);
    p.downforce_coef = {F(0.1f), F(0.3f)}, p.drag_coef = F(0.5f + 0.04f); // the core's: a tenth of the downforce in it
    p.susp[0].x_static = F(-0.05f), p.susp[1].x_static = F(-0.06f);
    sim::Spec s;
    s.set_b(0x25, 20), s.set_b(0x26, 4), s.set_b(0x27, 100);
    s.set_h(0x9A, 150), s.set_h(0x9C, 300); // ride heights: the rear's own
    s.set_b(0x92, 40), s.set_b(0x93, 250);  // bump travel
    s.set_h(0x66, 0), s.set_h(0x7E, 280);   // the gap to the progressive rate
    sim::gt4_params(s, sim::Program(), p);
    auto near = [](F v, double want) { return std::fabs(double(v.host()) - want) < 1e-5 * std::max(1.0, std::fabs(want)); };
    CHECK(near(p.pitch_lever[0], 1.2 / 2000) && near(p.pitch_lever[1], 1.3 / 2000));
    CHECK(near(p.roll_lever[0], 0.75 / 400) && near(p.roll_lever[1], 0.76 / 400));
    CHECK(near(p.ride_height[0], 0.15) && near(p.ride_height[1], 0.30));
    CHECK(near(p.cog_height, 0.15 * 0.52 + 0.30 * 0.48));
    CHECK(near(p.lat_transfer, (0.1 + 0.222) / (0.1 + 0.10)) && near(p.long_transfer, 2 * (0.1 + 0.222) / 0.2));
    CHECK(near(p.drag_coef, 0.54)); // (the drag's share is the program's byte: unchanged without a program)
    // Front: the bump stop 40 mm short of the ground, no gap: the progressive rate starts at the stop.
    CHECK(near(p.susp[0].offset, 0.30 - 0.15 - 0.05) && near(p.susp[0].bump_limit, -0.05 - 0.15 + 0.04) && near(p.susp[0].knee, -0.16));
    // Rear: the gap word past the travel: the rate starts there.
    CHECK(near(p.susp[1].bump_limit, -0.06 - 0.30 + 0.25) && near(p.susp[1].knee, -0.06 - 0.30 + 0.28));
}

TEST(sim_disc_program_tables) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program p = sim::Program::load(*disc);
    CHECK(p.known());
    // The static block: eight rolling-resistance points from 0, the tarmac's rolling resistance, a level tarmac.
    CHECK(p.b(sim::Program::kStatic + 0x0B) == 8 && p.b(sim::Program::kStatic + 0x0C) == 0);
    CHECK(p.h(sim::Program::kStatic + 0x34) > 0 && p.b(sim::Program::kStatic + 0x1C) == 0);
    // Sand and grass are soft, tarmac is not.
    CHECK(p.w(sim::Program::kSoft) == 0 && p.w(sim::Program::kSoft + 4 * 2) == 1);
    const gt3r::sim::exact::ProgramData d = sim::core_program(p);
    using T = gt3r::sim::exact::ProgramData::Table;
    CHECK(d.b(T::Static, 0x7C) == p.b(sim::Program::kStatic + 0xAC)); // the brake heat's constants follow the surfaces
    CHECK(d.h(T::Static, 0x2C + 2 * 12) == p.h(sim::Program::kStatic + 0x34 + 2 * 20));
    // Another disc's program (Prologue) is not a mapped build.
    for (const std::unique_ptr<Disc>& other : test::discs())
        if (other->info().game == Game::Prologue) CHECK(!sim::Program::load(*other).known());
}

TEST(sim_disc_car_specs) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    auto spec_of = [&](const char* label, sim::Spec& s) {
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, label, eq)) return false;
        sim::build_spec(sdb, program, eq, 0, s);
        return true;
    };
    auto ratios = [](const sim::Spec& s, std::initializer_list<int> want, int final_drive) {
        u32 i = 1;
        for (int w : want)
            if (s.h(0x138 + 2 * i++) != w) return false;
        return s.gears() == int(want.size()) && s.h(0x150) == final_drive;
    };
    sim::Spec s;
    // The Isuzu 117 Coupe '68: 1,050 kg, 120 PS, a four-speed, rear drive.
    CHECK(spec_of("_117_coupe_68", s));
    sim::SpecFigures f = sim::spec_figures(s);
    CHECK(s.weight_kg() == 1050 && s.drive() == 0 && s.h(0x1A) == 2500 && s.h(0x10) == 4280);
    CHECK(std::fabs(f.ps - 120) < 2 && f.power_rpm == 6500 && std::fabs(f.kgfm - 14.5f) < 0.1f && f.torque_rpm == 5000);
    CHECK(ratios(s, {3467, 1989, 1356, 1000}, 4100));
    // The Honda NSX Type R '02: 1,270 kg, mid engine, its six ratios and final drive.
    CHECK(spec_of("nsx_r_02", s));
    f = sim::spec_figures(s);
    CHECK(s.weight_kg() == 1270 && s.drive() == 3 && f.ps > 280 && f.ps < 310);
    CHECK(ratios(s, {3066, 1956, 1428, 1125, 914, 717}, 4235));
    // The Dodge Viper GTS '99: its six ratios and final drive, about 450 hp.
    CHECK(spec_of("viper_gts_99", s));
    f = sim::spec_figures(s);
    CHECK(ratios(s, {2660, 1780, 1300, 1000, 740, 500}, 3070) && f.ps > 440 && f.ps < 470 && s.drive() == 0);
    // A front-drive car and a four-wheel-drive one.
    CHECK(spec_of("civic_r_01", s) && s.drive() == 1);
    CHECK(spec_of("focus_wrc_99", s) && s.drive() == 2 && s.b(0x12A) > 0);

    // Every car builds: a weight, an engine with power, gears, tires of a size.
    size_t cars = 0, computed = 0;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, c.label, eq)) {
            CHECK(!"a car without parts");
            continue;
        }
        sim::build_spec(sdb, program, eq, 0, s);
        const sim::SpecFigures g = sim::spec_figures(s);
        const bool ok = s.weight_kg() > 200 && s.weight_kg() < 4000 && g.ps > 0.5f && g.ps < 1500 && s.gears() >= 1 && s.gears() <= 11 && g.tire_radius[0] > 0.2f &&
                        g.tire_radius[1] > 0.2f && g.tire_radius[1] < 0.8f && s.h(0x1A) > 1000 && s.h(0x138 + 2) > 0 && s.h(0x150) > 0;
        if (!ok) std::printf("  %s: %d kg, %.0f PS, %d gears\n", c.label.c_str(), s.weight_kg(), double(g.ps), s.gears());
        CHECK(ok);
        if (const ByteSpan gear = db::part_row(sdb, eq.part(sim::Equipment::Gear)); gear.size() > 0x25 && gear[0x25]) ++computed;
        ++cars;
    }
    CHECK(cars > 700 && computed > 50);
}

TEST(sim_disc_roads) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    size_t courses = 0;
    for (const std::string& name : disc->volume().list_dir("crs/")) {
        if (name.empty() || name.back() == '/' || name.find('.') != std::string::npos) continue;
        const std::shared_ptr<const course::Runway> road = open_road(*disc, name);
        if (!road || road->grid.empty()) continue;
        const sim::RoadGround g(*road);
        // A start place stands on the road (its own height is only rough on some courses), and the road there faces up.
        bool level = false;
        for (const course::Runway::Place& at : road->grid) {
            sim::GroundHit h;
            if (g.at_place(at.pos, h) && h.normal.y > 0.8f) level = true;
        }
        if (!level) std::printf("  %s: no start place on the road\n", name.c_str());
        CHECK(level);
        // Every checkpoint's middle is at its own distance.
        size_t placed = 0;
        for (const course::Runway::Checkpoint& c : road->checkpoints) {
            float v = 0;
            if (!g.distance_along(c.middle, v)) continue;
            float d = std::fabs(v - c.v);
            if (road->length > 0) d = std::min(d, std::fabs(road->length - d));
            placed += d < 0.5f;
        }
        if (placed * 10 < road->checkpoints.size() * 8) std::printf("  %s: %zu of %zu checkpoints at their distance\n", name.c_str(), placed, road->checkpoints.size());
        CHECK(placed * 10 >= road->checkpoints.size() * 8);
        ++courses;
    }
    CHECK(courses > 100);
}

// A program that is neither Gran Turismo 4's nor a placed Prologue build races nothing: the session refuses it (no rules or tables
// of the shared engine stand in).
TEST(sim_disc_unknown_program_races_nothing) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    sim::Entry e;
    CHECK(sim::stock_car(sdb, sim::Program::load(*disc), "nsx_r_02", e.equipment));
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "tsukuba");
    CHECK(road != nullptr);
    if (!road) return;
    const sim::Program unknown = sim::Program::from_image(Bytes(0x1000, 0), 0x100000);
    std::string what;
    try {
        sim::Session s(sdb, unknown, road, std::span<const sim::Entry>(&e, 1));
    } catch (const std::runtime_error& x) {
        what = x.what();
    }
    CHECK(what.find("not supported") != std::string::npos);
}

TEST(sim_disc_car_rests) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    for (const auto& [car, course] : {std::pair{"nsx_r_02", "tsukuba"}, std::pair{"civic_r_01", "seoul"}, std::pair{"focus_wrc_99", "grandcanyon"}}) {
        sim::Entry e;
        CHECK(sim::stock_car(sdb, program, car, e.equipment));
        const std::shared_ptr<const course::Runway> road = open_road(*disc, course);
        CHECK(road != nullptr);
        if (!road) continue;
        sim::Session s(sdb, program, road, std::span<const sim::Entry>(&e, 1));
        CHECK(s.notes().empty());
        const course::Vec3 start = s.cars()[0].position;
        for (int i = 0; i < 300; ++i) s.step_once({});
        const race::CarState& c = s.cars()[0];
        const float moved = std::hypot(c.position.x - start.x, c.position.z - start.z);
        // (An idling car creeps, and rolls on a slope.)
        if (moved >= 3.0f) std::printf("  %s on %s moved %.2f m\n", car, course, double(moved));
        CHECK(moved < 3.0f && std::fabs(c.speed) < 1.0f);
        // On its wheels on the road: the body a little above the ground, upright, the engine idling.
        sim::GroundHit h;
        CHECK(s.ground().under(c.position, h) && c.position.y - h.point.y > 0.05f && c.position.y - h.point.y < 1.0f);
        CHECK(c.up.y > 0.95f && c.engine_rpm > 400 && c.engine_rpm < 1500 && c.gear == 1);
        for (const race::WheelState& w : c.wheels) CHECK(w.on_ground);
        CHECK(std::fabs(s.time() - 5.0) < 1e-6);
    }
}

TEST(sim_disc_acceleration) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "testcourse_1000m");
    CHECK(road != nullptr);
    if (!road) return;
    double last_quarter = 0;
    // A small front-drive car, a mid-engine car, a powerful rear-drive car, a race car: each quicker than the one before.
    for (const char* car : {"civic_r_01", "nsx_r_02", "viper_gts_99", "_787b_91"}) {
        sim::Entry e;
        CHECK(sim::stock_car(sdb, program, car, e.equipment));
        sim::Session s(sdb, program, road, std::span<const sim::Entry>(&e, 1));
        const sim::SpecFigures f = sim::spec_figures(s.spec(0));
        const double mass = double(s.record(0).p.mass.host()), watts = double(f.ps) * 735.5;
        const Run r = drive(s, 40, false);
        // Through the gears, on the road, straight.
        CHECK(r.top_gear >= 3 && r.on_road && r.quarter > 0);
        // The standing 400 m against the power and the mass: t = k (m / P)^(1/3) with k about 69 in SI units (the
        // rule of thumb 5.825 (lb / hp)^(1/3)); more for a start that spins the wheels or bogs.
        const double k = r.quarter / std::cbrt(mass / watts);
        std::printf("  %s: 400 m in %.2f s at %.0f km/h (k %.1f)\n", car, r.quarter, r.quarter_speed * 3.6, k);
        CHECK(k > 62 && k < 90);
        // The speed there: v = c (P / m)^(1/3), c about 9 (234 (hp / lb)^(1/3) mph).
        const double c = r.quarter_speed / std::cbrt(watts / mass);
        CHECK(c > 7.5 && c < 10.5);
        if (last_quarter > 0) CHECK(r.quarter < last_quarter);
        last_quarter = r.quarter;
    }
}

TEST(sim_disc_top_speed) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "testcourse");
    CHECK(road != nullptr);
    if (!road) return;
    for (const char* car : {"nsx_r_02", "viper_gts_99"}) {
        sim::Entry e;
        CHECK(sim::stock_car(sdb, program, car, e.equipment));
        sim::Session s(sdb, program, road, std::span<const sim::Entry>(&e, 1));
        const Run r = drive(s, 150, true);
        // Where the power meets the drag: P = 1/2 rho Cd A v^3 (rho 1.2; the spec's drag coefficient and frontal area),
        // less for the rolling resistance and the gearing.
        const sim::Spec& spec = s.spec(0);
        const double cda = double(spec.b(0x42)) * 0.01 * double(spec.h(0x40)) * 0.0001;
        const double limit = std::cbrt(double(sim::spec_figures(spec).ps) * 735.5 / (0.5 * 1.2 * cda));
        std::printf("  %s: top %.1f km/h, drag limit %.1f km/h\n", car, r.top * 3.6, limit * 3.6);
        CHECK(r.top > limit * 0.75 && r.top < limit * 1.02 && r.top_gear >= 5);
    }
}

TEST(sim_disc_cars_and_courses) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    struct Case {
        const char* car;
        const char* course;
        bool loose;
        float least; // metres along the course in 40 s
    };
    // A small front-drive car on a city course, a mid-engine car on a circuit, a race car on a long circuit, a rally car on dirt.
    for (const Case& k : {Case{"civic_r_01", "seoul", false, 700}, Case{"nsx_r_02", "tsukuba", false, 700}, Case{"_787b_91", "sarthe", false, 1000},
                          Case{"focus_wrc_99", "grandcanyon", true, 500}}) {
        sim::Entry e;
        CHECK(sim::stock_car(sdb, program, k.car, e.equipment));
        const std::shared_ptr<const course::Runway> road = open_road(*disc, k.course);
        CHECK(road != nullptr);
        if (!road) continue;
        sim::Session s(sdb, program, road, std::span<const sim::Entry>(&e, 1));
        CHECK(s.loose() == k.loose);
        const Run r = drive(s, 40, true, true);
        if (r.along < k.least) std::printf("  %s on %s: %.0f m along the course\n", k.car, k.course, double(r.along));
        CHECK(r.along > k.least && r.top_gear >= 2 && r.on_road);
        const race::CarState& c = s.cars()[0];
        CHECK(c.up.y > 0.7f && std::isfinite(c.position.x) && std::isfinite(c.speed));
    }
}

TEST(sim_disc_wet_courses) {
    // Every course with wet road (triangle flags bit 1: the surfaces 20 .. 22) opens and drives: the session's first
    // steps (the start's hold) and 10 s on the line, with the wheels on the wet road.
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    size_t wet_courses = 0;
    for (const std::string& name : disc->volume().list_dir("crs/")) {
        if (name.empty() || name.back() == '/' || name.find('.') != std::string::npos) continue;
        const std::shared_ptr<const course::Runway> road = open_road(*disc, name);
        if (!road || road->grid.empty()) continue;
        size_t wet = 0;
        for (const course::Runway::Triangle& t : road->triangles) wet += (t.kind & 2) != 0;
        if (wet == 0) continue;
        ++wet_courses;
        sim::Entry e;
        CHECK(sim::stock_car(sdb, program, "r34_gtr_v2_nur_02", e.equipment));
        sim::Session s(sdb, program, road, std::span<const sim::Entry>(&e, 1));
        bool on_wet = false;
        sim::LineDriver driver(s, 0);
        for (int i = 0; i < 600; ++i) {
            const race::CarInput in = driver.drive(1.0f, true);
            s.step_once(std::span<const race::CarInput>(&in, 1));
            for (const race::WheelState& w : s.cars()[0].wheels) on_wet |= (w.surface & 2) != 0;
        }
        const race::CarState& c = s.cars()[0];
        if (!on_wet || !(c.speed > 5)) std::printf("  %s: %zu wet triangles, on them %d, speed %.1f\n", name.c_str(), wet, int(on_wet), double(c.speed));
        CHECK(on_wet && std::isfinite(c.position.x) && c.speed > 5 && c.up.y > 0.7f);
    }
    std::printf("  %zu courses with wet road\n", wet_courses);
    CHECK(wet_courses >= 1);
}

TEST(sim_disc_session_open) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A session by names, as the race view asks for one; stepped through the seam.
    const std::string cars[] = {"nsx_r_02", "civic_r_01", "viper_gts_99"};
    const std::unique_ptr<sim::Session> s = sim::Session::open(*disc, "tsukuba", cars);
    race::Simulation& sim = *s;
    CHECK(sim.cars().size() == 3);
    race::CarInput in[3];
    for (race::CarInput& i : in) i.throttle = 1;
    for (int frame = 0; frame < 120; ++frame) sim.step(in, 1.0f / 60.0f);
    CHECK(std::fabs(s->time() - 2.0) < 0.02);
    for (const race::CarState& c : sim.cars()) {
        CHECK(c.speed > 3.0f && c.engine_rpm > 1000 && c.gear >= 1);
        // A right-handed frame, the wheels a tire's radius above the road around the body's origin.
        const float rx = c.forward.y * c.up.z - c.forward.z * c.up.y;
        CHECK(std::fabs(rx - c.right.x) < 1e-4f && c.up.y > 0.9f);
        for (const race::WheelState& w : c.wheels) {
            CHECK(std::hypot(w.center.x - c.position.x, w.center.z - c.position.z) < 3.5f);
            CHECK(w.center.y - c.position.y > -0.2f && w.center.y - c.position.y < 0.6f && w.spin_rate > 1.0f);
        }
        // Left wheels to the left of the origin, front wheels ahead.
        auto side = [&](int w) { return (c.wheels[w].center.x - c.position.x) * c.right.x + (c.wheels[w].center.z - c.position.z) * c.right.z; };
        auto ahead = [&](int w) { return (c.wheels[w].center.x - c.position.x) * c.forward.x + (c.wheels[w].center.z - c.position.z) * c.forward.z; };
        CHECK(side(0) < 0 && side(1) > 0 && side(2) < 0 && side(3) > 0);
        CHECK(ahead(0) > 0 && ahead(1) > 0 && ahead(2) < 0 && ahead(3) < 0);
        CHECK(std::fabs(ahead(0) + ahead(2)) < 0.15f); // the origin is the wheelbase's middle
    }
    bool threw = false;
    try {
        sim::Session::open(*disc, "tsukuba", std::span<const std::string>());
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(sim_disc_walls_hold) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "tsukuba");
    CHECK(road != nullptr);
    if (!road) return;
    // Straight on at full throttle into the first corner: over the sand into the wall, which stops the
    // car's run (it may then scrape along the walls, as Gran Turismo 4's tires let it), and never
    // through a wall: every step on the road's ground, upright.
    sim::Entry e;
    CHECK(sim::stock_car(sdb, program, "nsx_r_02", e.equipment));
    sim::Session s(sdb, program, road, std::span<const sim::Entry>(&e, 1));
    float fastest = 0;
    bool left_tarmac = false, struck = false;
    for (int i = 0; i < 30 * 60; ++i) {
        race::CarInput in;
        in.throttle = 1;
        s.step_once(std::span<const race::CarInput>(&in, 1));
        const race::CarState& c = s.cars()[0];
        fastest = std::max(fastest, c.speed);
        if (fastest > 30.0f && c.speed < 0.6f * fastest) struck = true;
        sim::GroundHit h;
        const bool on = s.ground().under(c.position, h);
        CHECK(on); // the walls stand on the road's surface: inside them there is ground
        if (!on) break;
        if (h.material != 0) left_tarmac = true;
    }
    const race::CarState& c = s.cars()[0];
    CHECK(fastest > 30.0f && left_tarmac && struck);
    CHECK(c.up.y > 0.8f);
}

TEST(sim_disc_two_cars_meet) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = open_road(*disc, "testcourse_1000m");
    CHECK(road != nullptr);
    if (!road) return;
    // Two cars on the same start place's line, 8 m apart: the one behind drives into the one ahead, which stands.
    sim::Entry e[2];
    CHECK(sim::stock_car(sdb, program, "viper_gts_99", e[0].equipment) && sim::stock_car(sdb, program, "nsx_r_02", e[1].equipment));
    e[0].grid_place = 0, e[1].grid_place = 1;
    sim::Session s(sdb, program, road, e);
    const race::CarState& ahead_before = s.cars()[0];
    const course::Vec3 a0 = ahead_before.position, b0 = s.cars()[1].position;
    // Which one is behind: the one whose forward points at the other.
    const float towards = (a0.x - b0.x) * s.cars()[1].forward.x + (a0.z - b0.z) * s.cars()[1].forward.z;
    const size_t behind = towards > 0 ? 1 : 0, ahead = 1 - behind;
    const float gap0 = std::hypot(a0.x - b0.x, a0.z - b0.z);
    CHECK(gap0 > 4.0f);
    bool pushed = false;
    for (int i = 0; i < 600; ++i) {
        race::CarInput in[2];
        in[behind].throttle = 1;
        // Steer the car behind at the one ahead.
        const race::CarState &c = s.cars()[behind], &t = s.cars()[ahead];
        const float dx = t.position.x - c.position.x, dz = t.position.z - c.position.z;
        in[behind].steer = std::clamp(std::atan2(dx * c.right.x + dz * c.right.z, std::max(dx * c.forward.x + dz * c.forward.z, 1.0f)) * 3.0f, -1.0f, 1.0f);
        s.step_once(in);
        if (std::fabs(s.cars()[ahead].speed) > 1.0f) pushed = true;
        // The bodies never pass through each other.
        const float gap = std::hypot(s.cars()[0].position.x - s.cars()[1].position.x, s.cars()[0].position.z - s.cars()[1].position.z);
        CHECK(gap > 1.2f);
        if (gap <= 1.2f) break;
    }
    CHECK(pushed);
}

TEST(sim_disc_steer_angle) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A computer driver's road-wheel command is the steering command itself (past the pad's lock
    // curve), within the full lock; without one the pad's share steers.
    const std::string cars[] = {"nsx_r_02"};
    const std::unique_ptr<sim::Session> s = sim::Session::open(*disc, "tsukuba", cars);
    race::CarInput in;
    in.throttle = 0.5f, in.steer_angle = 0.2f;
    for (int i = 0; i < 90; ++i) s->step_once({&in, 1});
    CHECK(std::fabs(s->record(0).steer_cmd.host() - 0.2f) < 1e-6f);
    in.steer_angle = 10.0f;
    s->step_once({&in, 1});
    CHECK(s->record(0).steer_cmd.host() == s->record(0).p.max_lock.host());
    in.steer_angle = NAN, in.steer = 0;
    for (int i = 0; i < 60; ++i) s->step_once({&in, 1});
    CHECK(std::fabs(s->record(0).steer_cmd.host()) < 0.01f);
}

TEST(sim_disc_tsukuba_pace) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Each car alone at Tsukuba with its computer driver, three laps from rest: the best lap near the program's own on
    // the program's physics (gt4r_diff ai, s: Volkswagen 1100 90.8, Honda S500 89.2, Ford Ka 83.1, MG Midget 91.6).
    // The suspension, attitude and aids are Gran Turismo 4's: with Gran Turismo 3's the S500 lost 8 s in the slow corners.
    // (The S500 is still some 3 s slower: ❓ docs/specs/physics.md §8.)
    const std::pair<const char*, double> cars[] = {{"_1100_standard_type11_49", 90.8}, {"z_act_70", 99.6}, {"n360_67", 100.4}, {"ka_01", 83.1},
                                                   {"midget2_d_type_98", 91.6}};
    for (const auto& [car, theirs] : cars) {
        const sim::ai::FieldRun run = sim::ai::run_field(*disc, "tsukuba", {car}, 3);
        CHECK(run.cars.size() == 1);
        if (run.cars.empty()) continue;
        double best = 0;
        for (double l : run.cars[0].laps)
            if (best == 0 || l < best) best = l;
        std::printf("  %-26s best %.3f s (the program's %.1f)\n", car, best, theirs);
        CHECK(run.cars[0].finished && best > theirs - 1.5 && best < theirs + 1.5);
    }
}
