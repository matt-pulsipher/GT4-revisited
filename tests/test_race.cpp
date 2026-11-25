// The race view's parts that need no window: the stand-in simulation on a made-up road, then on
// the roads of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/rail_simulation.h"

#include <cmath>

using namespace gt4r;

namespace {

using course::Runway;
using race::Vec3;

float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(const Vec3& a) { return std::sqrt(dot(a, a)); }

// A flat square of road with a ring of checkpoints of radius `r`, driven clockwise seen from
// above (turning right all the way), the grid on the line.
Runway ring_road(float r, int n = 48) {
    Runway road;
    const float e = r + 60;
    road.vertices = {{{-e, 0, -e}}, {{e, 0, -e}}, {{e, 0, e}}, {{-e, 0, e}}};
    road.triangles = {{{0, 1, 2}}, {{0, 2, 3}}};
    road.length = 2 * 3.14159265f * r;
    road.min = {-e, 0, -e}, road.max = {e, 0, e};
    for (int i = 0; i < n; ++i) {
        // Travel direction at angle a: heading turns right as a grows.
        const float a = 2 * 3.14159265f * float(i) / float(n);
        const Vec3 out{std::cos(a), 0, std::sin(a)}; // from the ring's middle
        Runway::Checkpoint c;
        c.middle = {out.x * r, 0, out.z * r};
        c.left = {out.x * (r + 6), 0, out.z * (r + 6)};
        c.right = {out.x * (r - 6), 0, out.z * (r - 6)};
        c.v = road.length * float(i) / float(n);
        road.checkpoints.push_back(c);
    }
    for (int i = 0; i < 4; ++i) {
        const float a = -0.03f * float(i + 1);
        Runway::Place p;
        p.pos = {std::cos(a) * (r + (i & 1 ? 2.0f : -2.0f)), 0, std::sin(a) * (r + (i & 1 ? 2.0f : -2.0f))};
        road.grid.push_back(p);
    }
    return road;
}

void check_state(const race::CarState& s, const race::RailCar& spec) {
    CHECK(std::fabs(len(s.forward) - 1) < 1e-3f && std::fabs(len(s.up) - 1) < 1e-3f && std::fabs(len(s.right) - 1) < 1e-3f);
    CHECK(std::fabs(dot(s.forward, s.up)) < 1e-3f && std::fabs(dot(s.forward, s.right)) < 1e-3f);
    const Vec3 r = cross(s.forward, s.up);
    CHECK(dot(r, s.right) > 0.999f); // the right side is to the right of the direction faced
    CHECK(s.up.y > 0.7f);
    for (int w = 0; w < 4; ++w) {
        const race::WheelState& ws = s.wheels[w];
        CHECK(std::isfinite(ws.center.x) && std::isfinite(ws.spin) && std::isfinite(ws.travel));
        CHECK(std::fabs(ws.travel) < 0.25f);
        // Front wheels ahead of the rear ones, left wheels to the left.
        const Vec3 d{ws.center.x - s.position.x, ws.center.y - s.position.y, ws.center.z - s.position.z};
        CHECK((dot(d, s.forward) > 0) == (w < 2));
        CHECK((dot(d, s.right) > 0) == ((w & 1) == 1));
        CHECK(std::fabs(std::fabs(dot(d, s.forward)) - spec.wheelbase * 0.5f) < 0.2f);
    }
}

} // namespace

TEST(rail_line_ring) {
    const Runway road = ring_road(100);
    const race::RailLine line(road);
    CHECK(line.valid());
    CHECK(line.points.size() == 48);
    for (float v = 0; v < line.length; v += 7.3f) {
        const race::RailLine::Sample s = line.at(v);
        CHECK(std::fabs(std::hypot(s.pos.x, s.pos.z) - 100) < 0.5f);
        CHECK(std::fabs(s.curvature - 0.01f) < 0.002f); // turning right, radius 100
        CHECK(std::fabs(len(s.tangent) - 1) < 1e-4f);
        // To the right of travel is toward the ring's middle.
        CHECK(dot(s.across, {-s.pos.x, 0, -s.pos.z}) > 0);
        CHECK(s.left > 5 && s.right > 5);
        float pv, off;
        const Vec3 p{s.pos.x + s.across.x * 2, 0, s.pos.z + s.across.z * 2};
        line.project(p, pv, off);
        float dv = std::fabs(pv - v);
        dv = std::min(dv, line.length - dv);
        CHECK(dv < 1.0f);
        CHECK(std::fabs(off - 2) < 0.2f);
    }
    // It wraps both ways.
    CHECK(len({line.at(-5).pos.x - line.at(line.length - 5).pos.x, 0, line.at(-5).pos.z - line.at(line.length - 5).pos.z}) < 1e-3f);
}

TEST(rail_simulation_ring) {
    const Runway road = ring_road(100);
    std::vector<race::RailCar> specs(4);
    race::RailSimulation sim(road, specs);
    CHECK(sim.cars().size() == 4);
    for (const race::CarState& s : sim.cars()) {
        check_state(s, specs[0]);
        CHECK(s.speed == 0);
        CHECK(std::fabs(s.wheels[0].center.y - specs[0].tire_radius[0]) < 0.01f); // standing on the road
    }
    // Car 0 leads on the grid.
    CHECK(sim.cars()[0].course_v > sim.cars()[3].course_v || sim.cars()[0].course_v < 10);

    // Nobody touches anything: nothing moves.
    std::vector<race::CarInput> inputs(4);
    for (int i = 0; i < 60; ++i) sim.step(inputs, 1.0f / 60);
    CHECK(sim.cars()[0].speed == 0);

    // Two minutes with every car on the simple driver.
    float top = 0, max_travel = 0;
    bool braked = false, dipped = false;
    for (int i = 0; i < 120 * 60; ++i) {
        for (size_t c = 0; c < 4; ++c) inputs[c] = race::follow_line(sim.line(), sim.cars()[c], c & 1 ? 1.5f : -1.5f, 1.0f - 0.05f * float(c));
        if (i > 60 * 60 && i < 62 * 60) inputs[0].throttle = 0, inputs[0].brake = 1; // car 0 brakes hard for two seconds
        sim.step(inputs, 1.0f / 60);
        const race::CarState& s = sim.cars()[0];
        top = std::max(top, s.speed);
        for (int w = 0; w < 4; ++w) max_travel = std::max(max_travel, std::fabs(s.wheels[w].travel));
        if (i > 60 * 60 + 10 && i < 61 * 60) {
            braked |= s.brake_lamps;
            dipped |= s.wheels[0].travel > 0.005f && s.wheels[2].travel < -0.005f; // the nose dips: front springs shorten
        }
        if (i % 600 == 0)
            for (const race::CarState& c : sim.cars()) check_state(c, specs[0]);
    }
    CHECK(braked && dipped);
    CHECK(top > 20 && top < 40); // a 100 m bend holds about 31 m/s
    CHECK(max_travel > 0.005f);
    for (const race::CarState& s : sim.cars()) {
        CHECK(s.lap >= 2);
        CHECK(s.gear >= 1 && s.engine_rpm >= 900 && s.engine_rpm <= specs[0].rev_limit);
        CHECK(s.wheels[0].spin > 100);
        CHECK(s.wheels[0].steer > 0.005f); // turning right
        CHECK(std::fabs(std::hypot(s.position.x, s.position.z) - 100) < 9);
    }
    // Reverse: from rest the car backs up with its lamps on.
    race::RailSimulation one(road, std::vector<race::RailCar>(1));
    race::CarInput back;
    back.reverse = true, back.throttle = 1;
    for (int i = 0; i < 120; ++i) one.step({&back, 1}, 1.0f / 60);
    CHECK(one.cars()[0].speed < -1 && one.cars()[0].gear == -1 && one.cars()[0].reverse_lamps);
}

