#pragma once

// The flash faces (mFlashFace: the dealers' photo banners, the license center's and the event
// halls' backgrounds): their files are STRB movies (the program's "strobe" player) whose every
// frame places pictures in rectangles at keyed alphas. This reads the parts those menu movies
// use and gives a frame's rectangles, as the player's stateless path draws them (0x4742A0).
// docs/formats/menu.md "Flash".

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r::menu {

struct FlashQuad {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // on the stage
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // in the picture
    int image = -1;                       // the movie's picture (FlashMovie::images)
    float r = 1, g = 1, b = 1, a = 1;     // the colour multiply (0x80 = 1)
};

struct FlashMovie {
    float width = 0, height = 0, rate = 60;
    int frames = 0;
    struct Image {
        u32 offset = 0, size = 0; // a Tex1 in the file
        int width = 0, height = 0;
    };
    std::vector<Image> images;

    static bool is_movie(ByteSpan d) { return d.size() >= 0xC0 && gt3r::has_magic(d, "STRB"); }
    // Throws FormatError.
    static FlashMovie parse(ByteSpan file);
    // The rectangles of the frame at `time` seconds, back to front (none past the movie's end).
    std::vector<FlashQuad> at(float time) const;

private:
    struct Key {
        float sx = 1, sy = 1, x = 0, y = 0;
        float mul[4] = {1, 1, 1, 1};
    };
    struct Placement {
        int kind = 0, first = 0, end = 0, index = 0;
        std::vector<Key> keys;
    };
    struct Shape {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        int image = -1;
        float m[16] = {};
    };
    std::vector<Placement> placements_;
    std::vector<std::vector<int>> frame_lists_;
    int total_ = 0;
    std::vector<Shape> shapes_;
};

} // namespace gt4r::menu
