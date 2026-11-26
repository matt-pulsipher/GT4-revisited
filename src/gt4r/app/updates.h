#pragma once

// The update check and the update dialog (docs/specs/net.md "Versions and updates").
//
// The check (net/version_check.h): one small message to the relay server with this game's version, platform and game id; the
// answer is the latest release, the oldest version that may play online (min_online) and the release page. It runs
// - at startup when config.ini [online] update_check is not "off" and a server has been chosen ([online] server: none is
//   contacted before the player chooses one);
// - on Settings > Disc & saves > Check now (the chosen server, else the one the online page offers).
// The last answer is kept in [online] update_latest, update_min_online, update_url, update_checked, so a start without a network
// still knows. Scripted runs use only that unless GT4R_UPDATE_CHECK=1: they never reach a live server.
//
// The dialog (drawn by App over any screen, modal; the pad, the keyboard, the mouse):
// - "Update available" when a newer release exists; "Update required to play online" when this game is below min_online.
// - Download update (this platform's package), Later, and Release notes.
// - At startup it shows once per release, over the home screen or the game's title: Later remembers the release ([online]
//   update_dismissed). Settings opens it too.

#include "app/ui.h"
#include "gt4r/net/version_check.h"
#include "net/update_check.h"

#include <string>

namespace gt4r {

class App;

class Updates {
public:
    enum class State { Idle, Checking, Done, Failed };

    // Reads the last answer; checks when the setting is on, a server is chosen and `network` allows it.
    void startup(App& app, bool network = true);
    void check_now(App& app); // asked for (even with the startup check off)
    void poll(App& app);      // takes a finished answer (every frame)

    static bool enabled(App& app); // [online] update_check (default on)
    static void set_enabled(App& app, bool on);
    static std::string server(App& app); // the one asked: [online] server, else the offered one

    bool known() const { return latest_.known() || min_online_.known(); }
    bool available() const;
    bool required() const;
    gt3r::net::UpdatePrompt prompt() const;
    const net::GameVersion& latest() const { return latest_; }
    const net::GameVersion& min_online() const { return min_online_; }
    gt3r::net::UpdateLinks links() const; // this platform's package and the release page
    const std::string& checked() const { return checked_; }
    State state() const { return state_; }
    std::string status_text() const; // one line for Settings
    bool open_download() const;
    bool open_notes() const;

    // --- the dialog ---
    void open_dialog(); // the one that applies now (nothing when up to date)
    bool dialog_open() const { return dialog_.open; }
    // While true, App gives the dialog the menu input instead of the screen (also until the button that closed it is let go).
    bool modal() const { return dialog_.open || dialog_.wait_release; }
    // Every frame before the screens: shows the startup prompt when one is due and the top screen allows it.
    void frame(App& app, bool screen_allows);
    void dialog_update(App& app); // the menu input and the mouse (when modal())
    void dialog_render(gt3r::Ui& ui);
    std::string badge_text() const; // for the home screen ("" none)

    // Tests: an answer as if the relay had given it.
    void set_answer(App& app, const net::VersionAnswer& a);

private:
    void start(App& app, const std::string& server);
    void store(App& app);
    void update_due();
    void close_dialog(App& app, bool later);
    void activate(App& app, int item);

    net::VersionCheck check_;
    net::GameVersion latest_, min_online_, dismissed_, shown_;
    std::string url_, checked_, error_;
    State state_ = State::Idle;
    bool startup_due_ = false;
    struct Dialog {
        bool open = false, wait_release = false;
        gt3r::net::UpdatePrompt kind = gt3r::net::UpdatePrompt::None;
        int focus = 0; // 0 Download update, 1 Later, 2 Release notes
        gt3r::UiRect items[3];
    } dialog_;
};

Updates& updates(); // the process-wide one

} // namespace gt4r
