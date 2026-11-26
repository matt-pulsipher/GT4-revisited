#pragma once

// Discord status: "Playing GT4 Revisited" on the player's Discord profile with what they are doing ("Arcade race · Tsukuba
// Circuit", "Honda Civic Type R · Lap 2/3 · 1st of 6").
//
// Off unless the player turns it on (Settings > Disc & saves > Show what you're playing: config.ini [discord] status = on). The
// text comes from the top screen (Screen::presence; the screens below answer when it does not), asked about once a second.
// The elapsed time is the session's: from when the status starts (the launch, or turning it on) until another disc is opened.
// The engine's own client (platform/discord_presence.h) talks only to the Discord app on the same computer; scripted runs leave
// Discord alone unless GT4R_DISCORD=1.
//
// [discord] app_id: the Discord application to show as (the default is kDiscordAppId).

#include "gt4r/net/presence_text.h"
#include "platform/discord_presence.h"

#include <string>

namespace gt4r {

class App;

// GT4 Revisited's Discord application (its name is the "Playing ..." line, its "logo" art asset the picture). ❓ Not made yet:
// empty, so the status stays off until it is filled in or config.ini names one ([discord] app_id).
inline constexpr const char* kDiscordAppId = "";

using net::PresenceText;

class Presence {
public:
    void startup(App& app, bool allowed); // `allowed` false: never connects (scripted runs)
    void frame(App& app);                 // every frame: asks the screens about once a second
    void shutdown();                      // clears the status and disconnects

    static bool supported() { return gt3r::platform::DiscordPresence::supported(); }
    static bool enabled(App& app); // [discord] status (default off)
    void set_enabled(App& app, bool on);
    static std::string app_id(App& app); // [discord] app_id, else kDiscordAppId ("" none)
    std::string status_text(App& app) const; // one line for Settings

    // The activity a text makes (tests).
    static gt3r::platform::DiscordActivity activity(const PresenceText& t, long long since);

private:
    void apply(App& app);

    gt3r::platform::DiscordPresence client_;
    bool allowed_ = false;
    double next_ask_ = 0; // seconds (SDL ticks)
    std::string disc_;    // the disc the clock started for ("" none)
    long long since_ = 0; // Unix seconds: the session's start
};

Presence& discord_status(); // the process-wide one

// A course's name as the disc's database gives it ("Tsukuba Circuit"; the label when it has none). `app`'s open disc.
std::string course_title(App& app, const std::string& label);

} // namespace gt4r
