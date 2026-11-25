#pragma once

// A photo's picture: its size and sampling, the filters, the JPEG file the card holds.
// docs/formats/photo.md "The picture".

#include "gt4r/base/types.h"
#include "gt4r/menu/jpeg.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::photo {

// The photo aspects (the option `photo_aspect`; the program's table 0x692380): the picture's shape
// and one of its 2 x 2 tiles.
struct Aspect {
    float ratio = 4.0f / 3.0f;
    int w = 640, h = 480;
};
Aspect aspect_by_name(const std::string& name); // "4x3", "16x9", "3x2", "1x1"
inline constexpr int kTiles = 2;    // tiles a side: pictures twice a tile's size
inline constexpr int kSamples = 32; // pictures added up for a photo

// A sample of the lens and of the pixel (0x3323B0): a point of the unit disc, a place in the pixel 0 .. 1.
struct Sample {
    float lens_x = 0, lens_y = 0, x = 0, y = 0;
};
std::vector<Sample> samples(int count);

// The shear and the eye's move one sample puts on a projection (0x332550): for a projection whose x and y scales are px and py.
struct LensShift {
    float move_x = 0, move_y = 0;   // the eye's move in the camera's space, metres (added after the view)
    float shear_x = 0, shear_y = 0; // added to the projection's third column
};
LensShift lens_shift(const Sample& s, float focus, float f_number, float film_mm, float aspect, float px, float py, int w, int h);
// The focus for the car's nearest point at `distance` (0x3C7928): film 35 mm, 4:3, a blur of 1/960 of the picture.
float auto_focus(float distance, float f_number, float projection_y);

// What the filter page sets. The program's records: brightness -> gamma, white balance, the color filter, the vignette.
enum class ColorFilter : int { None, Mature, Monochrome };
ColorFilter filter_by_name(const std::string& name);
const char* filter_name(ColorFilter f);
struct Filters {
    int brightness = 0, contrast = 0, white_balance = 0;
    ColorFilter color = ColorFilter::None;
    float lightness = 10; // the vignette, 0 .. 100
};
// The picture's correction (0x3EFC10, 0x4A8CB8): each channel's gamma, the black and white levels, the saturation.
struct Correction {
    float gamma[3] = {1, 1, 1};
    float black = 0, white = 1;
    float saturation = 1;
    std::array<u8, 3> apply(u8 r, u8 g, u8 b) const;
    bool plain() const;
};
Correction correction(const Filters& f);
// A vignette (0x3BBBC0): a ring round the middle of a frame of 640 x 448, black, its alpha `strength` at the outer radius and 0 within
// the inner one. The photo's own and the course's (Env0 +0x950 .. +0x974).
struct Vignette {
    float strength = 0, inner = 200, outer = 420;
    float alpha(float x, float y) const; // at a place of the picture (-1 .. 1 each way)
};
Vignette vignette(const Filters& f);

// The photo's pictures added up as the program does it on the GS: each new one is blended under the average so far
// by a whole number of 128ths, rounded down, so a photo comes out a few levels darker than its pictures.
// The k-th (from 0) value on the average of those before it.
int exposure_weight(int k); // the average's weight, 0 .. 128
constexpr u8 expose(int average, int value, int weight) { return u8((((average - value) * weight) >> 7) + value); }

// The jpeg quality of a photo option's value ("STANDARD" 50, "FINE" 80, "SUPER_FINE" 95).
int jpeg_quality(const std::string& name);

// A baseline JPEG as the program's encoder writes it: JFIF 1.02, the focus record's segment, the
// standard tables at `quality`, chroma halved both ways. `rgb`: w x h x 3 bytes, from the top.
Bytes encode_jpeg(const std::vector<u8>& rgb, int w, int h, int quality, const std::optional<menu::JpegPicture::Focus>& focus);

// The 64 x 64 picture a photo's icon carries, as the icon's 16-bit pixels (0x330AE8).
std::vector<u16> icon_pixels(const std::vector<u8>& rgb, int w, int h);

} // namespace gt4r::photo