TEST(rail_simulation_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        for (const char* name : {"crs/tsukuba", "crs/grandvalley", "crs/yosemite_dirt", "crs/route5", "crs/nurburgring", "crs/ss_shibuya"}) {
            const gt3r::Volume::Entry* e = disc->volume().find(name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const ByteSpan section = file.section(data, course::kSlotRunway);
            if (!Runway::is_runway(section)) continue;
            const Runway road = Runway::parse(section);
            const race::RailLine line(road);
            if (road.checkpoints.size() < 3) continue; // (a photo location has no lap)
            CHECK(line.valid());
            // The line stays on the road and its length is the lap's.
            float total = 0;
            size_t off_road = 0;
            const size_t n = line.points.size();
            for (size_t i = 0; i < n; ++i) {
                const Vec3& a = line.points[i].pos;
                const Vec3& b = line.points[(i + 1) % n].pos;
                total += len({b.x - a.x, b.y - a.y, b.z - a.z});
                float y;
                off_road += !road.height_at(a.x, a.z, a.y + 3, y);
            }
            if (off_road * 20 > n) std::printf("  %s: %zu of %zu line points off the road\n", name, off_road, n);
            CHECK(off_road * 20 <= n); // (a few points of the longest courses lie over a gap in the road's mesh)
            CHECK(total > road.length * 0.9f && total < road.length * 1.1f);

            std::vector<race::RailCar> specs(6);
            race::RailSimulation sim(road, specs);
            std::vector<race::CarInput> inputs(6);
            float worst = 0;
            for (int i = 0; i < 45 * 60; ++i) {
                for (size_t c = 0; c < 6; ++c) inputs[c] = race::follow_line(sim.line(), sim.cars()[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.04f * float(c));
                sim.step(inputs, 1.0f / 60);
                if (i % 30 == 0)
                    for (const race::CarState& s : sim.cars()) {
                        check_state(s, specs[0]);
                        float y;
                        if (road.height_at(s.position.x, s.position.z, s.position.y + 3, y)) worst = std::max(worst, std::fabs(s.position.y - y));
                    }
            }
            CHECK(worst < 0.6f); // the body's reference point is at the road
            for (const race::CarState& s : sim.cars()) CHECK(s.speed > 5);
            // In 45 seconds the leader covers some hundreds of metres.
            const race::CarState& lead = sim.cars()[0];
            CHECK(lead.lap >= 0 || lead.course_v > 0);
            ++ran;
        }
    }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}

// --- The race display's numbers ---

#include "disc/core_image.h"
#include "gt4r/race/race_display.h"

TEST(race_display_numbers) {
    CHECK(race::format_lap_time(83456) == "1'23.456");
    CHECK(race::format_lap_time(0) == "0'00.000");
    CHECK(race::format_lap_time(10 * 60000 + 5) == "10'00.005");
    CHECK(race::format_lap_time(-1) == "\x7F'\x7F\x7F.\x7F\x7F\x7F"); // the font's dash
    // The bar's 40 segments: full at the red line / 0.7.
    CHECK(race::rev_bar_fraction(0, 7000) == 0);
    CHECK(std::fabs(race::rev_bar_fraction(7000, 7000) - 0.7f) < 1e-6f);
    CHECK(race::rev_bar_fraction(10000, 7000) == 1);
    CHECK(std::fabs(race::rev_bar_fraction(5100, 7000) - 20.0f / 40) < 1e-6f); // 0.51 of the bar: 20 whole segments
    // A color's 0x80 is full strength.
    const race::Rgba8 c = race::gs_color(0x80402010u);
    CHECK(c.r == 0x10 * 255 / 128 && c.g == 0x20 * 255 / 128 && c.b == 0x40 * 255 / 128 && c.a == 255);
    CHECK(race::gs_color(0x80FFFFFFu, 0.5f).r == 255 && race::gs_color(0x80FFFFFFu, 0.5f).a == 127);
    // Anchors: the middle tied to the left side; the right edge tied to the right.
    const race::DisplayPlace word{0, 56, 21, 0, 0, 6, 0};
    race::DisplayRect r = race::place_rect(word, 74, 20, false);
    CHECK(r.x == 56 - 37 && r.y == 21 && r.w == 74 && r.h == 20);
    r = race::place_rect(word, 74, 20, true);
    CHECK(r.x == 56 - 37 - 96);
    const race::DisplayPlace plate{0, 614, 420, 82, 26, 13, 0};
    r = race::place_rect(plate, 0, 0, true);
    CHECK(r.x == 614 + 96 - 82 && r.w == 82);
    const race::DisplayPlace box{0, 56, 86, 42, 45, 6, 1};
    r = race::place_rect(box, 42, 42, false);
    CHECK(r.x == 35 && r.y == 41 && r.h == 45);
    // Dials.
    CHECK(race::rev_dial_maximum(7500) == 8000 && race::rev_dial_maximum(7000) == 7000 && race::rev_dial_maximum(8200) == 9000);
    CHECK(std::fabs(race::needle_angle(0, 0, 240, 56, 276) - 146) < 1e-4f);
    CHECK(std::fabs(race::needle_angle(240, 0, 240, 56, 276) - 366) < 1e-4f);
    CHECK(std::fabs(race::needle_angle(999, 0, 240, 56, 276) - 368) < 1e-4f); // two degrees past the end at most
    // The map's numbers: the middle of the course at the middle of the picture, turned.
    race::CourseMap m;
    m.image.width = 180, m.image.height = 180;
    m.center_x = 10, m.center_z = -20, m.scale = 0.5f, m.rotation = 90;
    float px, py;
    m.map(10, -20, px, py);
    CHECK(px == 90 && py == 90);
    m.map(30, -20, px, py); // 20 m along +x, a quarter turn: down the picture
    CHECK(std::fabs(px - 90) < 1e-3f && std::fabs(py - 100) < 1e-3f);
    // A program that is not the one read: no places.
    const Bytes zeros(0x700000, 0);
    CHECK(!race::read_single_race_layout(zeros, 0x100000).valid());
    CHECK(race::read_rev_faces(zeros, 0x100000).empty());
}

TEST(race_display_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::CoreImage core = gt3r::CoreImage::decode(disc->read_program());
        const race::DisplayLayout layout = race::read_single_race_layout(core.program, core.load_address);
        CHECK(layout.valid());
        const race::DisplayPlace* rank = layout.find(race::element::kPosition);
        CHECK(rank && rank->x == 56 && rank->y == 86 && rank->w == 42 && rank->h == 45 && rank->anchor_x == 6 && rank->anchor_y == 1);
        const race::DisplayPlace* map = layout.find(race::element::kMap);
        CHECK(map && map->x == 24 && map->y == 96 && map->anchor_x == 4);
        const race::DisplayPlace* best = layout.find(race::element::kBestTime);
        CHECK(best && best->x == 550 && best->y == 58 && best->anchor_x == 14);
        CHECK(layout.find(race::element::kLapTime) && layout.find(race::element::kLastTime) && layout.find(race::element::kMessage));
        // The dials' faces.
        const std::vector<race::DialFace> revs = race::read_rev_faces(core.program, core.load_address);
        CHECK(revs.size() == 9);
        if (revs.size() == 9) {
            CHECK(revs[0].picture == "rev_4000" && revs[0].max == 4000 && revs[0].min == 0);
            CHECK(revs[4].picture == "rev_8000" && revs[4].min == 1000);
            CHECK(revs[8].picture == "rev_18000" && revs[8].min == 2000);
            const race::DialFace* f = race::pick_face(revs, 8000);
            CHECK(f && f->picture == "rev_8000");
            CHECK(race::pick_face(revs, 15000)->picture == "rev_18000");
        }
        const std::vector<race::DialFace> kmh = race::read_speed_faces(core.program, core.load_address, false);
        const std::vector<race::DialFace> mph = race::read_speed_faces(core.program, core.load_address, true);
        CHECK(kmh.size() == 4 && mph.size() == 3);
        if (kmh.size() == 4) CHECK(kmh[3].picture == "speed_320km" && race::pick_face(kmh, 240)->picture == "speed_240km");
        // Every course's map puts the grid's first place on the picture.
        size_t maps = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 4, "crs/") != 0 || e.path.find('/', 4) != std::string::npos || e.path.find('.') != std::string::npos) continue;
            if (e.path.compare(0, 7, "crs/ss_") == 0) continue; // (the photo locations' maps are an older layout)
            const Bytes data = disc->volume().read(e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const race::CourseMap m = race::CourseMap::parse(file.section(data, course::kSlotMiniMap));
            const ByteSpan rs = file.section(data, course::kSlotRunway);
            if (!m.valid() || !Runway::is_runway(rs)) continue;
            const Runway road = Runway::parse(rs);
            if (road.grid.empty()) continue;
            float px, py;
            m.map(road.grid[0].pos.x, road.grid[0].pos.z, px, py);
            CHECK(px >= 0 && py >= 0 && px < float(m.image.width) && py < float(m.image.height));
            ++maps;
        }
        CHECK(maps >= 80);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}

