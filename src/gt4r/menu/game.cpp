#include "gt4r/menu/game.h"

#include "gt4r/sim/equipment.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace gt4r::menu {

namespace {

struct Default {
    const char* name;
    OptionValue value;
};

// MOption's attributes (the names are the program's registration table at 0x189E38) with the
// values a new game starts from. The numbers are the program's constructors' (the option
// block of a new game's saved data, checked against the running game after a new game:
// volumes 115, the title's demo after 20 and the top menu after 600, ...). Several are names,
// the ones the option pages' scripts list ("NONE", "STEREO", "MPH", ...), saved by their numbers
// in the program's tables of names (save/game_data.cpp); a new game's are the constructors'.
const Default kDefaults[] = {
    {"game_zone", std::string("US")}, {"language", std::string("US")}, {"wide_mode", 0}, {"race_display_mode", std::string("normal")},
    {"screen_adjust_h", 0}, {"screen_adjust_v", 0}, {"monitor_size", std::string("normal")}, {"race_color_correction", std::string("3 0 0 0")}, {"replay_color_correction", std::string("0 0 0 0")},
    {"physical_monitor_size", 0}, {"sharpness_mode", std::string("MILD")},
    {"menu_bgm", 1}, {"menu_bgm_volume", 115}, {"menu_bgm_volume_raw", 115}, {"menu_se", 1}, {"menu_se_volume", 115}, {"menu_se_volume_raw", 115},
    {"race_bgm", 1}, {"race_bgm_volume", 115}, {"race_bgm_volume_raw", 115}, {"race_se", 1}, {"race_se_volume", 115}, {"race_se_volume_raw", 115},
    {"replay_bgm", 1}, {"replay_bgm_volume", 115}, {"replay_bgm_volume_raw", 115}, {"replay_se", 1}, {"replay_se_volume", 115},
    {"replay_se_volume_raw", 115}, {"slide_bgm_volume", 115}, {"slide_bgm_volume_raw", 115}, {"sound_type", std::string("STEREO")}, {"narration", 1}, {"enableAC3", 0},
    {"auto_save", 1}, {"race_laps", 2}, {"tire_damage", 0}, {"automatic_gear", 1}, {"automatic_ghost", 1}, {"assist_asm", 1}, {"assist_tcs", 1},
    {"assist", 1}, {"difficulty", 0}, {"run_with_enemies", 0}, {"license_transmission_MT", 0}, {"penalty_type", std::string("NONE")},
    {"limit_favorite_car_only", 0}, {"limit_favorite_course_only", 0}, {"limit_enemies_to_favorite_car_only", 0},
    {"view_type", std::string("SUBJECTIVE")}, {"view_type_1p", 0}, {"view_type_2p", std::string("SUBJECTIVE")}, {"split_race_laps", 2}, {"split_tire_damage", 0}, {"split_handicap", 0},
    {"split_boost_level", 1}, {"photo_quality", std::string("STANDARD")}, {"photo_manual_focus", 0}, {"photo_aspect", std::string("4x3")}, {"photo_shutter", std::string("typeA")}, {"photo_professional", 0},
    {"photo_saturation", 0}, {"photo_save_slot", 0}, {"photo_save_method", 2},
    {"controllers_1p", std::string()}, {"controllers_2p", std::string()}, {"vibration_1p", 1}, {"vibration_2p", 1}, {"steering_assist_1p", std::string("AMATEUR")}, {"steering_assist_2p", std::string("AMATEUR")}, {"active_steering_1p", 0},
    {"active_steering_2p", 0}, {"steering_forcefeedback_1p", 0}, {"steering_forcefeedback_2p", 0},
    {"replay_mode", std::string("NORMAL")}, {"replay_play_mode", std::string("STRAIGHT")}, {"replay_display_enable", 0}, {"race_display_config", 0}, {"race_display_map", 1},
    {"race_display_steering", 0}, {"race_display_meter", 1}, {"race_display_time", 1}, {"race_display_lap", 1}, {"race_display_position", 1},
    {"race_display_car_name", 1}, {"race_display_suggested_gear", 1}, {"race_display_g_meter", 1}, {"replay_2p_split", 1},
    {"replay_memory_card_slot", 0}, {"album_slide_effect", 1}, {"album_slide_play_time", 8}, {"album_slide_transition_time", 4},
    {"album_memory_card_slot", 0}, {"ustorage_photo_quality", std::string("SUPER_FINE")},
    {"timeout_count_to_autodemo", 20}, {"timeout_count_to_topmenu", 600}, {"machine_role", std::string()}, {"special_arcade_tuner", std::string()},
    {"arcade_skip_to_favorite", 0}, {"opening_after_autoload", 1}, {"can_watch_ending", 0}, {"demo_movie_interval", 3},
    {"netconf", std::string()}, {"LAN_machine_name", std::string("Player1")}, {"LAN_style", std::string("play")}, {"LAN_target_name", std::string("Player1")}, {"LAN_live_target", std::string("Player1")},
    {"LAN_monitor_num", 0}, {"LAN_monitor_position_x", 1}, {"LAN_monitor_position_y", 1}, {"LAN_delay_buffer_size", 2}, {"LAN_host", 0},
    {"LAN_heavy", 0}, {"LAN_player_num", 1}, {"LAN_auto_connect", 0}, {"LAN_auto_machine_num", 1}, {"LAN_auto_machine_idx", 0},
    {"event_name", std::string()}, {"arcade_time_limit", 0}, {"default_language", std::string("US")}, {"unit_velocity", std::string("KMPH")}, {"unit_power", std::string("PS")},
    {"unit_torque", std::string("KGFM")},
};

} // namespace

