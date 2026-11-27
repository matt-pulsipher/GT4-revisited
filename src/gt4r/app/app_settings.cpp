// App's settings: what the Settings page changes, applied to the window, the 3D device, the sound, the discs and the mods.

#include "gt4r/app/app.h"
#include "gt4r/app/game_area.h"

#include "audio/audio_engine.h"
#include "audio/voice_mixer.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/texture_pack.h"
#include "gt4r/formats/sound/sound_index.h"
#include "platform/music_folder.h"
#include "platform/render_scale.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace gt4r {

namespace fs = std::filesystem;

namespace {

float level_of(const gt3r::IniDocument& c, const char* key, float otherwise) {
    const std::optional<std::string> v = c.get("audio", key);
    if (!v) return otherwise;
    const float f = std::strtof(v->c_str(), nullptr);
    return std::isfinite(f) ? std::clamp(f, 0.0f, 1.0f) : otherwise;
}

std::string two_digits(float v) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", double(v));
    return b;
}

} // namespace

void App::load_settings() {
    video_ = settings::Video::load(config_);
    video_started_ = video_.gpu;
    sound_settings_.device = config_.get("audio", "device").value_or("");
    sound_settings_.music = level_of(config_, "music_volume", sound_settings_.music);
    sound_settings_.effects = level_of(config_, "effects_volume", sound_settings_.effects);
    sound_settings_.rear_cue = gt3r::to_lower(config_.get("audio", "rear_cue").value_or("on")) != "off";
    // The player's music.
    SDL_CreateDirectory(default_music_dir().c_str());
    music_.set_files(data_dir_);
    music_.set_source(audio::parse_song_source(config_.get("music", "source").value_or("")));
    music_.set_same_song(gt3r::to_lower(config_.get("music", "next_race").value_or("")) == "same");
    music_.set_last_song(audio::RaceSong::parse(config_.get("music", "race_song").value_or("")));
    follow_music_folder(false);
}

// --- the player's music ---

std::string App::music_folder() const {
    const std::string f = config_.get("music", "folder").value_or("");
    return f.empty() ? default_music_dir() : f;
}

bool App::music_folder_chosen() const { return !config_.get("music", "folder").value_or("").empty(); }

void App::set_music_folder(const std::string& path) {
    // The game's own folder picked in a picker (Android's shows it) is the default.
    if (path.empty() || gt3r::race::same_folder(path, default_music_dir())) config_.remove("music", "folder");
    else config_.set("music", "folder", path);
    save_config();
    follow_music_folder(false);
}

void App::follow_music_folder(bool rescan) {
    const std::string folder = music_folder();
    music_access_ = gt3r::platform::music_access(folder, data_dir_);
    const std::string use = music_access_ ? folder : std::string();
    if (use == music_.library().folder()) {
        if (rescan) music_.rescan();
    } else {
        music_.set_folder(use);
    }
}

bool App::show_song() const { return gt3r::to_lower(config_.get("music", "show_song").value_or("on")) != "off"; }

void App::measure_disc_music(const std::vector<int>& enabled) {
    audio::AppSound* s = vfs_ ? sound() : nullptr;
    if (!s) return;
    const int g = sound::stream_group("racing");
    const auto& groups = s->library().streams().groups;
    if (g < 0 || size_t(g) >= groups.size()) return;
    std::vector<int> pieces = enabled;
    if (pieces.empty())
        for (size_t n = 0; n < groups[size_t(g)].size(); ++n) pieces.push_back(int(n));
    std::sort(pieces.begin(), pieces.end());
    // At most eight, spread over the list: a piece takes seconds to measure (a phone many more); the list's level is their
    // mean (race_35 measures -7.7 LUFS).
    constexpr size_t kMeasured = 8;
    if (pieces.size() > kMeasured) {
        std::vector<int> some;
        for (size_t i = 0; i < kMeasured; ++i) some.push_back(pieces[i * pieces.size() / kMeasured]);
        pieces = std::move(some);
    }
    std::vector<gt3r::race::MusicLibrary::StreamOpener> songs;
    std::string names;
    for (const int n : pieces) {
        const std::string path = s->library().stream_file(g, n);
        std::optional<gt3r::Vfs::Stream> stream = path.empty() ? std::nullopt : vfs_->open_stream(path);
        if (!stream) continue;
        names += path + ':' + std::to_string(stream->size) + '\n';
        songs.push_back(audio::disc_piece_opener(*vfs_, path));
    }
    char key[24];
    std::snprintf(key, sizeof key, "%016llx", static_cast<unsigned long long>(gt3r::race::custom_song_id(names, 0)));
    if (songs.empty() || measured_key_ == key) return;
    measured_key_ = key;
    music_.library().set_disc_songs(key, std::move(songs));
    // (the loudest level a racing piece streams at: the mix table's stream level at the full option)
    music_.set_loudest_level(s->library().levels().level(0, sound::VolumeTable::Stream));
}

