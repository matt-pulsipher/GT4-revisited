#pragma once
// The picture's scale (Env0 +0x38, docs/formats/course.md "The picture's scale"): the race view
// draws its 3D picture with colors at 1 / scale of their final strength and multiplies the
// finished picture by the scale with a GS blend. Where the shared engine does that multiply
// (DrawOptions::picture_scale), the race view draws as the game does: the stored colors as they
// are, the lights and the fog colors divided by the scale, and the engine multiplies each pixel.
// Without it, the stored colors are scaled instead (the same picture, apart from the dark levels'
// steps of the dimmer picture).

#include "formats/model/gtm1.h"
#include "render/view3d.h"

#include <vector>

namespace gt4r::race {

// Whether this build's engine multiplies the picture (DrawOptions::picture_scale) and reads the
// models' fog colors (gtm1::Options::node_fog).
template <class O> inline constexpr bool kHasPictureScale = requires(O& o) { o.picture_scale; };
template <class O> inline constexpr bool kHasNodeFog = requires(O& o) { o.node_fog; };
inline constexpr bool kEngineMultipliesPicture = kHasPictureScale<gt3r::render::DrawOptions>;

// Whether the race view draws at the picture's scale and lets the engine multiply it.
inline constexpr bool engine_multiplies_picture() { return kEngineMultipliesPicture; }

// The scale on a set's stored colors: 1 when the engine multiplies the picture, else the picture's.
inline float stored_color_scale(float picture_scale) { return engine_multiplies_picture() ? 1.0f : picture_scale; }

// The race view's draw options at the picture's scale: the multiply asked of the engine and the
// fog color as the game hands it to the GS (over the scale; the lights go over it by the items,
// light_at_picture_scale). `scale` 1 or without the engine's multiply: nothing changes.
template <class O = gt3r::render::DrawOptions> void draw_at_picture_scale(O& o, float scale) {
    if constexpr (kHasPictureScale<O>) {
        if (scale == 1.0f || scale <= 0) return;
        o.picture_scale = scale;
        for (float& c : o.fog_color) c /= scale;
    } else {
        (void)o, (void)scale;
    }
}

// A picture that goes into the race view's frame before its multiply (the cars' reflection picture,
// the road's): drawn as the frame's own items are, at the picture's scale, without a multiply of its own.
template <class O = gt3r::render::DrawOptions> void draw_below_picture_scale(O& o, float scale) {
    draw_at_picture_scale(o, scale);
    if constexpr (kHasPictureScale<O>) o.picture_scale = 1;
}

// The lights of items drawn at the picture's scale: DrawItem::light_scale over the scale (the
// engine puts it on the draw's lights and on the item's own ItemLights alike).
inline void light_at_picture_scale(std::vector<gt3r::render::DrawItem>& items, size_t first, float scale) {
    if (!engine_multiplies_picture() || scale == 1.0f || scale <= 0) return;
    for (size_t k = first; k < items.size(); ++k)
        if (!items[k].late)
            for (float& l : items[k].light_scale) l /= scale;
}

// The models' own fog colors: a node program's op 0x21 sets the GS fog color for the draws after it
// (0 before the passes that add to the frame, so they are not fogged twice), where the engine reads it
// (gtm1::Options::node_fog).
template <class O = gt3r::gtm1::Options> void use_node_fog(O& o) {
    if constexpr (kHasNodeFog<O>) o.node_fog = true;
    else (void)o;
}

} // namespace gt4r::race
