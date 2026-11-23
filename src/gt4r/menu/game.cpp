#include "gt4r/menu/game.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace gt4r::menu {

namespace {

struct Default {
    const char* name;
    OptionValue value;
};

// MOption's attributes (the names are the program's registration table at 0x189E38) with the
// values a new game starts from. Several are names, the ones the option pages' scripts list
// ("NONE", "STEREO", "MPH", ...). ❓ The values are not read from the program's own defaults
// yet (MOption::setDefault, 0x189A60): they are plain choices that take the scripts down the
// retail game's paths (no show build, no arcade machine).
const Default kDefaults[] = {
    {"game_zone", std::string("us")}, {"language", std::string("US")}, {"wide_mode", 0}, {"race_display_mode", std::string("normal")},
    {"screen_adjust_h", 0}, {"screen_adjust_v", 0}, {"monitor_size", std::string("normal")}, {"race_color_correction", 0}, {"replay_color_correction", 0},
    {"physical_monitor_size", 0}, {"sharpness_mode", 0},
    {"menu_bgm", 1}, {"menu_bgm_volume", 100}, {"menu_bgm_volume_raw", 100}, {"menu_se", 1}, {"menu_se_volume", 100}, {"menu_se_volume_raw", 100},
    {"race_bgm", 1}, {"race_bgm_volume", 100}, {"race_bgm_volume_raw", 100}, {"race_se", 1}, {"race_se_volume", 100}, {"race_se_volume_raw", 100},
    {"replay_bgm", 1}, {"replay_bgm_volume", 100}, {"replay_bgm_volume_raw", 100}, {"replay_se", 1}, {"replay_se_volume", 100},
    {"replay_se_volume_raw", 100}, {"slide_bgm_volume", 100}, {"slide_bgm_volume_raw", 100}, {"sound_type", std::string("STEREO")}, {"narration", 1}, {"enableAC3", 0},
    {"auto_save", 0}, {"race_laps", 2}, {"tire_damage", 0}, {"automatic_gear", 1}, {"automatic_ghost", 1}, {"assist_asm", 1}, {"assist_tcs", 1},
    {"assist", 1}, {"difficulty", 0}, {"run_with_enemies", 1}, {"license_transmission_MT", 0}, {"penalty_type", std::string("NONE")},
    {"limit_favorite_car_only", 0}, {"limit_favorite_course_only", 0}, {"limit_enemies_to_favorite_car_only", 0},
    {"view_type", 0}, {"view_type_1p", 0}, {"view_type_2p", 0}, {"split_race_laps", 2}, {"split_tire_damage", 0}, {"split_handicap", 0},
    {"split_boost_level", 0}, {"photo_quality", std::string("STANDARD")}, {"photo_manual_focus", 0}, {"photo_aspect", 0}, {"photo_shutter", 0}, {"photo_professional", 0},
    {"photo_saturation", 0}, {"photo_save_slot", 0}, {"photo_save_method", 0},
    {"vibration_1p", 1}, {"vibration_2p", 1}, {"steering_assist_1p", std::string("AMATEUR")}, {"steering_assist_2p", std::string("AMATEUR")}, {"active_steering_1p", 0},
    {"active_steering_2p", 0}, {"steering_forcefeedback_1p", 0}, {"steering_forcefeedback_2p", 0},
    {"replay_mode", std::string("NORMAL")}, {"replay_play_mode", std::string("STRAIGHT")}, {"replay_display_enable", 1}, {"race_display_config", 0}, {"race_display_map", 1},
    {"race_display_steering", 1}, {"race_display_meter", 1}, {"race_display_time", 1}, {"race_display_lap", 1}, {"race_display_position", 1},
    {"race_display_car_name", 1}, {"race_display_suggested_gear", 1}, {"race_display_g_meter", 0}, {"replay_2p_split", 0},
    {"replay_memory_card_slot", 0}, {"album_slide_effect", 0}, {"album_slide_play_time", 5}, {"album_slide_transition_time", 1},
    {"album_memory_card_slot", 0}, {"ustorage_photo_quality", std::string("STANDARD")},
    // No show timeouts, no arcade machine, the full car and course lists.
    {"timeout_count_to_autodemo", 0}, {"timeout_count_to_topmenu", 0}, {"machine_role", std::string()}, {"special_arcade_tuner", std::string()},
    {"arcade_skip_to_favorite", 0}, {"opening_after_autoload", 0}, {"can_watch_ending", 0}, {"demo_movie_interval", 0},
    {"netconf", std::string()}, {"LAN_machine_name", std::string()}, {"LAN_style", std::string("play")}, {"LAN_target_name", std::string()}, {"LAN_live_target", 0},
    {"LAN_monitor_num", 1}, {"LAN_monitor_position_x", 0}, {"LAN_monitor_position_y", 0}, {"LAN_delay_buffer_size", 0}, {"LAN_host", 0},
    {"LAN_heavy", 0}, {"LAN_player_num", 0}, {"LAN_auto_connect", 0}, {"LAN_auto_machine_num", 0}, {"LAN_auto_machine_idx", 0},
    {"event_name", std::string()}, {"arcade_time_limit", 0}, {"default_language", std::string("US")}, {"unit_velocity", std::string("KMPH")}, {"unit_power", std::string("PS")},
    {"unit_torque", std::string("KGFM")},
};

} // namespace