void App::forget_disc_music() {
    music_.library().set_disc_songs({}, {}); // (its measuring reads the disc)
    measured_key_.clear();
}

// --- the display ---

void App::set_video(const settings::Video& v) {
    const settings::Video was = video_;
    video_ = v;
    video_.save(config_);
    save_config();
    if (v.fullscreen != was.fullscreen && !scripted_ && !gt3r::platform::is_mobile() && fullscreen() != v.fullscreen) SDL_SetWindowFullscreen(window_, v.fullscreen);
    if (v.vsync != was.vsync && !run_.windowless) SDL_SetRenderVSync(renderer_, v.vsync ? 1 : 0);
    if (v.limit_60fps != was.limit_60fps) apply_frame_limit();
    if (gpu_ && v.msaa != was.msaa) {
        gt4_pipelines_.stop(); // (the sample count is part of every pipeline)
        gpu_->set_msaa(v.msaa);
        gt4_pipelines_.start(*gpu_);
    }
    if (gpu_ && v.anisotropy != was.anisotropy) gpu_->set_anisotropy(v.anisotropy);
    if (v.render_scale != was.render_scale || v.dynamic_resolution != was.dynamic_resolution) reset_dynamic_scale();
    if (v.texture_packs != was.texture_packs) use_texture_packs();
}

bool App::fullscreen() const { return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN); }

double App::refresh_rate() const {
    const SDL_DisplayMode* mode = window_ ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)) : nullptr;
    return mode ? double(mode->refresh_rate) : 0;
}

// The frame limit is SDL's main loop rate: every second, third ... refresh, which vsync still shows on a refresh.
void App::apply_frame_limit() {
#if GT3R_XR
    if (xr_) return; // (a headset paces the frames: a rate under its refresh would make it miss frames)
#endif
    const double rate = settings::limited_rate(refresh_rate(), video_.limit_60fps);
    if (rate == frame_limit_) return;
    frame_limit_ = rate;
    if (rate > 0) SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, std::to_string(rate).c_str());
    else SDL_ResetHint(SDL_HINT_MAIN_CALLBACK_RATE);
}

// The 3D views' size: the setting's, or the dynamic one under it. Pictures (--shot) keep the setting's.
void App::reset_dynamic_scale() {
    dynamic_on_ = false;
    if (!gpu_) return;
#if GT3R_XR
    if (xr_) return; // (the eyes are sized by [vr] resolution)
#endif
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    const float scale = video_.render_scale ? *video_.render_scale : gt3r::platform::auto_render_scale(pw, ph, gt3r::platform::is_mobile());
    gpu_->set_render_scale(scale);
    gpu_->set_render_ceiling(0);
    if (!video_.dynamic_resolution || scripted_) return;
    dynamic_on_ = true;
    const float top = video_.render_scale ? scale : std::max(scale, gt3r::platform::dynamic_ceiling(pw, ph));
    dynamic_.reset(top, float(refresh_rate()), scale);
    gpu_->set_render_scale(dynamic_.scale());
    gpu_->set_render_ceiling(top); // (the targets sized once)
}

