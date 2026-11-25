#include "gt4r/app/app.h"

#include "gt4r/input/player_controls.h"

#include "app/button_prompts.h"
#include "base/file.h"
#include "base/log.h"
#include "base/log_file.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"
#include "platform/crash_handlers.h"
#include "platform/mac_layer.h"
#include "vfs/mod_archive.h"
#include "gt4r_version.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>

#if defined(__APPLE__)
#include <dlfcn.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace gt4r {

namespace {

const SDL_DialogFileFilter kDiscFilters[] = {
    {"PS2 disc images", "iso;chd"},
    {"All files", "*"},
};

void SDLCALL on_dialog(void* user, const char* const* files, int) {
    if (files && files[0]) static_cast<App*>(user)->request_disc(files[0]);
}

// The process's resident memory in bytes (0 where it is not read).
size_t resident_bytes() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) return size_t(info.resident_size);
#elif defined(__linux__)
    if (std::FILE* f = std::fopen("/proc/self/statm", "r")) {
        unsigned long pages = 0, resident = 0;
        const int n = std::fscanf(f, "%lu %lu", &pages, &resident);
        std::fclose(f);
        if (n == 2) return size_t(resident) * size_t(sysconf(_SC_PAGESIZE));
    }
#endif
    return 0;
}

// A scripted run keeps its pace when its window is hidden or covered: on macOS the system
// otherwise stretches a background app's sleeps (App Nap), by seconds at a time, and the run's
// frames fall out of step with what goes by the clock. The process says it is doing work the
// user asked for (NSProcessInfo beginActivityWithOptions:reason:), through the Objective-C
// runtime the system's frameworks already loaded.
void keep_pace_in_background() {
#if defined(__APPLE__)
    using Id = void*;
    using GetClass = Id (*)(const char*);
    using RegisterName = Id (*)(const char*);
    const auto get_class = reinterpret_cast<GetClass>(dlsym(RTLD_DEFAULT, "objc_getClass"));
    const auto sel = reinterpret_cast<RegisterName>(dlsym(RTLD_DEFAULT, "sel_registerName"));
    void* send = dlsym(RTLD_DEFAULT, "objc_msgSend");
    if (!get_class || !sel || !send) return;
    const Id info = reinterpret_cast<Id (*)(Id, Id)>(send)(get_class("NSProcessInfo"), sel("processInfo"));
    const Id reason = reinterpret_cast<Id (*)(Id, Id, const char*)>(send)(get_class("NSString"), sel("stringWithUTF8String:"), "a scripted run");
    if (!info || !reason) return;
    // NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
    const unsigned long long options = (0x00FFFFFFull & ~(1ull << 20)) | 0xFF00000000ull;
    const Id activity = reinterpret_cast<Id (*)(Id, Id, unsigned long long, Id)>(send)(info, sel("beginActivityWithOptions:reason:"), options, reason);
    if (activity) reinterpret_cast<Id (*)(Id, Id)>(send)(activity, sel("retain")); // held for the process's life
#endif
}

} // namespace

double App::resident_mb() { return double(resident_bytes()) / (1024.0 * 1024.0); }

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
    // The log file first, so that it has everything after it: gt3r.log in the data folder (the shared engine's name for it), the
    // session before in gt3r.prev.log, and why the game closed when it did not close by itself.
    {
        const std::string dir = user_data_dir();
        SDL_CreateDirectory(dir.c_str());
        gt3r::log_file::open(dir);
    }
    if (const char* c = SDL_getenv("GT4R_CRASH_TEST")) gt3r::platform::crash_test(c); // throw | segv | abort
#ifdef SDL_PLATFORM_WINDOWS
    // Xbox pads through SDL's RAWINPUT driver, for their impulse triggers, unless [input] xbox_driver = xinput: SDL takes it
    // as it starts its joysticks.
    {
        const std::optional<gt3r::IniDocument> cfg = gt3r::IniDocument::load(user_data_dir() + "config.ini");
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, gt3r::InputManager::xbox_rawinput(cfg ? *cfg : gt3r::IniDocument{}) ? "1" : "0");
    }
