#include "gt4r/net/presence_text.h"

#include <algorithm>

namespace gt4r::net {

namespace {

std::string ordinal(int n) {
    const int t = n % 100;
    const char* suffix = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

} // namespace

PresenceText race_presence(const RacePresence& r) {
    const std::string& q = r.sequence;
    std::string kind;
    bool watching = false;
    if (r.online) kind = "Multi-LAN Race";
    else if (r.replay || q == "replay") kind = "Watching a replay", watching = true;
    else if (r.demonstration) kind = "Watching a demonstration", watching = true;
    else if (q == "race_view") kind = "Watching an event preview", watching = true;
    else if (q == "license" || q == "license_race") kind = "License test";
    else if (q == "mission") kind = "Mission race";
    else if (q == "championship") kind = "Championship race";
    else if (q == "single_race") kind = "Arcade race";
    else if (q == "time_trial") kind = "Time trial";
    else if (q == "freerun") kind = "Free run";
    else if (q == "practice") kind = "Practice";
    else if (q == "machinetest") kind = "Machine test";
    else if (q == "photo_drive") kind = "Photo Travel";
    else if (!q.empty()) kind = "Gran Turismo mode race";
    else kind = "Driving";
    // (the database writes an apostrophe as a backquote: "Ford Ka `01", as the save editor reads it)
    const auto shown = [](std::string s) {
        std::replace(s.begin(), s.end(), '`', '\'');
        return s;
    };
    PresenceText t;
    t.details = r.course.empty() ? kind : kind + " · " + shown(r.course);
    std::string state = shown(r.car);
    const auto add = [&state](const std::string& s) { state += (state.empty() ? "" : " · ") + s; };
    if (!watching) {
        if (r.finished) add(r.cars > 1 && r.place > 0 ? "Finished " + ordinal(r.place) : "Finished");
        else if (r.lap > 0) {
            add(r.laps > 0 ? "Lap " + std::to_string(std::min(r.lap, r.laps)) + "/" + std::to_string(r.laps) : "Lap " + std::to_string(r.lap));
            if (r.cars > 1 && r.place > 0) add(ordinal(r.place) + " of " + std::to_string(r.cars));
        }
    }
    t.state = state;
    if (r.online) t.party_size = r.players, t.party_max = 6; // (six players at most: the pregrid's slots)
    return t;
}

PresenceText menu_presence(const std::string& project, const std::string& page) {
    PresenceText t;
    if (project == "boot") t.details = "Starting the game";
    else if (project == "arcade") t.details = page == "TopRoot" ? "Title menu" : page == "MatchingRoot" || page == "MonitorRoot" ? "Multi-LAN Race" : "Arcade";
    else if (project == "gtmode" || project == "event" || project == "ranking") t.details = "Gran Turismo mode";
    else if (project == "setting") t.details = "Car settings";
    else if (project == "option" || project == "option2" || project == "option3") t.details = "Options";
    else if (project == "labomode") t.details = page == "ReplayTheaterRoot" ? "Replay Theater" : "Photo Lab";
    else if (project == "photo_shoot") t.details = "Photo Travel";
    else if (project == "photo_save" || project == "print") t.details = "Photo Lab";
    else if (project == "memcard") t.details = "Saving and loading";
    else if (project == "network" || project == "online") t.details = "Multi-LAN Race";
    else if (!project.empty()) t.details = "In the menus";
    return t;
}

} // namespace gt4r::net