// --- A car's draw program settled for the race view, its sizes, its parts' places, the views ---

#include "formats/model/gtm1.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/race/car_pose.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/race/drive_camera.h"

#include <cstring>

namespace {

void put16(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8); }
void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }
void putf(Bytes& b, size_t o, float f) { std::memcpy(b.data() + o, &f, 4); }
void word(Bytes& b, u32 v) {
    b.resize(b.size() + 4);
    put32(b, b.size() - 4, v);
}

// A set of one model and one packed shape whose positions are 32-bit integers. The program is a
// car body's in small: a wheel's construct (callback 11: case 0 draws the shape, the brake
// parts), the brake lamps' switch (callback 0: case 1 draws the shape, the lit lens), then the
// shape again (the body).
Bytes car_like_set() {
    Bytes b(0x200, 0);
    std::memcpy(b.data(), "MDLS", 4);
    put16(b, 0x16, 1), put16(b, 0x18, 1), put16(b, 0x1A, 1);
    put32(b, 0x38, 0x80), put32(b, 0x3C, 0xB0), put32(b, 0x40, 0xC0), put32(b, 0x44, 0);
    putf(b, 0x80 + 0x08, 2.0f);
    put32(b, 0x80 + 0x18, 0x110);
    put32(b, 0xB0, 0x140);
    const u8 program[] = {
        0x06, 0x0B, 0x00, 0x02, 0x06, 0x00, 0x08, 0x00, // 0x110: callback 11 -> 0x11A, 0x11E
        0x08, 0x07,                                     // 0x118: to the construct's end, 0x120
        0x04, 0x00, 0x08, 0x03,                         // 0x11A: draw; to 0x120
        0x01, 0x01,                                     // 0x11E: (the tire alone: nothing here)
        0x06, 0x00, 0x00, 0x02, 0x06, 0x00, 0x06, 0x00, // 0x120: callback 0 -> 0x12A, 0x12C
        0x08, 0x05,                                     // 0x128: to 0x12E
        0x08, 0x03,                                     // 0x12A: unlit: to 0x12E
        0x04, 0x00,                                     // 0x12C: lit: draw
        0x04, 0x00, 0x00,                               // 0x12E: the body; end
    };
    std::memcpy(b.data() + 0x110, program, sizeof program);
    putf(b, 0x140 + 4, 0.01f);
    put16(b, 0x140 + 8, 0x4080), put16(b, 0x140 + 10, 1), put16(b, 0x140 + 12, 3), put16(b, 0x140 + 14, 1);
    Bytes p;
    word(p, 0x6801C0C0); // the GIF tag: 3 vertices
    word(p, 0x00008003), word(p, 0x301E4000), word(p, 0x00001412);
    word(p, 0x68038000); // positions: three vectors of 32-bit whole numbers at 0
    for (int v : {100000, 0, 0, 0, 200000, 0, -100000, 0, -50}) word(p, u32(v));
    word(p, 0x6E038080); // colors
    for (int i = 0; i < 3; ++i) word(p, 0x80404040u);
    word(p, 0x20000000), word(p, 0x3F3F3F3F);
    word(p, 0x7202C040), p.push_back(1), p.push_back(3), p.push_back(0), p.push_back(0); // one strip: a triangle
    while (p.size() & 15) p.push_back(0);
    put32(b, 0x140 + 0x10, 0x20), put16(b, 0x140 + 0x14, u32(p.size() / 16));
    b.resize(0x160);
    b.insert(b.end(), p.begin(), p.end());
    put32(b, 0x10, u32(b.size()));
    return b;
}

size_t draws_of(const Bytes& set, const mdls::Scene& scene) {
    const Bytes block = mdls::to_gtm1(set, nullptr, scene);
    return gt3r::gtm1::Gtm1::parse(block).run(0).size();
}

