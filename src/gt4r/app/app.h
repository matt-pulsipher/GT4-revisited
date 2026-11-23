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
#include "input/input_manager.h"
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
    virtual void on_event(App&, const SDL_Event&) {}
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

    void push(std::unique_ptr<Screen> s) { pending_.push_back(std::move(s)); }
    void pop() { ++pops_; }
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
    const MenuInput& menu() const { return menu_; }
    SDL_Renderer* renderer() { return renderer_; }
    // The 3D device; null when the system has none (the 2D screens still work).
    gt3r::render::GpuContext* gpu() { return gpu_.get(); }
    const std::string& data_dir() const { return data_dir_; }
    std::string mods_dir() const { return data_dir_ + "mods"; }
    const std::string& last_error() const { return last_error_; }
    int frame() const { return frame_; }

private:
    void apply_screen_changes();
    void save_screenshot(const std::string& path);
    gt3r::PadState scripted_keys(); // this frame's --keys press, none when the script is done

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
    int frame_ = 0;

    SDL_Mutex* dialog_mutex_ = nullptr;
    std::string dialog_result_;
    std::string last_error_;

    std::string browse_;                // --browse: a file the browser opens at start
    std::string start_;                 // --screen cars | courses: the hub's item opened at start
    std::string shot_path_;             // --shot: a picture of one frame, then quit
    int shot_frame_ = 30;               // --shot-frame
    std::vector<std::string> keys_;     // --keys: scripted menu presses (tests, pictures)
    size_t key_ = 0;
    int key_frames_ = 0;
};

// The folder of the config file and mods: GT4R_DATA_DIR, else the executable's folder when it
// has a portable.ini, else the system's per-user folder. Ends with a separator.
std::string user_data_dir();

std::unique_ptr<Screen> make_setup_screen();
std::unique_ptr<Screen> make_hub_screen();
std::unique_ptr<Screen> make_browser_screen(const std::string& open_path = "");
// The menu projects' pages drawn from the disc's layouts (still pictures).
std::unique_ptr<Screen> make_menu_viewer(App& app);
// A list of models and a 3D view of the selected one; `load` builds the model of an item.
using ModelLoader = std::function<gt3r::scene::Model(App& app, size_t index)>;
std::unique_ptr<Screen> make_model_viewer(std::string title, std::vector<std::string> items, ModelLoader load);
// Drive: a car on a course (screen_race.cpp). Throws FormatError when the disc's lists are not read.
std::unique_ptr<Screen> make_race_screen(App& app);
// The same for a race already chosen (the menus' request): no lists, and leaving it returns to
// the screen below.
namespace menu {
class Runtime;
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

} // namespace gt4r
