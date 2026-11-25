// The license test's trophy motion (race/license_trophy.h), the tire diagram's colors (race_display.h tire_color) and
// the road sections' own start places, with the movie's external pictures.
#include "test_framework.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/race/license_trophy.h"
#include "gt4r/race/race_display.h"
#include "gt4r/race/sections.h"
#include "gt4r/race/strobe.h"

#include <cmath>

using namespace gt4r;

TEST(license_trophy_motion) {
    race::LicenseTrophy t;
    t.start(race::LicenseTrophy::kind_for(0)); // gold
    CHECK(t.place[2].value == -0.5f && t.turn[1].value == -22.5f && t.place[2].gain == 0.35f);
    for (int i = 0; i < 60 * 30; ++i) t.step(1.0f / 60);
    CHECK(std::fabs(t.place[2].value + 2.5f) < 0.02f); // come away from the camera to -2.5
    CHECK(std::fabs(t.spin[1].value - 60) < 0.5f && std::fabs(t.fade.value - 1) < 0.01f);
    t.start(race::LicenseTrophy::kind_for(1)); // silver: spun fast, turned half round
    CHECK(t.turn[1].value == 180 && t.spin[1].value == 1000);
    t.start(race::LicenseTrophy::kind_for(2)); // bronze
    CHECK(t.turn[1].value == 270 && t.turn[0].value == -180);
    CHECK(race::LicenseTrophy::kind_for(3) == 0); // a failed test: none
    CHECK(race::LicenseTrophy::material_for(1, 3) == 0 && race::LicenseTrophy::material_for(3, 3) == 1 && race::LicenseTrophy::material_for(4, 3) == 2);
    CHECK(race::LicenseTrophy::material_for(4, 1) == 0); // (a model with one material set)
}

TEST(tire_diagram_colors) {
    CHECK(race::tire_color(-0x80) == 0);                // no wear counted: black
    CHECK(race::tire_color(-0x3F) == (0xFCu << 16));    // blue
    CHECK(race::tire_color(0) == (0xFCu << 8));         // green
    CHECK(race::tire_color(0x10) == ((0x80u) | (0xFCu << 8))); // green turning yellow
    CHECK(race::tire_color(0x40) == (0xFCu | (0x7Eu << 8)));   // red side (g = 2 x 63)
    CHECK((race::tire_color(0x7F) & 0xFFFF) == 0xFC);          // red
}

TEST(license_section_places_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("crs/deepforest");
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const course::CourseFile f = course::CourseFile::parse(data);
        const ByteSpan road = f.section(data, course::kSlotRunway);
        const std::vector<race::RoadSection> sections = race::road_sections(road);
        CHECK(sections.size() >= 3);
        if (sections.size() < 3) continue;
        // B-3's section: its start 3522.7 m, its goal 659.3 m, its own grid place.
        const race::RoadSection& s = sections[2];
        CHECK(std::fabs(s.start - 3522.72f) < 0.1f && std::fabs(s.goal - 659.31f) < 0.1f && s.grid.size() == 1);
        if (s.grid.empty()) continue;
        CHECK(std::fabs(s.grid[0].pos.x + 8.891f) < 0.01f && std::fabs(s.grid[0].pos.y - 7.280f) < 0.01f && std::fabs(s.grid[0].pos.z + 256.900f) < 0.01f);
        CHECK(std::fabs(s.grid[0].heading + 1.5626f) < 0.001f);
    }
}

TEST(strobe_external_pictures_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("menu/US/result_license.strb");
        CHECK(e != nullptr);
        if (!e) continue;
        const Bytes file = disc->volume().read(*e);
        const race::StrobeMovie m = race::StrobeMovie::parse(file);
        CHECK(m.externals.size() == 2);
        if (m.externals.size() < 2) continue;
        // The live picture (300 x 225 about its origin) and the trophy's panel.
        CHECK(m.externals[0].name == "screen" && std::fabs(m.externals[0].x1 - 150) < 0.1f && std::fabs(m.externals[0].y1 - 112.5f) < 0.1f);
        CHECK(m.externals[1].name == "TropyPanel");
    }
}

// Every course's sections: the count at +0x0A is the number of 16-byte places between the pointers +0x18 and +0x1C, and
// a section's first place lies along the course where the section starts (a car's length or so either way of it).
TEST(road_section_places_every_course_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        size_t courses = 0, sections = 0, with_place = 0, count_agrees = 0, near_start = 0;
        for (const std::string& name : disc->volume().list_dir("crs")) {
            const gt3r::Volume::Entry* e = disc->volume().find("crs/" + name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile f = course::CourseFile::parse(data);
            const ByteSpan road = f.section(data, course::kSlotRunway);
            if (road.size() < 0xA0 || !gt3r::has_magic(road, "RNW4")) continue;
            ++courses;
            const std::optional<course::Runway> parsed = course::course_road(f, data);
            const std::vector<race::RoadSection> list = race::road_sections(road);
            const u32 at = rd32(road, 0x9C);
            for (size_t i = 0; i < list.size(); ++i) {
                ++sections;
                const size_t rec = at + 0x20 * i;
                if (list[i].grid.empty()) continue;
                ++with_place;
                const u32 gap = rd32(road, rec + 0x1C) - rd32(road, rec + 0x18);
                if (gap == 16 * list[i].grid.size()) ++count_agrees;
                else std::printf("  %s section %zu: %zu places, %u bytes\n", name.c_str(), i, list[i].grid.size(), gap);
                if (!parsed) continue;
                // The nearest checkpoint to the place, against the section's start.
                float best = 1e9f, v = 0;
                for (const course::Runway::Checkpoint& c : parsed->checkpoints) {
                    const float d = std::hypot(c.middle.x - list[i].grid[0].pos.x, c.middle.z - list[i].grid[0].pos.z);
                    if (d < best) best = d, v = c.v;
                }
                float off = std::fabs(v - list[i].start);
                if (parsed->length > 0) off = std::min(off, std::fabs(parsed->length - off));
                if (off < 60) ++near_start;
                else std::printf("  %s section %zu: start %.1f, its place by %.1f\n", name.c_str(), i, double(list[i].start), double(v));
            }
        }
        std::printf("  %zu courses, %zu sections, %zu with places: the count agrees on %zu, near the start on %zu\n", courses, sections, with_place, count_agrees, near_start);
        CHECK(with_place > 0 && count_agrees == with_place);
    }
}
