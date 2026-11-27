#include "gt4r/app/app.h"
#include "gt4r/app/app_identity.h"
#include "gt4r/app/online.h"
#include "gt4r/app/presence.h"
#include "gt4r/app/updates.h"

#include "gt4r/input/player_controls.h"
#include "gt4r/menu/program_tables.h"

#include "app/button_prompts.h"
#include "base/file.h"
#include "base/log.h"
#include "base/log_file.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"
#include "game/gt_save.h"
#include "platform/crash_handlers.h"
#include "platform/mac_layer.h"
#include "platform/render_scale.h"
#include "vfs/mod_archive.h"
#include "gt3r_version.h"
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
#if defined(_WIN32)
        if (dir.back() != '/' && dir.back() != '\\') dir += '\\'; // (the system's own separator, as SDL_GetPrefPath ends)
#else
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
#endif
        return dir;
    }
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    if (const std::string beside = gt3r::game::app_bundle_folder(base); !beside.empty() && gt3r::host_path_exists(beside + "portable.ini")) {
        dir = beside;
    } else if (!base.empty() && gt3r::host_path_exists(base + "portable.ini")) {
        dir = base;
    } else if (char* pref = SDL_GetPrefPath("GT4Revisited", "GT4Revisited")) {
        dir = pref;
        SDL_free(pref);
    }
    return dir;
}

bool App::init(int argc, char** argv) {
    SDL_SetAppMetadata(kAppName, GT4R_VERSION, kAppId);
    name_the_engine();
    // The log file first, so that it has everything after it: gt4r.log in the data folder (app_identity.h), the session before
    // in gt4r.prev.log, and why the game closed when it did not close by itself.
    {
        const std::string dir = user_data_dir();
        SDL_CreateDirectory(dir.c_str());
        gt3r::log_file::open(dir);
    }
    if (const char* c = SDL_getenv("GT4R_CRASH_TEST")) gt3r::platform::crash_test(c); // throw | segv | abort
    const bool mobile = gt3r::platform::is_mobile();
    // Phones and tablets have no command line: <user data>/args.txt holds one argument a line (for tests), and "--env NAME=value"
    // there sets a variable the run reads.
    std::vector<std::string> arg_list(argv, argv + argc);
    std::vector<char*> arg_ptrs;
    if (mobile)
        if (const std::optional<Bytes> extra = gt3r::read_host_file(user_data_dir() + "args.txt")) {
            for (std::string_view line : gt3r::split(std::string_view(reinterpret_cast<const char*>(extra->data()), extra->size()), '\n'))
                if (const std::string_view a = gt3r::trim(line); !a.empty()) arg_list.emplace_back(a);
            for (size_t i = 1; i + 1 < arg_list.size(); ++i) {
                if (arg_list[i] != "--env") continue;
                const std::string& kv = arg_list[++i];
                if (const size_t eq = kv.find('='); eq != std::string::npos && eq > 0) {
                    const std::string name = kv.substr(0, eq), value = kv.substr(eq + 1);
                    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name.c_str(), value.c_str(), true);
#if !defined(_WIN32)
                    setenv(name.c_str(), value.c_str(), 1); // (what reads std::getenv)
#endif
                }
            }
            LOG_INFO("args.txt: %zu arguments", arg_list.size() - size_t(argc));
        }
    for (std::string& a : arg_list) arg_ptrs.push_back(a.data());
    argc = int(arg_ptrs.size()), argv = arg_ptrs.data();
#ifdef SDL_PLATFORM_WINDOWS
    // Xbox pads through SDL's RAWINPUT driver, for their impulse triggers, unless [input] xbox_driver = xinput: SDL takes it
    // as it starts its joysticks.
    {
        const std::optional<gt3r::IniDocument> cfg = gt3r::IniDocument::load(user_data_dir() + "config.ini");
        SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, gt3r::InputManager::xbox_rawinput(cfg ? *cfg : gt3r::IniDocument{}) ? "1" : "0");
    }
#endif
    run_ = settings::RunMode::of(std::vector<std::string>(argv + std::min(argc, 1), argv + argc), [](const char* name) { return SDL_getenv(name); });
    if (mobile) run_.windowless = false, run_.pace = 0; // (a phone's window is its screen, always shown and paced by it)
#if GT3R_XR
    // A headset's controllers' pad reaches the game without the window's focus, which the headset has (the Quest's window is
    // never shown); and text is typed on the game's own keyboard. (A scripted run on a computer keeps its window hidden.)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
