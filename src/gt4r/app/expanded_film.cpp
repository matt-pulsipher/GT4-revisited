#include "gt4r/app/expanded_film.h"

namespace gt4r {

ExpandedFilm::~ExpandedFilm() {
    for (SDL_Texture* t : {mask_, cut_, reflection_})
        if (t) SDL_DestroyTexture(t);
}

bool ExpandedFilm::target(SDL_Texture*& t, int w, int h) {
    if (!t) t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    return t && SDL_SetRenderTarget(r_, t);
}

void ExpandedFilm::part(const gt3r::video::MoviePlayer& film, float x, float y, float w, float h, const SDL_FRect& dst, SDL_BlendMode blend) const {
    SDL_Texture* t = film.texture();
    SDL_FRect src, whole;
    if (film.field_split()) film.picture_rects(dst, src, whole);
    else {
        // Until the player has found the fields in the pictures: the first one.
        float tw = 0, th = 0;
        SDL_GetTextureSize(t, &tw, &th);
        src = {0, 0, tw, th / 2}, whole = dst;
    }
    const SDL_FRect from{src.x + src.w * x / 640, src.y + src.h * y / 224, src.w * w / 640, src.h * h / 224};
    SDL_SetTextureBlendMode(t, blend);
    SDL_SetTextureAlphaMod(t, 255);
    SDL_RenderTexture(r_, t, &from, &whole);
}

void ExpandedFilm::draw_whole(const gt3r::video::MoviePlayer& film, const SDL_FRect& dst, float opacity) {
    SDL_Texture* t = film.texture();
    if (!t) return;
    SDL_Texture* before = SDL_GetRenderTarget(r_);
    if (target(reflection_, 128, 128)) part(film, 480, 0, 128, 128, SDL_FRect{0, 0, 128, 128}, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(r_, before);
    SDL_SetTextureColorModFloat(t, opacity, opacity, opacity);
    part(film, 0, 0, 320, 224, dst, SDL_BLENDMODE_NONE);
    SDL_SetTextureColorModFloat(t, 1, 1, 1);
}

void ExpandedFilm::draw_masked(const gt3r::video::MoviePlayer& film, const SDL_FRect& dst, float opacity) {
    if (!film.texture() || opacity <= 0) return;
    SDL_Texture* before = SDL_GetRenderTarget(r_);
    const SDL_FRect all{0, 0, 640, 448};
    bool ok = target(mask_, 640, 448);
    if (ok) {
        // The alpha, 128 of 255 full: drawn twice, one adding to the other.
        part(film, 320, 0, 160, 224, all, SDL_BLENDMODE_NONE);
        part(film, 320, 0, 160, 224, all, SDL_BLENDMODE_ADD);
    }
    ok = ok && target(cut_, 640, 448);
    if (ok) {
        part(film, 0, 0, 320, 224, all, SDL_BLENDMODE_NONE);
        SDL_SetTextureBlendMode(mask_, SDL_BLENDMODE_MOD);
        SDL_RenderTexture(r_, mask_, nullptr, &all);
    }
    SDL_SetRenderTarget(r_, before);
    if (!ok) return;
    // The page x (1 - alpha), then the colors x alpha over it.
    SDL_SetTextureBlendMode(mask_, SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD,
                                                              SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD));
    SDL_SetTextureColorModFloat(mask_, opacity, opacity, opacity);
    SDL_RenderTexture(r_, mask_, nullptr, &dst);
    SDL_SetTextureColorModFloat(mask_, 1, 1, 1);
    SDL_SetTextureBlendMode(cut_, SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                                             SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD));
    SDL_SetTextureColorModFloat(cut_, opacity, opacity, opacity);
    SDL_RenderTexture(r_, cut_, nullptr, &dst);
    SDL_SetTextureColorModFloat(cut_, 1, 1, 1);
}

SDL_GPUTexture* ExpandedFilm::reflection() const {
    if (!reflection_) return nullptr;
    return static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(SDL_GetTextureProperties(reflection_), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

} // namespace gt4r
