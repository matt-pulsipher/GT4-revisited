#pragma once

// The loading page between two menu projects (the program's LoadingPS2 in its "black" mode,
// docs/formats/menu.md "The loading page"): black fades in over the last page in 1 s; then the
// GT stripes or the GT logo (advertise/gtloading.img, one of the two at random) and the spinning
// tachometer (a picture of the program, 0x68C080) fade in over 1 s; at the end the whole page
// fades out in 0.25 s over the next project. Before a race ("capture", set by the scripts): the
// last page is held as it was, the tachometer fading in over it.

#include "base/log.h"
#include "disc/core_image.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "gt4r/app/app.h"
#include "gt4r/disc/disc.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <optional>

namespace gt4r {

class MenuLoading {
public:
    explicit MenuLoading(App& app) : app_(app) {}
    ~MenuLoading() {
        for (SDL_Texture* t : {logo_, stripes_, tacho_, kept_})
            if (t) SDL_DestroyTexture(t);
    }

    // The page shown from now; `pick` chooses the picture (the program's random bit, 0x579358).
    void begin(unsigned pick) {
        load();
        t_ = 0, ending_ = -1, stripes_pick_ = (pick & 1) != 0, angle_ = 0;
        active_ = true, capture_ = false;
    }
    // The capture mode (0x104B68): the picture drawn last is kept.
    void begin_capture() {
        load();
        t_ = 0, ending_ = -1, angle_ = 0;
        active_ = capture_ = true;
    }
    // The page as it is on the screen, kept for the capture (the menus close their pages when they
    // end: called on each picture from the scripts' asking for the mode on).
    void keep(float x, float scale) {
        drop_kept();
        const SDL_Rect page{int(x), 0, int(640 * scale), int(480 * scale)};
        if (SDL_Surface* shot = SDL_RenderReadPixels(app_.renderer(), &page)) {
            kept_ = SDL_CreateTextureFromSurface(app_.renderer(), shot);
            SDL_DestroySurface(shot);
        }
    }
    // The race takes the screen: gone at once (❓ the program fades it out over 0.25 s).
    void stop() {
        active_ = capture_ = false;
        drop_kept();
    }
    bool active() const { return active_; }
    // True once the black is whole: the next project may start under it.
    // (The capture: once the tachometer is whole. ❓ The original's shows when the race's loader
    // is under way and stays while the disc loads; ours is read at once after it.)
    bool covered() const { return active_ && t_ >= (capture_ ? 1.2f : 1.0f); }
    // The next project has started: the page holds a moment (❓ the original holds it while the
    // disc loads; ours loads at once) and fades out.
    void end() {
        if (ending_ < 0) ending_ = std::max(t_, 2.0f) + 0.5f;
    }
    // `dt` seconds of the menus.
    void step(float dt) {
        if (!active_) return;
        t_ += dt;
        angle_ = std::fmod(angle_ + 720.0f * dt, 360.0f); // two turns a second (0x104150)
        if (ending_ >= 0 && t_ >= ending_ + 0.25f) active_ = false;
    }

