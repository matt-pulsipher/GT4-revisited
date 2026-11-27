// The app's settings: the display, the sound, the controls, the discs and saved games, the mods. The game's own Options keep the
// game's own settings (volumes, units, the race display, the picture, the aids, the button configurations). Kept in config.ini and
// applied at once, except where a row says when.

#include "gt4r/app/app.h"
#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/app/app_identity.h"
#include "gt4r/app/online.h"
#include "gt4r/app/screen_saves.h"
#include "gt4r/app/settings_vr.h"
#include "gt4r/app/page.h"
#include "gt4r/app/presence.h"
#include "gt4r/app/updates.h"
#include "gt4r/app/texture_dump.h"
#include "gt4r/input/player_controls.h"
#include "gt4r/settings/mod_files.h"

#include "base/file.h"
#include "base/log.h"
#include "base/log_file.h"
#include "gt3r_version.h"
#include "gt4r_version.h"
#include "platform/render_scale.h"
#include "platform/user_files.h"
#include "vfs/mod_archive.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <thread>

namespace gt4r {

namespace {

namespace fs = std::filesystem;
using gt3r::UiRect;
using Pointer = gt3r::PointerInput::Action;

constexpr SDL_DialogFileFilter kZipFilters[] = {{"Mods (.zip)", "zip"}, {"All files", "*"}};

std::string count_text(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// Reading a mod's archive and unpacking it, off the app's thread.
class InstallJob {
public:
    enum class Stage { Reading, Planned, Installing, Done, Failed };
    InstallJob(std::string path, std::string mods_dir, std::string staging) : mods_dir_(std::move(mods_dir)), staging_(std::move(staging)) {
        thread_ = std::thread([this, path = std::move(path)] {
            zip_ = gt3r::ZipArchive::open(path, &error_);
            if (zip_) plan_ = gt3r::plan_mod_install(*zip_, gt3r::platform::picked_file_name(path), mods_dir_, &error_);
            stage_.store(plan_ ? Stage::Planned : Stage::Failed, std::memory_order_release);
        });
    }
    ~InstallJob() {
        if (thread_.joinable()) thread_.join();
    }
    void install() {
        thread_.join();
        stage_ = Stage::Installing;
        thread_ = std::thread([this] {
            const bool ok = gt3r::install_mod_archive(*zip_, *plan_, mods_dir_, staging_, &progress_, &error_);
            stage_.store(ok ? Stage::Done : Stage::Failed, std::memory_order_release);
        });
    }
    Stage stage() const { return stage_.load(std::memory_order_acquire); }
    const gt3r::ModArchivePlan& plan() const { return *plan_; } // from Planned on
    const std::string& error() const { return error_; }         // once Failed
    float progress() const {
        const u64 total = progress_.total;
        return total ? float(double(progress_.done) / double(total)) : 0.0f;
    }

private:
    std::string mods_dir_, staging_, error_;
    std::atomic<Stage> stage_{Stage::Reading};
    std::thread thread_;
    std::unique_ptr<gt3r::ZipArchive> zip_;
    std::optional<gt3r::ModArchivePlan> plan_;
    gt3r::ModInstallProgress progress_;
};

class SettingsScreen : public Screen {
public:
    enum Page { kDisplay, kAudio, kControls, kViews, kDisc, kMods, kPages };
    static_assert(int(kMods) == kModsPage && int(kDisc) == kDiscPage);

    SettingsScreen(App& app, int page, std::string install) : scripted_(app.scripted()), page_(std::clamp(page, 0, int(kPages) - 1)), install_(std::move(install)) {}

    bool menu_input_any_device() const override { return true; }
    // (nothing is taken in while a job reads the disc or the mods)
    bool takes_dropped_file(const std::string& path) const override { return dump_.running() || job_ || is_zip(path); }
    static bool is_zip(const std::string& path) { return path.size() > 4 && gt3r::to_lower(path.substr(path.size() - 4)) == ".zip"; }

    void on_event(App& app, const SDL_Event& e) override {
        if (e.type == SDL_EVENT_DROP_FILE && e.drop.data && is_zip(e.drop.data)) install(app, e.drop.data);
        // (back from a file manager: what was copied in or taken away)
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) look_again_ = true;
    }

    void update(App& app, double) override {
        if (!install_.empty()) {
            const std::string path = std::move(install_);
            install_.clear();
            install(app, path);
        }
        poll(app);
        if (look_again_ && !job_ && !dump_.running()) {
            look_again_ = false;
            discs_known_ = false;
            if (page_ == kMods) app.rescan_mods();
        }
        if (question_.open()) return question_.update(app);
        if (job_ && job_->stage() != InstallJob::Stage::Planned) return; // (unpacking: it waits)
        const MenuInput& m = app.menu();
        if (dump_.running()) {
            if (m.pressed(PadButton::Triangle)) dump_.cancel();
            return;
        }
        build(app);
        for (const Pointer& a : app.pointer()) {
            if (a.kind != Pointer::Kind::Click) continue;
            for (int i = 0; i < kPages; ++i)
                if (tabs_[i].contains(a.x, a.y)) return turn_to(app, i);
        }
        if (m.pressed(PadButton::Triangle)) return back(app);
        if (m.pressed(PadButton::L1) || m.pressed(PadButton::R1)) return turn_to(app, (page_ + (m.pressed(PadButton::R1) ? 1 : kPages - 1)) % kPages);
        if (on_tabs_) {
            if (m.horizontal()) return turn_to(app, (page_ + m.horizontal() + kPages) % kPages, true);
            if (m.vertical() > 0 || m.pressed(PadButton::Cross)) on_tabs_ = false;
            return;
        }
        const PageRows::Done done = rows_.update(app);
        if (done.above) on_tabs_ = true;
        build(app);
    }

    void render(App& app, Ui& ui) override {
        page_header(app, ui, "SETTINGS", "GT4 Revisited " GT4R_VERSION);
        static constexpr const char* kNames[kPages] = {"Display", "Audio", "Controls", "Views & replays", "Disc & saves", "Mods"};
        float x = 20;
        for (int i = 0; i < kPages; ++i) {
            tabs_[i] = {x, 25, ui.tab_width(kNames[i]), 17};
            ui.tab(x, 25, 17, kNames[i], i == page_, on_tabs_ && i == page_);
            x += tabs_[i].w + 6;
        }
        if (page_ == kMods && mod_.empty() && app.vfs() && app.vfs()->mods().empty())
            ui.text(30, 150, "No mods are installed.", colors::kDim);
        rows_.render(ui, {20, 48, 600, 17 * 16 + 1}, !on_tabs_);
        if (!message_.empty()) ui.text(24, 327, Ui::elide(message_, 590), message_bad_ ? colors::kBad : colors::kGood);
        std::string hint = on_tabs_ ? std::string("{Left/Right}: page") : rows_.hint();
        hint += (hint.empty() ? "" : "  ") + std::string(on_tabs_ ? "" : "{L1} {R1}: page  ") + (mod_.empty() ? "{Triangle}: back" : "{Triangle}: the list");
        ui.footer(hint);
        if (job_ && (job_->stage() == InstallJob::Stage::Reading || job_->stage() == InstallJob::Stage::Installing)) work(ui, job_->stage() == InstallJob::Stage::Reading ? "Reading the archive" : "Installing", job_->progress());
        if (dump_.running()) work(ui, "Saving the textures (" + std::to_string(dump_.saved()) + ")", dump_.progress(), "{Triangle}: stop");
        if (question_.open()) question_.render(ui);
    }

private:
    void turn_to(App& app, int page, bool keep_tabs = false) {
        if (page == page_) return;
        stop_preview(app);
        page_ = page, mod_.clear(), message_.clear();
        rows_.selected = 0, rows_.scroll.first = 0;
        on_tabs_ = keep_tabs;
        look_again_ = page == kMods || page == kDisc;
    }
    void back(App& app) {
        if (!mod_.empty()) {
            mod_.clear(), message_.clear();
            rows_.selected = list_row_, rows_.scroll.first = list_first_;
            return;
        }
        stop_preview(app);
        app.pop();
    }
    void say(std::string text, bool bad = false) { message_ = std::move(text), message_bad_ = bad; }

    static void work(Ui& ui, const std::string& what, float done, const char* hint = nullptr) {
        ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 170});
        ui.fill_round(170, 150, 300, 60, 6, colors::kPanel);
        ui.text_center(162, what, colors::kText);
        ui.bar(190, 182, 260, 6, done, colors::kAccent);
        if (hint) ui.text_center(194, hint, colors::kDim);
    }