race::CarState standing_car(const race::CarSpec& spec, bool position_on_road) {
    // Heading along -x, on level ground at y = 5.
    race::CarState s;
    s.forward = {-1, 0, 0}, s.up = {0, 1, 0}, s.right = {0, 0, -1};
    const float q = 0.5f * (spec.ride_height[0] + spec.ride_height[1]);
    s.position = {10, position_on_road ? 5.0f : 5.0f + q, 20};
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        const float along = (axle == 0 ? 0.5f : -0.5f) * spec.wheelbase, out = ((w & 1) ? 1.0f : -1.0f) * spec.half_track[axle];
        s.wheels[w].center = {10 - along, 5 + spec.tire_radius[axle], 20 - out};
    }
    return s;
}

} // namespace

TEST(mdls_callbacks_settled) {
    const Bytes set = car_like_set();
    // As a still model: every switch is the decoder's (its own value 0 takes each first case).
    CHECK(draws_of(set, {}) == 2);
    mdls::Scene s;
    s.resolve_callbacks = true;
    s.callback_values[11] = 99; // no such case: the wheel's construct is left out
    s.callback_values[0] = 0;
    CHECK(draws_of(set, s) == 1);
    s.callback_values[0] = 1; // the lit lens
    CHECK(draws_of(set, s) == 2);
    s.callback_values[11] = 0;
    CHECK(draws_of(set, s) == 3);
    s.only_inside = 11; // the wheel's own shapes only
    CHECK(draws_of(set, s) == 1);
    s.callback_values[11] = 1;
    CHECK(draws_of(set, s) == 0);
    // An id without a value takes the default.
    mdls::Scene d;
    d.resolve_callbacks = true;
    d.callback_default = 99;
    CHECK(draws_of(set, d) == 1);

    // The packed shape's 32-bit positions are whole numbers times its scale.
    const Bytes block = mdls::to_gtm1(set);
    const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
    const gt3r::gtm1::DecodedPacket p = g.decode_packet(g.chunks[0].packets[0]);
    CHECK(p.vertices.size() == 3);
    if (p.vertices.size() == 3) {
        CHECK(std::fabs(p.vertices[0].pos[0] - 1000.0f) < 1e-3f && std::fabs(p.vertices[1].pos[1] - 2000.0f) < 1e-3f);
        CHECK(std::fabs(p.vertices[2].pos[0] + 1000.0f) < 1e-3f && std::fabs(p.vertices[2].pos[2] + 0.5f) < 1e-5f);
    }
}

TEST(car_pose) {
    race::CarSpec spec;
    spec.wheelbase = 2.665f, spec.half_track[0] = 0.74f, spec.half_track[1] = 0.745f;
    spec.ride_height[0] = spec.ride_height[1] = 0.13f;
    spec.tire_width[0] = spec.tire_width[1] = 0.245f;
    spec.tire_radius[0] = spec.tire_radius[1] = 0.3266f, spec.rim_radius[0] = spec.rim_radius[1] = 0.2286f;
    // The body's origin is the ride height above the road whichever point the state's position is.
    for (bool on_road : {true, false}) {
        const race::CarState s = standing_car(spec, on_road);
        const gt3r::render::Mat4 body = race::body_matrix(s, spec);
        CHECK(std::fabs(body.at(1, 3) - 5.13f) < 1e-4f && std::fabs(body.at(0, 3) - 10) < 1e-4f);
        // The model's front (-z) points along the car's forward axis; its hubs are where the wheels are.
        const gt3r::render::Vec3 nose = body.transform_point({0, 0, -1});
        CHECK(std::fabs(nose.x - 9) < 1e-4f && std::fabs(nose.z - 20) < 1e-4f);
        const gt3r::render::Vec3 hub = body.transform_point({-0.74f, 0.3266f - 0.13f, -1.3325f}); // front left
        CHECK(std::fabs(hub.x - s.wheels[0].center.x) < 1e-3f && std::fabs(hub.y - s.wheels[0].center.y) < 1e-3f && std::fabs(hub.z - s.wheels[0].center.z) < 1e-3f);
    }
    race::CarState s = standing_car(spec, true);
    // A left wheel's model has its outer face at x = 0 and runs inward along +x: the face lands
    // half a tire's width outside the hub, the inner edge as far inside.
    gt3r::render::Vec3 face = race::wheel_matrix(s, spec, 0).transform_point({0, 0, 0});
    gt3r::render::Vec3 inner = race::wheel_matrix(s, spec, 0).transform_point({0.245f / 0.2286f, 0, 0});
    CHECK(std::fabs(face.z - (s.wheels[0].center.z + 0.1225f)) < 1e-4f); // (the car's left is +z here)
    CHECK(std::fabs(inner.z - (s.wheels[0].center.z - 0.1225f)) < 1e-4f);
    // The right wheel is the same model turned half round: its face is outside too.
    face = race::wheel_matrix(s, spec, 1).transform_point({0, 0, 0});
    inner = race::wheel_matrix(s, spec, 1).transform_point({0.245f / 0.2286f, 0, 0});
    CHECK(std::fabs(face.z - (s.wheels[1].center.z - 0.1225f)) < 1e-4f && std::fabs(inner.z - (s.wheels[1].center.z + 0.1225f)) < 1e-4f);
    // A point on the tread at the top (the tire's radius up, in rim radii) rolls forward and down
    // as the wheel turns forward.
    const gt3r::render::Vec3 top{0, 0.3266f / 0.2286f, 0};
    s.wheels[0].spin = 0.5f;
    const gt3r::render::Vec3 rolled = race::wheel_matrix(s, spec, 0).transform_point(top);
    CHECK(rolled.x < s.wheels[0].center.x - 0.1f && rolled.y < s.wheels[0].center.y + 0.3266f - 0.02f);
    // Steering to the right turns the front of the left wheel inward (toward the car's right, -z here).
    s.wheels[0].spin = 0, s.wheels[0].steer = 0.3f;
    const gt3r::render::Vec3 front = race::wheel_frame(s, spec, 0).transform_point({0, 0, -1});
    CHECK(front.z < race::wheel_frame(s, spec, 0).transform_point({0, 0, 0}).z - 0.2f);
    // The turning pictures from 5 degrees a step.
    race::WheelState w;
    w.spin_rate = 5.0f; // 4.8 degrees in a sixtieth
    CHECK(!race::wheel_turning(w, 1.0f / 60));
    w.spin_rate = -5.5f;
    CHECK(race::wheel_turning(w, 1.0f / 60));
}

