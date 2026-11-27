#pragma once

// A photo's exposure (0x333A00, 0x333E50: Photo Travel's and the photo pause's): its pictures drawn one a frame into a target of the
// photo's size, each read back, corrected and darkened by the vignettes when asked, and blended under the average so far
// (photo::expose); then the photo itself (menu::PhotoPicture) with its JPEG. docs/formats/photo.md "The photo".

#include "gt4r/app/app.h"
#include "gt4r/menu/photo_natives.h"
#include "gt4r/photo/picture.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace gt4r {

class PhotoExposure {
public:
    PhotoExposure() = default;
    ~PhotoExposure();
    PhotoExposure(const PhotoExposure&) = delete;
    PhotoExposure& operator=(const PhotoExposure&) = delete;

    // A photo of w x h. `correction`: each picture's own (Photo Travel's, the scene drawn plain; none: the scene drawn corrected);
    // `rings`: vignettes put over each picture (Photo Travel's: the course's and the photo's own).
    void begin(int w, int h, const std::optional<photo::Correction>& correction = std::nullopt, std::vector<photo::Vignette> rings = {});
    bool active() const { return active_; }
    int sample() const { return sample_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // The next picture: `draw` fills the target (cleared to black) over its whole size. False when the target could not be had.
    bool shoot(Ui& ui, const std::function<void(Ui& ui, const SDL_FRect& all)>& draw);
    bool done() const { return active_ && sample_ >= photo::kSamples; }
    // The photo, its JPEG at `quality` with the car's frame (none: no car), and the exposure over.
    std::shared_ptr<menu::PhotoPicture> develop(int quality, const std::optional<menu::JpegPicture::Focus>& focus);
    void cancel() { active_ = false, sum_.clear(); }

private:
    void add(const SDL_Surface& rgba);

    bool active_ = false;
    int sample_ = 0, width_ = 0, height_ = 0, target_w_ = 0, target_h_ = 0;
    u64 version_ = 0;
    std::vector<u8> sum_, dark_; // the average so far; the vignettes' alpha (128 whole), a byte each a pixel and ring
    size_t rings_ = 0;
    bool corrected_ = false;
    float saturation_ = 1;
    u8 lut_[3][256] = {};
    SDL_Texture* target_ = nullptr;
};

} // namespace gt4r
