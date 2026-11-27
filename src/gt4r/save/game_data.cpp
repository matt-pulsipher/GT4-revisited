#include "gt4r/save/game_data.h"

#include "gt4r/menu/game_settings.h"

#include "gt4r/save/codec.h"

#include <algorithm>
#include <cstring>

namespace gt4r::save {

using menu::CarRecord;
using menu::DayEvent;
using menu::GameState;
using menu::GarageEntry;

namespace {

// The blocks of the payload (save.md "The game data", the block map).
constexpr size_t kProfile = 0x10, kBspecBook = 0xA4, kDiaryCount = 0x230, kDiary = 0x234, kToday = 0xB8D4, kGarageList = 0xB8E0, kGarageTail = 0x135E0,
                 kRidingRecord = 0x135FC, kRaceUnits = 0x13AC0, kCourseRecord = 0x146C0, kMachineRecord = 0x262C0, kLicenseRecord = 0x26BA0,
                 kAvailable = 0x361C0, kFavoriteCars = 0x385D0, kFavoriteCourses = 0x386E0, kPresents = 0x387F0, kChampionship = 0x38870,
                 kUsedCar = 0x38900, kOptionHead = 0x38930, kGameZone = 0x39AF0, kControllers = 0x39B00, kLan = 0x39BC0, kNetwork = 0x39C6C,
                 kEventOptions = 0x39ECC, kLogger = 0x39F1C, kContext = 0x39F30;
// A payload offset of an Option field.
constexpr size_t option(size_t at) { return 0x38928 + at; }

u32 get32(ByteSpan b, size_t o) { return u32(b[o]) | u32(b[o + 1]) << 8 | u32(b[o + 2]) << 16 | u32(b[o + 3]) << 24; }
u64 get64(ByteSpan b, size_t o) { return u64(get32(b, o)) | u64(get32(b, o + 4)) << 32; }
void put32(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8), b[o + 2] = u8(v >> 16), b[o + 3] = u8(v >> 24); }
void put64(Bytes& b, size_t o, u64 v) { put32(b, o, u32(v)), put32(b, o + 4, u32(v >> 32)); }
void put_text(Bytes& b, size_t o, size_t n, const std::string& s) {
    std::memset(b.data() + o, 0, n);
    std::memcpy(b.data() + o, s.data(), std::min(s.size(), n - 1));
}
std::string get_text(ByteSpan b, size_t o, size_t n) {
    size_t len = 0;
    while (len < n && b[o + len]) ++len;
    return std::string(reinterpret_cast<const char*>(b.data() + o), len);
}

// The two ports' controller settings (Option +0x11D0, OptionRaceControllerPS2: input/key_config.h).
const std::pair<const char*, size_t> kControllerOptions[] = {{"controllers_1p", kControllers}, {"controllers_2p", kControllers + input::PortConfig::kSize}};

// The LAN settings (Option +0x1348 .. +0x13F3 as a block: the MOption natives' fields, 0x187750 on): two names, then numbers.
const std::pair<const char*, size_t> kLanNames[] = {{"LAN_machine_name", 0x00}, {"LAN_live_target", 0x44}};
const std::pair<const char*, size_t> kLanNumbers[] = {{"LAN_monitor_num", 0x84}, {"LAN_monitor_position_x", 0x88}, {"LAN_monitor_position_y", 0x8C}, {"LAN_delay_buffer_size", 0x90},
                                                      {"LAN_host", 0x94}, {"LAN_heavy", 0x98}, {"LAN_player_num", 0x9C}, {"LAN_auto_connect", 0xA0},
                                                      {"LAN_auto_machine_num", 0xA4}, {"LAN_auto_machine_idx", 0xA8}};

// The options that are plain numbers, by their place in the Option object (the MOption
// natives' fields; save.md "The options").
struct OptionField {
    const char* name;
    size_t at;
};
const OptionField kOptionNumbers[] = {
    {"wide_mode", 0x14}, {"screen_adjust_h", 0x18}, {"screen_adjust_v", 0x1C},
    {"menu_bgm", 0x5C}, {"menu_bgm_volume_raw", 0x60}, {"menu_se", 0x64}, {"menu_se_volume_raw", 0x68}, {"race_bgm", 0x6C}, {"race_bgm_volume_raw", 0x70},
    {"race_se", 0x74}, {"race_se_volume_raw", 0x78}, {"replay_bgm", 0x7C}, {"replay_bgm_volume_raw", 0x80}, {"replay_se", 0x84}, {"replay_se_volume_raw", 0x88},
    {"slide_bgm_volume_raw", 0x8C},
    {"narration", 0x10B4}, {"enableAC3", 0x10B8}, {"auto_save", 0x10BC}, {"race_laps", 0x10C0}, {"tire_damage", 0x10C4}, {"automatic_gear", 0x10C8},
    {"automatic_ghost", 0x10CC}, {"assist_asm", 0x10D0}, {"assist_tcs", 0x10D4}, {"difficulty", 0x10D8}, {"run_with_enemies", 0x10DC},
    {"license_transmission_MT", 0x10E0}, {"limit_favorite_car_only", 0x10E8}, {"limit_favorite_course_only", 0x10EC},
    {"limit_enemies_to_favorite_car_only", 0x10F0}, {"view_type_1p", 0x10F4}, {"view_type_2p", 0x10F8}, {"split_race_laps", 0x10FC},
    {"split_tire_damage", 0x1100}, {"split_handicap", 0x1104}, {"split_boost_level", 0x1108}, {"photo_manual_focus", 0x1110}, {"photo_aspect", 0x1114},
    {"photo_shutter", 0x1118}, {"photo_professional", 0x111C}, {"photo_saturation", 0x1120}, {"photo_save_slot", 0x1124}, {"photo_save_method", 0x1128},
    {"active_steering_1p", 0x1134}, {"active_steering_2p", 0x1138}, {"replay_2p_split", 0x1144}, {"replay_memory_card_slot", 0x1148},
    {"timeout_count_to_autodemo", 0x1150}, {"timeout_count_to_topmenu", 0x1154}, {"arcade_skip_to_favorite", 0x1160}, {"opening_after_autoload", 0x1164},
    {"can_watch_ending", 0x1168}, {"demo_movie_interval", 0x116C}, {"album_slide_effect", 0x1170}, {"album_slide_play_time", 0x1174},
    {"album_memory_card_slot", 0x1178}, {"album_slide_transition_time", 0x1180},
};
// The race display's parts: bits of the word at Option +0x1140 (the accessors 0x1851A8 on, the
// bit set by 0x437408).
const std::pair<const char*, u32> kDisplayBits[] = {
    {"replay_display_enable", 0x1}, {"race_display_map", 0x2}, {"race_display_steering", 0x4}, {"race_display_meter", 0x8},
    {"race_display_time", 0x10}, {"race_display_lap", 0x20}, {"race_display_position", 0x40}, {"race_display_car_name", 0x80},
    {"race_display_suggested_gear", 0x100}, {"race_display_g_meter", 0x200}, {"race_display_config", 0x400},
};
constexpr size_t kDisplayWord = 0x1140;
// The screen settings (brightness, contrast, saturation, color balance), four s32 each: the
// racing views' at Option +0x1184, the replays' at +0x1194 (the accessors 0x188EA0, 0x189158);
// kept in the options as text "b c s w".
const std::pair<const char*, size_t> kPictureSettings[] = {{"race_color_correction", 0x1184}, {"replay_color_correction", 0x1194}};
// The screen's figures (monitor_size's tables, 0x436D60): Option +0x24, 0x2C bytes.
constexpr size_t kMonitorParams = 0x24, kMonitorSize = 0x2C;
const char* const kZones[] = {"UNDEF", "JP", "US", "UK", "EU", "KR"};
// The options kept as names, by their numbers: the language (0x6AE3F8) at Option +0x0C, and the
// options MOption gives by name, each through its table of names (0x437758: the tables at
// 0x6A5998 on; the accessors 0x436810 on read the field and look its number up).
struct NamedOption {
    const char* name;
    size_t at;
    int bytes;
    std::vector<const char*> names;
};
const NamedOption kNamedOptions[] = {
    {"language", 0x0C, 4, {"JP", "US", "GB", "FR", "DE", "IT", "ES", "PT", "KR", "CN", "TW", "DB"}},
    {"unit_velocity", 0x10, 1, {"KMPH", "MPH"}},
    {"unit_power", 0x11, 1, {"PS", "HP", "BHP", "CH", "KW", "CV", "PF"}},
    {"unit_torque", 0x12, 1, {"KGFM", "FTLB", "NM"}},
    {"sharpness_mode", 0x20, 4, {"NORMAL", "MILD"}},
    {"sound_type", 0x10B0, 4, {"MONOPHONIC", "STEREO", "PROLOGIC2"}},
    {"penalty_type", 0x10E4, 4, {"NONE", "SPEED_LIMITATION", "PASSING_PITLANE"}},
    {"view_type", 0x10F4, 4, {"SUBJECTIVE", "BONNET", "OVERLOOK"}},
    {"view_type_2p", 0x10F8, 4, {"SUBJECTIVE", "BONNET", "OVERLOOK"}},
    {"photo_quality", 0x110C, 4, {"STANDARD", "FINE", "SUPER_FINE"}},
    {"photo_aspect", 0x1114, 4, {"4x3", "16x9", "3x2", "1x1"}},
    {"photo_shutter", 0x1118, 4, {"typeA", "typeB", "typeC", "typeD", "typeE", "typeF", "typeG", "typeH", "typeI", "typeJ", "typeK", "typeL", "typeM", "typeN", "typeO", "typeP", "typeQ", "typeR", "typeS"}},
    {"steering_assist_1p", 0x112C, 4, {"AMATEUR", "PROFESSIONAL", "SIMULATION"}},
    {"steering_assist_2p", 0x1130, 4, {"AMATEUR", "PROFESSIONAL", "SIMULATION"}},
    {"replay_mode", 0x113C, 4, {"NORMAL", "DIVE"}},
    {"replay_play_mode", 0x114C, 4, {"STRAIGHT", "SHUFFLE"}},
    {"ustorage_photo_quality", 0x117C, 4, {"STANDARD", "FINE", "SUPER_FINE"}},
};

// A play list's 128 rows of 16 bytes, each "nothing chosen" (00 01 FF FF, then zeros).
void fresh_playlist(Bytes& b, size_t rows) {
    for (size_t i = 0; i < 128; ++i) b[rows + 16 * i + 1] = 1, b[rows + 16 * i + 2] = 0xFF, b[rows + 16 * i + 3] = 0xFF;
}

void put_record(Bytes& b, size_t o, const CarRecord& r) { std::memcpy(b.data() + o, r.bytes.data(), CarRecord::kSize); }

// A sorted list of codes, the rest -1 (0x42E968).
void put_codes(Bytes& b, size_t o, size_t n, std::vector<u64> codes, bool sorted) {
    if (sorted) std::sort(codes.begin(), codes.end());
    for (size_t i = 0; i < n; ++i) put64(b, o + 8 * i, i < codes.size() ? codes[i] : ~u64(0));
}

} // namespace

