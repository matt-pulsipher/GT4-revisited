#include "gt4r/app/presence.h"

#include "gt4r/app/app.h"
#include "gt4r/net/version_check.h"

#include "base/config.h"

#include <SDL3/SDL.h>

#include <ctime>
#include <map>

namespace gt4r {

namespace {

constexpr const char* kLogo = "logo"; // the application's art asset: the app's icon
constexpr double kAskEvery = 1.0;     // seconds

} // namespace

Presence& discord_status() {
    static Presence p;
    return p;
}

bool Presence::enabled(App& app) { return gt3r::to_lower(app.config().get("discord", "status").value_or("off")) == "on"; }

std::string Presence::app_id(App& app) {
    const std::string id = std::string(gt3r::trim(app.config().get("discord", "app_id").value_or("")));
    return id.empty() ? std::string(kDiscordAppId) : id;
}

void Presence::set_enabled(App& app, bool on) {
    app.config().set("discord", "status", on ? "on" : "off");
    app.save_config();
    apply(app);
}

void Presence::startup(App& app, bool allowed) {
    allowed_ = allowed && supported();
    apply(app);
}

void Presence::apply(App& app) {
    const std::string id = app_id(app);
    if (allowed_ && enabled(app) && !id.empty()) {
        if (!client_.running()) { // the session's clock starts
            since_ = (long long)std::time(nullptr);
            disc_ = app.disc() ? app.disc()->info().key : "";
        }
        client_.start(id);
        next_ask_ = 0; // say what we are doing at once
    } else {
        client_.stop();
    }
}

void Presence::shutdown() { client_.stop(); }

gt3r::platform::DiscordActivity Presence::activity(const PresenceText& t, long long since) {
    gt3r::platform::DiscordActivity a;
    a.details = t.details.empty() ? "In the menus" : t.details;
    a.state = t.state;
    a.start = since;
    a.large_image = kLogo;
    a.large_text = "GT4 Revisited " + net::this_version().to_string();
    a.party_size = t.party_size;
    a.party_max = t.party_max;
    return a;
}

void Presence::frame(App& app) {
    if (!client_.running()) return;
    const double now = double(SDL_GetTicks()) / 1000.0;
    if (now < next_ask_) return;
    next_ask_ = now + kAskEvery;
    PresenceText t;
    if (!app.screen_presence(t)) t = {};
    // The clock is the session's; it starts again only with another disc, the nearest thing to starting another game.
    const std::string disc = app.disc() ? app.disc()->info().key : "";
    if (disc != disc_) disc_ = disc, since_ = (long long)std::time(nullptr);
    client_.set(activity(t, since_));
}

std::string Presence::status_text(App& app) const {
    if (!supported()) return "not on this computer";
    if (app_id(app).empty()) return "not available in this version";
    if (!client_.running()) return "off";
    return client_.status();
}

std::string course_title(App& app, const std::string& label) {
    static std::string disc;
    static std::map<std::string, std::string> names;
    const std::string key = app.disc() ? app.disc()->info().key : "";
    if (key != disc) {
        disc = key, names.clear();
        if (db::SpecDb* sdb = app.db()) try {
                for (const db::CourseEntry& c : sdb->courses())
                    if (!c.name.empty()) names[c.label] = c.name;
            } catch (const std::exception&) {
                // (the labels then)
            }
    }
    const auto it = names.find(label);
    return it == names.end() ? label : it->second;
}

} // namespace gt4r