SDL_Point App::menu_model_size(const SDL_FRect& dst) const {
    if (video_.sharp_menu_models || dst.w <= 0 || dst.h <= 0) return {0, 0};
    // (the page is 480 x 360 UI units, the console's picture of it 640 x 448)
    return {std::max(1, int(std::lround(dst.w * 640.0f / 480.0f))), std::max(1, int(std::lround(dst.h * 448.0f / 360.0f)))};
}

// The picture of the frame being drawn: the game's (Screen::follows_aspect) as wide as the aspect ratio makes it (a race's picture,
// Screen::view_fills_narrow, with fill down to 1:1), the app's pages 640 x 360, both as large as the window holds.
void App::layout_ui(bool game, bool view) {
    int pw = 0, ph = 0;
    SDL_GetRenderOutputSize(renderer_, &pw, &ph);
#if GT3R_XR
    if (xr_) SDL_GetCurrentRenderOutputSize(renderer_, &pw, &ph); // (the canvas stands in for the window)
#endif
    const int w = game ? settings::picture_width(video_.aspect, pw, ph, view) : Ui::kWidth;
    int cur_w = 0, cur_h = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer_, &cur_w, &cur_h, &mode);
    if (cur_w != w || cur_h != Ui::kHeight || mode != SDL_LOGICAL_PRESENTATION_LETTERBOX)
        SDL_SetRenderLogicalPresentation(renderer_, w, Ui::kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (ui_) ui_->set_layout(float(w), float(Ui::kHeight), {0, 0, float(w), float(Ui::kHeight)}, false);
    if (touch_ui_) touch_.controls().set_game_area(float(w), float(Ui::kHeight)); // (the on-screen controls lay out beside it)
    game_area::set_width(float(w));
}

// The window clear of a phone's display cutout and rounded corners (window units): where the on-screen controls go. SDL's own safe
// area counts the system's gesture bands too, so the cutout comes from the activity where it can be read.
SDL_FRect App::safe_area_window() const {
#if GT3R_XR
    if (xr_) return {0, 0, float(xr_->canvas_width()), float(xr_->canvas_height())};
#endif
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window_, &ww, &wh);
    SDL_FRect r{0, 0, float(ww), float(wh)};
    SDL_Rect sa{};
    float corner = 0;
    if (display_insets_ && ww > 0) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window_, &pw, &ph);
        const float k = pw > 0 ? float(ww) / float(pw) : 1.0f;
        const gt3r::platform::DisplayInsets& d = *display_insets_;
        r = {d.left * k, d.top * k, float(ww) - float(d.left + d.right) * k, float(wh) - float(d.top + d.bottom) * k};
        corner = float(d.corner_radius) * k;
    } else if (SDL_GetWindowSafeArea(window_, &sa) && sa.w > 0 && sa.h > 0) {
        r = {float(sa.x), float(sa.y), float(sa.w), float(sa.h)};
    }
    // A rectangle inset by 0.3 x the corners' radius from both edges keeps its corner on the screen (an unknown radius: about a
    // tenth of the short side).
    const float c = corner > 0 ? 0.3f * corner : 0.03f * float(std::min(ww, wh));
    const float x0 = std::max(r.x, c), y0 = std::max(r.y, c);
    const float x1 = std::min(r.x + r.w, float(ww) - c), y1 = std::min(r.y + r.h, float(wh) - c);
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

// The gamma: the frame goes into the pass's target, which its end puts on the window just before it is shown.
void App::begin_gamma_frame() {
    gamma_frame_ = false;
    bool headset = false;
#if GT3R_XR
    headset = xr_ != nullptr; // (the headset shows the canvases as they are)
#endif
    if (!gpu_ || headset || !gt3r::render::gamma_changes(video_.gamma)) {
        gamma_pass_.reset();
        return;
    }
    if (!gamma_pass_) gamma_pass_ = std::make_unique<gt3r::render::GammaPass>(*gpu_);
    int pw = 0, ph = 0;
    SDL_GetRenderOutputSize(renderer_, &pw, &ph);
    gamma_frame_ = gamma_pass_->begin(pw, ph);
}