    void build(App& app) {
        rows_.rows.clear();
        switch (page_) {
        case kDisplay: return display(app), vr_display_rows(app, rows_); // (a headset's build: its rows first)
        case kAudio: return audio(app);
        case kControls: return controls(app);
        case kViews: return views(app);
        case kDisc: return disc(app);
        default: return mod_.empty() ? mods(app) : mod(app);
        }
    }

    // --- Display ---

    void display(App& app) {
        App* a = &app;
        const settings::Video v = app.video();
        const auto set = [a](auto&& change) {
            settings::Video n = a->video();
            change(n);
            a->set_video(n);
        };
        if (!gt3r::platform::is_mobile()) // (phones and tablets: always the whole screen)
            rows_.choice("Screen", {"Window", "Full screen"}, v.fullscreen, [set](size_t i) { set([&](auto& n) { n.fullscreen = i == 1; }); });
        {
            using gt3r::platform::Aspect;
            static constexpr Aspect kShapes[] = {Aspect::Fill, Aspect::Wide, Aspect::Classic};
            const size_t at = v.aspect == Aspect::Wide ? 1 : v.aspect == Aspect::Classic ? 2 : 0;
            rows_.choice("Aspect ratio", {"Fill the screen", "16:9", "4:3"}, at, [set](size_t i) { set([&](auto& n) { n.aspect = kShapes[std::min<size_t>(i, 2)]; }); });
        }
        rows_.choice("VSync", {"Lowest input delay", "No tearing"}, v.vsync, [set](size_t i) { set([&](auto& n) { n.vsync = i == 1; }); });
        {
            const double hz = app.refresh_rate(), rate = settings::limited_rate(hz, true);
            const std::string all = hz > 0 ? "The display's, " + std::to_string(int(std::lround(hz))) + " fps" : "The display's";
            const std::string less = rate > 0 ? std::to_string(int(std::lround(rate))) + " fps" : hz > 0 ? all : "60 fps";
            PageRow& r = rows_.choice("Frame rate", {all, less}, v.limit_60fps, [set](size_t i) { set([&](auto& n) { n.limit_60fps = i == 1; }); });
            if (hz > 0 && rate <= 0) r.enabled = false, r.value = all;
        }
        rows_.choice("Frame rate counter", {"Hidden", "Shown"}, v.show_fps, [set](size_t i) { set([&](auto& n) { n.show_fps = i == 1; }); });
        rows_.slider("Gamma", v.gamma, gt3r::render::kGammaMin, gt3r::render::kGammaMax, gt3r::render::kGammaStep,
                     [](float g) {
                         char b[32];
                         std::snprintf(b, sizeof b, gt3r::render::gamma_changes(g) ? "%.2f" : "%.2f, unchanged", double(g));
                         return std::string(b);
                     },
                     [set](float g) { set([&](auto& n) { n.gamma = gt3r::render::gamma_changes(g) ? g : 1.0f; }); });
        PageRow& gamma = rows_.rows.back();
        gamma.other = [set] { set([](auto& n) { n.gamma = 1.0f; }); }, gamma.other_hint = "unchanged";
        gamma.enabled = app.gpu() != nullptr;

        const std::vector<int> levels = app.gpu() ? app.gpu()->msaa_levels() : std::vector<int>{1, 2, 4, 8};
        const int in_use = app.gpu() ? app.gpu()->msaa() : v.msaa;
        std::vector<std::string> names;
        size_t at = 0;
        for (size_t i = 0; i < levels.size(); ++i) {
            names.push_back(levels[i] > 1 ? std::to_string(levels[i]) + "x MSAA" : "None");
            if (in_use >= levels[i]) at = i;
        }
        rows_.choice("Anti-aliasing", names, at, [set, levels](size_t i) { set([&](auto& n) { n.msaa = levels[i]; }); }).enabled = app.gpu() != nullptr;
        static constexpr int kAniso[] = {1, 2, 4, 8, 16};
        const int aniso = app.gpu() ? app.gpu()->anisotropy() : v.anisotropy;
        at = 0;
        for (size_t i = 0; i < 5; ++i)
            if (aniso >= kAniso[i]) at = i;
        rows_.choice("Anisotropic filtering", {"None", "2x", "4x", "8x", "16x"}, at, [set](size_t i) { set([&](auto& n) { n.anisotropy = kAniso[i]; }); }).enabled =
            app.gpu() && app.gpu()->anisotropy_supported();
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(app.window(), &pw, &ph);
        const float automatic = gt3r::platform::auto_render_scale(pw, ph, gt3r::platform::is_mobile());
        const std::vector<settings::Resolution> sizes = settings::resolutions(automatic, v.dynamic_resolution ? std::max(automatic, gt3r::platform::dynamic_ceiling(pw, ph)) : automatic,
                                                                              gt3r::platform::supersample_limit(float(pw), float(ph)));
        names.clear();
        for (const settings::Resolution& r : sizes) names.push_back(r.name);
        rows_.choice("3D resolution", names, settings::resolution_index(sizes, v.render_scale), [set, sizes](size_t i) { set([&](auto& n) { n.render_scale = sizes[i].scale; }); }).enabled = app.gpu() != nullptr;
        std::string lowered = "Lowered when the GPU is too slow";
        if (const auto* d = app.dynamic_scale()) lowered += " (now " + settings::percent(d->scale()) + ")";
        rows_.choice("Dynamic resolution", {"Never lowered", lowered}, v.dynamic_resolution, [set](size_t i) { set([&](auto& n) { n.dynamic_resolution = i == 1; }); }).enabled = app.gpu() != nullptr;
        rows_.choice("Cars and prizes in menus", {"640x448, as the console's", "The screen's resolution"}, v.sharp_menu_models, [set](size_t i) { set([&](auto& n) { n.sharp_menu_models = i == 1; }); }).enabled = app.gpu() != nullptr;

        std::vector<std::pair<std::string, std::string>> gpus{{"0", "Automatic"}};
#if GT3R_GLES
#ifdef SDL_PLATFORM_WINDOWS
        gpus.emplace_back("1", "OpenGL ES (ANGLE)"), gpus.emplace_back("2", "OpenGL ES (the driver's)");
#else
        gpus.emplace_back("1", "OpenGL ES");
#endif
#endif
        gpus.emplace_back("3", "None: the 2D pages only");
        PageRow& gpu = rows_.pick("3D renderer", gpus, std::to_string(int(v.gpu)), [set](const std::string& s) { set([&](auto& n) { n.gpu = settings::Gpu(std::atoi(s.c_str())); }); });
        if (app.renderer_needs_restart()) gpu.tag = "after a restart";
#if GT3R_GLES
        if (app.gpu() && app.gpu()->gles()) rows_.info("Graphics driver", gt3r::render::gles::description());
        else
#endif
            rows_.info("Graphics driver", driver_name(app));
    }