Bytes playlist_block(const GameState::PlayList& list) {
    Bytes b(0x810);
    put32(b, 0, list.rows), b[4] = u8(list.position), b[5] = u8(list.position >> 8), b[6] = list.shuffle, b[7] = list.none;
    b[8] = u8(list.seed), b[9] = u8(list.seed >> 8), put32(b, 0xC, list.kept);
    for (size_t i = 0; i < 128; ++i) {
        const GameState::PlayList::Row& r = list.row[i];
        const size_t at = 0x10 + 16 * i;
        put32(b, at, u32(r.kept)), put32(b, at + 4, u32(r.kept >> 32));
        b[at + 8] = r.played, b[at + 9] = r.enabled, b[at + 10] = u8(r.order), b[at + 11] = u8(r.order2), put32(b, at + 12, r.kept_tail);
    }
    return b;
}

Codes database_codes(db::SpecDb& db) {
    auto labels = [&db](u32 table) -> const db::Labels* {
        switch (table) {
        case 0: return db.labels("GENERIC_CAR");
        case 0x22: return db.labels("COURSE");
        case 0x23: return db.labels("RACE");
        case 29: return db.labels("WHEEL");
        case 30: return db.labels("WING");
        default: return nullptr;
        }
    };
    return Codes{[labels](u32 table, const std::string& label) {
                     const db::Labels* l = labels(table);
                     const std::optional<u32> id = l && !label.empty() ? l->id(label) : std::nullopt;
                     return id ? (u64(table) << 32 | *id) : ~u64(0);
                 },
                 [labels](u64 code) {
                     const db::Labels* l = code == ~u64(0) ? nullptr : labels(u32(code >> 32));
                     const std::string* s = l ? l->label(u32(code)) : nullptr;
                     return s ? *s : std::string();
                 }};
}