TEST(drive_camera) {
    race::CarSpec spec;
    race::CarState s = standing_car(spec, false);
    std::vector<race::CameraMount> mounts(7);
    mounts[0] = {{0, 1.0f, 0.2f}, 0, 0, 0, 2.0503f, 5};
    mounts[1] = {{0, 1.7f, 6.0f}, 8, 0, 0, 2.699f, 2};
    mounts[6] = {{0, 1.4f, 0.23f}, 0, 0, 0, 2.0503f, 0xF};
    race::DriveCamera cam;
    cam.reset(s);
    gt3r::render::Mat4 body = race::body_matrix(s, spec);
    // The chase view: 6 m behind the origin and 1.7 m above it, level, looking along the car.
    race::ViewPose p = cam.pose(s, body, mounts, race::DriveCamera::kChase, false);
    CHECK(std::fabs(p.eye.x - (body.at(0, 3) + 6)) < 1e-4f && std::fabs(p.eye.y - (body.at(1, 3) + 1.7f)) < 1e-4f && std::fabs(p.eye.z - body.at(2, 3)) < 1e-4f);
    CHECK(!p.body_hidden);
    gt3r::render::Vec3 ahead = p.view.transform_point({p.eye.x - 10, p.eye.y, p.eye.z}); // a point 10 m ahead of the eye
    CHECK(std::fabs(ahead.x) < 1e-3f && std::fabs(ahead.y) < 1e-3f && std::fabs(ahead.z + 10) < 1e-3f);
    // Looking back: in front of the car, looking at it.
    p = cam.pose(s, body, mounts, race::DriveCamera::kChase, true);
    CHECK(std::fabs(p.eye.x - (body.at(0, 3) - 6)) < 1e-4f);
    CHECK(p.view.transform_point({body.at(0, 3), p.eye.y, body.at(2, 3)}).z < -5);
    // The car turns a quarter turn to its right at once: the view follows through its spring,
    // most of the way in half a second, all of it in two, the eye always 6 m from the origin.
    s.forward = {0, 0, -1}, s.right = {1, 0, 0};
    for (int w = 0; w < 4; ++w) s.wheels[w].center = {10, 5 + spec.tire_radius[0], 20};
    body = race::body_matrix(s, spec);
    float after_half = 0;
    for (int i = 0; i < 120; ++i) {
        cam.step(s, 1.0f / 60);
        p = cam.pose(s, body, mounts, race::DriveCamera::kChase, false);
        const float dx = p.eye.x - body.at(0, 3), dz = p.eye.z - body.at(2, 3);
        CHECK(std::fabs(std::hypot(dx, dz) - 6) < 1e-3f);
        if (i == 29) after_half = dz;
    }
    CHECK(after_half > 4.5f);                 // (behind the car is +z now)
    CHECK(std::fabs(p.eye.z - (body.at(2, 3) + 6)) < 0.05f && std::fabs(p.eye.x - body.at(0, 3)) < 0.3f);
    // The in-car view hides the body and rides it; the roof view shows it, from its mount's lens.
    p = cam.pose(s, body, mounts, race::DriveCamera::kInCar, false);
    CHECK(p.body_hidden && std::fabs(p.eye.y - (body.at(1, 3) + 1.0f)) < 1e-4f);
    p = cam.pose(s, body, mounts, race::DriveCamera::kRoof, false);
    CHECK(!p.body_hidden && std::fabs(p.fov - 2 * std::atan(1 / 2.0503f)) < 1e-5f);
    CHECK(std::fabs(p.fovy() - 2 * std::atan(std::tan(p.fov / 2) * 0.75f)) < 1e-6f);
}

TEST(car_spec_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        gt3r::Volume* vol = &disc->volume();
        db::SpecDb sdb(
            [vol](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = vol->find(path);
                return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
            },
            [vol](const std::string& dir) { return vol->list_dir(dir); });
        // The Skyline GT-R V-spec II Nur (R34): 245/40R18 all round, treads 1480 / 1490, 130 mm off the road.
        std::string label;
        size_t from_db = 0, cars = 0;
        for (const db::CarEntry& c : sdb.cars()) {
            if (c.model == "niss0021") label = c.label;
            const race::CarSpec s = race::read_car_spec(sdb, c.label);
            ++cars, from_db += s.from_database;
            // (The ranges take in the three-wheeler, whose front wheels are 12 mm apart, and the cars
            // of 1886 on 50 mm tires that turn 500 times a minute.)
            CHECK(s.wheelbase > 1.2f && s.wheelbase < 5.0f);
            for (int a = 0; a < 2; ++a) {
                CHECK(s.tire_radius[a] > 0.2f && s.tire_radius[a] < 0.6f && s.rim_radius[a] > 0.1f && s.rim_radius[a] < s.tire_radius[a]);
                CHECK(s.half_track[a] >= 0.0f && s.half_track[a] < 1.3f && s.tire_width[a] > 0.04f && s.tire_width[a] < 0.5f);
                CHECK(s.ride_height[a] > 0.02f && s.ride_height[a] < 0.5f);
            }
            CHECK(s.red_line >= 400 && s.red_line <= 20000 && s.rev_limit >= s.red_line);
        }
        CHECK(cars > 700 && from_db * 10 >= cars * 9);
        CHECK(!label.empty());
        const race::CarSpec r34 = race::read_car_spec(sdb, label);
        CHECK(r34.from_database);
        CHECK(std::fabs(r34.wheelbase - 2.665f) < 1e-3f);
        CHECK(std::fabs(r34.half_track[0] - 0.740f) < 1e-3f && std::fabs(r34.half_track[1] - 0.745f) < 1e-3f);
        CHECK(std::fabs(r34.ride_height[0] - 0.130f) < 1e-3f);
        CHECK(std::fabs(r34.tire_width[0] - 0.245f) < 1e-3f && std::fabs(r34.rim_radius[0] - 0.2286f) < 1e-3f && std::fabs(r34.tire_radius[0] - 0.3266f) < 1e-3f);
        const std::vector<u32> paints = race::car_paints(sdb, label);
        CHECK(paints.size() == 7);
        // Its camera places: the chase view 6 m behind and 1.7 m up.
        const std::vector<race::CameraMount> mounts = race::read_camera_mounts(vol->read(*vol->find("car/lod/niss0021")));
        CHECK(mounts.size() == 25);
        if (mounts.size() == 25) {
            CHECK(std::fabs(mounts[1].pos.y - 1.7f) < 1e-3f && std::fabs(mounts[1].pos.z - 6.0f) < 1e-3f && mounts[1].flags == 2);
            CHECK(mounts[0].flags == 5 && mounts[0].pos.y > 0.8f && mounts[0].pos.y < 1.3f && (mounts[6].flags & 2));
        }
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}

// --- Replay cameras ---

#include "gt4r/race/replay_script.h"