    static std::string driver_name(App& app) {
        std::string d = app.gpu() ? app.gpu()->driver_name() : SDL_GetRendererName(app.renderer());
        return d == "metal" ? "Metal" : d == "vulkan" ? "Vulkan" : d == "direct3d12" ? "Direct3D 12" : d;
    }

    // --- Audio ---

    void audio(App& app) {
        App* a = &app;
        const App::Sound s = app.sound_settings();
        const auto set = [a](auto&& change) {
            App::Sound n = a->sound_settings();
            change(n);
            a->set_sound(n);
        };
        std::vector<std::pair<std::string, std::string>> devices{{"", "The system's"}};
        for (const std::string& d : gt3r::AudioEngine::playback_devices()) devices.emplace_back(d, d);
        if (std::none_of(devices.begin(), devices.end(), [&](const auto& d) { return d.first == s.device; })) devices.emplace_back(s.device, s.device + " (not connected)");
        rows_.pick("Output device", devices, s.device, [set](const std::string& d) { set([&](auto& n) { n.device = d; }); });
        rows_.slider("Music volume", s.music, 0, 1, 0.05f, settings::percent, [this, set, a](float v) {
            set([&](auto& n) { n.music = v; });
            preview(*a);
        });
        rows_.slider("Effects volume", s.effects, 0, 1, 0.05f, settings::percent, [set, a](float v) {
            set([&](auto& n) { n.effects = v; });
            if (audio::AppSound* sound = a->sound()) sound->menu().play("ok");
        });
        rows_.choice("Sounds behind you", {"Plain stereo", "As the console sends them, for a surround decoder"}, s.rear_cue, [set](size_t i) { set([&](auto& n) { n.rear_cue = i == 1; }); });
    }
    // The menus' music while its volume is changed.
    void preview(App& app) {
        if (previewing_) return;
        if (audio::AppSound* sound = app.sound(); sound && sound->menu().start_music("arcade")) previewing_ = true;
    }
    void stop_preview(App& app) {
        if (!previewing_) return;
        previewing_ = false;
        if (audio::AppSound* sound = app.sound()) sound->menu().stop_music();
    }

