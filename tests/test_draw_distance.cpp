// What the race view draws at distance: the scenery's lists over time (race::ListFade), and the far planes of the disc's
// courses (docs/formats/course.md "Visibility lists", "The race view's frame").

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/race/list_fade.h"
#include "race/sun_glare.h"

#include <algorithm>
#include <cstring>
#include <set>

using namespace gt4r;

namespace {

// Every model the fade draws (whole or dithered) this picture.
std::set<u16> drawn(const race::ListFade& f) {
    std::set<u16> s;
    if (const std::vector<u16>* solid = f.solid()) s.insert(solid->begin(), solid->end());
    for (const race::ListFade::Group& g : f.faded()) s.insert(g.nodes.begin(), g.nodes.end());
    return s;
}

} // namespace

// A list's models: the first picture whole; a model that joins comes in over 0.6 s; one that leaves is gone at once, so no
// picture draws a model the list leaves out; a camera cut takes the new list whole.
TEST(scenery_list_fade) {
    race::ListFade f;
    const std::vector<u16> a{1, 2, 3}, b{2, 3, 4}, c{7, 8};
    const float dt = 1.0f / 60;
    gt3r::render::Vec3 eye{0, 0, 0};
    f.update(&a, eye, dt);
    CHECK(f.solid() && *f.solid() == a);
    CHECK(drawn(f) == std::set<u16>(a.begin(), a.end()));
    // Node 1 leaves and node 4 joins.
    eye.x += 0.5f;
    f.update(&b, eye, dt);
    CHECK(!drawn(f).count(1));
    CHECK((f.solid() && *f.solid() == std::vector<u16>{2, 3}));
    int pictures = 1;
    for (; pictures < 100; ++pictures) {
        for (u16 n : drawn(f)) CHECK(std::find(b.begin(), b.end(), n) != b.end());
        if (f.solid() && f.solid()->size() == b.size()) break;
        eye.x += 0.5f;
        f.update(&b, eye, dt);
    }
    // In the list's order once whole, after about 0.6 s (36 pictures; the last 1/32 counts as whole).
    CHECK(f.solid() && *f.solid() == b);
    CHECK(pictures >= 30 && pictures <= 36);
    // Back to the first list: 1 comes in again from nothing, 4 is gone at once.
    f.update(&a, eye, dt);
    CHECK(!drawn(f).count(4));
    CHECK((f.solid() && *f.solid() == std::vector<u16>{2, 3}));
    // A cut (20 m in a picture) takes the list whole.
    eye.x += 25;
    f.update(&c, eye, dt);
    CHECK(f.solid() && *f.solid() == c);
    CHECK(drawn(f) == std::set<u16>(c.begin(), c.end()));
    // No list: every model, nothing faded.
    f.update(nullptr, eye, dt);
    CHECK(f.solid() == nullptr);
    for (const race::ListFade::Group& g : f.faded()) CHECK(g.nodes.empty());
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
