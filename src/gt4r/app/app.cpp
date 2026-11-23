#include "gt4r/app/app.h"

#include "app/button_prompts.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/png_writer.h"
#include "gt4r_version.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace gt4r {

namespace {

const SDL_DialogFileFilter kDiscFilters[] = {
    {"PS2 disc images", "iso;chd"},
    {"All files", "*"},
};

void SDLCALL on_dialog(void* user, const char* const* files, int) {
    if (files && files[0]) static_cast<App*>(user)->request_disc(files[0]);
}

} // namespace

std::string user_data_dir() {
    std::string dir;
    if (const char* d = SDL_getenv("GT4R_DATA_DIR"); d && *d) {
        dir = d;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        return dir;
    }
    const char* base = SDL_GetBasePath();
    if (base && gt3r::host_path_exists(std::string(base) + "portable.ini")) {
        dir = base;
    } else if (char* pref = SDL_GetPrefPath("GT4Revisited", "GT4Revisited")) {
        dir = pref;
        SDL_free(pref);
    }
    return dir;
}

bool App::init(int argc, char** argv) {
    SDL_SetAppMetadata("GT4 Revisited", GT4R_VERSION, "org.gt4revisited.gt4r");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    data_dir_ = user_data_dir();
    SDL_CreateDirectory(data_dir_.c_str());
    SDL_CreateDirectory(mods_dir().c_str());
    config_path_ = data_dir_ + "config.ini";
    if (auto cfg = gt3r::IniDocument::load(config_path_)) config_ = std::move(*cfg);
    LOG_INFO("GT4 Revisited %s; user data: %s", GT4R_VERSION, data_dir_.c_str());

    window_ = SDL_CreateWindow("GT4 Revisited", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        LOG_ERROR("Window creation failed: %s", SDL_GetError());
        return false;
    }
    bool use_gpu = true;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-gpu") use_gpu = false;
    if (use_gpu) gpu_ = gt3r::render::GpuContext::create(window_, gt3r::render::GpuOptions{});
    if (gpu_) {
        renderer_ = gpu_->renderer2d();
        gpu_->warm_pipelines();
    } else {
        if (use_gpu) LOG_WARN("3D drawing is not available; continuing with the 2D screens only");
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            LOG_ERROR("Renderer creation failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer_, 1);
    }
    SDL_SetRenderLogicalPresentation(renderer_, Ui::kWidth, Ui::kHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    ui_ = std::make_unique<Ui>(renderer_);
    ui_->set_layout(float(Ui::kWidth), float(Ui::kHeight), {0, 0, float(Ui::kWidth), float(Ui::kHeight)}, false);

    input_ = std::make_unique<gt3r::InputManager>();
    input_->load(config_);
    audio_ = std::make_unique<gt3r::AudioEngine>(config_.get("audio", "device").value_or(""));
    dialog_mutex_ = SDL_CreateMutex();

    std::string disc_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has_value = i + 1 < argc;
        if (a == "--browse" && has_value) browse_ = argv[++i];
        else if (a == "--shot" && has_value) shot_path_ = argv[++i];
        else if (a == "--screen" && has_value) start_ = argv[++i];
        else if (a == "--shot-frame" && has_value) shot_frame_ = std::atoi(argv[++i]);
        else if (a == "--keys" && has_value) {
            for (std::string_view k : gt3r::split(argv[++i], ','))
                if (!gt3r::trim(k).empty()) keys_.emplace_back(gt3r::to_lower(gt3r::trim(k)));
        } else if (!a.empty() && a[0] != '-') disc_path = a;
    }
    if (disc_path.empty()) disc_path = config_.get("game", "disc").value_or("");
    if (!disc_path.empty()) last_error_ = open_disc(disc_path);
    if (disc_) {
        push(make_hub_screen());
        if (!browse_.empty()) push(make_browser_screen(browse_));
        else if (start_ == "menus") push(make_menu_viewer(*this));
        else if (start_ == "race") push(make_race_screen(*this));
        else if (start_ == "cars" || start_ == "courses") keys_.insert(keys_.begin(), start_ == "cars" ? "cross" : "down"), start_ == "courses" ? void(keys_.insert(keys_.begin() + 1, "cross")) : void();
    } else push(make_setup_screen());
    last_ticks_ = SDL_GetTicksNS();
    return true;
}

std::string App::open_disc(const std::string& path) {
    std::unique_ptr<Disc> disc;
    try {
        disc = Disc::open(path);
    } catch (const std::exception& e) {
        LOG_WARN("Could not open %s: %s", path.c_str(), e.what());
        return e.what();
    }
    if (audio_) audio_->stop_music(); // (it streams from the disc being closed)
    vfs_.reset();
    db_.reset(), db_tried_ = false;
    disc_ = std::move(disc);
    vfs_ = std::make_unique<gt3r::Vfs>(&disc_->volume());
    vfs_->scan_mods(mods_dir(), &config_);
    // Remembered as an absolute path, so a launch from another folder finds it.
    std::string saved = path;
    if (path.find("://") == std::string::npos) {
        if (char* real = SDL_GetCurrentDirectory(); real && !path.empty() && path[0] != '/' && !(path.size() > 1 && path[1] == ':'))
            saved = std::string(real) + path, SDL_free(real);
        else SDL_free(real);
    }
    if (config_.get("game", "disc").value_or("") != saved) {
        config_.set("game", "disc", saved);
        if (!config_.save(config_path_)) LOG_WARN("Could not save %s", config_path_.c_str());
    }
    return {};
}

db::SpecDb* App::db() {
    if (!db_tried_ && vfs_) {
        db_tried_ = true;
        gt3r::Vfs* vfs = vfs_.get();
        try {
            db_ = std::make_unique<db::SpecDb>([vfs](const std::string& path) { return vfs->read(path); },
                                               [vfs](const std::string& dir) { return vfs->list_dir(dir); });
        } catch (const std::exception& e) {
            LOG_WARN("Car database: %s", e.what());
        }
    }
    return db_.get();
}

void App::show_open_dialog() {
    SDL_ShowOpenFileDialog(on_dialog, this, window_, kDiscFilters, SDL_arraysize(kDiscFilters), nullptr, false);
}

void App::request_disc(std::string path) {
    SDL_LockMutex(dialog_mutex_);
    dialog_result_ = std::move(path);
    SDL_UnlockMutex(dialog_mutex_);
}

void App::apply_screen_changes() {
    for (; pops_ > 0 && !screens_.empty(); --pops_) screens_.pop_back();
    pops_ = 0;
    for (auto& s : pending_) screens_.push_back(std::move(s));
    pending_.clear();
}

// --keys: each name is a pad button held for two frames and let go for six; "waitN" is N
// frames without a press.
gt3r::PadState App::scripted_keys() {
    gt3r::PadState pad;
    if (key_ < keys_.size()) {
        const std::string& k = keys_[key_];
        const bool wait = k.compare(0, 4, "wait") == 0;
        const int frames = wait ? std::max(1, std::atoi(k.c_str() + 4)) : 8;
        if (!wait && key_frames_ < 2) {
            std::string name = k;
            name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
            if (name.size() == 2) name[1] = char(std::toupper(static_cast<unsigned char>(name[1]))); // "L1"
            if (const auto b = gt3r::pad_button_from_name(name)) {
                pad.buttons = u16(1u << unsigned(*b));
                pad.pressure[size_t(*b)] = 255;
            } else if (key_frames_ == 0) {
                LOG_WARN("--keys: no button named %s", k.c_str());
            }
        }
        if (++key_frames_ >= frames) ++key_, key_frames_ = 0;
    }
    return pad;
}

SDL_AppResult App::iterate() {
    const Uint64 now = SDL_GetTicksNS();
    double dt = std::min(double(now - last_ticks_) / 1e9, 0.1);
    last_ticks_ = now;
    if (!shot_path_.empty()) dt = 1.0 / 60.0; // pictures do not depend on the machine's speed

    std::string chosen;
    SDL_LockMutex(dialog_mutex_);
    chosen.swap(dialog_result_);
    SDL_UnlockMutex(dialog_mutex_);
    if (!chosen.empty()) {
        last_error_ = open_disc(chosen);
        if (last_error_.empty()) {
            screens_.clear();
            pending_.clear();
            push(make_hub_screen());
        }
    }

    apply_screen_changes();
    const gt3r::PadState scripted = scripted_keys();
    input_->update(dt);
    menu_.update(scripted.buttons ? scripted : input_->menu_pad(), dt);
    if (!screens_.empty()) screens_.back()->update(*this, dt);
    apply_screen_changes();
    if (screens_.empty()) quit_ = true;

    Ui::set_prompts(gt3r::button_prompts(*input_, false, false)); // "{Cross}" in the hints
    ui_->clear();
    if (!screens_.empty()) screens_.back()->render(*this, *ui_);
    ++frame_;
    if (!shot_path_.empty() && frame_ >= shot_frame_) {
        save_screenshot(shot_path_);
        quit_ = true;
    }
    SDL_RenderPresent(renderer_);
    if (gpu_) gpu_->frame_presented();
    return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult App::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    input_->handle_event(e);
    if (audio_ && (e.type == SDL_EVENT_AUDIO_DEVICE_ADDED || e.type == SDL_EVENT_AUDIO_DEVICE_REMOVED)) audio_->device_event(e);
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) request_disc(e.drop.data);
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11 && !e.key.repeat)
        SDL_SetWindowFullscreen(window_, !(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN));
    if (!screens_.empty()) screens_.back()->on_event(*this, e);
    return SDL_APP_CONTINUE;
}

void App::save_screenshot(const std::string& path) {
    SDL_Surface* shot = SDL_RenderReadPixels(renderer_, nullptr);
    if (!shot) return;
    SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(shot);
    if (!rgba) return;
    std::vector<u32> px(size_t(rgba->w) * rgba->h);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(&px[size_t(y) * rgba->w], static_cast<u8*>(rgba->pixels) + size_t(y) * rgba->pitch, size_t(rgba->w) * 4);
    const Bytes png = gt3r::encode_png(u32(rgba->w), u32(rgba->h), px);
    SDL_DestroySurface(rgba);
    if (gt3r::write_host_file(path, png)) LOG_INFO("Screenshot saved: %s", path.c_str());
}

void App::shutdown() {
    screens_.clear(); // (screens stop their streams before the audio and the disc go)
    pending_.clear();
    audio_.reset();
    db_.reset();
    vfs_.reset();
    disc_.reset();
    ui_.reset();
    input_.reset();
    if (dialog_mutex_) SDL_DestroyMutex(dialog_mutex_);
    if (gpu_) gpu_.reset(); // (its renderer goes with it)
    else if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

} // namespace gt4r