Bytes new_game_payload() {
    Bytes b(kPayloadSize, 0);
    const GameState fresh;
    // The named blocks' values of a new game come from write_game below; here the rest.
    // Options (Option+8 on): the block's own size, the sound's switches and volumes (115), the
    // play lists, the race and replay settings (MOption's constructors).
    put32(b, kOptionHead, 0x11C0);
    put32(b, option(0x20), 1);
    for (size_t at = 0x5C; at < 0x8C; at += 8) put32(b, option(at), 1), put32(b, option(at + 4), 115);
    put32(b, option(0x8C), 115);
    // ❓ The play lists (0x1020 bytes): two lists, each a header word and 128 rows; the header's
    // words are kept as the constructor leaves them.
    b[option(0x97)] = 1, b[option(0x98)] = 0x55, b[option(0x99)] = 0x33;
    fresh_playlist(b, option(0xA8));
    b[option(0x8A7)] = 1, b[option(0x8A8)] = 0x76, b[option(0x8A9)] = 0x34;
    fresh_playlist(b, option(0x8B8));
    static const std::pair<size_t, u32> kNumbers[] = {
        {0x10B0, 1}, {0x10B4, 1}, {0x10BC, 1}, {0x10C0, 2}, {0x10C8, 1}, {0x10CC, 1}, {0x10D0, 1}, {0x10D4, 1}, {0x10FC, 2}, {0x1108, 1},
        {0x1128, 2}, {0x1140, 0x3FA}, {0x1144, 1}, {0x1150, 20}, {0x1154, 600}, {0x1164, 1}, {0x116C, 3}, {0x1170, 1}, {0x1174, 8}, {0x117C, 2},
        {0x1180, 4}, {0x1184, 3},
    };
    for (const auto& [at, v] : kNumbers) put32(b, option(at), v);
    for (size_t i = 0; i < 2; ++i) input::PortConfig().write(b.data() + kControllers + input::PortConfig::kSize * i);
    // LAN: both names "Player1", then the monitor and the machines.
    put_text(b, kLan, 0x40, "Player1"), put_text(b, kLan + 0x44, 0x40, "Player1");
    put32(b, kLan + 0x88, 1), put32(b, kLan + 0x8C, 1), put32(b, kLan + 0x90, 2), put32(b, kLan + 0x9C, 1), put32(b, kLan + 0xA4, 1);
    // ❓ The network block's three set words.
    put32(b, kNetwork + 0x40, 1), put32(b, kNetwork + 0x104, 1), put32(b, kNetwork + 0x1CC, 0x50);
    std::memset(b.data() + kLogger, 1, 5);
    (void)fresh;
    return b;
}

