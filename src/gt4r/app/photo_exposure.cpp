#include "gt4r/app/photo_exposure.h"

#include <algorithm>

namespace gt4r {

PhotoExposure::~PhotoExposure() {
    if (target_) SDL_DestroyTexture(target_);
}

void PhotoExposure::begin(int w, int h, const std::optional<photo::Correction>& correction, std::vector<photo::Vignette> rings) {
    active_ = true, sample_ = 0, width_ = w, height_ = h;
    sum_.assign(size_t(w) * size_t(h) * 3, 0);
    rings_ = rings.size();
    dark_.resize(size_t(w) * size_t(h) * rings_);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (size_t k = 0; k < rings_; ++k)
                dark_[(size_t(y) * size_t(w) + size_t(x)) * rings_ + k] = u8(rings[k].alpha((float(x) + 0.5f) / float(w) * 2 - 1, (float(y) + 0.5f) / float(h) * 2 - 1) * 128);
    corrected_ = correction.has_value();
    saturation_ = correction ? correction->saturation : 1.0f;
    for (int i = 0; i < 256; ++i) {
        if (!correction) {
            for (int k = 0; k < 3; ++k) lut_[k][i] = u8(i);
            continue;
        }
        photo::Correction one = *correction;
        one.saturation = 1;
        const auto v = one.apply(u8(i), u8(i), u8(i));
        for (int k = 0; k < 3; ++k) lut_[k][i] = v[size_t(k)];
    }
}

bool PhotoExposure::shoot(Ui& ui, const std::function<void(Ui& ui, const SDL_FRect& all)>& draw) {
    if (!active_ || done()) return false;
    SDL_Renderer* r = ui.renderer();
    if (!target_ || target_w_ != width_ || target_h_ != height_) {
        if (target_) SDL_DestroyTexture(target_);
        target_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width_, height_);
        target_w_ = width_, target_h_ = height_;
    }
    if (!target_) return false;
    SDL_Texture* before = SDL_GetRenderTarget(r);
    if (!SDL_SetRenderTarget(r, target_)) return false;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    draw(ui, SDL_FRect{0, 0, float(width_), float(height_)});
    if (SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr)) {
        SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(shot);
        if (rgba) {
            add(*rgba);
            SDL_DestroySurface(rgba);
        }
    }
    SDL_SetRenderTarget(r, before);
    ++sample_;
    return true;
}

// A picture onto the average: corrected, the vignettes over it, blended (photo::expose).
void PhotoExposure::add(const SDL_Surface& rgba) {
    const int weight = photo::exposure_weight(sample_);
    const int w = std::min(rgba.w, width_), h = std::min(rgba.h, height_);
    for (int y = 0; y < h; ++y) {
        const u8* p = static_cast<const u8*>(rgba.pixels) + size_t(y) * size_t(rgba.pitch);
        u8* out = &sum_[size_t(y) * size_t(width_) * 3];
        const u8* dark = rings_ ? &dark_[size_t(y) * size_t(width_) * rings_] : nullptr;
        for (int x = 0; x < w; ++x, p += 4, out += 3) {
            int c[3] = {lut_[0][p[0]], lut_[1][p[1]], lut_[2][p[2]]};
            if (corrected_ && saturation_ != 1) {
                const float gray = 0.3f * float(c[0]) + 0.59f * float(c[1]) + 0.11f * float(c[2]);
                for (int& v : c) v = std::clamp(int(saturation_ * float(v) + (1 - saturation_) * gray), 0, 255);
            }
            for (int k = 0; k < 3; ++k) {
                for (size_t ring = 0; ring < rings_; ++ring)
                    if (dark[ring]) c[k] = ((-c[k] * dark[ring]) >> 7) + c[k];
                out[k] = photo::expose(out[k], c[k], weight);
            }
            if (dark) dark += rings_;
        }
    }
}

std::shared_ptr<menu::PhotoPicture> PhotoExposure::develop(int quality, const std::optional<menu::JpegPicture::Focus>& focus) {
    active_ = false;
    auto picture = std::make_shared<menu::PhotoPicture>();
    picture->width = width_, picture->height = height_, picture->version = ++version_;
    picture->rgba.resize(size_t(width_) * size_t(height_));
    for (size_t i = 0; i < picture->rgba.size(); ++i) {
        u32 px = 0xFF000000u;
        for (int k = 0; k < 3; ++k) px |= u32(sum_[i * 3 + size_t(k)]) << (8 * k);
        picture->rgba[i] = px;
    }
    picture->focus = focus;
    picture->jpeg = photo::encode_jpeg(sum_, width_, height_, quality, focus);
    sum_.clear();
    return picture;
}

} // namespace gt4r