#endif
    if (run_.windowless) {
        // Nothing of the run reaches whoever is at the machine: no Dock icon and no activation, no sound (unless the environment
        // names a driver: what is set there comes first), the display may sleep.
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        LOG_INFO("No window, no sound, no real input (--visible shows a scripted run)");
    }
#ifdef SDL_PLATFORM_ANDROID
    // Logitech wheels' force feedback (G29 in PS3 mode, G27, G25, Driving Force GT): Android plays none for them, so SDL's Logitech
    // driver talks to the wheel over USB itself (the player is asked to allow it; refused, the wheel steers without it).
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_LG4FF, "1");
#endif
    // (a scripted run's controllers are its own: none is opened)
    // (phones and tablets: the accelerometer, for tilt steering)
    const SDL_InitFlags input_flags = run_.scripted ? SDL_InitFlags(0) : SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | (mobile ? SDL_INIT_SENSOR : SDL_InitFlags(0));
    // (a recorded run opens no sound device: its sound is mixed a frame at a time into the recording, video_recorder.h)
    if (!SDL_Init(SDL_INIT_VIDEO | (run_.recording ? SDL_InitFlags(0) : SDL_INIT_AUDIO) | input_flags)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    scripted_ = run_.scripted;
    if (scripted_) SDL_SetEventFilter([](void*, SDL_Event* e) { return !settings::is_real_input(e->type); }, nullptr);
    data_dir_ = user_data_dir();
    SDL_CreateDirectory(data_dir_.c_str());
    SDL_CreateDirectory(mods_dir().c_str());
    config_path_ = data_dir_ + "config.ini";
    if (auto cfg = gt3r::IniDocument::load(config_path_)) config_ = std::move(*cfg);
    LOG_INFO("%s %s (engine %s); user data: %s", kAppName, GT4R_VERSION, GT3R_VERSION, data_dir_.c_str());
    load_settings();
    if (mobile) video_.fullscreen = true; // (phones and tablets: the whole screen, without the system's bars)

    // A scripted run does not take the keyboard from whoever is at the machine: its window is never shown, or (--visible) opens
    // without becoming the active one and cannot be focused.
    if (scripted_) SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    // (A picture's window in another shape, for checks: GT4R_WINDOW=1600x700.)
    int window_w = 1280, window_h = 720;
    if (const char* size = SDL_getenv("GT4R_WINDOW"); size && std::sscanf(size, "%dx%d", &window_w, &window_h) != 2) window_w = 1280, window_h = 720;
    // macOS: in full screen the menu bar comes down at the screen's top as in other apps.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight"); // phones and tablets: landscape, either way up
    window_ = SDL_CreateWindow(kAppName, std::clamp(window_w, 320, 8192), std::clamp(window_h, 180, 8192),
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                   (mobile             ? SDL_WINDOW_FULLSCREEN
                                    : run_.windowless  ? SDL_WINDOW_HIDDEN | SDL_WINDOW_NOT_FOCUSABLE
                                    : scripted_        ? SDL_WINDOW_NOT_FOCUSABLE
                                    : video_.fullscreen ? SDL_WINDOW_FULLSCREEN
                                                        : SDL_WindowFlags(0)));
    if (!window_) {
        LOG_ERROR("Window creation failed: %s", SDL_GetError());
        return false;
    }
    gt3r::render::GpuOptions gpu;
    gpu.msaa = video_.msaa, gpu.anisotropy = video_.anisotropy, gpu.vsync = video_.vsync && !run_.windowless;
    gpu.gles = video_.gpu == settings::Gpu::Gles || video_.gpu == settings::Gpu::GlesDriver;
    // The Vulkan pipelines built in one run, kept for the next (the Android build's SDL, android/fetch-sdl.sh; other builds' SDL
    // ignores it).
    gpu.pipeline_cache = user_data_dir() + "vulkan_pipelines.cache";
    gpu.angle = video_.gpu != settings::Gpu::GlesDriver;
    bool use_gpu = video_.gpu != settings::Gpu::Off;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-gpu") use_gpu = false;
#if GT3R_XR
    if (use_gpu) xr_start(gpu, argc, argv); // (the device for a headset when there is one: app_xr.cpp)
#else
    if (use_gpu) gpu_ = gt3r::render::GpuContext::create(window_, gpu);
#endif
    if (gpu_) {
        renderer_ = gpu_->renderer2d();
        gpu_->warm_pipelines();
        gt4_pipelines_.start(*gpu_);
        if (!run_.windowless) gt3r::platform::keep_composited(window_); // (macOS: no switching between the display's two ways of showing a full screen)
    } else {
        if (use_gpu) LOG_WARN("3D drawing is not available; continuing with the 2D screens only");
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            LOG_ERROR("Renderer creation failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer_, gpu.vsync ? 1 : 0);
    }
    ui_ = std::make_unique<Ui>(renderer_);
    layout_ui(false);
    reset_dynamic_scale();
    apply_frame_limit();

    input::register_extra_controls(); // (Nitrous: its default goes into the layouts the bindings are made from)
    input_ = std::make_unique<gt3r::InputManager>();
    {
        gt3r::IniDocument controls = config_; // (our words for the original's layouts as the engine has them)
        input::translate_config(controls);
        input_->load(controls);
    }
    // The on-screen controls: phones and tablets (GT4R_TOUCH=1: on a computer too, shown, for pictures).
    if (const char* t = SDL_getenv("GT4R_TOUCH"); mobile || (t && *t == '1')) {
        touch_ui_ = true;
        touch_.load(config_);
        place_touch_top(false);
        if (!mobile) touch_.controls().note_touch();
    }
#if GT3R_XR
    if (xr_) touch_ui_ = false; // (the controllers, never the on-screen ones)
#endif
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
        else if (a == "--record" && has_value) recorder_ = std::make_unique<VideoRecorder>(argv[++i]);
        else if (a == "--record-frames" && has_value) record_frames_ = std::atoi(argv[++i]);
        else if (a == "--stats" && has_value) stats_path_ = argv[++i];
        else if (a == "--fast") paced_ = false;
        else if (a == "--sweep" && has_value) sweep_ = argv[++i];
        else if (a == "--sweep-out" && has_value) sweep_out_ = argv[++i];
        else if (a == "--pace" && has_value) ++i; // (run_)
        else if (a == "--env" && has_value) ++i;  // (args.txt's, taken above)
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
    if (scripted_) keep_pace_in_background();
    if (recorder_) apply_sound(); // (no music in a recording)
    if (disc_path.empty()) disc_path = config_.get("game", "disc").value_or("");
    if (!disc_path.empty()) last_error_ = open_disc(disc_path);
    if (disc_) {
        push(make_hub_screen());
        if (!sweep_.empty()) push(make_sweep_screen(sweep_, sweep_out_.empty() ? std::string("sweep") : sweep_out_));
        else if (!browse_.empty()) push(make_browser_screen(browse_));
        else if (start_ == "menus") push(make_menu_viewer(*this));
        else if (start_ == "race") push(make_race_screen(*this));
        else if (start_ == "online") push(make_online_script_screen(*this));
        else if (start_ == "controls") push(make_controls_screen(*this));
        else if (start_ == "saveeditor") push(make_save_editor(*this));
        else if (start_ == "database") push(make_database_screen(*this));
        else if (start_ == "settings") push(make_settings_screen(*this));
        else if (start_ == "quickrace") push(make_quick_race_screen(*this));
        else if (start_ == "cars" || start_ == "courses") { // (Developer tools' first items: Cars, Courses)
            push(make_hub_screen(true));
            const std::vector<std::string> to = start_ == "cars" ? std::vector<std::string>{"cross"} : std::vector<std::string>{"down", "cross"};
            keys_.insert(keys_.begin(), to.begin(), to.end());
        }
        // A launch with a disc and nothing else asked for: the game from power on, the home
        // screen under it ([game] start = hub: the home screen).
        else if (start_ == "game" || (start_.empty() && config_.get("game", "start").value_or("game") != "hub")) push(make_game_screen(*this));
    } else push(make_setup_screen());
    // The update check and the Discord status: scripted runs reach neither unless asked (GT4R_UPDATE_CHECK=1, GT4R_DISCORD=1).
    const char* uc = SDL_getenv("GT4R_UPDATE_CHECK");
    const char* dc = SDL_getenv("GT4R_DISCORD");
    updates().startup(*this, !scripted_ || (uc && *uc == '1'));
    discord_status().startup(*this, !scripted_ || (dc && *dc == '1'));
    last_ticks_ = start_ticks_ = SDL_GetTicksNS();
    return true;
}

bool App::screen_presence(net::PresenceText& t) {
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it)
        if ((*it)->presence(*this, t)) return true;
    return false;
}