    // --- Controls ---

    void controls(App& app) {
        App* a = &app;
        gt3r::InputManager& in = app.input();
        // (a touch screen's on-screen controls are player 1's whenever no controller is)
        const std::string first = in.device_name(0);
        rows_.action("Player 1's controls", app.touch_ui() && first == "Keyboard" ? std::string("Touch screen") : first, "open", [a] { a->push(make_controls_screen(*a, 0)); });
        rows_.action("Player 2's controls", in.device_name(1), "open", [a] { a->push(make_controls_screen(*a, 1)); });
        const auto keep = [a] {
            input::save_config(a->input(), a->config());
            a->save_config();
        };
        if (!gt3r::platform::is_mobile()) // (a phone's app in the background stops)
            rows_.choice("Another window in front", {"The race pauses", "Controllers keep driving"}, in.background(), [&in, keep](size_t i) { in.set_background(i == 1), keep(); });
#ifdef SDL_PLATFORM_WINDOWS
        PageRow& xbox = rows_.choice("Xbox controllers", {"XInput", "Raw Input, with the triggers' motors"}, in.xbox_rawinput(), [&in, keep](size_t i) { in.set_xbox_rawinput(i == 1), keep(); });
        if (const char* started = SDL_GetHint(SDL_HINT_JOYSTICK_RAWINPUT); started && in.xbox_rawinput() != (*started == '1')) xbox.tag = "after a restart";
#endif
        if (app.touch_ui()) touch(app);
        rows_.heading("Connected");
        const std::vector<std::string> names = in.device_names();
        for (const std::string& n : names) rows_.info(n, "");
        if (names.empty()) rows_.info(app.touch_ui() ? "Only the touch screen" : "Only the keyboard", "");
    }

    // Phones and tablets: the on-screen controls (the shared engine's, input/touch_controls.h) and the phone's own vibration.
    void touch(App& app) {
        App* a = &app;
        const gt3r::TouchConfig c = app.touch_config();
        using TC = gt3r::TouchConfig;
        const auto set = [a, c](const std::function<void(TC&)>& change) {
            TC n = c;
            change(n);
            a->set_touch_config(n);
        };
        const auto percent = [](float v) { return std::to_string(int(std::lround(v * 100))) + " %"; };
        rows_.heading("Touch screen");
        rows_.choice("On-screen controls", {"Shown after a touch", "Always shown", "Never shown"}, size_t(c.mode), [set](size_t i) { set([&](TC& n) { n.mode = TC::Mode(i); }); });
        rows_.choice("Steering", {"Drag on the left half", "Tilt the device", "Left and right buttons"}, size_t(c.steering),
                     [set](size_t i) { set([&](TC& n) { n.steering = TC::Steering(i); }); });
        rows_.choice("Pedals", {"Higher on the pedal, harder", "As hard as the screen is pressed", "Always all the way"}, size_t(c.pedals),
                     [set](size_t i) { set([&](TC& n) { n.pedals = TC::Pedals(i); }); });
        rows_.slider("Steering drag for full lock", c.steer_range, 0.03f, 0.5f, 0.01f, [percent](float v) { return percent(v) + " of the screen"; },
                     [set](float v) { set([&](TC& n) { n.steer_range = v; }); });
        rows_.slider("Tilt for full lock", c.tilt_range, 10, 90, 1, [](float v) { return std::to_string(int(std::lround(v))) + " degrees"; },
                     [set](float v) { set([&](TC& n) { n.tilt_range = v; }); });
        rows_.choice("Look around", {"Off", "Drag sideways on the open screen"}, c.look, [set](size_t i) { set([&](TC& n) { n.look = i == 1; }); });
        rows_.slider("Controls' opacity", c.opacity, 0.05f, 1, 0.05f, percent, [set](float v) { set([&](TC& n) { n.opacity = v; }); });
        rows_.slider("Controls' size", c.size, 0.6f, 1.6f, 0.05f, percent, [set](float v) { set([&](TC& n) { n.size = v; }); });
        rows_.choice("Pedals on the", {"Right, steering on the left", "Left, steering on the right"}, c.left_handed,
                     [set](size_t i) { set([&](TC& n) { n.left_handed = i == 1; }); });
        if (gt3r::InputManager::phone_vibration_available()) {
            gt3r::InputManager& in = app.input();
            rows_.choice("Phone vibration", {"Off", "When the controls have no motor"}, in.phone_vibration(), [a, &in](size_t i) {
                in.set_phone_vibration(i == 1);
                input::save_config(a->input(), a->config());
                a->save_config();
            });
        }
    }

