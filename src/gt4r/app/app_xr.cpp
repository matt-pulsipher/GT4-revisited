// VR headsets (Meta Quest, PC VR) through the shared engine's OpenXR session (its src/xr and docs/dev/VR.md; docs/VR.md here):
// the game draws its frames as always, into the session's canvas instead of the window, and the headset shows the canvas on a
// large virtual screen in front of the player. While the player drives, the race draws each eye instead (app/race_vr.cpp) and
// the canvas holds its display, on a panel in front of the car. The controllers are a virtual pad, and the right one points at
// the screen like a mouse. Only builds with GT3R_XR have it.

#include "gt4r/app/app.h"

#if GT3R_XR

#include "gt4r/app/updates.h"

#include "base/file.h"
#include "base/log.h"
#include "formats/png_writer.h"
#include "base/config.h"
#include "platform/render_scale.h"

#include <cstring>

namespace gt4r {

void App::xr_start(gt3r::render::GpuOptions& gpu, int argc, char** argv) {
    // A headset when there is one ([vr] enabled = off or --no-vr: the window, as on a phone).
    gpu.xr = gt3r::to_lower(config_.get("vr", "enabled").value_or("on")) != "off";
    gpu.xr_runtime = gt3r::to_lower(config_.get("vr", "runtime").value_or("auto"));
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-vr") gpu.xr = false;
    gpu_ = gt3r::render::GpuContext::create(window_, gpu);
    if (!gpu_ || !gpu_->xr()) return;
    xr_ = gt3r::xr::Session::create(gpu_->device(), gpu_->renderer2d(), gpu_->xr_request(), gt3r::xr::Settings::load(config_));
    if (xr_) {
        gpu_->set_render_scale(1.0f); // (the eyes are sized by [vr] resolution; the canvas is small)
        if (!gt3r::platform::is_mobile()) xr_->mirror_to(window_);
        LOG_INFO("VR: %s", xr_->runtime_name().c_str());
        return;
    }
    LOG_WARN("VR: the headset's session could not be set up; the window instead");
    gpu_.reset();
    gpu.xr = false;
    gpu_ = gt3r::render::GpuContext::create(window_, gpu);
}

std::optional<SDL_AppResult> App::xr_begin_frame() {
    if (!xr_) return std::nullopt;
    xr_dt_ = 0;
    xr_->poll();
    if (xr_->exit_requested()) return SDL_APP_SUCCESS;
    // The system's menu over the game: a race the player drives pauses, as when the game goes to the background.
    if (xr_->take_focus_lost() && !screens_.empty()) {
        SDL_Event bg{};
        bg.type = SDL_EVENT_DID_ENTER_BACKGROUND;
        screens_.back()->on_event(*this, bg);
    }
    // Nothing to draw while the session is not running (the headset off, or not on the head).
    if (!xr_->begin_frame()) {
        SDL_Delay(10);
        last_ticks_ = SDL_GetTicksNS();
        xr_last_display_ = 0;
        return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
    }
    // The step: the headset's display times apart, even however long the frame's work takes.
    const s64 t = xr_->display_time();
    if (xr_last_display_ > 0 && t > xr_last_display_) xr_dt_ = double(t - xr_last_display_) / 1e9;
    xr_last_display_ = t;
    return std::nullopt;
}

void App::xr_input(double dt) {
    // The menus' pointer, except while driving (the right trigger is the throttle there).
    const Screen* top = screens_.empty() ? nullptr : screens_.back().get();
    const bool pointer = updates().modal() || (top && top->touch_layout() != gt3r::TouchLayout::Driving);
    xr_->update_input(dt, pointer);
    SDL_UpdateJoysticks(); // (the virtual pad's new state, this frame)
    xr_pointer();
    for (int p = 0; p < gt3r::InputManager::kMaxPlayers; ++p)
        if (input_->action_pressed(p, gt3r::PadAction::RecenterView)) xr_->recenter();
}

void App::set_xr_settings(const gt3r::xr::Settings& s) {
    if (!xr_) return;
    xr_->set_settings(s);
    s.save(config_);
    save_config();
}

// The right controller points at the virtual screen (menus, movies; not while driving in stereo): a dot follows it and the
// trigger clicks, as the mouse's events (of kXrPointerMouse, the engine's ui.h) in UI units, which the page takes as the mouse's.
void App::xr_pointer() {
    auto dispatch = [this](const SDL_Event& e) {
        pointer_.event(e);
        if (!screens_.empty()) screens_.back()->on_event(*this, e);
    };
    float u = 0, v = 0;
    bool pressed = false;
    if (!xr_->pointer(u, v, pressed)) {
        if (xr_pointer_down_) { // (let go off the screen)
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.which = gt3r::kXrPointerMouse;
            e.button.x = xr_pointer_x_, e.button.y = xr_pointer_y_;
            dispatch(e);
        }
        xr_pointer_on_ = xr_pointer_down_ = false;
        xr_->set_a_clicks(false);
        ui_->set_pointer(false, 0, 0);
        return;
    }
    float x = 0, y = 0;
    canvas_to_ui(u, v, x, y);
    if (!xr_pointer_on_ || x != xr_pointer_x_ || y != xr_pointer_y_) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.which = gt3r::kXrPointerMouse;
        e.motion.x = x, e.motion.y = y;
        e.motion.xrel = xr_pointer_on_ ? x - xr_pointer_x_ : 0;
        e.motion.yrel = xr_pointer_on_ ? y - xr_pointer_y_ : 0;
        e.motion.state = xr_pointer_down_ ? SDL_BUTTON_LMASK : 0;
        dispatch(e);
    }
    xr_pointer_on_ = true;
    xr_pointer_x_ = x, xr_pointer_y_ = y;
    if (pressed != xr_pointer_down_) {
        SDL_Event e{};
        e.type = pressed ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.which = gt3r::kXrPointerMouse;
        e.button.down = pressed;
        e.button.clicks = 1;
        e.button.x = x, e.button.y = y;
        dispatch(e);
        xr_pointer_down_ = pressed;
    }
    // (A is the pad's Cross everywhere: our pages and the game's menus take the trigger's click.)
    xr_->set_a_clicks(false);
    ui_->set_pointer(true, x, y); // (buttons and tabs under it light up)
}