std::vector<std::string> App::parse_key_script(std::string_view text) {
    std::vector<std::string> keys;
    for (std::string_view line : gt3r::split(text, '\n')) {
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        for (std::string_view k : gt3r::split(line, ','))
            if (const std::string_view t = gt3r::trim(k); !t.empty()) keys.emplace_back(t[0] == '@' || t.substr(0, 5) == "drop:" || t.substr(0, 5) == "type:" || t.substr(0, 9) == "typefile:" ? std::string(t) : gt3r::to_lower(t)); // (a file's name as written)
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
    forget_disc_music();
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
            if (disc_ && disc_->info().game == Game::GT4) try { // (a mod car's maker by its name: the program's makers)
                    const std::shared_ptr<const gt3r::CoreImage> image = disc_->program_image();
                    const menu::ProgramTables names = menu::ProgramTables::find(image->program, image->load_address);
                    db_->set_names(names.makers, names.tuners, names.countries); // (the mods' makers numbered after them)
                } catch (const std::exception& e) {
                    LOG_WARN("Car makers: %s", e.what());
                }
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
// "reverse=<0..1>" (Triangle's), "handbrake=0|1" (Circle), "release" (all let go). The second
// player's: the same names with a 2 ("gas2=1"), and "p2:<button>" pressed on his pad.
gt3r::PadState App::scripted_keys() {
    gt3r::PadState pad;
    while (key_ < keys_.size()) { // the tokens that take no frame
        const std::string& k = keys_[key_];
        const size_t eq = k.find('=');
        if (k == "quit") {
            quit_ = true, ++key_;
            return pad;
        }
        if (k == "f12") { // a screenshot, as the key takes one
            screenshot_wanted_ = true, ++key_;
            continue;
        }
        if (k == "enter") { // the keyboard's Enter key, down and up (a text field's "done")
            ++key_;
            for (const SDL_EventType type : {SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP}) {
                SDL_Event e{};
                e.type = type, e.key.key = SDLK_RETURN, e.key.scancode = SDL_SCANCODE_RETURN, e.key.down = type == SDL_EVENT_KEY_DOWN;
                event(e);
            }
            continue;
        }
        if (k == "release") {
            drive_ = {}, drive2_ = {}, ++key_;
            continue;
        }
        if (eq == std::string::npos || k.compare(0, 5, "shot:") == 0) break;
        std::string name = k.substr(0, eq);
        // ("gas2=", "steer2=" ..: the second pad's; "l2=" and "r2=" are buttons.)
        const bool second = name.size() > 2 && name.back() == '2';
        if (second) name.pop_back();
        ScriptedDrive& drive = second ? drive2_ : drive_;
        const float v = float(std::atof(k.c_str() + eq + 1));
        if (name == "gas" || name == "throttle") drive.gas = std::clamp(v, 0.0f, 1.0f);
        else if (name == "brake") drive.brake = std::clamp(v, 0.0f, 1.0f);
        else if (name == "steer") drive.steer = std::clamp(v, -1.0f, 1.0f);
        else if (name == "reverse") drive.reverse = std::clamp(v, 0.0f, 1.0f);
        else if (name == "handbrake") drive.handbrake = v != 0;
        else if (name == "lx" || name == "ly" || name == "rx" || name == "ry") drive.sticks[name[0] == 'l' ? (name[1] == 'x' ? 0 : 1) : (name[1] == 'x' ? 2 : 3)] = std::clamp(v, -1.0f, 1.0f);
        else {
            // A button held ("l1=1" .. "l1=0"): the photo mode's.
            std::string button = name;
            button[0] = char(std::toupper(static_cast<unsigned char>(button[0])));
            if (button.size() == 2) button[1] = char(std::toupper(static_cast<unsigned char>(button[1])));
            if (const auto b = gt3r::pad_button_from_name(button)) {
                const u16 bit = u16(1u << unsigned(*b));
                drive.held = u16(v != 0 ? drive.held | bit : drive.held & ~bit);
            } else LOG_WARN("--keys: no control named %s", name.c_str());
        }
        ++key_, key_frames_ = 0;
    }
    press2_ = {};
    if (key_ < keys_.size()) {
        const std::string& whole = keys_[key_];
        const bool second = whole.compare(0, 3, "p2:") == 0;
        const std::string k = second ? whole.substr(3) : whole;
        gt3r::PadState& to = second ? press2_ : pad;
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
        if (k.compare(0, 5, "type:") == 0 || k.compare(0, 9, "typefile:") == 0) { // text typed on a keyboard (typefile: a file's, read then)
            SDL_Event e{};
            std::string text = k.substr(5);
            if (k[4] == 'f') {
                const std::optional<Bytes> file = gt3r::read_host_file(k.substr(9));
                text = file ? std::string(gt3r::trim(std::string_view(reinterpret_cast<const char*>(file->data()), file->size()))) : std::string();
                if (!file) LOG_WARN("--keys: could not read %s", k.c_str() + 9);
            }
            for (size_t at; (at = text.find("\\n")) != std::string::npos;) text.replace(at, 2, "\n"); // ("\n": Enter)
            ++key_, key_frames_ = 0;
            e.type = SDL_EVENT_TEXT_INPUT, e.text.text = text.c_str();
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
                    to.buttons |= u16(1u << unsigned(*b));
                    to.pressure[size_t(*b)] = 255;
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
#if GT3R_XR
    if (const std::optional<SDL_AppResult> r = xr_begin_frame()) return *r; // (the headset's frame: app_xr.cpp)
#endif
    const Uint64 now = SDL_GetTicksNS();
    const double wall_ms = double(now - last_ticks_) / 1e6; // since the last frame (since its wait, where the frames are held to a pace)
    double dt = std::min(wall_ms / 1e3, 0.1);
    last_ticks_ = now;
    if (run_.pace != 0) { // no display holds the frames: this does
        const double rate = run_.pace > 0 ? run_.pace : refresh_rate() > 0 ? refresh_rate() : settings::RunMode::kPace;
        if (wall_ms < 1000.0 / rate) SDL_DelayPrecise(Uint64((1000.0 / rate - wall_ms) * 1e6)), last_ticks_ = SDL_GetTicksNS();
        dt = std::min(wall_ms / 1e3 + double(last_ticks_ - now) / 1e9, 0.1);
    }
#if GT3R_XR
    if (xr_dt_ > 0) dt = std::min(xr_dt_, 0.1);
#endif
    idle_cursor(now);
    if (fps_last_) ++fps_frames_, fps_time_ += double(now - fps_last_) / 1e9; // (a picture's frame: with the wait in it)
    fps_last_ = now;
    if (fps_time_ >= 0.5) fps_shown_ = fps_frames_ / fps_time_, fps_frames_ = 0, fps_time_ = 0;
    if (!sweep_.empty()) dt = 1.0 / 60.0; // (a sweep's frames: a 60th of a second each, as fast as they come)
    if (!shot_path_.empty() || recorder_) {
        // Pictures do not depend on the machine's speed: a frame is a 60th of a second, and the
        // frames are held to that pace so what runs by the clock (the menus' movies) keeps step.
        // (A recording held to the clock keeps its own schedule: video_recorder.h.)
        dt = 1.0 / 60.0;
        if (paced_ && !(recorder_ && recorder_->realtime()) && wall_ms < 1000.0 / 60.0) {
            SDL_DelayPrecise(Uint64((1000.0 / 60.0 - wall_ms) * 1e6));
            last_ticks_ = SDL_GetTicksNS();
            if (const double slept = double(last_ticks_ - now) / 1e6; slept > 250) LOG_WARN("Frame %d: a wait of %.1f ms took %.0f ms", frame_, 1000.0 / 60.0 - wall_ms, slept);
        }
    }
    frame_time_ns_ += Uint64(dt * 1e9);
    // A frame that came long after the last one ended: the time went outside the frame's work.
    if (end_ticks_ && double(now - end_ticks_) / 1e6 > 250) LOG_WARN("Frame %d: %.0f ms between frames", frame_, double(now - end_ticks_) / 1e6);

    std::string chosen, mod;
    SDL_LockMutex(dialog_mutex_);
    chosen.swap(dialog_result_);
    mod.swap(dropped_mod_);
    SDL_UnlockMutex(dialog_mutex_);
    if (!mod.empty() && disc_) push(make_settings_screen(*this, kModsPage, mod)); // a .zip dropped: the Mods page installs it
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
    updates().poll(*this);
    music_.poll();
    // (the song a run ended on, for the next run's [music] next_race = same, and the next start's)
    if (const std::string last = music_.last_song().text(); !last.empty() && last != config_.get("music", "race_song").value_or("")) {
        config_.set("music", "race_song", last);
        save_config();
    }
    discord_status().frame(*this);
    updates().frame(*this, !screens_.empty() && screens_.back()->update_prompts());
    const bool modal = updates().modal(); // the update dialog takes the menu input
    if (touch_ui_) { // (player 1's pad from the on-screen controls, under what a script presses)
        const Screen* top = screens_.empty() ? nullptr : screens_.back().get();
        if (const bool row = top && !modal && top->touch_top_row(); row != touch_top_row_) place_touch_top(row);
        touch_.update(dt, modal || !top ? gt3r::TouchLayout::None : top->touch_layout(), false, *input_);
    }
    gt3r::PadState scripted = scripted_keys();
    if (back_pressed_) { // (Android's back button, this frame only)
        back_pressed_ = false;
        const Screen* top = screens_.empty() ? nullptr : screens_.back().get();
        const PadButton b = !modal && top && top->back_is_start() ? PadButton::Start : PadButton::Triangle;
        scripted.buttons |= u16(1u << unsigned(b)), scripted.pressure[size_t(b)] = 255;
    }
    {
        // The script's presses and held race controls reach the race's pad too (as an overlay on
        // player 1's: a pad plugged in still drives).
        for (int player = 0; player < 2; ++player) {
            gt3r::PadState ov = player ? press2_ : scripted;
            const ScriptedDrive& drive = player ? drive2_ : drive_;
            const auto hold = [&ov](PadButton b, float v) {
                if (v <= 0) return;
                ov.buttons |= u16(1u << unsigned(b));
                ov.pressure[size_t(b)] = std::max(ov.pressure[size_t(b)], u8(std::lround(v * 255)));
            };
            hold(PadButton::Cross, drive.gas), hold(PadButton::Square, drive.brake), hold(PadButton::Triangle, drive.reverse);
            hold(PadButton::Circle, drive.handbrake ? 1.0f : 0.0f);
            ov.set_axis(gt3r::PadAxis::LX, drive.sticks[0] != 0 ? drive.sticks[0] : drive.steer);
            ov.set_axis(gt3r::PadAxis::LY, drive.sticks[1]), ov.set_axis(gt3r::PadAxis::RX, drive.sticks[2]), ov.set_axis(gt3r::PadAxis::RY, drive.sticks[3]);
            for (unsigned b = 0; b < 16; ++b)
                if (drive.held & (1u << b)) ov.buttons |= u16(1u << b), ov.pressure[b] = 255;
            ov.connected = ov.buttons != 0 || drive.steer != 0 || drive.sticks[0] != 0 || drive.sticks[1] != 0 || drive.sticks[2] != 0 || drive.sticks[3] != 0;
            if (player == 0) menu_pad_ = ov;
            if (player == 0 && !ov.connected && touch_ui_) ov = touch_.controls().pad(); // (the on-screen controls when no script plays)
            input_->set_overlay(player, ov);
        }
        for (unsigned b = 0; b < 16; ++b)
            if (drive_.held & (1u << b)) scripted.buttons |= u16(1u << b), scripted.pressure[b] = 255;
    }
#if GT3R_XR
    if (xr_) xr_input(dt);
#endif
    input_->update(dt);
    pointer_actions_ = pointer_.take();
    pointer_actions_.insert(pointer_actions_.end(), scripted_pointer_.begin(), scripted_pointer_.end());
    scripted_pointer_.clear();
    if (!menu_pad_.connected) menu_pad_ = input_->menu_pad();
    const bool sticks = drive_.sticks[0] != 0 || drive_.sticks[1] != 0;
    {
        Screen* top = screens_.empty() ? nullptr : screens_.back().get();
        // The script's presses on top of the menu pad, not in its place: a control held by the script (gas=1) stays
        // down under a key pressed meanwhile, where dropping it for a frame made a press of its own afterwards.
        gt3r::PadState pad = top && top->menu_input_any_device() ? input_->any_device_pad() : input_->menu_pad();
        pad.buttons |= scripted.buttons;
        for (size_t b = 0; b < pad.pressure.size(); ++b) pad.pressure[b] = std::max(pad.pressure[b], scripted.pressure[b]);
        if (sticks) pad.set_axis(gt3r::PadAxis::LX, drive_.sticks[0]), pad.set_axis(gt3r::PadAxis::LY, drive_.sticks[1]);
        pad.connected = pad.connected || scripted.buttons != 0 || sticks;
        // The mouse's right button goes back on the app's own pages, as does a click or a tap on their "< Back" (the header's
        // height, a little past its sides: a finger's reach).
        if (top && !top->follows_aspect())
            for (const gt3r::PointerInput::Action& a : pointer_actions_) {
                const gt3r::UiRect& b = back_button_;
                const bool on_back = a.kind == gt3r::PointerInput::Action::Kind::Click && b.w > 0 && a.x >= b.x - 4 && a.x < b.right() + 4 && a.y >= 0 && a.y < 22;
                if (a.kind == gt3r::PointerInput::Action::Kind::Back || on_back) pad.buttons |= u16(1u << unsigned(PadButton::Triangle)), pad.connected = true;
            }
        menu_.update(pad, dt);
    }
    if (modal) updates().dialog_update(*this);
    else if (!screens_.empty()) screens_.back()->update(*this, dt);
    apply_screen_changes();
    if (screens_.empty()) quit_ = true;

    // Phones: the display's cutout and corners (asked again until the window has them, and when the safe area changes).
    if (gt3r::platform::is_mobile() && !display_insets_ && --insets_retry_ <= 0) {
        display_insets_ = gt3r::platform::query_display_insets();
        insets_retry_ = 30;
        if (display_insets_)
            LOG_INFO("Display cutout %d,%d,%d,%d px, corner radius %d px", display_insets_->left, display_insets_->top, display_insets_->right,
                     display_insets_->bottom, display_insets_->corner_radius);
    }
    const bool game = !screens_.empty() && screens_.back()->follows_aspect();
#if GT3R_XR
    if (xr_) SDL_SetRenderTarget(renderer_, xr_->canvas()); // (the frame goes into the headset's canvas)
#endif
    layout_ui(game, game && screens_.back()->view_fills_narrow());
    Ui::set_prompts(gt3r::button_prompts(*input_, touch_ui_ && touch_.controls().visible(), touch_ui_)); // "{Cross}" in the hints
    begin_gamma_frame();
    ui_->clear();
    back_button_ = {}; // (the page draws it again: page_header)
    if (!screens_.empty()) screens_.back()->render(*this, *ui_);
    updates().dialog_render(*ui_); // (centered on whatever width the screen below laid out)
    if (touch_ui_) {
        touch_.set_safe_area(safe_area_window());
        touch_.render(*ui_);
    }
    draw_fps(!game);
#if GT3R_XR
    xr_draw_pointer();
#endif
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
    if (recorder_) {
        recorder_->frame(renderer_, audio_ ? &audio_->voices() : nullptr);
        if (recorder_->failed() || (record_frames_ > 0 && recorder_->frames() >= record_frames_)) quit_ = true;
    }
    if (!stats_path_.empty()) write_stats(std::max(0.0, wall_ms - gpu_wait_ms_));
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
    if (!run_.windowless) SDL_RenderPresent(renderer_);
    else {
        // Nothing is presented: the display would hold the frames back (or, asleep, not give a picture to draw into). Reading a
        // pixel back instead has what the frame drew drawn and waits for it, so frames do not pile up. (The middle's: inside the
        // game's picture whatever its shape; a viewport without it has all of itself read.)
        int pw = 0, ph = 0;
        SDL_GetRenderOutputSize(renderer_, &pw, &ph);
        const SDL_Rect pixel{pw / 2, ph / 2, 1, 1};
        SDL_Surface* read = SDL_RenderReadPixels(renderer_, &pixel);
        SDL_DestroySurface(read ? read : SDL_RenderReadPixels(renderer_, nullptr));
        gpu_wait_ms_ = double(SDL_GetTicksNS() - before_present) / 1e6;
    }
    if (gpu_) gpu_->frame_presented();
#if GT3R_XR
    if (xr_) xr_->end_frame(); // (the canvases to the headset)
#endif
    // A graphics driver that gave up never comes back, and the frames that never finish fill the memory: closed with a message.
    if (gpu_ && std::strstr(SDL_GetError(), "DEVICE_LOST")) {
        LOG_ERROR("The graphics device was lost: %s", SDL_GetError());
        if (!scripted_) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kAppName, "The graphics driver stopped responding, so the game has to close.", window_);
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

void App::place_touch_top(bool row) {
    // Over the game's menus Select and Start go under L1 and R1, at the screen's sides, not over the middle of the page's top
    // (the shared engine's place for them). Over a race's display all four make one row at the top's middle (the display's
    // place and lap at the left corner, the best and last laps at the right). A place the player gave one in [touch] stays theirs.
    touch_top_row_ = row;
    gt3r::TouchConfig tc = touch_.controls().config();
    struct Top {
        const char* key;
        float row_x; // the row's: from the middle, in the button's 40 x 20 units
    };
    for (const Top& t : {Top{"menu.l1", -66}, Top{"menu.select", -22}, Top{"menu.start", 22}, Top{"menu.r1", 66}}) {
        if (config_.get("touch", t.key)) continue;
        const bool side = std::string_view(t.key) == "menu.select" || std::string_view(t.key) == "menu.start";
        gt3r::TouchPlacement p = tc.placement(t.key);
        if (row) {
            p.ax = gt3r::TouchPlacement::AnchorX::Centre, p.ay = gt3r::TouchPlacement::AnchorY::Top;
            p.x = t.row_x * tc.size, p.y = 10 * tc.size;
        } else if (side) {
            p.ax = std::string_view(t.key) == "menu.select" ? gt3r::TouchPlacement::AnchorX::Left : gt3r::TouchPlacement::AnchorX::Right;
            p.ay = gt3r::TouchPlacement::AnchorY::Top;
            p.x = 20 * tc.size, p.y = 36 * tc.size; // (L1 and R1 are 40 x 20 at the corners)
        } else {
            p = gt3r::TouchPlacement{}; // (L1 and R1: the engine's corners)
        }
        tc.set_placement(t.key, p);
    }
    touch_.controls().configure(tc);
}

SDL_AppResult App::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    // Android's back button: not a key of the controls (where it would be Triangle, a race's reverse too, held as long as the system
    // holds it), but one press of the button the top screen gives it (Screen::back_is_start), taken in iterate().
    if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && e.key.scancode == SDL_SCANCODE_AC_BACK) {
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) back_pressed_ = true;
        return SDL_APP_CONTINUE;
    }
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
#if GT3R_XR
    if (xr_ && e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F9 && !e.key.repeat) xr_->recenter(); // (Recenter view)
#endif
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
    if (e.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_DID_ENTER_FOREGROUND)
        display_insets_.reset(), insets_retry_ = 0; // (asked again: a turn, a folding phone)
    // Songs copied in while the game was away (the Files app on phones), music access allowed or taken back meanwhile; no songs
    // measured while a phone's game is in the background.
    if (e.type == SDL_EVENT_DID_ENTER_FOREGROUND) follow_music_folder(true);
    if (gt3r::platform::is_mobile() && (e.type == SDL_EVENT_WILL_ENTER_BACKGROUND || e.type == SDL_EVENT_DID_ENTER_FOREGROUND))
        music_.library().set_away(e.type == SDL_EVENT_WILL_ENTER_BACKGROUND);
    SDL_Event local = e; // the pointer in UI units
    SDL_ConvertEventToRenderCoordinates(renderer_, &local);
#if GT3R_XR
    if (xr_) mirror_pointer(local);
#endif
    if (touch_ui_ && touch_.event(local)) return SDL_APP_CONTINUE; // a finger on the on-screen controls
    if (!scripted_) pointer_.event(local); // (a scripted run's mouse is its script's)
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
#if GT3R_XR
    save_eyes(path);
#endif
}

// --stats: a line a second (60 frames): the frame, the seconds since launch, the frames' mean
// and longest work in ms, the process's resident memory in MB; a run without a window adds the frames' mean wait for the GPU in
// ms, which its work is without (with a window the work has the presenting, whose wait for the display it is when nothing paces
// the frames).
void App::write_stats(double frame_ms) {
    if (frame_ > 1) stats_ms_ += frame_ms, stats_wait_ms_ += gpu_wait_ms_, stats_max_ = std::max(stats_max_, frame_ms), ++stats_frames_;
    if (frame_ % 60 != 0 || !stats_frames_) return;
    if (std::FILE* f = std::fopen(stats_path_.c_str(), "a")) {
        std::fprintf(f, "%d %.1f %.2f %.2f %.1f", frame_, double(SDL_GetTicksNS() - start_ticks_) / 1e9, stats_ms_ / stats_frames_, stats_max_,
                     double(resident_bytes()) / (1024.0 * 1024.0));
        if (run_.windowless) std::fprintf(f, " %.2f", stats_wait_ms_ / stats_frames_);
        std::fprintf(f, "\n");
        std::fclose(f);
    }
    stats_ms_ = stats_max_ = stats_wait_ms_ = 0, stats_frames_ = 0;
}

void App::shutdown() {
    discord_status().shutdown(); // clears the Discord status
    recorder_.reset(); // (the video and its sound closed)
    screens_.clear(); // (screens stop their streams before the audio and the disc go)
    pending_.clear();
    music_.library().cancel(); // (a scan's and the measuring's threads)
    forget_disc_music();
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
#if GT3R_XR
    xr_.reset(); // (its canvases and swapchains before the renderer and the device)
#endif
    gt4_pipelines_.stop();
    if (gpu_) gpu_.reset(); // (its renderer goes with it)
    else if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    gt3r::log_file::close();
    SDL_Quit();
}

} // namespace gt4r