    // --- Views & replays ---

    // The race reads these from the config as it starts and as its pause closes (race/replay_effects.h). `otherwise`:
    // the value while the config has none of `options`.
    void config_row(App& app, const char* label, const char* section, const char* key, const std::vector<std::pair<std::string, std::string>>& options, const char* otherwise) {
        App* a = &app;
        std::string now = gt3r::to_lower(app.config().get(section, key).value_or(otherwise));
        if (std::none_of(options.begin(), options.end(), [&](const auto& o) { return o.first == now; })) now = otherwise;
        rows_.pick(label, options, now, [a, section, key](const std::string& v) {
            a->config().set(section, key, v);
            a->save_config();
        });
    }

    void views(App& app) {
        rows_.heading("Races");
        config_row(app, "Views", "cameras", "race_views", {{"original", "The game's three"}, {"all", "All"}}, "original");
        config_row(app, "In-car camera", "cameras", "in_car_shake", {{"off", "Steady"}, {"on", "Shakes as the game's"}}, "on");
        rows_.heading("Replays");
        const std::vector<std::pair<std::string, std::string>> drawn{{"off", "Not drawn"}, {"on", "As the game draws it"}};
        config_row(app, "Heat haze", "video", "heat_haze", drawn, "off");
        config_row(app, "Vignette", "video", "replay_vignette", drawn, "on");
        config_row(app, "Motion trail", "video", "replay_trail", drawn, "on");
        config_row(app, "Depth of field", "video", "depth_of_field", {{"off", "Not drawn"}, {"standard", "Standard"}, {"high", "High"}}, "high");
        if (!app.gpu())
            for (PageRow& r : rows_.rows) r.enabled = false;
    }

    // --- Disc & saves ---