Bytes write_game(const GameState& s, const Codes& codes) {
    Bytes b = s.saved_image.size() == kPayloadSize ? s.saved_image : new_game_payload();
    // The user profile.
    put_text(b, kProfile, 0x20, s.username), put_text(b, kProfile + 0x20, 8, s.password), put_text(b, kProfile + 0x28, 0x10, s.last_entry_name);
    put64(b, 0x48, u64(s.score)), put64(b, 0x50, u64(s.total_prize_money)), put32(b, 0x58, s.total_prize_cars);
    for (size_t i = 0; i < 7; ++i) put32(b, 0x5C + 4 * i, s.rank_counts[i]);
    put64(b, 0x78, u64(s.aspec_distance)), put64(b, 0x80, u64(s.bspec_distance));
    put32(b, 0x88, u32(s.withdrawn)), put32(b, 0x8C, u32(s.withdrawn_gt4p)), b[0x90] = u8(s.met_type);
    for (size_t i = 0; i < s.bspec_book.size(); ++i) b[kBspecBook + 2 * i] = u8(s.bspec_book[i]), b[kBspecBook + 2 * i + 1] = u8(s.bspec_book[i] >> 8);
    // The calendar: the days kept, oldest first; today.
    put32(b, kDiaryCount, u32(s.diary.size()));
    std::memset(b.data() + kDiary, 0, GameState::kDiaryDays * 16);
    for (size_t i = 0; i < s.diary.size(); ++i) {
        const DayEvent& e = s.diary[i];
        const size_t o = kDiary + 16 * i;
        b[o] = e.type, b[o + 1] = e.b1, b[o + 2] = e.b2, b[o + 3] = e.b3, put32(b, o + 4, e.word), put64(b, o + 8, e.code);
    }
    put32(b, kToday, s.today);
    // The garage's list, its tail and the car driven's record.
    for (size_t i = 0; i < GameState::kGarageSize; ++i) {
        const size_t o = kGarageList + 0x20 * i;
        if (i >= s.garage.size()) {
            put64(b, o, ~u64(0)), put64(b, o + 8, 0), put32(b, o + 0x10, 0), put32(b, o + 0x14, 0), put64(b, o + 0x18, 0x3FF);
            continue;
        }
        const GarageEntry& e = s.garage[i];
        put64(b, o, codes.code(0, e.car));
        u64 flags = e.kept_flags & ~(u64(0x7FFFFFF) | (u64(0x1FFF) << 32));
        flags |= u64(e.used) | u64(e.record.has_value()) << 1 | u64(e.paint & 0x7F) << 2 | u64(e.power & 0x3FFFF) << 9 | u64(e.weight & 0x1FFF) << 32;
        put64(b, o + 8, flags);
        put32(b, o + 0x10, (e.kept_day & 0xFF000000u) | ((e.day - GameState::kFirstDay) & 0xFFFFFF));
        put32(b, o + 0x14, e.odometer);
        put64(b, o + 0x18, (e.kept_rank & ~u64(0x3FF)) | u64(e.ride_rank & 0x3FF));
    }
    put32(b, kGarageTail, u32(s.riding_car)), put32(b, kGarageTail + 4, s.garage_key);
    for (size_t i = 0; i < 4; ++i) put32(b, kGarageTail + 8 + 4 * i, u32(s.garage_kept[i]));
    put32(b, kGarageTail + 0x18, s.garage_serial);
    const bool riding = s.riding_car >= 0 && size_t(s.riding_car) < s.garage.size() && s.garage[size_t(s.riding_car)].record;
    put_record(b, kRidingRecord, riding ? *s.garage[size_t(s.riding_car)].record : CarRecord());
    // The records.
    for (size_t i = 0; i < s.race_units.size(); ++i) put32(b, kRaceUnits + 4 * i, s.race_units[i]);
    auto put_block = [&](size_t o, const Bytes& v, size_t n) {
        if (v.size() >= n) std::memcpy(b.data() + o, v.data(), n);
    };
    put_block(kCourseRecord, s.course_record, menu::kCourseUnit * menu::kCourseUnits);
    put_block(kMachineRecord, s.machine_record, menu::kCourseUnit * menu::kMachineUnits);
    put_block(kLicenseRecord, s.license_record, menu::kLicenseUnit * menu::kLicenseUnits);
    // What is available, sorted by code; the favorites in their order.
    std::vector<u64> cars, courses;
    for (const std::string& l : s.available_cars)
        if (const u64 c = codes.code(0, l); c != ~u64(0)) cars.push_back(c);
    for (const std::string& l : s.available_courses)
        if (const u64 c = codes.code(0x22, l); c != ~u64(0)) courses.push_back(c);
    put_codes(b, kAvailable, 1024, cars, true), put_codes(b, kAvailable + 0x2000, 128, courses, true);
    put32(b, kAvailable + 0x2400, s.all_available);
    auto favorites = [&](size_t o, const std::vector<std::string>& list, u32 table) {
        std::vector<u64> c;
        for (const std::string& l : list)
            if (const u64 v = codes.code(table, l); v != ~u64(0)) c.push_back(v);
        put32(b, o, u32(GameState::kFavoriteLimit)), put32(b, o + 4, u32(std::min<size_t>(c.size(), 32)));
        put_codes(b, o + 8, 32, c, false);
    };
    favorites(kFavoriteCars, s.favorite_cars, 0), favorites(kFavoriteCourses, s.favorite_courses, 0x22);
    for (size_t i = 0; i < s.presents.size(); ++i) put32(b, kPresents + 4 * i, s.presents[i]);
    put_text(b, kChampionship, 0x20, s.suspended_racecode);
    b[kChampionship + 0x20] = u8(s.suspended), b[kChampionship + 0x21] = u8(s.suspended_session);
    if (s.championship_rest.size() == 0x66) std::memcpy(b.data() + kChampionship + 0x22, s.championship_rest.data(), 0x66);
    // A championship left in the middle (0x119BD8): its race count, then its six entrants
    // (u64 the car's code, s32 the paint, s32 the points).
    if (s.suspended && s.series.label == s.suspended_racecode) {
        b[kChampionship + 0x22] = u8(s.series.race_count);
        for (size_t k = 0; k < 6; ++k) {
            const GameState::Series::Entrant e = k < s.series.entrants.size() ? s.series.entrants[k] : GameState::Series::Entrant{};
            const size_t o = kChampionship + 0x28 + 0x10 * k;
            put32(b, o, u32(e.code)), put32(b, o + 4, u32(e.code >> 32)), put32(b, o + 8, u32(e.paint)), put32(b, o + 12, u32(e.points));
        }
    }
    for (size_t i = 0; i < 8; ++i) put32(b, kUsedCar + 4 * i, s.used_sold[i]);
    put32(b, kUsedCar + 0x20, s.used_week);
    // The controllers' settings.
    for (const auto& [name, at] : kControllerOptions)
        if (const std::string t = s.option_text(name); !t.empty()) input::port_from_text(t).write(b.data() + at);
    for (const auto& [name, at] : kLanNames)
        if (const std::string t = s.option_text(name); !t.empty()) put_text(b, kLan + at, 0x40, t.substr(0, 0x3F));
    for (const auto& [name, at] : kLanNumbers)
        if (const auto it = s.options.find(name); it != s.options.end())
            if (const s32* v = std::get_if<s32>(&it->second)) put32(b, kLan + at, u32(*v));
    // The options that are numbers, and the zone.
    for (const OptionField& f : kOptionNumbers)
        if (const auto it = s.options.find(f.name); it != s.options.end())
            if (const s32* v = std::get_if<s32>(&it->second)) put32(b, option(f.at), u32(*v));
    {
        u32 word = get32(b, option(kDisplayWord));
        for (const auto& [name, bit] : kDisplayBits)
            if (const auto it = s.options.find(name); it != s.options.end())
                if (const s32* v = std::get_if<s32>(&it->second)) word = *v ? word | bit : word & ~bit;
        put32(b, option(kDisplayWord), word);
    }
    if (s.monitor_params.size() == kMonitorSize) std::memcpy(b.data() + option(kMonitorParams), s.monitor_params.data(), kMonitorSize);
    for (const auto& [name, at] : kPictureSettings)
        if (const auto it = s.options.find(name); it != s.options.end())
            if (const std::string* t = std::get_if<std::string>(&it->second)) {
                const std::array<int, 4> v = menu::picture_from_text(*t, {0, 0, 0, 0});
                for (size_t i = 0; i < 4; ++i) put32(b, option(at) + 4 * i, u32(v[i]));
            }
    // The play lists (Option +0x90 and +0x8A0): u32 rows, u16 position, u8 shuffle, u8 none,
    // u16 seed, a word; 128 rows of 16 bytes: 8 bytes, u8 played, u8 enabled, s8 order, s8 its
    // copy, 4 bytes.
    for (const auto& [at, list] : {std::pair<size_t, const GameState::PlayList*>{0x90, &s.bgm_playlist}, {0x8A0, &s.slide_playlist}}) {
        const Bytes block = playlist_block(*list);
        std::copy(block.begin(), block.end(), b.begin() + std::ptrdiff_t(option(at)));
    }
    for (const NamedOption& o : kNamedOptions) {
        const std::string v = s.option_text(o.name);
        for (size_t i = 0; i < o.names.size(); ++i)
            if (v == o.names[i]) {
                if (o.bytes == 4) put32(b, option(o.at), u32(i));
                else b[option(o.at)] = u8(i);
            }
    }
    {
        const std::string zone = s.option_text("game_zone");
        u32 z = 0;
        for (u32 i = 0; i < 6; ++i)
            if (zone == kZones[i]) z = i;
        put32(b, kGameZone, z);
    }
    // The menus' place.
    put_text(b, kContext, 0x20, s.major_menu_project), put_text(b, kContext + 0x20, 0x20, s.major_menu_page);
    std::memcpy(b.data() + kLogger, s.logger_display.data(), 5); // (the Analyzer's switches)
    put32(b, kContext + 0x40, u32(s.saved_menu_depth));
    if (s.saved_menu_stack.size() == 0x100) std::memcpy(b.data() + kContext + 0x44, s.saved_menu_stack.data(), 0x100);
    // The header: magic, minus the body's length, the body's CRC.
    put32(b, 0, kPayloadMagic), put32(b, 4, u32(-s32(kPayloadSize - 0x10))), put32(b, 12, 0);
    put32(b, 8, crc32(ByteSpan(b.data() + 0x10, kPayloadSize - 0x10)));
    return b;
}