#endif
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
    load_settings();

    // A scripted run (pictures, a sweep) does not take the keyboard from whoever is at the
    // machine: its window opens without becoming the active one and cannot be focused.
    bool& scripted = scripted_;
    for (int i = 1; i < argc; ++i)
        if (const std::string a = argv[i]; a == "--shot" || a == "--sweep") scripted = true;
    if (scripted) SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    // (A picture's window in another shape, for checks: GT4R_WINDOW=1600x700.)
    int window_w = 1280, window_h = 720;
    if (const char* size = SDL_getenv("GT4R_WINDOW"); size && std::sscanf(size, "%dx%d", &window_w, &window_h) != 2) window_w = 1280, window_h = 720;
    // macOS: in full screen the menu bar comes down at the screen's top as in other apps.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1");
    window_ = SDL_CreateWindow("GT4 Revisited", std::clamp(window_w, 320, 8192), std::clamp(window_h, 180, 8192),
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                   (scripted ? SDL_WINDOW_NOT_FOCUSABLE : video_.fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WindowFlags(0)));
    if (!window_) {
        LOG_ERROR("Window creation failed: %s", SDL_GetError());
        return false;
    }
    gt3r::render::GpuOptions gpu;
    gpu.msaa = video_.msaa, gpu.anisotropy = video_.anisotropy, gpu.vsync = video_.vsync;
    gpu.gles = video_.gpu == settings::Gpu::Gles || video_.gpu == settings::Gpu::GlesDriver;
    gpu.angle = video_.gpu != settings::Gpu::GlesDriver;
    bool use_gpu = video_.gpu != settings::Gpu::Off;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-gpu") use_gpu = false;
    if (use_gpu) gpu_ = gt3r::render::GpuContext::create(window_, gpu);
    if (gpu_) {
        renderer_ = gpu_->renderer2d();
        gpu_->warm_pipelines();
        gt3r::platform::keep_composited(window_); // (macOS: no switching between the display's two ways of showing a full screen)
    } else {
        if (use_gpu) LOG_WARN("3D drawing is not available; continuing with the 2D screens only");
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            LOG_ERROR("Renderer creation failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer_, video_.vsync ? 1 : 0);
    }
    ui_ = std::make_unique<Ui>(renderer_);
    layout_ui(false);
    reset_dynamic_scale();
    apply_frame_limit();

    input_ = std::make_unique<gt3r::InputManager>();
    {
        gt3r::IniDocument controls = config_; // (our words for the original's layouts as the engine has them)
        input::translate_config(controls);
        input_->load(controls);
    }
    audio_ = std::make_unique<gt3r::AudioEngine>(sound_settings_.device);
    apply_sound();
    dialog_mutex_ = SDL_CreateMutex();

    std::string disc_path;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has_value = i + 1 < argc;
        if (a == "--browse" && has_value) browse_ = argv[++i];
        else if (a == "--shot" && has_value) shot_path_ = argv[++i];
        else if (a == "--screen" && has_value) start_ = argv[++i];
        else if (a == "--shot-frame" && has_value) shot_frame_ = std::atoi(argv[++i]);
        else if (a == "--stats" && has_value) stats_path_ = argv[++i];
        else if (a == "--fast") paced_ = false;
        else if (a == "--sweep" && has_value) sweep_ = argv[++i];
        else if (a == "--sweep-out" && has_value) sweep_out_ = argv[++i];
        else if (a == "--keys" && has_value) {
            // "@<file>" among the keys: the keys of that file there (files may name files).
            std::function<void(const std::string&, int)> add = [&](const std::string& text, int depth) {
                for (std::string& k : parse_key_script(text)) {
                    if (k[0] != '@') {
                        keys_.push_back(std::move(k));
                        continue;
                    }
                    const std::optional<Bytes> file = depth < 8 ? gt3r::read_host_file(k.substr(1)) : std::nullopt;
                    if (!file) LOG_WARN("--keys: could not read %s", k.c_str() + 1);
                    else add(std::string(file->begin(), file->end()), depth + 1);
                }
            };
            add(argv[++i], 0);
        } else if (!a.empty() && a[0] != '-') disc_path = a;
    }
    if (!shot_path_.empty() || !sweep_.empty()) keep_pace_in_background();
    if (disc_path.empty()) disc_path = config_.get("game", "disc").value_or("");
    if (!disc_path.empty()) last_error_ = open_disc(disc_path);
    if (disc_) {
        push(make_hub_screen());
        if (!sweep_.empty()) push(make_sweep_screen(sweep_, sweep_out_.empty() ? std::string("sweep") : sweep_out_));
        else if (!browse_.empty()) push(make_browser_screen(browse_));
        else if (start_ == "menus") push(make_menu_viewer(*this));
        else if (start_ == "race") push(make_race_screen(*this));
        else if (start_ == "controls") push(make_controls_screen(*this));
        else if (start_ == "settings") push(make_settings_screen(*this));
        else if (start_ == "cars" || start_ == "courses") { // (the hub's items: Play, Cars, Courses)
            const std::vector<std::string> to = start_ == "cars" ? std::vector<std::string>{"down", "cross"} : std::vector<std::string>{"down", "down", "cross"};
            keys_.insert(keys_.begin(), to.begin(), to.end());
        }
        // A launch with a disc and nothing else asked for: the game from power on, the home
        // screen under it ([game] start = hub: the home screen).
        else if (start_ == "game" || (start_.empty() && config_.get("game", "start").value_or("game") != "hub")) push(make_game_screen(*this));
    } else push(make_setup_screen());
    last_ticks_ = start_ticks_ = SDL_GetTicksNS();
    return true;
}

