#pragma once

// How the app was started: played, or run by a script (pictures, keys, sweeps, measurements). A scripted run shows nothing: no
// window, no Dock icon, no focus taken, none of the machine's real input read, no sound played. --visible (GT4R_VISIBLE=1) puts
// its window on the screen, --headless (GT4R_HEADLESS=1) hides a run that would have shown one.

#include <SDL3/SDL_events.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::settings {

struct RunMode {
    bool pictures = false;   // --shot, --sweep: the frames do not go by the clock
    bool windowless = false; // nothing on the screen
    bool scripted = false;   // pictures or windowless: opens nothing on the screen, reads no real input
    bool recording = false;  // --record: a video of the run (pictures: frames a 60th of a second each; no sound device opened)
    // The frames a second a windowless run that goes by the clock is held to, as the display would hold it (--pace: a number,
    // "display" for the display's own rate = -1, "off" = 0).
    double pace = 0;

    static constexpr double kPace = 60, kPaceDisplay = -1;

    // `args` without the program's own name; `env` is a variable's value, null when it is not set.
    static RunMode of(const std::vector<std::string>& args, const std::function<const char*(const char*)>& env) {
        RunMode m;
        const auto on = [&env](const char* name) {
            const char* v = env(name);
            return v && *v && std::string_view(v) != "0";
        };
        bool script = false;
        for (const char* name : {"GT4R_MENU", "GT4R_RACE", "GT4R_VIEWER"}) script = script || on(name);
        int hidden = on("GT4R_VISIBLE") ? 0 : on("GT4R_HEADLESS") ? 1 : -1; // (-1: as the run is)
        double pace = kPace;
        for (size_t i = 0; i < args.size(); ++i) {
            const std::string_view a = args[i];
            if (a == "--headless") hidden = 1;
            else if (a == "--visible") hidden = 0;
            if (i + 1 >= args.size()) break;
            if (a == "--shot" || a == "--sweep") m.pictures = script = true, ++i;
            else if (a == "--record") m.pictures = m.recording = script = true, ++i;
            else if (a == "--keys" || a == "--stats") script = true, ++i;
            else if (a == "--pace") {
                const std::string& v = args[++i];
                pace = v == "display" ? kPaceDisplay : v == "off" ? 0 : std::atof(v.c_str());
                if (pace < 0 && pace != kPaceDisplay) pace = 0;
            } else if (a == "--browse" || a == "--screen" || a == "--shot-frame" || a == "--sweep-out" || a == "--record-frames") ++i;
        }
        m.windowless = hidden < 0 ? script : hidden == 1;
        m.scripted = m.pictures || m.windowless;
        m.pace = m.windowless && !m.pictures ? pace : 0;
        return m;
    }
};

// The events a scripted run does not take from the system: the keyboard's, the mouse's, a touch screen's, a pen's, a controller's, the
// clipboard's, what is dropped on the window, a change of focus.
inline bool is_real_input(Uint32 type) {
    switch (type) {
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST: return true;
    default: break;
    }
    const auto in = [type](Uint32 first, Uint32 last) { return type >= first && type <= last; };
    return in(SDL_EVENT_KEY_DOWN, SDL_EVENT_CLIPBOARD_UPDATE) || in(SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_POSITION) ||
           in(SDL_EVENT_PEN_PROXIMITY_IN, SDL_EVENT_PEN_AXIS);
}

} // namespace gt4r::settings