    void disc(App& app) {
        App* a = &app;
        if (!discs_known_) {
            discs_known_ = true;
            discs_.clear();
            for (const std::string& p : app.recent_discs()) discs_.push_back({p, p.find("://") != std::string::npos || gt3r::host_path_exists(p), app.disc_record(p)});
        }
        const std::string current = app.disc() ? app.config().get("game", "disc").value_or("") : "";
        if (app.disc()) {
            const DiscInfo& d = app.disc()->info();
            rows_.info("Disc", d.description + (d.serial.empty() ? std::string() : ", " + d.key + " v" + d.version));
            rows_.info("Its files", d.verified ? "a tested release's own" : d.modified ? "a modified copy of a tested release" : "not a tested release");
            if (d.problem.empty()) rows_.info("The image", "complete");
            else {
                PageRow& r = rows_.info("The image", "not complete");
                r.bad = true;
                for (const std::string& line : Ui::wrap(d.problem, 560)) rows_.info(line, "");
            }
        }
        rows_.action("Open a disc image", ".iso or .chd", "choose the file", [a] { a->show_open_dialog(); });
        if (!app.last_error().empty()) {
            rows_.info("The last one could not be opened", "").bad = true;
            for (const std::string& line : Ui::wrap(app.last_error(), 560)) rows_.info(line, "");
        }
        if (!discs_.empty()) rows_.heading("Discs opened before");
        for (const Known& k : discs_) {
            const bool open = !current.empty() && gt3r::platform::same_disc_path(k.path, current);
            const std::string what = k.record ? k.record->description + (k.record->modified ? ", modified" : "") : std::string();
            PageRow& r = rows_.action(gt3r::platform::disc_file_name(k.path), open ? "in use" : !k.there ? "not found" : what, open || !k.there ? "" : "open", [a, open, k] {
                if (!open && k.there) a->request_disc(k.path);
            });
            r.bad = !k.there;
            r.other = [this, a, path = k.path] { a->forget_disc(path), discs_known_ = false; }, r.other_hint = "off the list";
        }
        rows_.heading("Saves");
        PageRow& saves = rows_.action("Save folder", app.save_dir_chosen() ? app.save_dir() : std::string("the data folder"), "choose another", [this, a] { folder_.open_folder(*a); });
        if (app.save_dir_chosen()) saves.other = [this, a] { use_save_folder(*a, ""); }, saves.other_hint = "the data folder";
        rows_.action("Saved games", "", "open the list", [a] { a->push(make_saves_screen(*a)); });
        // The server the races over the internet meet through (the Multi-LAN Race's page asks for one the first time, and takes a
        // name typed): none is contacted before one is chosen.
        rows_.heading("Online races");
        {
            const std::string now = app.config().get("online", "server").value_or("");
            std::vector<std::pair<std::string, std::string>> servers{{"", "none"}, {kOfferedServer, kOfferedServer}};
            if (!now.empty() && now != kOfferedServer) servers.emplace_back(now, now);
            rows_.pick("Internet server", servers, now, [a](const std::string& v) {
                a->config().set("online", "server", v);
                a->save_config();
            });
        }
        updates_rows(app);
        rows_.heading("This app");
        rows_.choice("Starts in", {"The game", "The home screen"}, app.config().get("game", "start").value_or("game") == "hub", [a](size_t i) {
            a->config().set("game", "start", i ? "hub" : "game");
            a->save_config();
        });
        rows_.action("Data folder", app.data_dir(), "open it", [this, a] { show(*a, a->data_dir()); });
        rows_.action("Screenshots (F12)", "", "open the folder", [this, a] {
            SDL_CreateDirectory((a->data_dir() + "screenshots").c_str());
            show(*a, a->data_dir() + "screenshots");
        });
        const std::string log = gt3r::log_file::path();
        if (!log.empty()) {
            rows_.action("Log of this session", "", "open it", [this, a, log] { show(*a, log); });
            const std::string before = app.data_dir() + log_file_name(true);
            if (gt3r::host_path_exists(before)) rows_.action("Log of the session before", "", "open it", [this, a, before] { show(*a, before); });
            rows_.action("Copy both logs", "to the clipboard", "copy", [this, log, before] { copy_logs(log, before); });
        }
        rows_.info("Version", "GT4 Revisited " GT4R_VERSION ", engine " GT3R_VERSION);
        rows_.info("Settings file", app.config_path());
    }

    // The update check (app/updates.h) and the Discord status (app/presence.h).
    void updates_rows(App& app) {
        App* a = &app;
        Updates& up = updates();
        rows_.heading("Updates");
        rows_.choice("Check for updates", {"Only when asked", "At startup"}, Updates::enabled(app), [a](size_t i) { Updates::set_enabled(*a, i == 1); });
        rows_.action("Check now", up.status_text(), "ask " + Updates::server(app), [a] { updates().check_now(*a); });
        rows_.info("This version", net::this_version().to_string());
        rows_.info("Latest release", (up.latest().known() ? up.latest().number() : std::string("unknown")) + (up.checked().empty() ? "" : "  (as of " + up.checked() + ")"));
        // (races through the relay server need it; the local network and addresses never do)
        rows_.info("Online play needs", up.min_online().known() ? "version " + up.min_online().number() + " or later" : "any version");
        const gt3r::net::UpdateLinks links = up.links();
        const bool due = up.prompt() != gt3r::net::UpdatePrompt::None;
        rows_.action("Download update", due ? "version " + up.latest().number() : up.known() ? std::string("up to date") : std::string("no release known"), "open the update", [] { updates().open_dialog(); }).enabled = due;
        rows_.action("Release notes", links.notes.empty() ? std::string("not known yet") : links.notes, "open the page", [] { updates().open_notes(); }).enabled = !links.notes.empty();
        if (Presence::supported() && !Presence::app_id(app).empty()) {
            rows_.heading("Discord");
            rows_.choice("Show what you're playing", {"No", "Yes"}, Presence::enabled(app), [a](size_t i) { discord_status().set_enabled(*a, i == 1); });
            rows_.info("Discord", discord_status().status_text(app));
        }
    }

    void use_save_folder(App& app, const std::string& path) {
        bool its_own = false;
        const std::string why = app.set_save_dir(path, &its_own);
        if (!why.empty()) return say(why, true);
        say((its_own ? "The games already there are used: " : "Games, replays and photos are kept there now: ") + (app.save_dir_chosen() ? app.save_dir() : std::string("the data folder")));
    }

    void show(App& app, const std::string& path) {
        if (app.scripted()) return say("(not opened in a scripted run) " + path);
        if (!gt3r::platform::open_user_folder(path, app.data_dir())) say("It could not be opened: " + path, true);
    }

    void copy_logs(const std::string& now, const std::string& before) {
        std::string text;
        for (const std::string& p : {before, now})
            if (const auto b = gt3r::read_host_file(p)) text += "--- " + fs::path(p).filename().string() + "\n" + std::string(b->begin(), b->end()) + "\n";
        // (the clipboard of a scripted run is left alone)
        if (scripted_ || SDL_SetClipboardText(text.c_str())) say("Copied (" + std::to_string(text.size() / 1024 + 1) + " KB).");
        else say(std::string("Not copied: ") + SDL_GetError(), true);
    }

    // --- Mods ---

