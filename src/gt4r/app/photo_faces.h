#pragma once

// The photo faces of the running menus (MPhotoRenderFace): the photo just taken or one of the
// card's, fitted to the face and moved and zoomed by the page (0x331E80).

#include "gt4r/app/app.h"
#include "gt4r/menu/layout.h"
#include "gt4r/menu/photo_natives.h"

#include <functional>

namespace gt4r {

class PhotoFaces {
public:
    explicit PhotoFaces(App& app) : app_(app) {}
    ~PhotoFaces() {
        if (texture_) SDL_DestroyTexture(texture_);
    }
    PhotoFaces(const PhotoFaces&) = delete;
    PhotoFaces& operator=(const PhotoFaces&) = delete;

    // The picture to show (null: none yet).
    std::function<const menu::PhotoPicture*()> picture;

    // `at`: the face on the page, `dst`: on the screen.
    void draw(const menu::Rect& at, const SDL_FRect& dst, float opacity) {
        const menu::PhotoPicture* p = picture ? picture() : nullptr;
        if (!p || p->width <= 0 || p->rgba.size() < size_t(p->width) * size_t(p->height) || at.w <= 0 || at.h <= 0) return;
        SDL_Renderer* r = app_.renderer();
        if (!texture_ || shown_ != p || version_ != p->version) {
            if (texture_) SDL_DestroyTexture(texture_);
            texture_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, p->width, p->height);
            if (!texture_) return;
            SDL_UpdateTexture(texture_, nullptr, p->rgba.data(), p->width * 4);
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
            shown_ = p, version_ = p->version;
        }
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture_, u8(std::clamp(opacity, 0.0f, 1.0f) * 255));
        const float k = dst.w / at.w, ratio = float(p->width) / float(p->height);
        float w = dst.w, h = dst.h;
        if (w / h < ratio) h = w / ratio;
        else w = h * ratio;
        w *= p->scale, h *= p->scale;
        const float x = dst.x + dst.w * 0.5f + p->ofs_x * p->scale * k, y = dst.y + dst.h * 0.5f + p->ofs_y * p->scale * k;
        const SDL_FRect to{x - w * 0.5f, y - h * 0.5f, w, h};
        SDL_Rect before{};
        const bool had = SDL_GetRenderClipRect(r, &before) && before.w > 0;
        const SDL_Rect clip{int(dst.x), int(dst.y), int(dst.w + 0.5f), int(dst.h + 0.5f)};
        if (!had) SDL_SetRenderClipRect(r, &clip);
        SDL_RenderTexture(r, texture_, nullptr, &to);
        if (!had) SDL_SetRenderClipRect(r, nullptr);
    }

private:
    App& app_;
    SDL_Texture* texture_ = nullptr;
    const menu::PhotoPicture* shown_ = nullptr;
    u64 version_ = 0;
};

} // namespace gt4r
