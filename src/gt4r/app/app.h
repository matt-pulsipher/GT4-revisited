#pragma once

// The executable's shell: window, input, audio, the open disc and a stack of screens. It
// draws with the shared engine's Ui (640x360 units) and reads the menus through its virtual
// pad, as GT3 Revisited's own screens do.

#include "app/ui.h"
#include "audio/audio_engine.h"
#include "base/config.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/settings/run_mode.h"
#include "gt4r/settings/video.h"
#include "input/input_manager.h"
#include "platform/recent_discs.h"
#include "render/dynamic_scale.h"
#include "render/gamma_pass.h"
#include "render/gpu_context.h"
#include "scene/model.h"
#include "vfs/vfs.h"

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt4r {

using gt3r::Color;
using gt3r::MenuInput;
using gt3r::PadButton;
using gt3r::Ui;
namespace colors = gt3r::colors;

class App;

class Screen {
public:
    virtual ~Screen() = default;
    virtual void update(App& app, double dt) = 0;
    virtual void render(App& app, Ui& ui) = 0;
    virtual void on_event(App&, const SDL_Event&) {} // pointer coordinates in UI units
    // The game's own pictures (its menus, its races): laid out over the picture of Settings > Display > Aspect ratio (Ui::width() is
    // 640 at 16:9, 480 at 4:3, the window's shape with fill; 360 tall). The app's own pages keep 640 x 360.
    virtual bool follows_aspect() const { return false; }
    // Read from every controller and the keyboard, whatever player 1's are: pages that choose the controllers.
    virtual bool menu_input_any_device() const { return false; }
    // A file dropped on the window that this screen takes itself (it gets the event); else a .zip is a mod to install and
    // anything else a disc image.
    virtual bool takes_dropped_file(const std::string&) const { return false; }
};

class App {
public:
    bool init(int argc, char** argv);
    SDL_AppResult iterate();
    SDL_AppResult event(const SDL_Event& e);
    void shutdown();

    // Opens a disc image and makes it the current one; an error message for the player on
    // failure (the open disc stays), empty on success.
    std::string open_disc(const std::string& path);
    void show_open_dialog();
    // A disc to open on the next frame (the dialog's answer, a dropped file).
    void request_disc(std::string path);
    // The discs opened before ([game] recent1.., the latest first), what each was then, and one taken off the list.
    std::vector<std::string> recent_discs() const;
    std::optional<gt3r::platform::DiscRecord> disc_record(const std::string& path) const;
    void forget_disc(const std::string& path);

    // The display's settings (settings/video.h), applied at once and kept in config.ini; the 3D renderer is taken at the start.
    const settings::Video& video() const { return video_; }
    void set_video(const settings::Video& v);
    bool renderer_needs_restart() const { return video_.gpu != video_started_; }
    bool fullscreen() const;
    double refresh_rate() const; // the display's, 0 when it is not known
    // The scale dynamic resolution has settled on now, null while it is off.
    const gt3r::render::DynamicScale* dynamic_scale() const { return dynamic_on_ ? &dynamic_ : nullptr; }
    // The menus' cars and prizes as the console draws them (640 x 448) while Settings says so: the fixed size of a 3D view over
    // `dst` (UI units), 0 x 0 for the screen's own.
    SDL_Point menu_model_size(const SDL_FRect& dst) const;

    // The sound's settings ([audio]): the output device, the music's and the effects' level (on top of the game's own options), the
    // rear cue.
    struct Sound {
        std::string device; // empty: the system's
        float music = 0.8f, effects = 0.8f;
        bool rear_cue = true;
    };
    const Sound& sound_settings() const { return sound_settings_; }
    void set_sound(const Sound& s);

    // Where the saved games, the replays and the photos are: [saves] folder, by default the data folder; ends with a separator.
    std::string save_dir() const;
    bool save_dir_chosen() const;
    // Another folder (empty: the data folder again): what the new one does not have of the saves is copied there first. An error
    // for the player, empty when it is done.
    std::string set_save_dir(const std::string& path, bool* has_its_own = nullptr); // (the folder has games already: those are used)

    // The mods folder read again; a mod switched on or off for this disc.
    void rescan_mods();
    void set_mod_enabled(const std::string& id, bool on);

    // What the mouse did since the last frame (UI units), for the page on top.
    const std::vector<gt3r::PointerInput::Action>& pointer() const { return pointer_actions_; }
    bool mouse_at(float& x, float& y) const { return !cursor_hidden_ && pointer_.hover_pos(x, y); }
    SDL_Window* window() { return window_; }
    bool scripted() const { return scripted_; } // opens nothing on the screen, reads no real input (settings/run_mode.h)
    const std::string& config_path() const { return config_path_; }

    void push(std::unique_ptr<Screen> s) { pending_.push_back(std::move(s)); }
    void pop() { ++pops_; }
    // A pop asked for this frame taken back (true when there was one): for a screen that runs
    // another inside it (the sweep).
    bool take_pop() { return pops_ > 0 ? (--pops_, true) : false; }
    // A picture of this frame saved to `path` once it is drawn.
    void save_picture(std::string path) { pictures_.push_back(std::move(path)); }
    // The process's resident memory in MB (0 where it is not read).
    static double resident_mb();
    void quit() { quit_ = true; }

    Disc* disc() { return disc_.get(); }
    gt3r::Vfs* vfs() { return vfs_.get(); }
    // The open disc's car database, read on first use; null when the disc has none (Prologue's is
    // read in Gran Turismo 4's layout: formats/db/specdb_prologue.h).
    db::SpecDb* db();
    gt3r::InputManager& input() { return *input_; }
    gt3r::AudioEngine& audio() { return *audio_; }
    // The open disc's sound (the menus' effects and music, the bank library the cars' sounds
    // use), made on first use; null without a disc.
    audio::AppSound* sound();
    gt3r::IniDocument& config() { return config_; }
    void save_config();
    const MenuInput& menu() const { return menu_; }
    // The pad the menus read this frame, its sticks too.
    const gt3r::PadState& menu_pad() const { return menu_pad_; }
    SDL_Renderer* renderer() { return renderer_; }
    // The 3D device; null when the system has none (the 2D screens still work).
    gt3r::render::GpuContext* gpu() { return gpu_.get(); }
    const std::string& data_dir() const { return data_dir_; }
    std::string mods_dir() const { return data_dir_ + "mods"; }
    const std::string& last_error() const { return last_error_; }
    int frame() const { return frame_; }
    // The time the frames have taken, for what moves by itself on a page: the machine's, but in pictures (--shot, --sweep) a 60th of
    // a second a frame.
    static Uint64 frame_time_ns() { return frame_time_ns_; }

    // The scripted keys' race controls held now ("gas=1", "steer=-0.4", ...: app.cpp), for tests.
    struct ScriptedDrive {
        float steer = 0, gas = 0, brake = 0, reverse = 0;
        bool handbrake = false;
        float sticks[4] = {}; // lx, ly, rx, ry ("lx=0.5")
        u16 held = 0;         // buttons held ("l1=1")
        bool any() const { return steer != 0 || gas > 0 || brake > 0 || reverse > 0 || handbrake; }
    };
    const ScriptedDrive& scripted_drive() const { return drive_; }

    // A script of --keys tokens (comma or line separated, '#' starts a comment): what a run does.
    static std::vector<std::string> parse_key_script(std::string_view text);

private:
    void apply_screen_changes();
    void save_screenshot(const std::string& path);
    gt3r::PadState menu_pad_; // pad()
    gt3r::PadState scripted_keys(); // this frame's --keys press, none when the script is done
    void write_stats(double frame_ms);
    // (app_settings.cpp)
    void layout_ui(bool game);
    void load_settings();
    void apply_frame_limit();
    void reset_dynamic_scale();
    void apply_sound();
    void begin_gamma_frame();
    void draw_fps(bool own_page);
    void idle_cursor(Uint64 now);
    void mouse_event(const SDL_Event& e);
    void use_texture_packs();
    bool pointer_token(const std::string& token); // --keys' mouse
    void push_dropped_mod(const std::string& path);

    settings::Video video_;
    settings::Gpu video_started_ = settings::Gpu::Automatic;
    Sound sound_settings_;
    settings::RunMode run_;
    bool scripted_ = false; // run_'s
    double frame_limit_ = 0;
    gt3r::render::DynamicScale dynamic_;
    bool dynamic_on_ = false;
    std::unique_ptr<gt3r::render::GammaPass> gamma_pass_;
    bool gamma_frame_ = false;
    int fps_frames_ = 0;
    double fps_time_ = 0, fps_shown_ = 0;
    Uint64 fps_last_ = 0;
    Uint64 mouse_used_ns_ = 0;
    bool cursor_hidden_ = false, screenshot_wanted_ = false;
    gt3r::PointerInput pointer_;
    std::vector<gt3r::PointerInput::Action> pointer_actions_, scripted_pointer_;
    std::string dropped_mod_;

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr; // the 3D device's when there is one
    std::unique_ptr<gt3r::render::GpuContext> gpu_;
    std::unique_ptr<Ui> ui_;
    std::unique_ptr<gt3r::InputManager> input_;
    std::unique_ptr<gt3r::AudioEngine> audio_;
    std::unique_ptr<Disc> disc_;
    std::unique_ptr<gt3r::Vfs> vfs_;
    std::unique_ptr<db::SpecDb> db_;
    std::unique_ptr<audio::AppSound> sound_; // (after audio_ and vfs_: gone before them)
    bool db_tried_ = false;
    gt3r::IniDocument config_;
    std::string data_dir_, config_path_;
    MenuInput menu_;
    std::vector<std::unique_ptr<Screen>> screens_, pending_;
    int pops_ = 0;
    bool quit_ = false;
    Uint64 last_ticks_ = 0;
    static inline Uint64 frame_time_ns_ = 1; // (never 0, which the pages take as not read yet)
    int frame_ = 0;

    SDL_Mutex* dialog_mutex_ = nullptr;
    std::string dialog_result_;
    std::string last_error_;

    std::string browse_;                // --browse: a file the browser opens at start
    std::string start_;                 // --screen cars | courses: the hub's item opened at start
    std::string shot_path_;             // --shot: a picture of one frame, then quit
    int shot_frame_ = 30;               // --shot-frame
    bool paced_ = true;                 // --shot's frames held to 60 a second (--fast: as fast as they come)
    std::vector<std::string> keys_;     // --keys: scripted menu presses (tests, pictures)
    size_t key_ = 0;
    int key_frames_ = 0;
    std::string key_shot_;              // a "shot:<name>" key reached: saved beside --shot after this frame
    ScriptedDrive drive_;               // the race controls the script holds
    std::string stats_path_;            // --stats: frame times and memory, a line a second
    double stats_ms_ = 0, stats_max_ = 0, stats_wait_ms_ = 0;
    double gpu_wait_ms_ = 0;            // a windowless run's last frame: the wait for what it drew
    int stats_frames_ = 0;
    Uint64 start_ticks_ = 0, end_ticks_ = 0; // the launch, the last frame's end
    std::vector<std::string> pictures_; // save_picture's, saved after this frame's drawing
    std::string sweep_, sweep_out_;     // --sweep <list>, --sweep-out <dir>
};

// The folder of the config file and mods: GT4R_DATA_DIR, else the executable's folder when it
// has a portable.ini, else the system's per-user folder. Ends with a separator.
std::string user_data_dir();

std::unique_ptr<Screen> make_setup_screen();
std::unique_ptr<Screen> make_hub_screen();
// --sweep: the races of a list run one after another in this window, a picture of each
// (screen_sweep.cpp).
std::unique_ptr<Screen> make_sweep_screen(const std::string& list, const std::string& out);
// The player's controls: the device, the race's layout, rumble and force feedback, the bindings.
std::unique_ptr<Screen> make_controls_screen(App& app, int player = 0);
// The app's settings (screen_settings.cpp), from a page on (0 Display, 1 Audio, 2 Controls, 3 Disc & saves, 4 Mods).
std::unique_ptr<Screen> make_settings_screen(App& app, int page = 0, const std::string& install = {});
std::unique_ptr<Screen> make_browser_screen(const std::string& open_path = "");
// The menu projects' pages drawn from the disc's layouts (still pictures).
std::unique_ptr<Screen> make_menu_viewer(App& app);
// The game itself: the menus running from power on, over the whole window, as a player sees
// them (screen_menu_viewer.cpp). Select and Start together leave it.
std::unique_ptr<Screen> make_game_screen(App& app);
// A list of models and a 3D view of the selected one; `load` builds the model of an item.
using ModelLoader = std::function<gt3r::scene::Model(App& app, size_t index)>;
std::unique_ptr<Screen> make_model_viewer(std::string title, std::vector<std::string> items, ModelLoader load);
// Drive: a car on a course (screen_race.cpp). Throws FormatError when the disc's lists are not read.
std::unique_ptr<Screen> make_race_screen(App& app);
// The same for a race already chosen (the menus' request): no lists, and leaving it returns to
// the screen below.
namespace menu {
class Runtime;
struct RaceRequest;
}

struct RaceStart {
    std::string course; // a course file's name ("tsukuba")
    std::string car;    // a car's label
    int paint = 0;      // the car's paint by index
    int cars = 6;       // on the grid
    int laps = 3;
    // The running menus the race came from (null: none) and the race sequence they asked for
    // ("single_race"): the race runs that sequence's own menus on them and leaves them where it ends.
    menu::Runtime* menus = nullptr;
    std::string sequence;
};
std::unique_ptr<Screen> make_race_screen(App& app, const RaceStart& start);
// The photo shoot the menus ask for (the sequence "photo_shoot"): the location and the car of the running menus' request; it runs the
// project photo_shoot on them and leaves them where that project ends (screen_photo.cpp).
std::unique_ptr<Screen> make_photo_screen(App& app, menu::Runtime& menus, const menu::RaceRequest& request);

} // namespace gt4r