// The frames drawn a second, over everything: at the top left of the window, on the app's own pages (whose header is there) at the
// bottom right.
void App::draw_fps(bool own_page) {
    if (!video_.show_fps || fps_shown_ <= 0) return;
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &pw, &ph);
    const float k = std::max(0.25f, std::min(float(pw) / Ui::kWidth, float(ph) / Ui::kHeight)); // pixels per UI unit
    char text[16];
    std::snprintf(text, sizeof text, "%d FPS", int(std::lround(fps_shown_)));
    const float w = std::ceil(Ui::text_width(text)) + 8, h = Ui::kChar + 4;
    const float x = own_page ? float(pw) / k - 2 - w : 2, y = own_page ? float(ph) / k - 2 - h : 2;
    SDL_SetRenderScale(renderer_, k, k);
    ui_->fill_round(x, y, w, h, 3, Color{0, 0, 0, 150});
    ui_->text(x + 4, y + 2, text);
    SDL_SetRenderScale(renderer_, 1, 1);
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, mode);
}

// --- the mouse ---

// The cursor goes 2 s after the mouse was last used, and comes back with it.
void App::idle_cursor(Uint64 now) {
    if (cursor_hidden_ || scripted_) return;
    if (!mouse_used_ns_) mouse_used_ns_ = now;
    if (now - mouse_used_ns_ < 2'000'000'000) return;
    SDL_HideCursor();
    cursor_hidden_ = true;
}

void App::mouse_event(const SDL_Event& e) {
    const bool used = (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.which != SDL_TOUCH_MOUSEID) ||
                      ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.which != SDL_TOUCH_MOUSEID) ||
                      (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.which != SDL_TOUCH_MOUSEID);
    if (!used) return;
    mouse_used_ns_ = SDL_GetTicksNS();
    if (cursor_hidden_) SDL_ShowCursor(), cursor_hidden_ = false;
}

// The mouse in a script (--keys): "move:<x>:<y>", "click:<x>:<y>", "back:<x>:<y>" (the right button), "wheel:<turns>:<x>:<y>",
// "drag:<x0>:<y0>:<x1>:<y1>", in UI units.
bool App::pointer_token(const std::string& token) {
    using Action = gt3r::PointerInput::Action;
    const std::vector<std::string_view> part = gt3r::split(token, ':');
    const auto num = [&part](size_t i) { return i < part.size() ? float(std::atof(std::string(part[i]).c_str())) : 0.0f; };
    const std::string_view what = part.empty() ? std::string_view() : part[0];
    Action a;
    if (what == "move" && part.size() == 3) a = {Action::Kind::Hover, num(1), num(2)};
    else if (what == "click" && part.size() == 3) a = {Action::Kind::Click, num(1), num(2)};
    else if (what == "back" && part.size() == 3) a = {Action::Kind::Back, num(1), num(2)};
    else if (what == "wheel" && part.size() == 4) a = {Action::Kind::Wheel, num(2), num(3)}, a.wheel = num(1);
    else if (what == "drag" && part.size() == 5) {
        scripted_pointer_.push_back({Action::Kind::Drag, num(3), num(4), num(1), num(2)});
        a = {Action::Kind::DragEnd, num(3), num(4), num(1), num(2)};
    } else return false;
    scripted_pointer_.push_back(a);
    return true;
}

// --- the sound ---

void App::set_sound(const Sound& s) {
    sound_settings_ = s;
    if (s.device.empty()) config_.remove("audio", "device");
    else config_.set("audio", "device", s.device);
    config_.set("audio", "music_volume", two_digits(s.music));
    config_.set("audio", "effects_volume", two_digits(s.effects));
    config_.set("audio", "rear_cue", s.rear_cue ? "on" : "off");
    save_config();
    apply_sound();
}

void App::apply_sound() {
    if (!audio_) return;
    if (audio_->output_device() != sound_settings_.device) audio_->set_output_device(sound_settings_.device);
    audio_->set_music_volume(recorder_ ? 0.0f : sound_settings_.music); // (a recording has no music: video_recorder.h)
    audio_->set_sfx_volume(sound_settings_.effects);
    if (sound_) audio_->voices().set_surround_cue(sound_settings_.rear_cue); // (AppSound puts the game's on)
}

