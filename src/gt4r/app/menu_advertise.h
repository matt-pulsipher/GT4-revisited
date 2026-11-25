#pragma once

// The pictures before the first menus (advertise/us/main.img, GT4FIRSTADVERTISE): the copyright
// notice (texture 0) and "Sony Computer Entertainment America Presents" (texture 2), each in the
// middle of the 640 x 448 frame buffer on black, brightened in, held and darkened out
// (DisplayAdvertiseImage, 0x109048 / 0x109080: 15 steps in, the seconds given x 30 steps held,
// 15 steps out, a step every other field). docs/formats/menu.md "Before the menus".

#include "base/log.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "gt4r/app/app.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <vector>

namespace gt4r {

class MenuAdvertise {
public:
    explicit MenuAdvertise(App& app) : app_(app) {
        try {
            if (const std::optional<Bytes> f = app.vfs()->read("advertise/us/main.img"); f && gt3r::Tex1::is_tex1(*f)) {
                const gt3r::Tex1 set = gt3r::Tex1::parse(*f);
                gt3r::GsMemory gs;
                set.upload(*f, gs);
                // ❓ The notice's hold (1 s) is judged from the reference (shown about 2 s); the
                // program's list (0x618700) holds "Presents" 3 s.
                for (const auto& [index, hold] : {std::pair<size_t, float>{0, 1.0f}, {2, 3.0f}})
                    if (index < set.textures.size()) {
                        const gt3r::Tex1::Image img = set.decode(gs, index);
                        SDL_Texture* t = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
                        if (!t) continue;
                        SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
                        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
                        shows_.push_back({t, float(img.width), float(img.height), hold});
                    }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Pictures before the menus: %s", e.what());
        }
    }
    ~MenuAdvertise() {
        for (const Show& s : shows_) SDL_DestroyTexture(s.texture);
    }
    bool active() const { return at_ < shows_.size(); }
    void skip() { at_ = shows_.size(); }
    void step(float dt) {
        if (!active()) return;
        t_ += dt;
        if (t_ >= 0.5f + shows_[at_].hold + 0.5f) t_ = 0, ++at_;
    }
    void draw(float x, float scale) {
        if (!active()) return;
        const Show& s = shows_[at_];
        const float level = std::clamp(std::min(t_ / 0.5f, (0.5f + s.hold + 0.5f - t_) / 0.5f), 0.0f, 1.0f);
        const float k = 480.0f / 448.0f;
        const SDL_FRect dst{x + (640 - s.w) / 2 * scale, (448 - s.h) / 2 * k * scale, s.w * scale, s.h * k * scale};
        const u8 c = u8(level * 255);
        SDL_SetTextureColorMod(s.texture, c, c, c);
        SDL_SetTextureBlendMode(s.texture, SDL_BLENDMODE_BLEND);
        SDL_RenderTexture(app_.renderer(), s.texture, nullptr, &dst);
    }

private:
    struct Show {
        SDL_Texture* texture;
        float w, h, hold;
    };
    App& app_;
    std::vector<Show> shows_;
    size_t at_ = 0;
    float t_ = 0;
};

} // namespace gt4r
