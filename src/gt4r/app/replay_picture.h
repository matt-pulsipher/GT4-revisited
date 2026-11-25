#pragma once

// A saved replay's picture: the middle 4:3 of the race as it stands on the screen, brought down to the 64 x 64 the Replay
// Theater's rows show (❓ where the program takes its own is not read; its captures show the road from the driver's view).

#include <SDL3/SDL_render.h>

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r {

inline std::vector<u32> replay_picture(SDL_Renderer* r) {
    std::vector<u32> out;
    SDL_Surface* raw = SDL_RenderReadPixels(r, nullptr);
    SDL_Surface* shot = raw ? SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (raw) SDL_DestroySurface(raw);
    if (!shot) return out;
    constexpr int kSide = 64;
    const int w = shot->w, h = shot->h, cw = w * 3 >= h * 4 ? h * 4 / 3 : w, x0 = (w - cw) / 2;
    out.resize(kSide * kSide);
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            u64 sum[3] = {};
            int count = 0;
            for (int v = y * h / kSide; v < (y + 1) * h / kSide; v += 2)
                for (int u = x0 + x * cw / kSide; u < x0 + (x + 1) * cw / kSide; u += 2, ++count) {
                    const u8* p = static_cast<const u8*>(shot->pixels) + size_t(v) * size_t(shot->pitch) + size_t(u) * 4;
                    for (int k = 0; k < 3; ++k) sum[k] += p[k];
                }
            u32 c = 0xFF000000u;
            for (int k = 0; k < 3 && count; ++k) c |= u32(sum[k] / u64(count)) << (8 * k);
            out[size_t(y * kSide + x)] = c;
        }
    SDL_DestroySurface(shot);
    return out;
}

} // namespace gt4r