namespace {

void curve(Bytes& b, std::initializer_list<std::array<float, 2>> keys) {
    word(b, u32(keys.size()));
    for (const auto& k : keys) {
        b.resize(b.size() + 8);
        putf(b, b.size() - 8, k[0]), putf(b, b.size() - 4, k[1]);
    }
}

// A script of two shots on a lap of 1000 m: a camera standing beside the road that follows the
// car from 0 to 500 with a 30 degree lens, then one moving along x that looks at a fixed point,
// its lens the picture's width at the target.
Bytes two_shot_script() {
    Bytes b(0x48 + 2 * 0x7C, 0);
    std::memcpy(b.data(), "REP4", 4);
    putf(b, 0x14, 1000.0f), putf(b, 0x18, 2000.0f);
    put32(b, 0x1C, 1), put32(b, 0x20, 2), put32(b, 0x24, 0x48);
    auto add = [&](size_t record, size_t field, std::initializer_list<std::array<float, 2>> keys) {
        put32(b, 0x48 + record * 0x7C + field, u32(b.size()));
        curve(b, keys);
    };
    size_t r = 0x48;
    put32(b, r, 4), putf(b, r + 4, 0.0f), putf(b, r + 8, 500.0f), putf(b, r + 0x1C, 8.0f), putf(b, r + 0x5C, 1.0f), putf(b, r + 0x60, 100.0f);
    add(0, 0x34, {{0, 10}, {1, 10}}), add(0, 0x38, {{0, 2}, {1, 2}}), add(0, 0x3C, {{0, 0}, {1, 0}});
    add(0, 0x4C, {{0, 30}, {1, 30}});
    r += 0x7C;
    put32(b, r, 0x100), putf(b, r + 4, 500.0f), putf(b, r + 8, 2000.0f), putf(b, r + 0x5C, 1.0f), putf(b, r + 0x60, 100.0f);
    add(1, 0x34, {{0, 0}, {1, 100}}), add(1, 0x38, {{0, 5}, {1, 5}}), add(1, 0x3C, {{0, 0}, {1, 0}});
    add(1, 0x40, {{0, 50}, {1, 50}}), add(1, 0x44, {{0, 5}, {1, 5}}), add(1, 0x48, {{0, 40}, {1, 40}});
    add(1, 0x4C, {{0, 20}, {1, 20}});
    put32(b, 0x0C, u32(b.size()));
    return b;
}

} // namespace