    // Over the page's 640 x 480 at (x, 0), `scale` screen pixels a page unit.
    void draw(float x, float scale) {
        if (!active_) return;
        SDL_Renderer* r = app_.renderer();
        if (capture_) return draw_capture(x, scale);
        const float out = ending_ >= 0 && t_ > ending_ ? std::clamp(1.0f - (t_ - ending_) / 0.25f, 0.0f, 1.0f) : 1.0f;
        const float black = std::clamp(t_, 0.0f, 1.0f) * out;
        const float picture = std::clamp(t_ - 1.0f, 0.0f, 1.0f) * out;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, u8(black * 255));
        const SDL_FRect whole{x, 0, 640 * scale, 480 * scale};
        SDL_RenderFillRect(r, &whole);
        if (picture <= 0) return;
        // Places on the 640 x 448 frame buffer, shown over 480 lines.
        const float k = 480.0f / 448.0f;
        auto quad = [&](SDL_Texture* t, float x0, float y0, float x1, float y1, double angle, SDL_Color c) {
            if (!t) return;
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(t, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(t, u8(picture * 255));
            const SDL_FRect dst{x + x0 * scale, y0 * k * scale, (x1 - x0) * scale, (y1 - y0) * k * scale};
            SDL_RenderTextureRotated(r, t, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
        };
        if (stripes_pick_) quad(stripes_, 240, 176, 400, 256, 0, {255, 255, 255, 255});
        else quad(logo_, 220, 168, 412, 264, 0, {255, 255, 255, 255});
        // The tachometer's light picture (tex 1: the needle and its trail), (130, 130, 150),
        // turning clockwise about (320, 341).
        quad(tacho_, 288, 309, 352, 373, angle_, {130, 130, 150, 255});
    }

private:
    void drop_kept() {
        if (kept_) SDL_DestroyTexture(kept_), kept_ = nullptr;
    }
    void draw_capture(float x, float scale) {
        SDL_Renderer* r = app_.renderer();
        const SDL_FRect whole{x, 0, 640 * scale, 480 * scale};
        if (kept_) SDL_RenderTexture(r, kept_, nullptr, &whole);
        const float a = std::clamp(t_ - 0.2f, 0.0f, 1.0f);
        if (!tacho_ || a <= 0) return;
        const float k = 480.0f / 448.0f;
        SDL_SetTextureBlendMode(tacho_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(tacho_, 130, 130, 150);
        SDL_SetTextureAlphaMod(tacho_, u8(a * 255));
        const SDL_FRect dst{x + 288 * scale, 309 * k * scale, 64 * scale, 64 * k * scale};
        SDL_RenderTextureRotated(r, tacho_, nullptr, &dst, angle_, nullptr, SDL_FLIP_NONE);
    }
    SDL_Texture* texture(const gt3r::Tex1::Image& img) {
        if (!img.width || !img.height) return nullptr;
        SDL_Texture* t = SDL_CreateTexture(app_.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
        if (t) SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4)), SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return t;
    }
    std::optional<gt3r::Tex1::Image> decode(ByteSpan data, size_t index) {
        if (!gt3r::Tex1::is_tex1(data)) return std::nullopt;
        const gt3r::Tex1 set = gt3r::Tex1::parse(data);
        if (set.textures.size() <= index) return std::nullopt;
        gt3r::GsMemory gs;
        set.upload(data, gs);
        return set.decode(gs, index);
    }
    void load() {
        if (tried_) return;
        tried_ = true;
        try {
            if (const std::optional<Bytes> f = app_.vfs()->read("advertise/gtloading.img")) {
                if (auto img = decode(*f, 0)) logo_ = texture(*img);
                if (auto img = decode(*f, 1)) stripes_ = texture(*img);
            }
            // The program's own picture of the tachometer (its address in Gran Turismo 4 US: a
            // Tex1 there, else none).
            if (Disc* disc = app_.disc()) {
                const std::shared_ptr<const gt3r::CoreImage> image = disc->program_image();
                const gt3r::CoreImage& core = *image;
                constexpr u32 kTachometer = 0x68C080;
                if (kTachometer >= core.load_address && kTachometer - core.load_address + 0x40 < core.program.size()) {
                    const ByteSpan at = ByteSpan(core.program).subspan(kTachometer - core.load_address);
                    if (auto img = decode(at, 1)) tacho_ = texture(*img);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Loading page: %s", e.what());
        }
    }

    bool capture_ = false;
    SDL_Texture* kept_ = nullptr; // the picture the capture holds
    App& app_;
    bool tried_ = false, active_ = false, stripes_pick_ = true;
    float t_ = 0, ending_ = -1, angle_ = 0;
    SDL_Texture *logo_ = nullptr, *stripes_ = nullptr, *tacho_ = nullptr;
};

} // namespace gt4r