std::vector<std::string> App::parse_key_script(std::string_view text) {
    std::vector<std::string> keys;
    for (std::string_view line : gt3r::split(text, '\n')) {
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        for (std::string_view k : gt3r::split(line, ','))
            if (const std::string_view t = gt3r::trim(k); !t.empty()) keys.emplace_back(t[0] == '@' || t.substr(0, 5) == "drop:" ? std::string(t) : gt3r::to_lower(t)); // (a file's name as written)
    }
    return keys;
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
    sound_.reset();
    vfs_.reset();
    db_.reset(), db_tried_ = false;
    disc_ = std::move(disc);
    disc_->prepare_program(); // (on a thread: the menus and the races read its tables)
    vfs_ = std::make_unique<gt3r::Vfs>(&disc_->volume());
    vfs_->scan_mods(mods_dir(), &config_);
    use_texture_packs();
    // Remembered as an absolute path, so a launch from another folder finds it.
    std::string saved = path;
    if (path.find("://") == std::string::npos) {
        if (char* real = SDL_GetCurrentDirectory(); real && !path.empty() && path[0] != '/' && !(path.size() > 1 && path[1] == ':'))
            saved = std::string(real) + path, SDL_free(real);
        else SDL_free(real);
    }
    // The disc opened at the next start, the list of the discs opened, what this one is.
    const DiscInfo& d = disc_->info();
    const std::string before = config_.serialize();
    std::vector<std::string> recent = recent_discs();
    config_.set("game", "disc", saved);
    gt3r::platform::save_recent_discs(config_, gt3r::platform::push_recent_disc(std::move(recent), saved));
    gt3r::platform::save_disc_record(config_, saved, {d.serial, d.version, "", d.digest, d.key, d.description, d.verified, d.modified});
    if (config_.serialize() != before) save_config();
    return {};
}