void GameState::option_defaults(const std::string& region) {
    options.clear();
    for (const Default& d : kDefaults) options[d.name] = d.value;
    // MOption::setGameZoneDefault (0x435D48): the zone by the program's region, by its name
    // (0 UNDEF, 1 JP, 2 US, 3 UK, 4 EU, 5 KR; table 0x622DA8).
    options["game_zone"] = std::string(region == "us" ? "US" : region == "uk" ? "EU" : region == "kr" ? "KR" : "JP");
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

GameState::Unit velocity_unit_of(const std::string& u) { return u == "MPH" ? GameState::Unit{0.621371f, "mph"} : GameState::Unit{1.0f, "km/h"}; }

GameState::Unit power_unit_of(const std::string& u) {
    // The factors as the program's conversions load them (0x4729A0's table at 0x6AD460: 0x4729D0 for HP and BHP, 0x4729E8
    // for kW; the torque's 0x472A08).
    if (u == "HP") return {std::bit_cast<float>(u32(0x3F7C76F2)), "HP"};
    if (u == "BHP") return {std::bit_cast<float>(u32(0x3F7C76F2)), "BHP"};
    if (u == "KW") return {std::bit_cast<float>(u32(0x3F3C49BA)), "kW"};
    if (u == "CH") return {1.0f, "ch"};
    if (u == "CV") return {1.0f, "CV"};
    if (u == "PF") return {1.0f, "PF"};
    return {1.0f, "PS"}; // (the program's text, 0x6AD370; Prologue's 0x578728)
}

GameState::Unit torque_unit_of(const std::string& u) {
    if (u == "FTLB") return {std::bit_cast<float>(u32(0x40E773EA)), "ft.lb"};
    if (u == "NM") return {std::bit_cast<float>(u32(0x411CCCCC)), "N.m"};
    return {1.0f, "kgf.m"};
}

GameState::Unit GameState::velocity_unit() const { return velocity_unit_of(option_text("unit_velocity")); }
GameState::Unit GameState::power_unit() const { return power_unit_of(option_text("unit_power")); }
GameState::Unit GameState::torque_unit() const { return torque_unit_of(option_text("unit_torque")); }

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

CarRecord::CarRecord() {
    // Three cleared equipment records (0x4442B8), no wheels, sheet 0, no wear.
    const sim::Equipment empty;
    for (int i = 0; i < 3; ++i) std::memcpy(sheet(i), empty.bytes.data(), kSheet);
    for (int c = 0; c < 4; ++c) set_wheel(c, -1);
    bytes[0x4AC] = 1;
}

float CarRecord::condition(size_t field) const {
    const u32 v = u32at(0x498 + 4 * field);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void CarRecord::set_condition(size_t field, float v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    put32(0x498 + 4 * field, u);
}

GameState::GameState() { new_game(); }

// 0x110800 (from the license sequence, 0x114E38): a run's day with its result, the note 14 and
// its time; a day with nothing in it when the result is 13 (no run, or failed).
void GameState::end_race_sequence() {
    if (race_session.license_open) {
        const DayEvent& d = race_session.license_day;
        put_day(d.type == 4 && d.b1 != 13 ? d : DayEvent{});
        race_session.license_open = false;
    }
    race_session.sequence.clear();
}

namespace {

void put32(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8), b[o + 2] = u8(v >> 16), b[o + 3] = u8(v >> 24); }

// A ranking's empty entries (the constructors 0x432368, 0x433AA0): no time, the date 0, the
// pass code "--------".
void fresh_license_unit(Bytes& b, size_t at) {
    for (size_t e = 0; e < 10; ++e) {
        const size_t o = at + e * 0x2C;
        put32(b, o, kNoTime);
        b[o + 8] = 15; // no result
        std::memset(b.data() + o + 9, '-', 8);
    }
    for (size_t i = 0; i < 16; ++i) put32(b, at + 0x1B8 + 4 * i, kNoTime);
}

void fresh_course_unit(Bytes& b, size_t at, bool speed) {
    std::memset(b.data() + at, 0xFF, 8); // no course
    for (size_t e = 0; e < 10; ++e) {
        const size_t o = at + 8 + e * 0x30;
        put32(b, o, speed ? 0 : kNoTime);
        std::memset(b.data() + o + 8, 0xFF, 8); // no car
        std::memset(b.data() + o + 0x10, '-', 8);
    }
    for (size_t i = 0; i < 16; ++i) put32(b, at + 0x1E8 + 4 * i, kNoTime);
}

} // namespace

void GameState::new_game() {
    // The play lists as the option's constructor leaves them (seeds 0x3355, 0x3476).
    bgm_playlist = {}, slide_playlist = {};
    bgm_playlist.seed = 0x3355, slide_playlist.seed = 0x3476;
    money = 0, username.clear(), password.clear(), last_entry_name.clear();
    score = 0, total_prize_money = 0, total_prize_cars = 0, rank_counts = {}, aspec_distance = 0, bspec_distance = 0, bspec_book = {};
    withdrawn = 0, withdrawn_gt4p = 0, met_type = 0;
    today = kFirstDay, diary.clear();
    garage.clear(), riding_car = -1, garage_key = 0, garage_serial = 0, garage_kept = {};
    race_units.fill(0x0000FFF0u); // type none, the three results none, score 0
    license_record.assign(kLicenseUnit * kLicenseUnits, 0);
    for (size_t u = 0; u < kLicenseUnits; ++u) fresh_license_unit(license_record, u * kLicenseUnit);
    course_record.assign(kCourseUnit * kCourseUnits, 0);
    for (size_t u = 0; u < kCourseUnits; ++u) fresh_course_unit(course_record, u * kCourseUnit, false);
    machine_record.assign(kCourseUnit * kMachineUnits, 0);
    for (size_t u = 0; u < kMachineUnits; ++u) fresh_course_unit(machine_record, u * kCourseUnit, u == 2); // ❓ unit 2: the top speed's
    available_cars.clear(), available_courses.clear(), all_available = false;
    favorite_cars.clear(), favorite_courses.clear();
    presents = {}, used_sold = {}, used_week = 0;
    suspended_racecode.clear(), suspended = 0, suspended_session = 0;
    championship_rest.assign(0x88 - 0x22, 0);
    saved_menu_depth = -1, saved_menu_stack.assign(0x100, 0);
    saved_image.clear();
}

void GameState::put_day(const DayEvent& e) {
    if (diary.size() >= kDiaryDays) diary.pop_front();
    diary.push_back(e);
    ++today;
    need_save = true;
}

int GameState::garage_count() const {
    int n = 0;
    for (const GarageEntry& e : garage) n += e.used;
    return n;
}

int GameState::add_car(GarageEntry e) {
    e.used = true;
    for (size_t i = 0; i < garage.size(); ++i)
        if (!garage[i].used) {
            garage[i] = std::move(e);
            return int(i);
        }
    if (garage.size() >= kGarageSize) return -1;
    garage.push_back(std::move(e));
    return int(garage.size()) - 1;
}

void GameState::suspend_series(bool keep) {
    suspended = keep;
    if (!keep) return;
    suspended_racecode = series.label;
    suspended_session = series.races_run;
}

std::optional<sim::Equipment> race_equipment(const GameState& game, int garage_slot) {
    if (garage_slot < 0 || size_t(garage_slot) >= game.garage.size()) return std::nullopt;
    const GarageEntry& e = game.garage[size_t(garage_slot)];
    if (!e.used || !e.record) return std::nullopt;
    sim::Equipment q;
    std::copy_n(e.record->active(), sim::Equipment::kSize, q.bytes.begin());
    return q;
}

std::optional<std::array<u8, 0x20>> race_condition(const GameState& game, int garage_slot) {
    if (garage_slot < 0 || size_t(garage_slot) >= game.garage.size()) return std::nullopt;
    const GarageEntry& e = game.garage[size_t(garage_slot)];
    if (!e.used || !e.record) return std::nullopt;
    std::array<u8, 0x20> c{};
    std::copy_n(e.record->bytes.begin() + 0x498, std::min<size_t>(0x20, e.record->bytes.size() - 0x498), c.begin());
    return c;
}

} // namespace gt4r::menu
