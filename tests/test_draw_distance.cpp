// What the race view draws at distance: the scenery's lists over time (race::ListFade; docs/formats/course.md "Visibility lists").

#include "test_framework.h"

#include "gt4r/race/list_fade.h"

#include <algorithm>
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