audio::AppSound* App::sound() {
    if (!sound_ && audio_ && vfs_) sound_ = std::make_unique<audio::AppSound>(*audio_, *vfs_), apply_sound();
    return sound_.get();
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

void App::save_config() {
    if (!config_.save(config_path_)) LOG_WARN("Could not save %s", config_path_.c_str());
}

void App::show_open_dialog() {
    if (scripted_) { // (no window over whoever is at the machine: GT4R_DIALOG is the answer)
        if (const char* path = SDL_getenv("GT4R_DIALOG")) request_disc(path);
        return;
    }
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

// --keys: each name is a pad button held for two frames and let go for six ("select+start": both
// together); "waitN" is N
// frames without a press; "shot:<name>" a picture; "quit" ends the run. The race's controls
// are held from their token until another changes them: "gas=<0..1>" (the throttle, Cross's
// pressure), "brake=<0..1>" (Square's), "steer=<-1..1>" (the left stick, left negative),
// "reverse=<0..1>" (Triangle's), "handbrake=0|1" (Circle), "release" (all let go).
gt3r::PadState App::scripted_keys() {
    gt3r::PadState pad;
    while (key_ < keys_.size()) { // the tokens that take no frame
        const std::string& k = keys_[key_];
        const size_t eq = k.find('=');
        if (k == "quit") {
            quit_ = true, ++key_;
            return pad;
        }
        if (k == "release") {
            drive_ = {}, ++key_;
            continue;
        }
        if (eq == std::string::npos || k.compare(0, 5, "shot:") == 0) break;
        const std::string name = k.substr(0, eq);
        const float v = float(std::atof(k.c_str() + eq + 1));
        if (name == "gas" || name == "throttle") drive_.gas = std::clamp(v, 0.0f, 1.0f);
        else if (name == "brake") drive_.brake = std::clamp(v, 0.0f, 1.0f);
        else if (name == "steer") drive_.steer = std::clamp(v, -1.0f, 1.0f);
        else if (name == "reverse") drive_.reverse = std::clamp(v, 0.0f, 1.0f);
        else if (name == "handbrake") drive_.handbrake = v != 0;
        else if (name == "lx" || name == "ly" || name == "rx" || name == "ry") drive_.sticks[name[0] == 'l' ? (name[1] == 'x' ? 0 : 1) : (name[1] == 'x' ? 2 : 3)] = std::clamp(v, -1.0f, 1.0f);
        else {
            // A button held ("l1=1" .. "l1=0"): the photo mode's.
            std::string button = name;
            button[0] = char(std::toupper(static_cast<unsigned char>(button[0])));
            if (button.size() == 2) button[1] = char(std::toupper(static_cast<unsigned char>(button[1])));
            if (const auto b = gt3r::pad_button_from_name(button)) {
                const u16 bit = u16(1u << unsigned(*b));
                drive_.held = u16(v != 0 ? drive_.held | bit : drive_.held & ~bit);
            } else LOG_WARN("--keys: no control named %s", name.c_str());
        }
        ++key_, key_frames_ = 0;
    }
    if (key_ < keys_.size()) {
        const std::string& k = keys_[key_];
        const bool wait = k.compare(0, 4, "wait") == 0;
        if (pointer_token(k)) { // the mouse
            ++key_, key_frames_ = 0;
            return pad;
        }
        if (k.compare(0, 5, "drop:") == 0) { // a file dropped on the window
            SDL_Event e{};
            const std::string path = k.substr(5);
            ++key_, key_frames_ = 0;
            e.type = SDL_EVENT_DROP_FILE, e.drop.data = path.c_str();
            event(e);
            return pad;
        }
        if (k.compare(0, 5, "shot:") == 0) { // a picture of this frame, then the next key
            key_shot_ = k.substr(5), ++key_, key_frames_ = 0;
            return pad;
        }
        const int frames = wait ? std::max(1, std::atoi(k.c_str() + 4)) : 8;
        if (!wait && key_frames_ < 2) {
            for (std::string_view part : gt3r::split(k, '+')) { // "select+start": together
                std::string name(part);
                if (name.empty()) continue;
                name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
                if (name.size() == 2) name[1] = char(std::toupper(static_cast<unsigned char>(name[1]))); // "L1"
                if (const auto b = gt3r::pad_button_from_name(name)) {
                    pad.buttons |= u16(1u << unsigned(*b));
                    pad.pressure[size_t(*b)] = 255;
                } else if (key_frames_ == 0) {
                    LOG_WARN("--keys: no button named %s", name.c_str());
                }
            }
        }
        if (++key_frames_ >= frames) ++key_, key_frames_ = 0;
    }
    return pad;
}

SDL_AppResult App::iterate() {
    const Uint64 now = SDL_GetTicksNS();
    const double wall_ms = double(now - last_ticks_) / 1e6; // since the last frame
    double dt = std::min(wall_ms / 1e3, 0.1);
    last_ticks_ = now;
    idle_cursor(now);
    if (fps_last_) ++fps_frames_, fps_time_ += double(now - fps_last_) / 1e9; // (a picture's frame: with the wait in it)
    fps_last_ = now;
    if (fps_time_ >= 0.5) fps_shown_ = fps_frames_ / fps_time_, fps_frames_ = 0, fps_time_ = 0;
    if (!sweep_.empty()) dt = 1.0 / 60.0; // (a sweep's frames: a 60th of a second each, as fast as they come)
    if (!shot_path_.empty()) {
        // Pictures do not depend on the machine's speed: a frame is a 60th of a second, and the
        // frames are held to that pace so what runs by the clock (the menus' movies) keeps step.
        dt = 1.0 / 60.0;
        if (paced_ && wall_ms < 1000.0 / 60.0) {
            SDL_DelayPrecise(Uint64((1000.0 / 60.0 - wall_ms) * 1e6));
            last_ticks_ = SDL_GetTicksNS();
            if (const double slept = double(last_ticks_ - now) / 1e6; slept > 250) LOG_WARN("Frame %d: a wait of %.1f ms took %.0f ms", frame_, 1000.0 / 60.0 - wall_ms, slept);
        }
    }
    // A frame that came long after the last one ended: the time went outside the frame's work.
    if (end_ticks_ && double(now - end_ticks_) / 1e6 > 250) LOG_WARN("Frame %d: %.0f ms between frames", frame_, double(now - end_ticks_) / 1e6);

    std::string chosen, mod;
    SDL_LockMutex(dialog_mutex_);
    chosen.swap(dialog_result_);
    mod.swap(dropped_mod_);
    SDL_UnlockMutex(dialog_mutex_);
    if (!mod.empty() && disc_) push(make_settings_screen(*this, 4, mod)); // a .zip dropped: the Mods page installs it
    if (!chosen.empty()) {
        last_error_ = open_disc(chosen);
        if (last_error_.empty()) {
            screens_.clear();
            pending_.clear();
            push(make_hub_screen());
            if (config_.get("game", "start").value_or("game") != "hub") push(make_game_screen(*this)); // the game from power on
        }
    }

    apply_screen_changes();
    gt3r::PadState scripted = scripted_keys();
    {
        // The script's presses and held race controls reach the race's pad too (as an overlay on
        // player 1's: a pad plugged in still drives).
        gt3r::PadState ov = scripted;
        const auto hold = [&ov](PadButton b, float v) {
            if (v <= 0) return;
            ov.buttons |= u16(1u << unsigned(b));
            ov.pressure[size_t(b)] = std::max(ov.pressure[size_t(b)], u8(std::lround(v * 255)));
        };
        hold(PadButton::Cross, drive_.gas), hold(PadButton::Square, drive_.brake), hold(PadButton::Triangle, drive_.reverse);
        hold(PadButton::Circle, drive_.handbrake ? 1.0f : 0.0f);
        ov.set_axis(gt3r::PadAxis::LX, drive_.sticks[0] != 0 ? drive_.sticks[0] : drive_.steer);
        ov.set_axis(gt3r::PadAxis::LY, drive_.sticks[1]), ov.set_axis(gt3r::PadAxis::RX, drive_.sticks[2]), ov.set_axis(gt3r::PadAxis::RY, drive_.sticks[3]);
        for (unsigned b = 0; b < 16; ++b)
            if (drive_.held & (1u << b)) ov.buttons |= u16(1u << b), ov.pressure[b] = 255;
        ov.connected = ov.buttons != 0 || drive_.steer != 0 || drive_.sticks[0] != 0 || drive_.sticks[1] != 0 || drive_.sticks[2] != 0 || drive_.sticks[3] != 0;
        input_->set_overlay(0, ov);
        menu_pad_ = ov;
        for (unsigned b = 0; b < 16; ++b)
            if (drive_.held & (1u << b)) scripted.buttons |= u16(1u << b), scripted.pressure[b] = 255;
    }
    input_->update(dt);
    pointer_actions_ = pointer_.take();
    pointer_actions_.insert(pointer_actions_.end(), scripted_pointer_.begin(), scripted_pointer_.end());
    scripted_pointer_.clear();
    if (!menu_pad_.connected) menu_pad_ = input_->menu_pad();
    const bool sticks = drive_.sticks[0] != 0 || drive_.sticks[1] != 0;
    if (sticks) scripted.set_axis(gt3r::PadAxis::LX, drive_.sticks[0]), scripted.set_axis(gt3r::PadAxis::LY, drive_.sticks[1]);
    {
        Screen* top = screens_.empty() ? nullptr : screens_.back().get();
        gt3r::PadState pad = scripted.buttons || sticks ? scripted : top && top->menu_input_any_device() ? input_->any_device_pad() : input_->menu_pad();
        // The mouse's right button goes back on the app's own pages.
        if (top && !top->follows_aspect())
            for (const gt3r::PointerInput::Action& a : pointer_actions_)
                if (a.kind == gt3r::PointerInput::Action::Kind::Back) pad.buttons |= u16(1u << unsigned(PadButton::Triangle)), pad.connected = true;
        menu_.update(pad, dt);
    }
    if (!screens_.empty()) screens_.back()->update(*this, dt);
    apply_screen_changes();
    if (screens_.empty()) quit_ = true;

    const bool game = !screens_.empty() && screens_.back()->follows_aspect();
    layout_ui(game);
    Ui::set_prompts(gt3r::button_prompts(*input_, false, false)); // "{Cross}" in the hints
    begin_gamma_frame();
    ui_->clear();
    if (!screens_.empty()) screens_.back()->render(*this, *ui_);
    draw_fps(!game);
    // (GT4R_SHOT_GAMMA=1: the pictures as the window shows them, for checks)
    static const bool shot_gamma = SDL_getenv("GT4R_SHOT_GAMMA") != nullptr;
    if (gamma_frame_ && shot_gamma) gamma_pass_->end(video_.gamma), gamma_frame_ = false;
    ++frame_;
    if (!key_shot_.empty() && !shot_path_.empty()) {
        const size_t slash = shot_path_.find_last_of('/');
        save_screenshot((slash == std::string::npos ? std::string() : shot_path_.substr(0, slash + 1)) + key_shot_ + ".png");
        key_shot_.clear();
    }
    for (const std::string& p : pictures_) save_screenshot(p);
    pictures_.clear();
    if (!stats_path_.empty()) write_stats(wall_ms);
    if (!shot_path_.empty() && frame_ >= shot_frame_) {
        save_screenshot(shot_path_);
        quit_ = true;
    }
    if (screenshot_wanted_) { // F12
        screenshot_wanted_ = false;
        const std::string dir = data_dir_ + "screenshots";
        SDL_CreateDirectory(dir.c_str());
        char stamp[40];
        const std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", std::localtime(&t));
        std::string path = dir + "/GT4R " + stamp + ".png";
        for (int n = 2; gt3r::host_path_exists(path); ++n) path = dir + "/GT4R " + stamp + " (" + std::to_string(n) + ").png";
        save_screenshot(path);
    }
    if (gamma_frame_) gamma_pass_->end(video_.gamma); // (after the pictures: they keep the game's own colors)
    const Uint64 before_present = SDL_GetTicksNS();
    SDL_RenderPresent(renderer_);
    if (gpu_) gpu_->frame_presented();
    // A graphics driver that gave up never comes back, and the frames that never finish fill the memory: closed with a message.
    if (gpu_ && std::strstr(SDL_GetError(), "DEVICE_LOST")) {
        LOG_ERROR("The graphics device was lost: %s", SDL_GetError());
        if (!scripted_) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GT4 Revisited", "The graphics driver stopped responding, so the game has to close.", window_);
        return SDL_APP_FAILURE;
    }
    if (dynamic_on_)
        if (const float s = dynamic_.frame(wall_ms, double(SDL_GetTicksNS() - before_present) / 1e6); s != gpu_->render_scale()) {
            LOG_INFO("Dynamic resolution: %d %%", int(std::lround(s * 100)));
            gpu_->set_render_scale(s);
        }
    end_ticks_ = SDL_GetTicksNS();
    if (double(end_ticks_ - before_present) / 1e6 > 250) LOG_WARN("Frame %d: presenting took %.0f ms", frame_, double(end_ticks_ - before_present) / 1e6);
    if (double(before_present - now) / 1e6 > 250) LOG_WARN("Frame %d: its work took %.0f ms", frame_, double(before_present - now) / 1e6);
    return quit_ ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult App::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    input_->handle_event(e);
    // Back in front: the controllers' motors as they should be now (a stop sent while away can be lost).
    if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) input_->window_focus_gained();
    if (audio_ && (e.type == SDL_EVENT_AUDIO_DEVICE_ADDED || e.type == SDL_EVENT_AUDIO_DEVICE_REMOVED)) audio_->device_event(e);
    mouse_event(e);
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && (screens_.empty() || !screens_.back()->takes_dropped_file(e.drop.data))) {
        const std::string path = e.drop.data;
        const bool zip = path.size() > 4 && gt3r::to_lower(path.substr(path.size() - 4)) == ".zip";
        if (!zip) request_disc(path);
        // (a mod for the open disc; over a game or a race being played only once that is left)
        else if (disc_ && !screens_.empty() && !screens_.back()->follows_aspect()) push_dropped_mod(path);
    }
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F12 && !e.key.repeat) screenshot_wanted_ = true;
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11 && !e.key.repeat) {
        settings::Video v = video_;
        v.fullscreen = !fullscreen();
        video_.fullscreen = fullscreen();
        set_video(v);
    }
    // Full screen entered or left another way (macOS's green button, the window manager): kept too.
    if ((e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) && !scripted_) {
        if (const bool on = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN; video_.fullscreen != on) {
            settings::Video v = video_;
            v.fullscreen = on;
            set_video(v);
        }
    }
    if (e.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) apply_frame_limit(); // (another display's rate)
    SDL_Event local = e; // the pointer in UI units
    SDL_ConvertEventToRenderCoordinates(renderer_, &local);
    pointer_.event(local);
    if (!screens_.empty()) screens_.back()->on_event(*this, local);
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

