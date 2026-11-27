// The race view at the picture's scale (race/picture_scale.h, course.md "The picture's scale").

#include "test_framework.h"

#include "gt4r/race/picture_scale.h"

using namespace gt4r;

namespace {

struct OptionsWithScale {
    float picture_scale = 1;
    float fog_color[3] = {0.3f, 0.3f, 0.29f};
};
struct OptionsWithout {
    float fog_color[3] = {0.3f, 0.3f, 0.29f};
};
struct DecodeWithFog {
    bool node_fog = false;
};
struct DecodeWithout {};

} // namespace

TEST(picture_scale_draw_options) {
    // An engine that multiplies: the multiply asked for, the fog color as the game hands it to the GS.
    OptionsWithScale o;
    race::draw_at_picture_scale(o, 2.0f);
    CHECK(o.picture_scale == 2.0f && o.fog_color[0] == 0.15f && o.fog_color[2] == 0.145f);
    // A picture that goes into the frame before the multiply: the same strength, no multiply of its own.
    OptionsWithScale below;
    race::draw_below_picture_scale(below, 2.0f);
    CHECK(below.picture_scale == 1.0f && below.fog_color[0] == 0.15f);
    // Scale 1, or an engine without the multiply: nothing changes.
    OptionsWithScale one;
    race::draw_at_picture_scale(one, 1.0f);
    CHECK(one.picture_scale == 1.0f && one.fog_color[0] == 0.3f);
    OptionsWithout old;
    race::draw_at_picture_scale(old, 2.0f);
    CHECK(old.fog_color[0] == 0.3f);
    // The models' own fog colors where the engine reads them.
    DecodeWithFog d;
    race::use_node_fog(d);
    CHECK(d.node_fog);
    DecodeWithout none;
    race::use_node_fog(none);
}

TEST(picture_scale_lights_and_stored_colors) {
    // The lights of the scene's items over the scale, the late ones (drawn after the multiply) as they
    // are; the stored colors scaled only where the engine does not multiply.
    std::vector<gt3r::render::DrawItem> items(3);
    items[2].late = true;
    race::light_at_picture_scale(items, 1, 2.0f);
    const float k = race::engine_multiplies_picture() ? 0.5f : 1.0f;
    CHECK(items[0].light_scale[0] == 1.0f);
    CHECK(items[1].light_scale[0] == k && items[1].light_scale[1] == k && items[1].light_scale[2] == k);
    CHECK(items[2].light_scale[0] == 1.0f);
    CHECK(race::stored_color_scale(2.0f) == (race::engine_multiplies_picture() ? 1.0f : 2.0f));
}

TEST(picture_scale_scenery_multiply) {
    // The frame's second multiply (Env0 +0x3C) on the items before the cars, where the engine has it (DrawItem::pre_fix); late
    // items never; 128 (none) changes nothing.
    struct WithPreFix { int pre_fix = 128; bool late = false; };
    struct Without { bool late = false; };
    std::vector<WithPreFix> items(4);
    items[1].late = true;
    race::scenery_multiply(items, 0, 3, 127);
    CHECK(items[0].pre_fix == 127 && items[1].pre_fix == 128 && items[2].pre_fix == 127 && items[3].pre_fix == 128);
    std::vector<WithPreFix> same(2);
    race::scenery_multiply(same, 0, 2, 128);
    CHECK(same[0].pre_fix == 128 && same[1].pre_fix == 128);
    std::vector<Without> old(2);
    race::scenery_multiply(old, 0, 2, 127);
}
