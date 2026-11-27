#pragma once
// GT4's road reflection (docs/formats/course.md "Road reflection"; RacePS2Base::vf111, 0x3411D0): the scenery mirrored below
// the road and its sky drawn into a small picture (Env0 +0xC24 x +0xC28) with the race view's own projection, blurred
// +0xC2C times and laid over the road where slot 13's mask lets it. The shared engine's pass (gt3r::race::WetRoad, Gran
// Turismo 3's) draws the picture, the mask and the frame's alpha clear; GT4's own are the blur passes, the composite's
// texture coordinates and its color:
// - the picture is drawn half a pixel off the GS's whole pixels (XYOFFSET (2048 - w / 2) x 16 + 8, 0x4A2478 / 0x4A2BC8),
//   so it lines up with a GPU's picture of the same projection: no half-texel shift on the way back (Gran Turismo 3's
//   composite has one);
// - each blur pass copies the picture onto the other buffer with texture coordinates (0.5, 0.5) .. (w + 0.5, h + 0.5):
//   every pass half a texel the same way (Gran Turismo 3 alternates);
// - the composite stretches (0, 0) .. (w + 0.5 - n, h + 0.5 - n) texels over the frame, n the blur passes;
// - its color is (+0xC10, +0xC14, +0xC18) x +0xC1C as GS bytes (0x4AA1D8: int(min(c x 128 + 0.5, 255))), the picture
//   modulated by it: the picture itself holds the scenery at full strength.

#include "gt4r/base/types.h"
#include "race/wet_road.h"
#include "render/math3d.h"
#include "render/view3d.h"

#include <array>
#include <memory>
#include <vector>

namespace gt3r {
class Vfs;
}
namespace gt3r::render {
class GpuContext;
class GpuModel;
} // namespace gt3r::render

namespace gt4r::race {

// The composite's texture coordinates at the frame's right / bottom edge, in texels: w + 0.5 - n (0x341600..0x341698).
float road_reflection_extent(int size, int blur);
// The composite's color as the GS bytes the program makes from Env0 +0xC10..+0xC1C (0x4AA1D8).
std::array<u8, 3> road_reflection_color(ByteSpan env0);

class RoadReflection {
public:
    RoadReflection();
    ~RoadReflection();
    // `course`: the course file of slots 9, 10, 13, 14 (CourseScene::reflection_course); `env0` the course's Env0. Without
    // `gt4` (Prologue, whose program's pass is not read) the shared engine's pass is used as it is.
    bool load(gt3r::render::GpuContext& ctx, const gt3r::Vfs& vfs, ByteSpan course, ByteSpan env0, bool gt4 = true);
    // Whether GT4's blur and composite are used (the buffer then holds the scenery at full strength).
    bool gt4() const { return gt4_; }
    bool active() const { return wet_.active(); }
    const gt3r::race::WetRoadParams& params() const { return wet_.params(); } // (its blur is 0: the passes are ours)
    int blur() const { return blur_; }
    void add_buffer_items(std::vector<gt3r::render::DrawItem>& items, const gt3r::render::Mat4& view, const gt3r::render::Vec3& eye) {
        wet_.add_buffer_items(items, view, eye, {});
    }
    // The picture and its blur passes; the texture to lay over the frame (null on failure).
    SDL_GPUTexture* render(gt3r::render::View3D& view3d, const gt3r::render::Mat4& view, const gt3r::render::Mat4& proj,
                           const gt3r::render::Vec3& eye, const std::vector<gt3r::render::DrawItem>& items,
                           const gt3r::render::DrawOptions& main);
    // The frame's items after the scenery: the alpha clear, the mask and the composite.
    void add_frame_items(std::vector<gt3r::render::DrawItem>& items, const gt3r::render::Mat4& view, const gt3r::render::Frustum& fr,
                         SDL_GPUTexture* buffer);

private:
    struct Composite {
        gt3r::render::Frustum fr;
        std::unique_ptr<gt3r::render::GpuModel> quad;
        u64 used = 0;
    };
    const gt3r::render::GpuModel* composite(const gt3r::render::Frustum& fr);
    gt3r::race::WetRoad wet_;
    gt3r::render::GpuContext* ctx_ = nullptr;
    int width_ = 64, height_ = 64, blur_ = 0;
    bool gt4_ = true;
    std::array<u8, 3> color_{128, 128, 128};
    std::unique_ptr<gt3r::render::GpuModel> blur_quad_;
    Composite composites_[2]; // per view frustum (VR's two eyes)
    u64 uses_ = 0;
};

} // namespace gt4r::race
