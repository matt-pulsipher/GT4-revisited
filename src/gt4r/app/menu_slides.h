#pragma once

// The slide show faces of the running menus (mSlideShowFace, docs/formats/menu.md "The slide
// show"): the disc's photos one after another over the whole page, each cross-fading into the next,
// moving from the place the photo looks at to the whole picture (or back). The memory card's own
// list of photos (FileGT4SlideShowList) is not read: ❓ always the disc's.

#include "base/log.h"
#include "gt4r/app/app.h"
#include "gt4r/menu/jpeg.h"
#include "gt4r/menu/runtime.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <vector>

namespace gt4r {

class MenuSlides {
public:
    explicit MenuSlides(App& app) : app_(app) {}
    ~MenuSlides() {
        for (auto& [w, s] : shows_) clear(*s);
    }

    // How many photos the disc's list names (the configuration's DemonstrationSlideNum).
    void set_count(int n) { count_ = std::max(n, 0); }
    // The options' seconds a photo stands and fades, and whether it moves.
    void set_options(int play, int transition, bool zoom) {
        play_ = std::clamp(play, 3, 60) * 60, fade_ = std::max(transition, 0) * 60, zoom_ = zoom;
    }
    void begin_frame(u64 frame) {
        frame_ = frame;
        for (auto& [w, s] : shows_) s->seen = false;
    }
    void end_frame() {
        for (auto it = shows_.begin(); it != shows_.end();)
            if (!it->second->seen) clear(*it->second), it = shows_.erase(it);
            else ++it;
    }
    // `at` the face's rectangle in the page, `dst` on the screen: the photos cover the page's 640 x 480.
    void draw(const menu::LiveWidget& face, const menu::Rect& at, const SDL_FRect& dst, float opacity) {
        if (face.data.number("slide_playing") == 0 || count_ <= 0 || at.w <= 0) {
            if (const auto it = shows_.find(&face); it != shows_.end()) clear(*it->second), shows_.erase(it);
            return;
        }
        std::unique_ptr<Show>& s = shows_[&face];
        if (!s) s = std::make_unique<Show>(), s->stepped = frame_, s->shuffle = face.data.number("shuffle") != 0;
        s->seen = true;
        for (int n = 0; s->stepped < frame_ && n < 10; ++n, ++s->stepped) step(*s);
        s->stepped = frame_;
        const float scale = dst.w / at.w;
        const SDL_FRect page{dst.x - at.x * scale, dst.y - at.y * scale, 640 * scale, 480 * scale};
        SDL_Renderer* r = app_.renderer();
        SDL_Rect before{};
        const bool clipped = SDL_RenderClipEnabled(r);
        SDL_GetRenderClipRect(r, &before);
        const SDL_Rect cut{int(page.x), int(page.y), int(page.w), int(page.h)};
        SDL_SetRenderClipRect(r, &cut);
        for (int i : {1 - s->current, s->current}) draw_layer(s->layer[i], page, scale, opacity);
        SDL_SetRenderClipRect(r, clipped ? &before : nullptr);
    }

private:
    enum class State { FadeIn, Shown, Waiting, FadeOut, Gap, Empty };
    // One photo (0x1C5E48 on).
    struct Layer {
        State state = State::Empty;
        SDL_Texture* texture = nullptr;
        float w = 0, h = 0;
        std::optional<menu::JpegPicture::Focus> focus;
        int age = 0, phase = 0, moved = 0; // pictures since it came, in its state, of its movement
        float r1 = 0, r2 = 0;
        bool back = false; // moves the other way
    };
    struct Show {
        Layer layer[2];
        int current = 0;
        std::vector<int> order;
        size_t next = 0;
        bool shuffle = false, seen = false;
        u64 stepped = 0;
        std::future<std::optional<menu::JpegPicture>> loading;
        std::optional<menu::JpegPicture> ready;
        int failures = 0;
    };