// The pointer's dot on the virtual screen, over everything.
void App::xr_draw_pointer() {
    if (!xr_ || !xr_pointer_on_) return;
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 230);
    const SDL_FRect dot{xr_pointer_x_ - 2, xr_pointer_y_ - 2, 4, 4};
    SDL_RenderFillRect(renderer_, &dot);
    SDL_SetRenderDrawColor(renderer_, 20, 90, 200, 255);
    SDL_RenderRect(renderer_, &dot);
}

// The canvas's pixels (u, v from its top left, 0..1) in UI units, through its logical presentation (layout_ui).
void App::canvas_to_ui(float u, float v, float& x, float& y) {
    x = u * float(xr_->canvas_width()), y = v * float(xr_->canvas_height());
    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    SDL_SetRenderTarget(renderer_, xr_->canvas());
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_FRect lr{};
    if (SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode) && mode != SDL_LOGICAL_PRESENTATION_DISABLED && lw > 0 && lh > 0 &&
        SDL_GetRenderLogicalPresentationRect(renderer_, &lr) && lr.w > 0 && lr.h > 0) {
        x = (x - lr.x) * float(lw) / lr.w;
        y = (y - lr.y) * float(lh) / lr.h;
    }
    SDL_SetRenderTarget(renderer_, prev);
}

// The mouse on a computer's window, which shows the canvas whole (xr::Session::mirror_to): the offscreen renderer leaves its
// events in the window's units.
void App::mirror_pointer(SDL_Event& e) {
    float* x = nullptr;
    float* y = nullptr;
    if (e.type == SDL_EVENT_MOUSE_MOTION) x = &e.motion.x, y = &e.motion.y;
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) x = &e.button.x, y = &e.button.y;
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) x = &e.wheel.mouse_x, y = &e.wheel.mouse_y;
    else return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    const SDL_FRect r = gt3r::xr::fit_picture(xr_->canvas_width(), xr_->canvas_height(), float(ww), float(wh), false);
    canvas_to_ui((*x - r.x) / r.w, (*y - r.y) / r.h, *x, *y);
}

// A stereo frame's picture (the canvas: the race's display over transparency) has the two eyes beside it, side by side, as
// <path>-eyes.png.
void App::save_eyes(const std::string& path) {
    if (!xr_ || !xr_->stereo_submitted()) return;
    SDL_Texture* prev = SDL_GetRenderTarget(renderer_);
    const gt3r::xr::Eye& e0 = xr_->eye(0);
    const int w = e0.width, h = e0.height;
    std::vector<u32> both(size_t(w) * 2 * size_t(h));
    for (int i = 0; i < 2; ++i) {
        SDL_SetRenderTarget(renderer_, xr_->eye(i).canvas);
        SDL_Surface* s = SDL_RenderReadPixels(renderer_, nullptr);
        SDL_Surface* c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (s) SDL_DestroySurface(s);
        if (!c) continue;
        for (int y = 0; y < std::min(h, c->h); ++y)
            std::memcpy(&both[size_t(y) * size_t(w) * 2 + size_t(i) * size_t(w)], static_cast<u8*>(c->pixels) + size_t(y) * size_t(c->pitch),
                        size_t(std::min(w, c->w)) * 4);
        SDL_DestroySurface(c);
    }
    SDL_SetRenderTarget(renderer_, prev);
    const size_t dot = path.rfind('.');
    const std::string eyes = (dot == std::string::npos ? path : path.substr(0, dot)) + "-eyes.png";
    if (gt3r::write_host_file(eyes, gt3r::encode_png(u32(w) * 2, u32(h), both))) LOG_INFO("The eyes saved: %s", eyes.c_str());
}

} // namespace gt4r

#endif // GT3R_XR
