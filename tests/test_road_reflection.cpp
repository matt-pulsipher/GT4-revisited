// GT4's road reflection (src/gt4r/race/road_reflection.h; docs/formats/course.md "Road reflection"): the composite's texture
// coordinates and color as RacePS2Base::vf111 (0x3411D0) makes them, checked against the original's GS dump of dry Tsukuba
// (the composite strips' texture coordinates 0 .. 64.5 over the 640 x 448 frame, color 64 64 64).

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/road_reflection.h"

#include <cstring>
#include <map>

using namespace gt4r;

namespace {

void put_f32(Bytes& b, size_t o, float v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    for (int k = 0; k < 4; ++k) b[o + size_t(k)] = u8(u >> (8 * k));
}

} // namespace

// The composite's far edge: w + 0.5 - n texels (the blur count less a half, from the picture's size).
TEST(road_reflection_extent) {
    CHECK(race::road_reflection_extent(64, 0) == 64.5f); // the dump's dry Tsukuba: 0 .. 64.5
    CHECK(race::road_reflection_extent(64, 1) == 63.5f);
    CHECK(race::road_reflection_extent(256, 2) == 254.5f); // wet Tsukuba's 256 x 128 picture, 2 passes
    CHECK(race::road_reflection_extent(128, 2) == 126.5f);
}

// The composite's color: each of +0xC10..+0xC18 times the gain +0xC1C, as a GS byte int(min(c x 128 + 0.5, 255)).
TEST(road_reflection_color) {
    Bytes env(0x1C00, 0);
    std::memcpy(env.data(), "Env0", 4);
    for (int i = 0; i < 3; ++i) put_f32(env, 0xC10 + size_t(i) * 4, 1.0f);
    put_f32(env, 0xC1C, 0.5f);
    CHECK((race::road_reflection_color(env) == std::array<u8, 3>{64, 64, 64}));
    put_f32(env, 0xC10, 0.9f), put_f32(env, 0xC14, 0.9f), put_f32(env, 0xC18, 0.85f), put_f32(env, 0xC1C, 1.0f);
    CHECK((race::road_reflection_color(env) == std::array<u8, 3>{115, 115, 109}));
    put_f32(env, 0xC10, 3.0f), put_f32(env, 0xC14, -1.0f);
    CHECK((race::road_reflection_color(env) == std::array<u8, 3>{255, 0, 109}));
}

// The discs' courses: dry Tsukuba's composite is 64 64 64 with no blur (the dump's); the colors and blur counts counted.
TEST(road_reflection_courses_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t courses = 0;
    for (const auto& disc : test::discs()) {
        std::map<std::array<u8, 3>, int> colors;
        std::map<int, int> blurs;
        for (const std::string& name : disc->volume().list_dir("crs/")) {
            if (name.find('.') != std::string::npos || name.back() == '/') continue;
            const gt3r::Volume::Entry* e = disc->volume().find("crs/" + name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const ByteSpan env = file.section(data, course::kSlotEnv);
            if (env.size() < 0xC30) continue;
            const std::array<u8, 3> c = race::road_reflection_color(env);
            ++colors[c], ++blurs[int(rd32(env, 0xC2C))];
            if (name == "tsukuba") {
                CHECK((c == std::array<u8, 3>{64, 64, 64}));
                CHECK(rd32(env, 0xC2C) == 0);
                CHECK(race::road_reflection_extent(int(rd32(env, 0xC24)), 0) == 64.5f);
            }
            ++courses;
        }
        std::printf("road reflection colors:");
        for (const auto& [c, n] : colors) std::printf(" %d %d %d x%d;", c[0], c[1], c[2], n);
        std::printf(" blur passes:");
        for (const auto& [b, n] : blurs) std::printf(" %d x%d", b, n);
        std::printf("\n");
    }
    if (!courses) SKIP("no course files");
}