void GameState::option_defaults(const std::string& region) {
    options.clear();
    for (const Default& d : kDefaults) options[d.name] = d.value;
    options["game_zone"] = region;
    // Miles and horsepower where the game is sold with them (the option pages' own names).
    const bool imperial = region == "us" || region == "uk";
    options["unit_velocity"] = std::string(imperial ? "MPH" : "KMPH");
    options["unit_power"] = std::string(region == "us" ? "HP" : region == "uk" ? "BHP" : "PS");
    options["unit_torque"] = std::string(region == "us" || region == "uk" ? "FTLB" : "KGFM");
}

s32 GameState::option_int(const std::string& name, s32 fallback) const {
    const auto it = options.find(name);
    if (it == options.end()) return fallback;
    if (const s32* i = std::get_if<s32>(&it->second)) return *i;
    if (const float* f = std::get_if<float>(&it->second)) return s32(*f);
    return fallback;
}

std::string GameState::option_text(const std::string& name) const {
    const auto it = options.find(name);
    if (it == options.end()) return {};
    if (const std::string* s = std::get_if<std::string>(&it->second)) return *s;
    if (const s32* i = std::get_if<s32>(&it->second)) return std::to_string(*i);
    char b[32];
    std::snprintf(b, sizeof b, "%g", double(std::get<float>(it->second)));
    return b;
}

GameState::Unit GameState::velocity_unit() const { return option_text("unit_velocity") == "MPH" ? Unit{0.621371f, "mph"} : Unit{1.0f, "km/h"}; }

GameState::Unit GameState::power_unit() const {
    const std::string u = option_text("unit_power");
    if (u == "HP") return {0.98632f, "HP"};
    if (u == "BHP") return {0.98632f, "BHP"};
    if (u == "KW") return {0.73550f, "kW"};
    if (u == "CH") return {1.0f, "ch"};
    if (u == "CV") return {1.0f, "CV"};
    if (u == "PF") return {1.0f, "PF"};
    return {1.0f, "ps"};
}

GameState::Unit GameState::torque_unit() const {
    const std::string u = option_text("unit_torque");
    if (u == "FTLB") return {7.23301f, "ft.lb"};
    if (u == "NM") return {9.80665f, "N.m"};
    return {1.0f, "kgf.m"};
}

// "name = 12", "name = 0.5", "name = \"text\"": a line each, in name order.
std::string GameState::options_text() const {
    std::string out;
    for (const auto& [name, value] : options) {
        out += name + " = ";
        if (const std::string* s = std::get_if<std::string>(&value)) {
            out += '"';
            for (char c : *s) {
                if (c == '"' || c == '\\') out += '\\';
                if (c != '\n' && c != '\r') out += c;
            }
            out += '"';
        } else if (const s32* i = std::get_if<s32>(&value)) out += std::to_string(*i);
        else {
            char b[32];
            std::snprintf(b, sizeof b, "%.9g", double(std::get<float>(value)));
            out += b;
            if (out.find_first_of(".e", out.size() - std::char_traits<char>::length(b)) == std::string::npos) out += ".0";
        }
        out += '\n';
    }
    return out;
}

void GameState::options_from_text(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        const std::string name = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (name.empty() || value.empty()) continue;
        if (value[0] == '"') {
            std::string s;
            for (size_t i = 1; i < value.size() && value[i] != '"'; ++i) {
                if (value[i] == '\\' && i + 1 < value.size()) ++i;
                s += value[i];
            }
            options[name] = s;
        } else if (value.find_first_of(".eE") != std::string::npos) options[name] = std::strtof(value.c_str(), nullptr);
        else options[name] = s32(std::strtol(value.c_str(), nullptr, 10));
    }
}

} // namespace gt4r::menu