// --- discs ---

std::vector<std::string> App::recent_discs() const {
    std::vector<std::string> list = gt3r::platform::load_recent_discs(config_);
    if (list.empty())
        if (const auto d = config_.get("game", "disc"); d && !d->empty()) list.push_back(*d); // (a config from before the list)
    return list;
}

std::optional<gt3r::platform::DiscRecord> App::disc_record(const std::string& path) const { return gt3r::platform::load_disc_record(config_, path); }

void App::forget_disc(const std::string& path) {
    gt3r::platform::save_recent_discs(config_, gt3r::platform::remove_recent_disc(recent_discs(), path));
    save_config();
}

// --- saves ---

bool App::save_dir_chosen() const { return !config_.get("saves", "folder").value_or("").empty(); }

std::string App::save_dir() const {
    std::string dir = config_.get("saves", "folder").value_or("");
    if (dir.empty()) return data_dir_;
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir;
}

std::string App::set_save_dir(const std::string& path, bool* has_its_own) {
    std::string to = path;
    if (!to.empty() && to.back() != '/' && to.back() != '\\') to += '/';
    if (to == data_dir_) to.clear();
    const std::string from = save_dir(), target = to.empty() ? data_dir_ : to;
    if (from == target) return {};
    std::error_code ec;
    fs::create_directories(fs::path(target), ec);
    // It can be written in: a file made and taken away again.
    {
        const std::string probe = target + ".gt4r-folder-check";
        const u8 byte = 0;
        if (!gt3r::write_host_file(probe, gt3r::ByteSpan(&byte, 1))) return "The folder cannot be written in: " + target;
        fs::remove(fs::path(probe), ec);
    }
    if (has_its_own) *has_its_own = fs::exists(fs::path(target) / "memorycard", ec) || fs::exists(fs::path(target) / "games", ec);
    // What the new folder does not have is copied there; nothing of either is written over or taken away.
    for (const char* part : {"memorycard", "memorycard2", "games", "replays", "photos"}) {
        const fs::path a = fs::path(from) / part, b = fs::path(target) / part;
        if (!fs::exists(a, ec) || fs::exists(b, ec)) continue;
        fs::copy(a, b, fs::copy_options::recursive, ec);
        if (ec) {
            LOG_WARN("Save folder: %s: %s", a.string().c_str(), ec.message().c_str());
            return "The saves could not be copied to " + target + " (" + ec.message() + ")";
        }
    }
    if (to.empty()) config_.remove("saves", "folder");
    else config_.set("saves", "folder", to);
    save_config();
    LOG_INFO("Save folder: %s", target.c_str());
    return {};
}

// --- mods ---

// The enabled mods' pictures stand in for the game's textures from what loads next on.
void App::use_texture_packs() { gt3r::texture_pack::set_pack(vfs_ && video_.texture_packs ? vfs_->textures() : nullptr); }

void App::rescan_mods() {
    if (!vfs_) return;
    forget_disc_music(); // (mods may replace the pieces)
    vfs_->scan_mods(mods_dir(), &config_);
    use_texture_packs();
    sound_.reset(); // (its banks are read again, as the database is)
    db_.reset(), db_tried_ = false;
}

void App::set_mod_enabled(const std::string& id, bool on) {
    if (!vfs_) return;
    forget_disc_music();
    config_.set(vfs_->mods_section(), id, on ? "on" : "off");
    save_config();
    vfs_->set_mod_enabled(id, on);
    use_texture_packs();
    sound_.reset(); // (its banks are read again, as the database is)
    db_.reset(), db_tried_ = false;
}

void App::push_dropped_mod(const std::string& path) {
    SDL_LockMutex(dialog_mutex_);
    dropped_mod_ = path;
    SDL_UnlockMutex(dialog_mutex_);
}

} // namespace gt4r