TEST(replay_script) {
    const Bytes file = two_shot_script();
    const race::ReplayScript s = race::ReplayScript::parse(file);
    CHECK(s.records.size() == 2 && s.lap_length == 1000 && s.end == 2000);
    CHECK(s.period() == 1000 && s.variant_count() == 1);
    CHECK(s.find(0) == &s.records[0] && s.find(499) == &s.records[0] && s.find(500) == &s.records[1] && s.find(2500) == nullptr);
    CHECK(s.distance(0, 250) == 250 && s.distance(3, 250) == 250);
    // The program's distance (0x3DDF18): crossings x L + v, at or past the end back by whole periods, a
    // negative distance kept; its choice (0x3DDFF0) as `find`, none before the first record.
    CHECK(s.distance(1, 250) == 1250 && s.distance(2, 250) == 250 && s.distance(0, -30) == -30);
    CHECK(s.choose(1250) == &s.records[1] && s.choose(250) == &s.records[0] && s.choose(-30) == nullptr);
    const race::ReplayCurve& c = s.records[1].eye[0];
    CHECK(c.at(-1) == 0 && c.at(0.25f) == 25 && c.at(2) == 100);
    CHECK(s.records[0].looks_at_car() && !s.records[1].looks_at_car() && s.records[1].lens_is_width() && !s.records[0].on_time());

    // The first shot: from (10, 2, 0), looking at the car 0.4 m above its origin.
    race::CarState car;
    car.course_v = 100, car.lap = -1; // (before the line: no crossing yet, the script distance is v)
    gt3r::render::Mat4 body;
    body.at(0, 3) = 10, body.at(1, 3) = 0, body.at(2, 3) = -30;
    race::ReplayCamera cam;
    race::ViewPose p;
    CHECK(cam.pose(s, car, body, p));
    CHECK(cam.record() == &s.records[0]);
    CHECK(p.eye.x == 10 && p.eye.y == 2 && p.eye.z == 0);
    const gt3r::render::Vec3 seen = p.view.transform_point({10, 0.4f, -30});
    // (A car shot keeps the car below the middle by 0.15 x the lens angle x cos(pitch): 0x36EBE8.)
    const float pitch = std::atan2(1.6f, 30.0f), below = 0.15f * 30 * std::cos(pitch) * 3.14159265f / 180;
    CHECK(std::fabs(seen.x) < 1e-3f && seen.z < -29);
    CHECK(std::fabs(std::atan2(-seen.y, -seen.z) - below) < 1e-3f);
    CHECK(std::fabs(p.fov - 30 * 3.14159265f / 180) < 1e-5f);
    // The second: a quarter of the way along its path, its lens from the picture's width at the target.
    car.course_v = 875;
    CHECK(cam.pose(s, car, body, p));
    CHECK(cam.record() == &s.records[1] && std::fabs(p.eye.x - 25) < 1e-3f);
    const float distance = std::sqrt(25.0f * 25 + 40 * 40);
    CHECK(std::fabs(p.fov - 2 * std::atan(20 / (2 * distance))) < 1e-4f);
    CHECK(std::fabs(p.view.transform_point({50, 5, 40}).x) < 1e-3f);

    // Not a script; a record table past the block.
    bool threw = false;
    try {
        race::ReplayScript::parse(ByteSpan(file).first(0x30));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
    Bytes bad = file;
    put32(bad, 0x20, 4000);
    threw = false;
    try {
        race::ReplayScript::parse(bad);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

// Another car followed from the same trackside camera: the shot's first view is on the new car (no turn left over
// from the last one), and it says it is a cut, so that no frame is shown between the two cars' views.
TEST(replay_camera_another_car) {
    const race::ReplayScript s = race::ReplayScript::parse(two_shot_script());
    race::CarState a, b;
    a.course_v = 100, a.lap = -1;
    b.course_v = 300, b.lap = -1;
    gt3r::render::Mat4 at_a, at_b;
    at_a.at(0, 3) = 10, at_a.at(2, 3) = -30;
    at_b.at(0, 3) = 10, at_b.at(2, 3) = 30;
    race::ReplayCamera cam;
    race::ViewPose p, q, r;
    CHECK(cam.pose(s, a, at_a, p) && cam.cut());
    for (int i = 0; i < 30; ++i) CHECK(cam.pose(s, a, at_a, p) && !cam.cut());
    cam.reset();
    CHECK(cam.pose(s, b, at_b, q) && cam.cut());
    CHECK(cam.record() == &s.records[0] && q.eye.x == p.eye.x && q.eye.z == p.eye.z);
    CHECK(std::fabs(q.view.transform_point({10, 0.4f, 30}).x) < 1e-3f && q.view.transform_point({10, 0.4f, 30}).z < -29);
    CHECK(cam.pose(s, b, at_b, r) && !cam.cut());
    CHECK(std::fabs(r.view.transform_point({10, 0.4f, 30}).x) < 0.5f);
    // (By their places alone the two views would be blended.)
    CHECK(race::blend_poses(p, q, 0.5f).view.at(2, 2) != q.view.at(2, 2));
    // From one shot to the next.
    b.course_v = 875;
    CHECK(cam.pose(s, b, at_b, r) && cam.cut());
}

TEST(replay_script_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t scripts = 0, records = 0, covered = 0, asked = 0, backward = 0, focused = 0, hazy = 0;
    bool full_game = false;
    for (const auto& disc : test::discs()) {
        full_game |= disc->info().game == Game::GT4;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 4, "crs/") != 0 || e.path.find('/', 4) != std::string::npos || e.path.find('.') != std::string::npos) continue;
            const Bytes data = disc->volume().read(e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            // Every script of the course reads: the replay's, the two round the car, the preview's.
            for (int slot : {44, 45, 46, 48}) {
                const ByteSpan section = file.section(data, slot);
                if (!race::ReplayScript::is_script(section)) continue;
                const race::ReplayScript s = race::ReplayScript::parse(section);
                CHECK(!s.records.empty());
                // (One record of Prologue's paris_tarmac ends before it starts: no car is ever in it.)
                for (const race::ReplayRecord& r : s.records) {
                    backward += r.end < r.start;
                    CHECK(!r.eye[0].empty() && r.aperture >= 1);
                }
                if (slot != 44) continue;
                ++scripts, records += s.records.size();
                // What the shots ask of the picture: the depth of field with every opening but 8, the haze on some.
                if (disc->info().game == Game::GT4)
                    for (const race::ReplayRecord& r : s.records) {
                        CHECK(r.depth_of_field == (r.aperture != 8));
                        focused += r.depth_of_field, hazy += r.haze;
                    }
                // A car driven round the lap has a shot nearly everywhere.
                const ByteSpan rs = file.section(data, course::kSlotRunway);
                if (!Runway::is_runway(rs) || !(s.lap_length > 0)) continue;
                // (The gymkhana areas, the drag strips and the photo locations have a shot for a
                // stretch only.)
                if (e.path.find("gymkhana") != std::string::npos || e.path.find("testcourse_") != std::string::npos || e.path.find("/ss_") != std::string::npos) continue;
                race::ReplayCamera cam;
                gt3r::render::Mat4 body;
                for (float v = 0; v < s.lap_length; v += s.lap_length / 50) {
                    race::CarState car;
                    car.course_v = v, car.lap = 1;
                    race::ViewPose p;
                    ++asked, covered += cam.pose(s, car, body, p);
                    if (!cam.record() && std::getenv("GT4R_TEST_VERBOSE"))
                        std::printf("  %s: no shot at %.0f of %.0f (script %.0f .. %.0f, %d variants, stated %u)\n", e.path.c_str(), v, s.lap_length,
                                    s.records.front().start, s.end, s.variant_count(), s.variants);
                }
            }
        }
    }
    if (!scripts) SKIP("no Gran Turismo 4 disc");
    if (full_game) CHECK(scripts >= 100 && records > 6000 && focused > 2000 && hazy > 200);
    CHECK(backward <= 3);
    CHECK(covered == asked);
}

// --- The model sets' scripts and a course's animated models ---

#include "gt4r/formats/model/model_script.h"
#include "gt4r/race/animated_models.h"

namespace {

u32 float_bits(float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    return v;
}
void op(Bytes& b, u8 code) { b.push_back(code); }
void op16(Bytes& b, u8 code, u32 v) { b.push_back(code), b.push_back(u8(v)), b.push_back(u8(v >> 8)); }
void opf(Bytes& b, float f) {
    b.push_back(0x11);
    const u32 v = float_bits(f);
    for (int k = 0; k < 4; ++k) b.push_back(u8(v >> (8 * k)));
}

} // namespace

TEST(model_script_machine) {
    // A flag's script: t = fmod(time / 1000 + 0.5, 1) x 10; register 1 its whole part (the key
    // shape), register 0 its fraction (the weight).
    mdls::Script s;
    s.register_cells = 2;
    s.host = {{"deltaT", 1, 1}, {"time", 1, 0}};
    Bytes& c = s.code;
    c.push_back(0x20), c.push_back(1);                 // a local
    op16(c, 0x12, 0x8000), op(c, 0x3C);                // time as a float
    opf(c, 1000), op(c, 0x83), opf(c, 0.5f), op(c, 0x80), opf(c, 1), op(c, 0x84);
    opf(c, 10), op(c, 0x82), op16(c, 0x13, 2);         // local 2 = t
    op16(c, 0x10, 2), op(c, 0x8C), op16(c, 0x14, 1);   // register 1 = int(t)
    op16(c, 0x10, 2), opf(c, 1), op(c, 0x84), op16(c, 0x14, 0); // register 0 = fmod(t, 1)
    c.push_back(0x23), c.push_back(0);
    CHECK(s.host_cell("time") == 0 && s.host_cell("deltaT") == 1 && s.host_cell("brake") == -1);
    mdls::ScriptState st(s);
    st.set_host(0, 1230);
    CHECK(mdls::run_script(s, 0, st));
    CHECK(st.register_i(1) == 7 && std::fabs(st.register_f(0) - 0.3f) < 1e-4f);
    st.set_host(0, 500);
    CHECK(mdls::run_script(s, 0, st));
    CHECK(st.register_i(1) == 0 && std::fabs(st.register_f(0)) < 1e-5f);

    // Integers, a jump when zero, a question to the owner, a variable of a segment the set lacks.
    mdls::Script q;
    q.register_cells = 4;
    Bytes& d = q.code;
    op16(d, 0x12, 0x4005);                              // segment 1 has no cells: 0
    op16(d, 0x26, 5);                                   // when 0: over the next command (5 bytes)
    d.push_back(0x11), d.push_back(99), d.push_back(0), d.push_back(0), d.push_back(0); // (skipped) push 99
    d.push_back(0x11), d.push_back(7), d.push_back(0), d.push_back(0), d.push_back(0);
    d.push_back(0x11), d.push_back(3), d.push_back(0), d.push_back(0), d.push_back(0);
    op(d, 0x34), op16(d, 0x14, 2);                      // register 2 = 7 % 3
    d.push_back(0xC1), d.push_back(1), d.push_back(0), d.push_back(0), d.push_back(0); // ask id 1 on register 0
    op16(d, 0x14, 3);
    d.push_back(0x23), d.push_back(0);
    mdls::ScriptState qs(q);
    qs.registers[0] = float_bits(4.0f);
    u32 asked = 99;
    CHECK(mdls::run_script(q, 0, qs, [&](u32 id, std::span<const u32> cells) {
        asked = id;
        return cells.empty() ? 0.0f : 2.5f;
    }));
    CHECK(asked == 1 && qs.register_i(2) == 1 && std::fabs(qs.register_f(3) - 2.5f) < 1e-6f);
    // It stops on an unknown command and on a program that never ends.
    mdls::Script bad;
    bad.code = {0xEE};
    mdls::ScriptState bs(bad);
    CHECK(!mdls::run_script(bad, 0, bs));
    bad.code = {0x25, 0xFD, 0xFF}; // a jump to itself
    CHECK(!mdls::run_script(bad, 0, bs));
    CHECK(!mdls::run_script(bad, 5, bs));
}

TEST(animated_models_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        const gt3r::Volume::Entry* e = disc->volume().find("crs/tsukuba");
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const course::CourseFile file = course::CourseFile::parse(data);
        const ByteSpan set = file.section(data, course::kSlotWorld);
        // Every script of the set runs to its end.
        const mdls::Script script = mdls::Script::parse(set);
        CHECK(!script.empty() && script.host_cell("time") == 0);
        mdls::ScriptState st(script);
        st.set_host(script.host_cell("time"), 1230);
        size_t draws = 0;
        for (const mdls::Script::Entries& m : script.models)
            if (m.draw >= 0) {
                ++draws;
                CHECK(mdls::run_script(script, m.draw, st));
            }
        CHECK(draws == 19); // the flags

        gt3r::gtm1::Options o;
        o.course_env_maps = true, o.alpha_test = true, o.gs_state = true;
        race::AnimatedModels flags(set, 2.0f, o);
        CHECK(flags.models().size() == 19);
        CHECK(!flags.textures().textures.empty());
        // A second into the race and a third of a second later: the same flags, other shapes.
        for (int i = 0; i < 60; ++i) flags.tick(1.0f / 60, {});
        const std::vector<gt3r::scene::Mesh> a = flags.meshes({});
        for (int i = 0; i < 20; ++i) flags.tick(1.0f / 60, {});
        const std::vector<gt3r::scene::Mesh> b = flags.meshes({});
        CHECK(a.size() == 19 && b.size() == 19);
        size_t moved = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            CHECK(a[i].node == b[i].node && !a[i].vertices.empty() && !a[i].indices.empty());
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (const gt3r::scene::Vertex& v : a[i].vertices)
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v.pos[k]), hi[k] = std::max(hi[k], v.pos[k]);
            // A flag is a few metres of cloth above the pits, not a shape across the course.
            CHECK(hi[0] - lo[0] < 12 && hi[1] - lo[1] < 12 && hi[2] - lo[2] < 12 && lo[1] > 5 && hi[1] < 40);
            if (a[i].vertices.size() == b[i].vertices.size())
                for (size_t v = 0; v < a[i].vertices.size(); ++v)
                    if (std::fabs(a[i].vertices[v].pos[0] - b[i].vertices[v].pos[0]) + std::fabs(a[i].vertices[v].pos[2] - b[i].vertices[v].pos[2]) > 0.01f) {
                        ++moved;
                        break;
                    }
        }
        CHECK(moved >= 15);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}