bool valid_game(ByteSpan p) {
    if (p.size() < kPayloadSize) return false;
    return get32(p, 0) == kPayloadMagic && get32(p, 4) == u32(-s32(kPayloadSize - 0x10)) &&
           get32(p, 8) == crc32(ByteSpan(p.data() + 0x10, kPayloadSize - 0x10));
}

namespace {

// Japan's options block: its size word, its own fields moved, the blocks after it.
constexpr u32 kJapanOptionBlock = 0x11D8;
constexpr size_t kJapanShift = 0x20, kJapanPayloadSize = kPayloadSize + kJapanShift;
// [ours, Japan's, bytes] of the Option fields that sit elsewhere in Japan's block (0x188EA0, 0x189158, 0x186070 and the
// option's reset 0x4363A0 in each program).
constexpr size_t kJapanMoved[][3] = {{0x1184, 0x1180, 0x10}, {0x1194, 0x1190, 0x10}, {0x1180, 0x11A0, 4}};

void fill_header(Bytes& b) {
    put32(b, 0, kPayloadMagic), put32(b, 4, u32(-s32(b.size() - 0x10))), put32(b, 12, 0);
    put32(b, 8, crc32(ByteSpan(b.data() + 0x10, b.size() - 0x10)));
}

} // namespace

Layout layout_of_product(const std::string& product) {
    const std::string p = product.size() > 2 && product[0] == 'B' ? product.substr(2) : product;
    return p == "SCPS-17001" ? Layout::Japan : Layout::Us;
}

