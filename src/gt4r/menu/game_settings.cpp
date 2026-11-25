#include "gt4r/menu/game_settings.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace gt4r::menu {

namespace {

struct Reader {
    const std::map<std::string, OptionValue>& o;
    int number(const char* name, int fallback) const {
        const auto it = o.find(name);
        if (it == o.end()) return fallback;
        if (const s32* i = std::get_if<s32>(&it->second)) return *i;
        if (const float* f = std::get_if<float>(&it->second)) return int(*f);
        return fallback;
    }
    std::string text(const char* name, const char* fallback) const {
        const auto it = o.find(name);
        if (it == o.end()) return fallback;
        if (const std::string* s = std::get_if<std::string>(&it->second)) return *s;
        return fallback;
    }
    bool flag(const char* name, bool fallback) const { return number(name, fallback ? 1 : 0) != 0; }
    // An on/off option and its raw volume 0..128 as a gain.
    float gain(const char* on, const char* raw) const { return flag(on, true) ? float(std::clamp(number(raw, 115), 0, 128)) / 128.0f : 0.0f; }
};

} // namespace

std::array<int, 4> picture_from_text(const std::string& text, std::array<int, 4> fallback) {
    std::istringstream in(text);
    std::array<int, 4> v{};
    for (int& x : v)
        if (!(in >> x)) return fallback;
    for (int& x : v) x = std::clamp(x, -5, 5);
    return v;
}

std::string picture_text(const std::array<int, 4>& v) {
    char b[48];
    std::snprintf(b, sizeof b, "%d %d %d %d", v[0], v[1], v[2], v[3]);
    return b;
}

GameSettings settings_from(const std::map<std::string, OptionValue>& options) {
    const Reader r{options};
    GameSettings s;
    s.menu = {r.gain("menu_bgm", "menu_bgm_volume_raw"), r.gain("menu_se", "menu_se_volume_raw")};
    s.race = {r.gain("race_bgm", "race_bgm_volume_raw"), r.gain("race_se", "race_se_volume_raw")};
    s.replay = {r.gain("replay_bgm", "replay_bgm_volume_raw"), r.gain("replay_se", "replay_se_volume_raw")};
    s.slide_music = float(std::clamp(r.number("slide_bgm_volume_raw", 115), 0, 128)) / 128.0f;
    s.sound_type = r.text("sound_type", "STEREO");
    s.narration = r.flag("narration", true);

    s.race_picture = picture_from_text(r.text("race_color_correction", ""), s.race_picture);
    s.replay_picture = picture_from_text(r.text("replay_color_correction", ""), s.replay_picture);
    s.screen_adjust_h = r.number("screen_adjust_h", 0), s.screen_adjust_v = r.number("screen_adjust_v", 0);
    s.wide = r.flag("wide_mode", false);
    s.sharpness = r.text("sharpness_mode", "MILD");

    s.velocity = velocity_unit_of(r.text("unit_velocity", "KMPH"));
    s.power = power_unit_of(r.text("unit_power", "PS"));
    s.torque = torque_unit_of(r.text("unit_torque", "KGFM"));

    s.automatic = r.flag("automatic_gear", true);
    s.asm_on = r.flag("assist_asm", true), s.tcs_on = r.flag("assist_tcs", true);
    s.license_manual = r.flag("license_transmission_MT", false);
    s.steering_assist = r.text("steering_assist_1p", "AMATEUR");
    s.active_steering = r.flag("active_steering_1p", false);
    s.controllers.port = {input::port_from_text(r.text("controllers_1p", "")), input::port_from_text(r.text("controllers_2p", ""))};
    s.laps = std::max(1, r.number("race_laps", 2));
    s.tire_damage = r.flag("tire_damage", false);
    s.penalty = r.text("penalty_type", "NONE");
    s.view = r.text("view_type", "SUBJECTIVE");

    GameSettings::Display& d = s.display;
    d.map = r.flag("race_display_map", d.map), d.steering = r.flag("race_display_steering", d.steering);
    d.meter = r.flag("race_display_meter", d.meter), d.time = r.flag("race_display_time", d.time), d.lap = r.flag("race_display_lap", d.lap);
    d.position = r.flag("race_display_position", d.position), d.car_name = r.flag("race_display_car_name", d.car_name);
    d.suggested_gear = r.flag("race_display_suggested_gear", d.suggested_gear), d.g_meter = r.flag("race_display_g_meter", d.g_meter);
    s.replay_display = r.flag("replay_display_enable", false);
    s.replay_mode = r.text("replay_mode", "NORMAL");
    s.replay_shuffle = r.text("replay_play_mode", "STRAIGHT") == "SHUFFLE";

    s.demo_after_s = r.number("timeout_count_to_autodemo", 20), s.top_after_s = r.number("timeout_count_to_topmenu", 600);
    s.demo_movie_interval = r.number("demo_movie_interval", 3);
    s.opening_after_autoload = r.flag("opening_after_autoload", true), s.auto_save = r.flag("auto_save", true);
    return s;
}

} // namespace gt4r::menu