    void clear(Show& s) {
        for (Layer& l : s.layer)
            if (l.texture) SDL_DestroyTexture(l.texture), l.texture = nullptr;
        if (s.loading.valid()) s.loading.wait();
    }
    float random() { return std::uniform_real_distribution<float>(0, 1)(random_); }
    // The next photo read here (the disc's reader is this thread's), decoded on a worker.
    void ask(Show& s) {
        if (s.ready || s.failures > 3) return;
        if (s.loading.valid()) {
            if (s.loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            s.ready = s.loading.get();
            if (!s.ready) ++s.failures;
            else s.failures = 0;
            return;
        }
        if (s.next >= s.order.size()) {
            s.order.resize(size_t(count_)), s.next = 0;
            for (int i = 0; i < count_; ++i) s.order[size_t(i)] = i;
            if (s.shuffle)
                for (int i = 0; i < count_; ++i) std::swap(s.order[size_t(i)], s.order[size_t(int(float(count_) * random()) % count_)]);
        }
        char path[32];
        std::snprintf(path, sizeof path, "photo/%03d.JPG", s.order[s.next++]);
        std::optional<Bytes> file = app_.vfs()->read(path);
        if (!file) {
            ++s.failures;
            return;
        }
        s.loading = std::async(std::launch::async, [f = std::move(*file), name = std::string(path)]() -> std::optional<menu::JpegPicture> {
            try {
                return menu::decode_jpeg(f);
            } catch (const std::exception& e) {
                LOG_WARN("Slide show %s: %s", name.c_str(), e.what());
                return std::nullopt;
            }
        });
    }
    void step(Show& s) {
        ask(s);
        for (Layer& l : s.layer) {
            ++l.age, ++l.phase;
            l.moved = std::min(l.moved + 1, play_ + 2 * fade_);
            if (l.state == State::FadeIn && l.phase >= fade_) l.state = State::Shown;
            if (l.state == State::Shown && l.age >= play_) l.state = State::Waiting;
            if (l.state == State::FadeOut && l.phase >= fade_) l.state = State::Gap, l.phase = 0;
            if (l.state == State::Gap && l.phase >= 4) l.state = State::Empty;
        }
        Layer& now = s.layer[s.current];
        Layer& other = s.layer[1 - s.current];
        if (!s.ready || (now.state != State::Waiting && now.state != State::Empty) || other.state != State::Empty) return;
        if (other.texture) SDL_DestroyTexture(other.texture), other.texture = nullptr;
        const menu::JpegPicture& p = *s.ready;
        other.texture = SDL_CreateTexture(app_.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, p.width, p.height);
        if (other.texture) {
            SDL_UpdateTexture(other.texture, nullptr, p.rgba.data(), p.width * 4);
            SDL_SetTextureScaleMode(other.texture, SDL_SCALEMODE_LINEAR);
            SDL_SetTextureBlendMode(other.texture, SDL_BLENDMODE_BLEND);
        }
        other.w = float(p.width), other.h = float(p.height), other.focus = p.focus;
        other.state = State::FadeIn, other.age = other.phase = other.moved = 0;
        other.r1 = random(), other.r2 = random(), other.back = random() < 0.5f;
        if (now.state != State::Empty) now.state = State::FadeOut, now.phase = 0;
        s.current = 1 - s.current;
        s.ready.reset();
    }
    void draw_layer(const Layer& l, const SDL_FRect& page, float scale, float opacity) {
        if (!l.texture || l.state == State::Empty || l.state == State::Gap) return;
        float alpha = 1;
        if (l.state == State::FadeIn && fade_ > 0) alpha = std::min(float(l.phase) / float(fade_), 1.0f);
        if (l.state == State::FadeOut && fade_ > 0) alpha = std::max(1.0f - float(l.phase) / float(fade_), 0.0f);
        // Where (0x1C6160): fitted in the page; with the effect, a photo with a focus record goes
        // between its focus, magnified, and the whole.
        const float fit = std::min(640 / l.w, 480 / l.h);
        float cx = l.w / 2, cy = l.h / 2, k = fit;
        if (zoom_ && l.focus) {
            const menu::JpegPicture::Focus& f = *l.focus;
            const float half = std::clamp(std::max(f.x1 - f.x0, f.y1 - f.y0) / 2, 0.5f, 1.0f); // ❓ the limiting axis: the longer
            const float z = std::clamp(half + (1 - half) * l.r1 * l.r1 - 0.2f * l.r2, 0.5f, 1.0f), near = fit / z;
            const float hw = 320 / near, hh = 240 / near;
            float fx = l.w * (f.x + 1) / 2, fy = l.h * (1 - f.y) / 2;
            fx = hw * 2 <= l.w ? std::clamp(fx, hw, l.w - hw) : l.w / 2;
            fy = hh * 2 <= l.h ? std::clamp(fy, hh, l.h - hh) : l.h / 2;
            float u = std::clamp(float(l.moved) / float(std::max(play_ + 2 * fade_, 1)), 0.0f, 1.0f);
            if (l.back) u = 1 - u;
            cx = fx + (cx - fx) * u, cy = fy + (cy - fy) * u;
            k = near / std::max(1.0f, (near / fit - 1) * u + 1);
        }
        SDL_SetTextureAlphaModFloat(l.texture, alpha * opacity);
        const SDL_FRect to{page.x + (320 - cx * k) * scale, page.y + (240 - cy * k) * scale, l.w * k * scale, l.h * k * scale};
        SDL_RenderTexture(app_.renderer(), l.texture, nullptr, &to);
    }

    App& app_;
    std::map<const menu::LiveWidget*, std::unique_ptr<Show>> shows_;
    u64 frame_ = 0;
    int count_ = 0, play_ = 8 * 60, fade_ = 4 * 60;
    bool zoom_ = true;
    std::mt19937 random_{0x47543452};
};

} // namespace gt4r