    static std::string contents(const gt3r::ModInfo& m) {
        std::string out;
        const auto add = [&out](const std::string& s) { out += (out.empty() ? "" : ", ") + s; };
        if (!m.files.empty()) add(count_text(m.files.size(), "file", "files"));
        if (!m.cars.empty()) add(count_text(m.cars.size(), "car", "cars"));
        if (!m.textures.empty()) add(count_text(m.textures.size(), "texture", "textures"));
        return out.empty() ? "nothing this game reads" : out;
    }

    void mods(App& app) {
        App* a = &app;
        rows_.action("Install a mod", gt3r::platform::is_mobile() ? ".zip" : ".zip, or drop it on the window", "choose the file", [this, a] { archive_.open_file(*a, kZipFilters, SDL_arraysize(kZipFilters)); });
        rows_.action("Mods folder", app.mods_dir(), "open it", [this, a] { show(*a, a->mods_dir()); });
        {
            const settings::Video v = app.video();
            rows_.choice("Texture packs", {"The game's own textures", "The mods' textures"}, v.texture_packs, [a](size_t i) {
                settings::Video n = a->video();
                n.texture_packs = i == 1;
                a->set_video(n);
            });
        }
        rows_.action("Save the game's textures", "for making a pack", "save them", [this, a] {
            question_.ask("Save the game's textures?", {"Every car's, course's and figure's texture as a PNG picture,", "a few minutes and about a gigabyte, into", Ui::elide(dump_dir(*a), 340)}, "Save", "Cancel",
                          [this, a] { dump_.start(*a, dump_dir(*a)); });
        });
        if (!app.vfs() || app.vfs()->mods().empty()) return;
        rows_.heading("The upper one wins where two change the same file");
        for (const settings::ModPlace& place : places(app))
            for (const gt3r::ModInfo& m : app.vfs()->mods())
                if (m.id == place.id) {
                    PageRow& r = rows_.choice(m.version.empty() ? m.name : m.name + "  " + m.version, {"Not used", "Used"}, m.enabled, [a, id = m.id](size_t i) { a->set_mod_enabled(id, i == 1); });
                    r.value = (m.for_disc ? r.value : "For another disc") + "   " + contents(m);
                    r.enabled = true;
                    if (!m.for_disc) r.change = nullptr;
                    r.press = [this, id = m.id] { mod_ = id, message_.clear(), list_row_ = rows_.selected, list_first_ = rows_.scroll.first, rows_.selected = 0, rows_.scroll.first = 0; }, r.press_hint = "open";
                    r.other = [this, a, id = m.id] { ask_remove(*a, id); }, r.other_hint = "remove";
                }
    }

    static std::vector<settings::ModPlace> places(App& app) {
        std::vector<settings::ModPlace> out;
        for (const gt3r::ModInfo& m : app.vfs()->mods()) out.push_back({m.id, m.priority});
        settings::sort_places(out);
        return out;
    }

    const gt3r::ModInfo* find(App& app, const std::string& id) const {
        if (app.vfs())
            for (const gt3r::ModInfo& m : app.vfs()->mods())
                if (m.id == id) return &m;
        return nullptr;
    }