size_t payload_size(Layout l) { return l == Layout::Japan ? kJapanPayloadSize : kPayloadSize; }

Bytes to_layout(ByteSpan p, Layout l) {
    if (l == Layout::Us || p.size() < kPayloadSize) return Bytes(p.begin(), p.end());
    Bytes b(kJapanPayloadSize, 0);
    std::memcpy(b.data(), p.data(), kGameZone);
    std::memcpy(b.data() + kGameZone + kJapanShift, p.data() + kGameZone, kPayloadSize - kGameZone);
    // ❓ Japan's block from +0x11A4 to its end is left as zeros (its reset sets none of it; no accessor of it was found).
    std::memset(b.data() + option(0x1180), 0, kGameZone + kJapanShift - option(0x1180));
    for (const auto& m : kJapanMoved) std::memcpy(b.data() + option(m[1]), p.data() + option(m[0]), m[2]);
    put32(b, kOptionHead, kJapanOptionBlock);
    fill_header(b);
    return b;
}

std::optional<Bytes> from_layout(ByteSpan p, Layout l) {
    if (l == Layout::Us) {
        if (!valid_game(p)) return std::nullopt;
        return Bytes(p.begin(), p.begin() + kPayloadSize);
    }
    if (p.size() < kJapanPayloadSize || get32(p, 0) != kPayloadMagic || get32(p, 4) != u32(-s32(kJapanPayloadSize - 0x10)) ||
        get32(p, 8) != crc32(ByteSpan(p.data() + 0x10, kJapanPayloadSize - 0x10)))
        return std::nullopt;
    Bytes b(kPayloadSize, 0);
    std::memcpy(b.data(), p.data(), kGameZone);
    std::memcpy(b.data() + kGameZone, p.data() + kGameZone + kJapanShift, kPayloadSize - kGameZone);
    std::memset(b.data() + option(0x1180), 0, kGameZone - option(0x1180));
    for (const auto& m : kJapanMoved) std::memcpy(b.data() + option(m[0]), p.data() + option(m[1]), m[2]);
    put32(b, kOptionHead, 0x11C0);
    fill_header(b);
    return b;
}

