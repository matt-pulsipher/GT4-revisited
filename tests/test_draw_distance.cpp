// What the race view draws at distance: the scenery's lists over time (gt3r::race::SceneryFade), and the far planes of the disc's
// courses (docs/formats/course.md "Visibility lists", "The race view's frame").

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "race/scenery_fade.h"
#include "race/sun_glare.h"

#include <algorithm>
#include <cstring>
#include <set>

using namespace gt4r;

namespace {

// Every model the fade draws (whole or dithered) this picture.
std::set<u16> drawn(const gt3r::race::SceneryFade& f) {
    std::set<u16> s;
    if (const std::vector<u16>* solid = f.solid()) s.insert(solid->begin(), solid->end());
    for (const gt3r::race::SceneryFade::Group& g : f.faded()) s.insert(g.nodes.begin(), g.nodes.end());
    return s;
}

} // namespace

// The race view's scenery fade as RaceWorld uses it: a model leaving the list fades out and one joining fades in over
// 0.6 s (ours on purpose: GT4 switches at once, docs/formats/course.md "Visibility lists"); a photo's picture, stepped by
// the fade's whole time, takes its list whole, nothing left over from the race view's.
TEST(scenery_fade_photo_whole) {
    gt3r::race::SceneryFade f;
    const std::vector<u16> race{1, 2, 3}, photo{2, 3, 4, 5};
    const float dt = 1.0f / 60;
    gt3r::render::Vec3 eye{0, 0, 0};
    f.update(&race, eye, dt);
    CHECK(f.solid() && *f.solid() == race);
    // The race view: 1 leaves and 4 joins, both dithered for a while.
    eye.x += 0.5f;
    const std::vector<u16> next{2, 3, 4};
    for (int k = 0; k < 6; ++k) f.update(&next, eye, dt);
    CHECK(drawn(f).count(1));
    CHECK(drawn(f).count(4));
    CHECK(!(f.solid() && *f.solid() == next));
    // A photo's picture: its list whole.
    f.update(&photo, eye, gt3r::race::SceneryFade::kSeconds);
    CHECK(f.solid() && *f.solid() == photo);
    CHECK(drawn(f) == std::set<u16>(photo.begin(), photo.end()));
}

// Every course of the discs has its own far plane (Env0 +0x20), so CourseScene::kScreenFar (the screen's starting 750 m) is
// never used on them; slot 8's lists (the photo exposures') and the far planes counted.
TEST(course_far_planes_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t courses = 0;
    for (const auto& disc : test::discs()) {
        size_t here = 0, at_5000 = 0, photo_lists = 0;
        float lo = 1e9f, hi = 0;
        for (const std::string& name : disc->volume().list_dir("crs/")) {
            if (name.find('.') != std::string::npos || name.back() == '/') continue;
            const gt3r::Volume::Entry* e = disc->volume().find("crs/" + name);
            if (!e) continue;
            const Bytes data = disc->volume().read(*e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const ByteSpan env = file.section(data, course::kSlotEnv);
            CHECK(env.size() >= 0x24 && std::memcmp(env.data(), "Env0", 4) == 0);
            const gt3r::race::SunParams p = gt3r::race::SunParams::from_env0(env);
            CHECK(p.has_env && p.far_clip > 0);
            if (!(p.has_env && p.far_clip > 0)) std::printf("no far plane: %s\n", name.c_str());
            lo = std::min(lo, p.far_clip), hi = std::max(hi, p.far_clip);
            at_5000 += p.far_clip == 5000;
            const ByteSpan vls = file.section(data, course::kSlotPhotoVis);
            photo_lists += vls.size() >= 4 && std::memcmp(vls.data(), "Vls0", 4) == 0;
            ++here;
        }
        std::printf("course far planes: %zu courses, %.0f .. %.0f m, %zu at 5000; %zu with slot 8's lists\n", here, double(lo), double(hi), at_5000,
                 photo_lists);
        courses += here;
    }
    if (!courses) SKIP("no course files");
}
