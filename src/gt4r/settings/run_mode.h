#pragma once

// How the app was started: played, or run by a script (pictures, keys, sweeps, measurements). A scripted run shows nothing: no
// window, no Dock icon, no focus taken, none of the machine's real input read, no sound played. --visible (GT4R_VISIBLE=1) puts
// its window on the screen, --headless (GT4R_HEADLESS=1) hides a run that would have shown one.

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
            else if (a == "--keys" || a == "--stats") script = true, ++i;
            else if (a == "--pace") {
                const std::string& v = args[++i];
                pace = v == "display" ? kPaceDisplay : v == "off" ? 0 : std::atof(v.c_str());
                if (pace < 0 && pace != kPaceDisplay) pace = 0;
            } else if (a == "--browse" || a == "--screen" || a == "--shot-frame" || a == "--sweep-out") ++i;
        }
        m.windowless = hidden < 0 ? script : hidden == 1;
        m.scripted = m.pictures || m.windowless;
        m.pace = m.windowless && !m.pictures ? pace : 0;
        return m;
    }
};

} // namespace gt4r::settings