TEST(animated_models_turning_signs_disc) {
    // The turning signs of seoul and lasvegas (their programs turn the whole model by the registers
    // builtin_ra, rx, ry, rz: command 0x48) turn about their own axis as the time goes on.
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs())
        for (const auto& [name, model] : {std::pair{"crs/seoul", 44u}, std::pair{"crs/lasvegas", 41u}}) {
            if (disc->info().game != Game::GT4) continue; // (Prologue's courses are other sets)
            const gt3r::Volume::Entry* e = disc->volume().find(name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            const course::CourseFile file = course::CourseFile::parse(data);
            gt3r::gtm1::Options o;
            o.course_env_maps = true, o.alpha_test = true, o.gs_state = true;
            race::AnimatedModels set(file.section(data, course::kSlotWorld), 2.0f, o);
            const std::vector<u32> taken = set.models();
            CHECK(std::find(taken.begin(), taken.end(), model) != taken.end());
            auto sign = [&](std::vector<gt3r::scene::Mesh> meshes) {
                for (gt3r::scene::Mesh& m : meshes)
                    if (m.node == int(model)) return m;
                return gt3r::scene::Mesh{};
            };
            for (int i = 0; i < 60; ++i) set.tick(1.0f / 60, {});
            const gt3r::scene::Mesh a = sign(set.meshes({}));
            for (int i = 0; i < 30; ++i) set.tick(1.0f / 60, {});
            const gt3r::scene::Mesh b = sign(set.meshes({}));
            CHECK(!a.vertices.empty() && a.vertices.size() == b.vertices.size());
            if (a.vertices.empty() || a.vertices.size() != b.vertices.size()) continue;
            // Turned in place: the vertices moved (but stay within the sign's size), each at the same height, the shape
            // rigid (every vertex as far from the first as before).
            float moved = 0, worst = 0;
            for (size_t v = 0; v < a.vertices.size(); ++v) {
                const float* p = a.vertices[v].pos;
                const float* q = b.vertices[v].pos;
                const float* p0 = a.vertices[0].pos;
                const float* q0 = b.vertices[0].pos;
                moved = std::max(moved, std::hypot(p[0] - q[0], p[2] - q[2]));
                const float dp = std::hypot(p[0] - p0[0], p[1] - p0[1], p[2] - p0[2]), dq = std::hypot(q[0] - q0[0], q[1] - q0[1], q[2] - q0[2]);
                worst = std::max({worst, std::fabs(p[1] - q[1]), std::fabs(dp - dq)});
            }
            CHECK(moved > 0.05f && moved < 20.0f && worst < 0.01f);
            ++ran;
        }
    if (!ran) SKIP("no Gran Turismo 4 disc");
}

// --- A course's far sets: positions too large for 16 bits ---

TEST(course_backdrop_positions_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t sets = 0;
    for (const auto& disc : test::discs()) {
        for (const char* name : {"crs/tsukuba", "crs/grandvalley", "crs/route5", "crs/yosemite_dirt"}) {
            const gt3r::Volume::Entry* e = disc->volume().find(name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            for (int slot : {course::kSlotFar, course::kSlotWorld}) {
                const ByteSpan set = file.section(data, slot);
                if (!mdls::is_mdls(set)) continue;
                gt3r::gtm1::Options o;
                o.textures = false;
                const gt3r::scene::Model m = mdls::build_model(set, o, {}, name);
                size_t vertices = 0, at_origin = 0, wild = 0;
                for (const gt3r::scene::Mesh& mesh : m.meshes)
                    for (const gt3r::scene::Vertex& v : mesh.vertices) {
                        ++vertices;
                        at_origin += v.pos[0] == 0 && v.pos[1] == 0 && v.pos[2] == 0;
                        wild += !(std::isfinite(v.pos[0]) && std::isfinite(v.pos[1]) && std::isfinite(v.pos[2])) || std::fabs(v.pos[0]) > 30000 ||
                                std::fabs(v.pos[1]) > 30000 || std::fabs(v.pos[2]) > 30000;
                    }
                // No vertex is left at the origin by a position read as the wrong kind of number, and
                // none is thrown far off the course.
                if (vertices < 100) continue; // (a course whose backdrop is a shape or two)
                CHECK(at_origin * 1000 <= vertices);
                CHECK(wild == 0);
                ++sets;
            }
        }
    }
    if (!sets) SKIP("no Gran Turismo 4 disc");
}