    // One mod's page.
    void mod(App& app) {
        App* a = &app;
        const gt3r::ModInfo* m = find(app, mod_);
        if (!m) {
            mod_.clear();
            return mods(app);
        }
        rows_.heading(m->version.empty() ? m->name : m->name + "  " + m->version);
        const std::string id = m->id;
        if (m->for_disc) rows_.choice("In the game", {"Not used", "Used"}, m->enabled, [a, id](size_t i) { a->set_mod_enabled(id, i == 1); });
        else {
            std::string discs;
            for (const std::string& d : m->discs) discs += (discs.empty() ? "" : ", ") + d;
            rows_.info("For the discs", discs);
        }
        const std::vector<settings::ModPlace> all = places(app);
        size_t at = 0;
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].id == id) at = i;
        if (at > 0) rows_.action("Move up", "over " + name_of(app, all[at - 1].id), "move", [this, a, all, at] { reorder(*a, all, at, -1); });
        if (at + 1 < all.size()) rows_.action("Move down", "under " + name_of(app, all[at + 1].id), "move", [this, a, all, at] { reorder(*a, all, at, 1); });
        rows_.action("Its folder", m->root, "open it", [this, a, root = m->root] { show(*a, root); });
        rows_.action("Remove", "", "remove it", [this, a, id] { ask_remove(*a, id); });
        rows_.info("Holds", contents(*m));
        if (!m->author.empty()) rows_.info("By", m->author);
        for (const std::string& line : Ui::wrap(m->description, 560)) rows_.info(line, "");
        mod_cars(app, *m);
        if (!m->makers.empty() || !m->events.empty() || !m->liveries.empty()) rows_.info("Makers, events and liveries of mods are not read by this game yet", "");
    }
    // A mod's cars (formats/db/mod_cars.h): each one's name, or why it is not in the game.
    void mod_cars(App& app, const gt3r::ModInfo& m) {
        if (m.cars.empty()) return;
        rows_.heading("Its cars");
        db::SpecDb* sdb = m.active() ? app.db() : nullptr;
        if (!sdb) {
            rows_.info(count_text(m.cars.size(), "car", "cars"), m.active() ? "the disc's car database is not read" : "not used");
            return;
        }
        if (sdb->prologue()) {
            rows_.info(count_text(m.cars.size(), "car", "cars"), "Gran Turismo 4 Prologue takes no mod cars");
            return;
        }
        for (const db::ModCarStatus& c : sdb->mod_cars().statuses()) {
            if (c.mod != m.id) continue;
            rows_.info(c.name.empty() ? c.label : c.name, c.loaded() ? (c.warnings.empty() ? "in the game" : "in the game; " + c.warnings.front()) : c.error);
            if (c.loaded())
                for (size_t i = 1; i < c.warnings.size() && i < 4; ++i) rows_.info("", c.warnings[i]);
        }
    }

    std::string name_of(App& app, const std::string& id) const {
        const gt3r::ModInfo* m = find(app, id);
        return m ? m->name : id;
    }

    void reorder(App& app, const std::vector<settings::ModPlace>& all, size_t at, int direction) {
        for (const settings::ModPlace& p : settings::move_mod(all, at, direction)) {
            const std::string path = app.mods_dir() + "/" + p.id + "/mod.ini";
            const std::optional<Bytes> was = gt3r::read_host_file(path);
            const std::string text = settings::with_priority(was ? std::string_view(reinterpret_cast<const char*>(was->data()), was->size()) : std::string_view(), p.priority);
            if (!gt3r::write_host_file(path, gt3r::ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()))) return say("It could not be written: " + path, true);
        }
        app.rescan_mods();
    }

    void ask_remove(App& app, const std::string& id) {
        App* a = &app;
        const gt3r::ModInfo* m = find(app, id);
        if (!m) return;
        question_.ask("Remove " + m->name + "?", {"Its folder and everything in it is deleted:", Ui::elide(m->root, 340)}, "Remove", "Keep", [this, a, id] {
            std::string error;
            if (gt3r::remove_mod_folder(a->mods_dir(), id, &error)) say("Removed.");
            else say(error, true);
            mod_.clear();
            a->rescan_mods();
        });
    }

    void install(App& app, const std::string& path) {
        if (job_ || dump_.running()) return;
        page_ = kMods, mod_.clear(), on_tabs_ = false;
        say("");
        job_ = std::make_unique<InstallJob>(path, app.mods_dir(), app.data_dir() + "mods-staging");
    }

    static std::string dump_dir(App& app) { return app.data_dir() + "textures/" + app.disc()->info().key; }

    void poll(App& app) {
        std::string path, error;
        if (archive_.take(path, error)) {
            if (!error.empty()) say(error, true);
            else if (!path.empty()) install(app, path);
        }
        if (folder_.take(path, error)) {
            if (!error.empty()) say(error, true);
            else if (!path.empty()) use_save_folder(app, path);
        }
        if (dump_.finished()) {
            say(dump_.message(), !dump_.ok());
            dump_.close(app);
        }
        if (!job_) return;
        switch (job_->stage()) {
        case InstallJob::Stage::Planned: {
            if (question_.open() || asked_) break;
            const gt3r::ModArchivePlan& plan = job_->plan();
            bool replaces = false;
            std::vector<std::string> lines;
            for (const gt3r::ArchiveMod& m : plan.mods) {
                replaces = replaces || m.replaces;
                if (lines.size() < 6) lines.push_back(m.name + (m.version.empty() ? "" : " " + m.version) + (m.replaces ? "  (replaces " + (m.installed_version.empty() ? std::string("the one there") : m.installed_version) + ")" : ""));
            }
            if (!replaces) {
                job_->install();
                break;
            }
            asked_ = true;
            question_.ask("Replace what is installed?", lines, "Replace", "Cancel", [this] { job_->install(), asked_ = false; });
            break;
        }
        case InstallJob::Stage::Done: {
            std::string names;
            for (const gt3r::ArchiveMod& m : job_->plan().mods) names += (names.empty() ? "" : ", ") + m.name;
            job_.reset();
            app.rescan_mods();
            say("Installed: " + names);
            break;
        }
        case InstallJob::Stage::Failed:
            say("Not installed: " + job_->error(), true);
            job_.reset();
            break;
        default: break;
        }
        if (job_ && asked_ && !question_.open() && job_->stage() == InstallJob::Stage::Planned) job_.reset(), asked_ = false; // (cancelled)
    }

    struct Known {
        std::string path;
        bool there = false;
        std::optional<gt3r::platform::DiscRecord> record;
    };

    bool scripted_ = false;
    int page_ = 0, list_row_ = 0, list_first_ = 0;
    bool on_tabs_ = false, look_again_ = true, discs_known_ = false, message_bad_ = false, previewing_ = false, asked_ = false;
    PageRows rows_;
    UiRect tabs_[kPages];
    PageQuestion question_;
    std::vector<Known> discs_;
    FileWindow archive_, folder_;
    std::unique_ptr<InstallJob> job_;
    TextureDump dump_;
    std::string install_, message_, mod_;
};

} // namespace

std::unique_ptr<Screen> make_settings_screen(App& app, int page, const std::string& install) { return std::make_unique<SettingsScreen>(app, page, install); }

} // namespace gt4r
