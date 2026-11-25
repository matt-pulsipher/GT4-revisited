#pragma once

// A film whose picture carries its own alpha beside it (MMovieFace::expandAlpha): of the 640 x 224
// field the left 320 pixels are the colors, the next 160 the alpha (128: full) and a 128 x 128
// picture at 480 what a car standing in it reflects. docs/formats/prologue.md, "The new-car page".

#include "video/movie_player.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>

namespace gt4r {

class ExpandedFilm {
public:
    explicit ExpandedFilm(SDL_Renderer* r) : r_(r) {}
    ~ExpandedFilm();
    ExpandedFilm(const ExpandedFilm&) = delete;
    ExpandedFilm& operator=(const ExpandedFilm&) = delete;

    // The colors over all of `dst` (`blend` 2), and the film's reflection picture kept for the car.
    void draw_whole(const gt3r::video::MoviePlayer& film, const SDL_FRect& dst, float opacity);
    // What the film's alpha leaves of it over the page.
    void draw_masked(const gt3r::video::MoviePlayer& film, const SDL_FRect& dst, float opacity);
    // The reflection picture as the renderer's texture (nullptr before the first draw_whole).
    SDL_GPUTexture* reflection() const;

private:
    bool target(SDL_Texture*& t, int w, int h);
    void part(const gt3r::video::MoviePlayer& film, float x, float y, float w, float h, const SDL_FRect& dst, SDL_BlendMode blend) const;

    SDL_Renderer* r_;
    SDL_Texture* mask_ = nullptr;
    SDL_Texture* cut_ = nullptr;
    SDL_Texture* reflection_ = nullptr;
};

} // namespace gt4r