// --stats: a line a second (60 frames): the frame, the seconds since launch, the frames' mean
// and longest wall time in ms, the process's resident memory in MB.
void App::write_stats(double frame_ms) {
    if (frame_ > 1) stats_ms_ += frame_ms, stats_max_ = std::max(stats_max_, frame_ms), ++stats_frames_;
    if (frame_ % 60 != 0 || !stats_frames_) return;
    if (std::FILE* f = std::fopen(stats_path_.c_str(), "a")) {
        std::fprintf(f, "%d %.1f %.2f %.2f %.1f\n", frame_, double(SDL_GetTicksNS() - start_ticks_) / 1e9, stats_ms_ / stats_frames_, stats_max_,
                     double(resident_bytes()) / (1024.0 * 1024.0));
        std::fclose(f);
    }
    stats_ms_ = stats_max_ = 0, stats_frames_ = 0;
}

void App::shutdown() {
    screens_.clear(); // (screens stop their streams before the audio and the disc go)
    pending_.clear();
    sound_.reset();
    audio_.reset();
    db_.reset();
    gt3r::texture_pack::set_pack(nullptr);
    vfs_.reset();
    disc_.reset();
    gamma_pass_.reset();
    ui_.reset();
    input_.reset();
    if (dialog_mutex_) SDL_DestroyMutex(dialog_mutex_);
    if (gpu_) gpu_.reset(); // (its renderer goes with it)
    else if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    gt3r::log_file::close();
    SDL_Quit();
}

} // namespace gt4r
