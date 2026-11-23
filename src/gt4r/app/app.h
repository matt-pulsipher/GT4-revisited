#pragma once

// The executable's shell: window, input, audio, the open disc and a stack of screens. It
// draws with the shared engine's Ui (640x360 units) and reads the menus through its virtual
// pad, as GT3 Revisited's own screens do.

#include "app/ui.h"
#include "audio/audio_engine.h"
#include "base/config.h"
#include "gt4r/disc/disc.h"
#include "input/input_manager.h"
#include "vfs/vfs.h"

#include <SDL3/SDL.h>

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
    gt3r::InputManager& input() { return *input_; }
    gt3r::AudioEngine& audio() { return *audio_; }
    gt3r::IniDocument& config() { return config_; }
    const MenuInput& menu() const { return menu_; }
    SDL_Renderer* renderer() { return renderer_; }
    const std::string& data_dir() const { return data_dir_; }
    std::string mods_dir() const { return data_dir_ + "mods"; }
    const std::string& last_error() const { return last_error_; }
    int frame() const { return frame_; }

private:
    void apply_screen_changes();
    void save_screenshot(const std::string& path);
    gt3r::PadState scripted_keys(); // this frame's --keys press, none when the script is done

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<Ui> ui_;
    std::unique_ptr<gt3r::InputManager> input_;
    std::unique_ptr<gt3r::AudioEngine> audio_;
    std::unique_ptr<Disc> disc_;
    std::unique_ptr<gt3r::Vfs> vfs_;
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
std::unique_ptr<Screen> make_browser_screen(const std::string& open_path = "");

} // namespace gt4r