bool read_game(ByteSpan p, GameState& s, const Codes& codes, std::vector<int>* records_wanted) {
    if (!valid_game(p)) return false;
    s.new_game();
    s.saved_image.assign(p.begin(), p.begin() + kPayloadSize);
    s.username = get_text(p, kProfile, 0x20), s.password = get_text(p, kProfile + 0x20, 8), s.last_entry_name = get_text(p, kProfile + 0x28, 0x10);
    s.score = s64(get64(p, 0x48)), s.total_prize_money = s64(get64(p, 0x50)), s.total_prize_cars = get32(p, 0x58);
    for (size_t i = 0; i < 7; ++i) s.rank_counts[i] = get32(p, 0x5C + 4 * i);
    s.aspec_distance = s64(get64(p, 0x78)), s.bspec_distance = s64(get64(p, 0x80));
    s.withdrawn = int(get32(p, 0x88)), s.withdrawn_gt4p = int(get32(p, 0x8C)), s.met_type = p[0x90];
    for (size_t i = 0; i < s.bspec_book.size(); ++i) s.bspec_book[i] = u16(p[kBspecBook + 2 * i] | p[kBspecBook + 2 * i + 1] << 8);
    const u32 days = std::min<u32>(get32(p, kDiaryCount), GameState::kDiaryDays);
    for (u32 i = 0; i < days; ++i) {
        const size_t o = kDiary + 16 * i;
        s.diary.push_back(DayEvent{p[o], p[o + 1], p[o + 2], p[o + 3], get32(p, o + 4), get64(p, o + 8)});
    }
    s.today = get32(p, kToday);
    s.riding_car = s32(get32(p, kGarageTail)), s.garage_key = get32(p, kGarageTail + 4);
    for (size_t i = 0; i < 4; ++i) s.garage_kept[i] = s32(get32(p, kGarageTail + 8 + 4 * i));
    s.garage_serial = get32(p, kGarageTail + 0x18);
    size_t last = 0;
    for (size_t i = 0; i < GameState::kGarageSize; ++i)
        if (get64(p, kGarageList + 0x20 * i) != ~u64(0)) last = i + 1;
    s.garage.resize(last);
    for (size_t i = 0; i < last; ++i) {
        const size_t o = kGarageList + 0x20 * i;
        GarageEntry& e = s.garage[i];
        const u64 flags = get64(p, o + 8);
        e.car = codes.label(get64(p, o));
        e.used = flags & 1, e.paint = int((flags >> 2) & 0x7F), e.power = u32((flags >> 9) & 0x3FFFF), e.weight = u32((flags >> 32) & 0x1FFF);
        e.kept_flags = flags;
        const u32 day = get32(p, o + 0x10);
        e.day = GameState::kFirstDay + (day & 0xFFFFFF), e.kept_day = day;
        e.odometer = get32(p, o + 0x14);
        const u64 rank = get64(p, o + 0x18);
        e.ride_rank = int(rank & 0x3FF), e.kept_rank = rank;
        if (flags & 2) {
            if (int(i) == s.riding_car) {
                e.record.emplace();
                std::memcpy(e.record->bytes.data(), p.data() + kRidingRecord, CarRecord::kSize);
            } else if (records_wanted) records_wanted->push_back(int(i));
        }
    }
    for (size_t i = 0; i < s.race_units.size(); ++i) s.race_units[i] = get32(p, kRaceUnits + 4 * i);
    s.course_record.assign(p.begin() + kCourseRecord, p.begin() + kCourseRecord + menu::kCourseUnit * menu::kCourseUnits);
    s.machine_record.assign(p.begin() + kMachineRecord, p.begin() + kMachineRecord + menu::kCourseUnit * menu::kMachineUnits);
    s.license_record.assign(p.begin() + kLicenseRecord, p.begin() + kLicenseRecord + menu::kLicenseUnit * menu::kLicenseUnits);
    for (size_t i = 0; i < 1024; ++i)
        if (const u64 c = get64(p, kAvailable + 8 * i); c != ~u64(0))
            if (std::string l = codes.label(c); !l.empty()) s.available_cars.insert(std::move(l));
    for (size_t i = 0; i < 128; ++i)
        if (const u64 c = get64(p, kAvailable + 0x2000 + 8 * i); c != ~u64(0))
            if (std::string l = codes.label(c); !l.empty()) s.available_courses.insert(std::move(l));
    s.all_available = get32(p, kAvailable + 0x2400) != 0;
    auto favorites = [&](size_t o, std::vector<std::string>& list) {
        const u32 n = std::min<u32>(get32(p, o + 4), 32);
        for (u32 i = 0; i < n; ++i)
            if (std::string l = codes.label(get64(p, o + 8 + 8 * i)); !l.empty()) list.push_back(std::move(l));
    };
    favorites(kFavoriteCars, s.favorite_cars), favorites(kFavoriteCourses, s.favorite_courses);
    for (size_t i = 0; i < s.presents.size(); ++i) s.presents[i] = get32(p, kPresents + 4 * i);
    s.suspended_racecode = get_text(p, kChampionship, 0x20), s.suspended = p[kChampionship + 0x20], s.suspended_session = p[kChampionship + 0x21];
    s.championship_rest.assign(p.begin() + kChampionship + 0x22, p.begin() + kChampionship + 0x88);
    s.series = {};
    if (s.suspended) {
        s.series.label = s.suspended_racecode, s.series.races_run = s.suspended_session, s.series.race_count = p[kChampionship + 0x22];
        for (size_t k = 0; k < 6; ++k) {
            const size_t o = kChampionship + 0x28 + 0x10 * k;
            s.series.entrants.push_back({get64(p, o), s32(get32(p, o + 8)), s32(get32(p, o + 12))});
        }
    }
    for (size_t i = 0; i < 8; ++i) s.used_sold[i] = get32(p, kUsedCar + 4 * i);
    s.used_week = get32(p, kUsedCar + 0x20);
    for (const auto& [name, at] : kControllerOptions) {
        input::PortConfig c;
        c.read(p.data() + at);
        s.options[name] = input::port_text(c);
    }
    for (const auto& [name, at] : kLanNames) s.options[name] = get_text(p, kLan + at, 0x40);
    for (const auto& [name, at] : kLanNumbers) s.options[name] = s32(get32(p, kLan + at));
    for (const OptionField& f : kOptionNumbers) s.options[f.name] = s32(get32(p, option(f.at)));
    if (const u32 z = get32(p, kGameZone); z > 0 && z < 6) s.options["game_zone"] = std::string(kZones[z]);
    for (const auto& [name, bit] : kDisplayBits) s.options[name] = s32((get32(p, option(kDisplayWord)) & bit) != 0);
    s.monitor_params.assign(p.begin() + option(kMonitorParams), p.begin() + option(kMonitorParams) + kMonitorSize);
    if (std::all_of(s.monitor_params.begin(), s.monitor_params.end(), [](u8 c) { return c == 0; })) s.monitor_params.clear();
    for (const auto& [name, at] : kPictureSettings) {
        std::array<int, 4> v{};
        for (size_t i = 0; i < 4; ++i) v[i] = s32(get32(p, option(at) + 4 * i));
        s.options[name] = menu::picture_text(v);
    }
    for (const auto& [at, list] : {std::pair<size_t, GameState::PlayList*>{0x90, &s.bgm_playlist}, {0x8A0, &s.slide_playlist}}) {
        const size_t o = option(at);
        list->rows = get32(p, o), list->position = u16(p[o + 4] | p[o + 5] << 8), list->shuffle = p[o + 6], list->none = p[o + 7];
        list->seed = u16(p[o + 8] | p[o + 9] << 8), list->kept = get32(p, o + 0xC);
        for (size_t i = 0; i < 128; ++i) {
            GameState::PlayList::Row& r = list->row[i];
            const size_t at_row = o + 0x10 + 16 * i;
            r.kept = get64(p, at_row), r.played = p[at_row + 8], r.enabled = p[at_row + 9], r.order = gt3r::s8(p[at_row + 10]), r.order2 = gt3r::s8(p[at_row + 11]);
            r.kept_tail = get32(p, at_row + 12);
        }
    }
    for (const NamedOption& o : kNamedOptions) {
        const u32 v = o.bytes == 4 ? get32(p, option(o.at)) : p[option(o.at)];
        if (v < o.names.size()) s.options[o.name] = std::string(o.names[v]);
    }
    s.major_menu_project = get_text(p, kContext, 0x20), s.major_menu_page = get_text(p, kContext + 0x20, 0x20);
    std::memcpy(s.logger_display.data(), p.data() + kLogger, 5);
    s.saved_menu_depth = s32(get32(p, kContext + 0x40));
    s.saved_menu_stack.assign(p.begin() + kContext + 0x44, p.begin() + kContext + 0x144);
    return true;
}

} // namespace gt4r::save
