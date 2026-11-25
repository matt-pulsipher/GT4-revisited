// The save editor's changes other than the garage's (save_editor_cars.cpp): career, licenses, events, missions, presents,
// records, Arcade. The lists are the scripts' own (Gran Turismo mode's event and present data), the rules the natives' and the
// scripts' functions.

#include "gt4r/menu/save_editor.h"

#include "gt4r/menu/game_bspec.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/game_progress.h"
#include "gt4r/sim/ai/bspec_skill.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace gt4r::menu {

using script::Kind;
using script::Value;

namespace {

u16 r16(ByteSpan r, size_t o) { return r.size() >= o + 2 ? u16(r[o] | r[o + 1] << 8) : u16(0); }
u32 get32(const Bytes& b, size_t o) { return u32(b[o]) | u32(b[o + 1]) << 8 | u32(b[o + 2]) << 16 | u32(b[o + 3]) << 24; }
void put32(Bytes& b, size_t o, u32 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8), b[o + 2] = u8(v >> 16), b[o + 3] = u8(v >> 24); }
u64 get64(const Bytes& b, size_t o) { return u64(get32(b, o)) | u64(get32(b, o + 4)) << 32; }

constexpr u32 kFreshUnit = 0x0000FFF0u; // no kind, the three results none, no points
// A license ranking's places.
constexpr size_t kRankEntry = 0x2C, kRankEntries = 10;

// A text of the game's on one line.
std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string string_of(const Value& v) { return v.kind == Kind::String ? v.as_string() : std::string(); }

// The license tests' rankings by their label (game_records.cpp, 0x4323D0).
int ranking_of(const std::string& label) {
    static const char* const cats[7] = {"0b", "0a", "ib", "ia", "0s", "0c", "0m"};
    int cat = -1;
    for (int i = 0; i < 7; ++i)
        if (label.compare(1, 2, cats[i]) == 0) cat = i;
    if (cat < 0) return -1;
    const int n = std::atoi(label.c_str() + 3);
    const int u = cat < 5 ? cat * 16 + n - 1 : cat == 5 ? 79 + n : 84 + n;
    return u >= 0 && u < int(kLicenseUnits) ? u : -1;
}

} // namespace

SaveEditor::SaveEditor(FileReader read, RuntimeConfig config) : read_(read) {
    config.data_dir.clear(); // no card, no options file: the state is the save's alone
    rt_ = std::make_unique<Runtime>(std::move(read), std::move(config));
}
SaveEditor::~SaveEditor() = default;

GameNatives& SaveEditor::natives() {
    const std::shared_ptr<GameNatives> n = natives_of(*rt_);
    if (!n) throw std::runtime_error("the game's natives are not there");
    return *n;
}
Value SaveEditor::lookup(const std::string& path) { return rt_->vm().lookup(path); }
Value SaveEditor::call(const Value& f, std::vector<Value> args) {
    rt_->vm().instructions = 0;
    return rt_->vm().call(f, std::span<const Value>(args.data(), args.size()));
}
Value SaveEditor::method(const Value& object, const std::string& name, std::vector<Value> args) { return call(rt_->vm().attribute(object, name), std::move(args)); }
Value SaveEditor::game() { return lookup("game"); }

std::string SaveEditor::text(const std::string& page, const std::string& group, const std::string& key) {
    const std::string t = one_line(rt_->translate(lookup("GTmodeProject::" + page), group, key));
    return t;
}
std::string SaveEditor::setting_label(const std::string& page, const std::string& key) const {
    const std::string* t = setting_text_ ? setting_text_->text().find("SettingProject::" + page, key) : nullptr;
    return t ? one_line(*t) : key;
}

std::string SaveEditor::course_name(const std::string& label) {
    // The menus' own names ("Tsukuba Circuit", "Tokyo R246 Reverse"), else the database's.
    if (const std::string t = text("TopRoot", "CourseName", label); t != label && !t.empty()) return t;
    for (const db::CourseEntry& c : natives().db().courses)
        if (c.label == label && !c.name.empty()) return c.name;
    return label;
}

void SaveEditor::start(const GameState& loaded) {
    // The project is loaded for the game's options (its zone picks the present lists), on a new game's state: its start looks
    // for presents, which would take them.
    const std::string zone = loaded.option_text("game_zone");
    if (!started_ || zone != started_zone_) {
        GameState fresh;
        fresh.options = state().options;
        for (const auto& [name, value] : loaded.options) fresh.options[name] = value;
        rt_->boot();
        state() = fresh;
        rt_->start_project("gtmode");
        rt_->settle();
        setting_text_.reset();
        try {
            setting_text_ = std::make_unique<Project>(read_, "setting", rt_->language());
        } catch (const std::exception&) {
            // (the settings pages' names then read as their keys)
        }
        started_ = true, started_zone_ = zone;
        natives().db();
        read_cars(), read_wheels(), read_licenses(), read_events(), read_missions(), read_presents(), read_courses();
    }
    const auto options = state().options;
    state() = loaded;
    for (const auto& [name, value] : options)
        if (!state().options.count(name)) state().options[name] = value;
}

// ---- Career -------------------------------------------------------------------------------------------------------------------

void SaveEditor::set_name(const std::string& v) {
    std::string out;
    for (char c : v)
        if (std::isalnum(static_cast<unsigned char>(c)) && out.size() < kNameLetters) out += c;
    if (!out.empty()) state().username = out;
}

void SaveEditor::set_days(int v) {
    GameState& s = state();
    v = std::clamp(v, 0, kMaxDays);
    while (days() < v) s.put_day(DayEvent{});
    while (days() > v) {
        if (!s.diary.empty()) s.diary.pop_back();
        --s.today;
    }
}

int SaveEditor::completion() {
    rt_->vm().instructions = 0;
    return completion_permille(*rt_);
}

int SaveEditor::aspec_points() {
    completion(); // (the scripts' kept figures dropped)
    const Value r = call(lookup("GTmodeProject::getRaceEventRecordStatistics"));
    const std::vector<Value>* items = r.items();
    return items && items->size() > 1 ? int((*items)[1].as_float()) : 0;
}

SaveEditor::BSpec SaveEditor::bspec() {
    const sim::ai::BSpecTables t = sim::ai::BSpecTables::load(natives().db().program);
    const bspec::Book& b = state().bspec_book;
    return {bspec::total_experience(b, t.weight), bspec::car_experience(b, t.weight), bspec::course_experience(b, t.weight), bspec::battle_experience(b, t.weight)};
}

// What the figures count of a cell: the first two tables up to 1,024, the third up to 256 (game_bspec.cpp). The cell [7][0] of
// the second and third tables is the courses' sum.
namespace {
bool class_cell(int t, int r, int c) { return !(c == 0 && r == 7 && t > 0); }
int cell_most(int t) { return t == 2 ? 256 : 1024; }
} // namespace

int SaveEditor::bspec_class_level(int k) const {
    s64 got = 0, most = 0;
    for (int t = 0; t < 3; ++t)
        for (int r = 0; r < 8; ++r) {
            if (!class_cell(t, r, k) || (k == 0 && r == 7)) continue;
            got += std::min<int>(state().bspec_book[size_t(t * 64 + r * 8 + k)], cell_most(t)) * (t == 2 ? 4 : 1), most += 1024;
        }
    return most ? int((got * 100 + most / 2) / most) : 0;
}

void SaveEditor::set_bspec_class_level(int k, int percent) {
    if (k < 0 || k >= kCarClasses) return;
    percent = std::clamp(percent, 0, 100);
    for (int t = 0; t < 3; ++t)
        for (int r = 0; r < 8; ++r)
            if (class_cell(t, r, k) && !(k == 0 && r == 7)) state().bspec_book[size_t(t * 64 + r * 8 + k)] = u16((cell_most(t) * percent + 50) / 100);
}

// The courses whose marks the B-spec driver keeps: the second character of the course's attribute text (save.md).
static std::vector<std::string> marked_courses(Database& d) {
    std::vector<std::string> out;
    for (const db::CourseEntry& c : d.courses) {
        const std::string a = course_attribute(d, c.label);
        if (a.size() > 1 && std::isxdigit(static_cast<unsigned char>(a[1])) && a[1] != '0') out.push_back(c.label);
    }
    return out;
}

int SaveEditor::bspec_course_level() {
    // The courses' sums against the program's most (0xE718 = 58 courses of 4 marks of 255).
    const u32 sum = u32(state().bspec_book[64 + 56]) + u32(state().bspec_book[128 + 56]);
    return int(std::min<u32>(100, (sum * 100 + 0xE718) / (2 * 0xE718)));
}

void SaveEditor::set_bspec_course_level(int percent) {
    GameState& s = state();
    percent = std::clamp(percent, 0, 100);
    Database& d = natives().db();
    // A course of the marked ones has a record (made as the game makes it) and its eight marks the level; the two sums are the
    // marks' (four each), as the program keeps them.
    const Value record = rt_->vm().attribute(game(), "course_record");
    const u8 mark = u8((255 * percent + 50) / 100);
    std::vector<size_t> done;
    for (const std::string& c : marked_courses(d)) {
        std::string own = c; // (a 24 hours' course keeps its marks with the plain one's: bspec_course_unit)
        if (own.size() >= 2 && own.compare(own.size() - 2, 2, "24") == 0) own.resize(own.size() - 2);
        if (percent > 0) method(record, "create", {Value::string(own)});
        const std::optional<size_t> at = bspec_course_unit(d, s, c);
        if (!at || std::find(done.begin(), done.end(), *at) != done.end()) continue;
        done.push_back(*at);
        for (size_t i = 0; i < 8; ++i) s.course_record[*at + 0x228 + i] = mark;
    }
    u32 sums[2] = {};
    for (size_t u = 0; u < kCourseUnits; ++u) {
        if (get64(s.course_record, u * kCourseUnit) == kNoCode) continue;
        for (size_t i = 0; i < 8; ++i) sums[i / 4] += s.course_record[u * kCourseUnit + 0x228 + i];
    }
    s.bspec_book[64 + 56] = u16(std::min<u32>(sums[0], 0xFFFF)), s.bspec_book[128 + 56] = u16(std::min<u32>(sums[1], 0xFFFF));
}

void SaveEditor::set_finishes(int place, u32 n) {
    if (place >= 0 && place < 7) state().rank_counts[size_t(place)] = std::min(n, kMaxCount);
}

std::string SaveEditor::championship_under_way() {
    if (!state().suspended) return {};
    for (const EventList& l : events_)
        for (const Event& e : l.events)
            if (e.code == state().suspended_racecode) return e.name;
    return state().suspended_racecode.empty() ? "A championship" : state().suspended_racecode;
}

// ---- Licenses -----------------------------------------------------------------------------------------------------------------

void SaveEditor::read_licenses() {
    licenses_.clear();
    Database& d = natives().db();
    for (const char* cat : {"l0b", "l0a", "lib", "lia", "l0s", "l0c"}) {
        License l;
        l.category = cat, l.name = text("PresentRoot", "LicenseName", std::string(cat) + "0000");
        for (int k = 1; k <= 16; ++k) {
            char label[16];
            std::snprintf(label, sizeof label, "%s%04d", cat, k);
            const int u = d.race_unit(label);
            if (u < 0) continue;
            LicenseTest t;
            t.label = label, t.name = text("PresentRoot", "LicenseName", label), t.unit = u, t.ranking = ranking_of(label);
            ByteSpan row;
            for (const std::string& tried : {std::string(label), "us" + std::string(label), "jp" + std::string(label)})
                if (row.empty()) row = d.row("RACE", tried);
            for (size_t m = 0; m < 3; ++m) {
                const size_t at = 0x76 + 2 * m, ms = 0x1A + 2 * m;
                if (row.size() > 0x7B) t.limit[3 - m] = u32(row[at]) * 60'000 + u32(row[at + 1]) * 1'000 + r16(row, ms);
            }
            l.tests.push_back(std::move(t));
        }
        if (!l.tests.empty()) licenses_.push_back(std::move(l));
    }
}

SaveEditor::Medal SaveEditor::medal(const LicenseTest& t) const {
    const u32 w = state().race_units[size_t(t.unit)];
    if ((w & 15) == 0) return kNone;
    const u32 r = w >> 4 & 15;
    return r == 9 ? kGold : r == 10 ? kSilver : r == 11 ? kBronze : kNone;
}

u32 SaveEditor::best_time(const LicenseTest& t) const { return t.ranking < 0 ? kNoTime : get32(state().license_record, size_t(t.ranking) * kLicenseUnit); }

void SaveEditor::set_medal(const LicenseTest& t, Medal m) {
    GameState& s = state();
    if (m == kNone) unit(t.unit) = kFreshUnit;
    else {
        const u32 result = m == kGold ? 9 : m == kSilver ? 10 : 11;
        unit(t.unit) = (unit(t.unit) & ~0xFFFu) | 1u | result << 4 | result << 8;
    }
    if (t.ranking < 0) return;
    const size_t base = size_t(t.ranking) * kLicenseUnit;
    const Value record = rt_->vm().attribute(game(), "license_record");
    const Value u = method(record, "getUnit", {Value::string(t.label)});
    if (u.is_nil()) return;
    // The times that would earn more than the medal are taken out.
    for (int i = int(kRankEntries) - 1; i >= 0; --i) {
        const u32 time = get32(s.license_record, base + size_t(i) * kRankEntry);
        if (time == kNoTime) continue;
        bool more = m == kNone;
        for (int b = int(m) + 1; b <= int(kGold) && !more; ++b) more = t.limit[b] && time <= t.limit[b];
        if (more) method(u, "deleteEntry", {Value::integer(i)});
    }
    if (m == kNone || !t.limit[m]) return;
    if (const u32 best = get32(s.license_record, base); best != kNoTime && best <= t.limit[m]) return;
    method(u, "insertEntry", {Value::integer(s32(t.limit[m])), Value::string(m == kGold ? "gold" : m == kSilver ? "silver" : "bronze"), Value::string("--------")});
}

bool SaveEditor::set_time(const LicenseTest& t, u32 ms) {
    if (t.ranking < 0 || ms == 0 || !t.limit[kBronze] || ms > t.limit[kBronze]) return false;
    const Medal m = ms <= t.limit[kGold] ? kGold : ms <= t.limit[kSilver] ? kSilver : kBronze;
    const Value u = method(rt_->vm().attribute(game(), "license_record"), "getUnit", {Value::string(t.label)});
    if (u.is_nil()) return false;
    GameState& s = state();
    const size_t base = size_t(t.ranking) * kLicenseUnit;
    // Every better time goes, and this one is the first.
    for (int i = int(kRankEntries) - 1; i >= 0; --i)
        if (get32(s.license_record, base + size_t(i) * kRankEntry) <= ms) method(u, "deleteEntry", {Value::integer(i)});
    method(u, "insertEntry", {Value::integer(s32(ms)), Value::string(m == kGold ? "gold" : m == kSilver ? "silver" : "bronze"), Value::string("--------")});
    const u32 result = m == kGold ? 9 : m == kSilver ? 10 : 11;
    unit(t.unit) = (unit(t.unit) & ~0xFFFu) | 1u | result << 4 | result << 8;
    return true;
}

bool SaveEditor::license_held(const License& l) {
    return method(rt_->vm().attribute(game(), "race_record"), "isLicensePassed", {Value::string(l.category)}).truthy();
}

// ---- Events -------------------------------------------------------------------------------------------------------------------

void SaveEditor::read_events() {
    events_.clear();
    Database& d = natives().db();
    const Value all = lookup("GTmodeProject::EventData::AllEvents");
    const std::vector<Value>* lists = all.items();
    for (size_t i = 0; lists && i < lists->size(); ++i) {
        const std::vector<Value>* items = (*lists)[i].items();
        if (!items || items->empty()) continue;
        EventList list;
        const std::string key = string_of((*items)[0]);
        list.title = text("EventHallRoot", "", key);
        if (list.title == key) {
            list.title = text("EventHallRoot", "", "INFO|" + key);
            if (list.title == "INFO|" + key) list.title = text("EventHallRoot", "INFO", key);
        }
        for (size_t j = 1; j < items->size(); ++j) {
            Event e;
            e.code = string_of(rt_->vm().attribute((*items)[j], "race_code"));
            if (e.code.size() < 6) continue;
            e.name = text("EventHallRoot", "EventName", e.code.substr(0, e.code.size() - 5));
            if (e.name.empty()) e.name = e.code;
            const Value level = call(lookup("GTmodeProject::getEventLevelByRaceCode"), {Value::string(e.code)});
            if (level.kind == Kind::String) e.level = text("EventHallRoot", "", "Level_" + level.as_string());
            e.championship = call(lookup("GTmodeProject::isChampionship"), {Value::string(e.code)}).truthy();
            if (e.championship) e.unit = d.race_unit(e.code);
            const Value subs = call(lookup("GTmodeProject::extractSubRaceCode"), {Value::string(e.code)});
            if (const std::vector<Value>* races = subs.items())
                for (const Value& r : *races) {
                    EventRace er;
                    er.label = string_of(r), er.unit = d.race_unit(er.label);
                    const ByteSpan row = d.row("RACE", er.label);
                    if (row.size() >= 8) er.name = course_name(d.label(u64(r16(row, 0)) | u64(r16(row, 2)) << 16 | u64(r16(row, 4)) << 32 | u64(r16(row, 6)) << 48));
                    if (er.unit >= 0) e.races.push_back(std::move(er));
                }
            if (!e.races.empty()) list.events.push_back(std::move(e));
        }
        if (!list.events.empty()) events_.push_back(std::move(list));
    }
}

int SaveEditor::place(int u) const {
    if (u < 0) return kNotRun;
    const u32 w = state().race_units[size_t(u)];
    if ((w & 15) == 0) return kNotRun;
    const u32 r = w >> 4 & 15;
    return r < 6 ? int(r) : kOther;
}

bool SaveEditor::entered(const Event& e) const {
    for (const EventRace& r : e.races)
        if (state().race_units[size_t(r.unit)] & 15) return true;
    return false;
}

bool SaveEditor::won(const Event& e) const {
    if (e.championship) return place(e.unit) == 0;
    for (const EventRace& r : e.races)
        if (place(r.unit) != 0) return false;
    return true;
}

void SaveEditor::set_place(const Event& e, int race, int p) {
    if (race < 0 || size_t(race) >= e.races.size()) return;
    u32& w = unit(e.races[size_t(race)].unit);
    if (p < 0) {
        w = kFreshUnit;
        return;
    }
    const u32 r = u32(std::min(p, 5));
    // A single race: the result shown on the map is its place, the last result taken by the present page. A championship's race
    // shows its best, its last result stays as the race left it.
    w = (w & 0xFFFF0000u) | 2u | r << 4 | (e.championship ? r << 8 | 15u << 12 : 15u << 8 | r << 12);
}

void SaveEditor::set_series(const Event& e, int p) {
    if (!e.championship || e.unit < 0) return;
    u32& w = unit(e.unit);
    w = p < 0 ? kFreshUnit : (w & 0xFFFF0000u) | 2u | u32(std::min(p, 5)) << 4 | 15u << 8 | 15u << 12;
}

void SaveEditor::set_won(const Event& e, bool on) {
    // (A race already won stays as it is: its prize car may be waiting.)
    for (size_t i = 0; i < e.races.size(); ++i)
        if (!on || place(e.races[i].unit) != 0) set_place(e, int(i), on ? 0 : kNotRun);
    if (!on || place(e.unit) != 0) set_series(e, on ? 0 : kNotRun);
}

void SaveEditor::win_all(bool on) {
    for (const EventList& l : events_)
        for (const Event& e : l.events) set_won(e, on);
}

void SaveEditor::set_score(int u, int points) {
    if (u < 0) return;
    unit(u) = (unit(u) & ~(0xFFu << 16)) | u32(std::clamp(points, 0, 255)) << 16;
}

// The present page's test for an event ("win"): a championship's own last result first; else every race shown first on the map
// or about to be (its last result), one of them better than what the map showed.
bool SaveEditor::prize_waiting(const Event& e) const {
    if (e.championship) return e.unit >= 0 && (state().race_units[size_t(e.unit)] >> 8 & 15) == 0;
    bool now = false;
    for (const EventRace& r : e.races) {
        const u32 w = state().race_units[size_t(r.unit)], last = w >> 8 & 15, shown = w >> 12 & 15;
        if (last != 15) {
            if (last != 0 && shown != 0) return false;
            now = now || (last < 8 && last < shown);
        } else if (shown != 0) return false;
    }
    return now;
}

void SaveEditor::set_prize_waiting(const Event& e, bool on) {
    if (!won(e) || on == prize_waiting(e)) return;
    if (e.championship) {
        u32& w = unit(e.unit);
        w = (w & ~0xF00u) | (on ? 0u : 15u) << 8;
        return;
    }
    for (const EventRace& r : e.races) {
        u32& w = unit(r.unit);
        w = (w & ~0xFF00u) | (on ? 15u << 12 : 15u << 8);
    }
}

void SaveEditor::all_prizes_waiting() {
    for (const EventList& l : events_)
        for (const Event& e : l.events)
            if (!e.prize.empty() && won(e)) set_prize_waiting(e, true);
}

// ---- Missions -----------------------------------------------------------------------------------------------------------------

void SaveEditor::read_missions() {
    missions_.clear();
    Database& d = natives().db();
    for (int k = 1; k <= 99; ++k) {
        char label[16], key[24];
        std::snprintf(label, sizeof label, "l0m%04d", k);
        Mission m;
        m.label = label, m.unit = d.race_unit(label);
        if (m.unit < 0) break;
        std::snprintf(key, sizeof key, "TITLE_%02d", k);
        m.name = text("MissionRoot", "MissionName", key);
        if (m.name == key) break;
        std::snprintf(key, sizeof key, "COURSE_%02d", k);
        m.course = text("MissionRoot", "MissionName", key);
        missions_.push_back(std::move(m));
    }
}

void SaveEditor::set_mission(const Mission& m, int p) {
    u32& w = unit(m.unit);
    // (Its run writes the best and the last result: unit_result, kind 3.)
    w = p < 0 ? kFreshUnit : (w & 0xFFFF0000u) | 3u | u32(std::min(p, 5)) << 4 | u32(std::min(p, 5)) << 8 | 15u << 12;
}

// ---- Presents -----------------------------------------------------------------------------------------------------------------

void SaveEditor::read_presents() {
    presents_.clear();
    const Value data = lookup("GTmodeProject::PresentData");
    if (data.is_nil()) return;
    const Value conditions = lookup("GTmodeProject::PresentData::condition_carlist");
    const std::vector<Value>* list = conditions.items();
    const size_t n = list ? list->size() : 0;
    for (size_t i = 0; i < n; ++i) {
        const Value column = call(lookup("GTmodeProject::PresentRoot::makeColumn"), {Value::integer(s32(i))});
        const std::vector<Value>* c = column.items();
        if (!c || c->size() < 5) continue;
        const std::string kind = string_of((*c)[0]), about = string_of((*c)[1]), type = string_of((*c)[2]), what = string_of((*c)[3]);
        const std::string extra = string_of((*c)[4]);
        if (kind.empty()) continue;
        const bool car = type == "car";
        int paint = -1;
        if (car && extra != "-" && extra != "ending") paint = std::max(0, std::atoi(extra.c_str()) - 1);
        if (kind == "win") {
            for (EventList& l : events_)
                for (Event& e : l.events)
                    if (car && (e.code == about || (!e.championship && e.code.substr(0, e.code.size() - 2) == about.substr(0, std::max<size_t>(about.size(), 2) - 2))))
                        e.prize = what, e.prize_paint = paint, e.prize_ending = extra == "ending"; // (the present's test goes by the races, whichever of them it names)
            continue;
        }
        Present p;
        p.id = int(i), p.kind = kind, p.about = about, p.paint = paint, p.ending = type == "movie" || extra == "ending";
        if (car) p.car = what, p.what = car_name(what);
        else p.what = type == "paper" ? text("PresentRoot", "LicenseName", about.substr(0, 3) + "0000") + ": its certificate" : "A movie";
        auto license = [&](const char* suffix) { p.how = text("PresentRoot", "LicenseName", about.substr(0, 3) + "0000") + suffix; };
        auto missions = [&](const char* place) {
            const size_t comma = about.find(',');
            const int from = std::atoi(about.c_str()), to = comma == std::string::npos ? from : std::atoi(about.c_str() + comma + 1);
            p.how = "Missions " + std::to_string(from) + " to " + std::to_string(to) + ", " + place + " in each";
        };
        if (kind == "license") license("");
        else if (kind == "all_silver") license(", silver or better in every test");
        else if (kind == "all_gold") license(", gold in every test");
        else if (kind == "mission_1st") missions("first");
        else if (kind == "mission_2nd") missions("second or better");
        else if (kind == "mission_3rd") missions("third or better");
        else if (kind == "achievement") p.how = about + " % of the game complete";
        else p.how = kind + " " + about;
        presents_.push_back(std::move(p));
    }
}

bool SaveEditor::present_given(const Present& p) const { return state().presents[size_t(p.id) >> 5] >> (p.id & 31) & 1; }

void SaveEditor::set_present_given(const Present& p, bool on) {
    u32& w = state().presents[size_t(p.id) >> 5];
    w = on ? w | 1u << (p.id & 31) : w & ~(1u << (p.id & 31));
}

bool SaveEditor::present_earned(const Present& p) {
    if (present_given(p)) return false;
    completion();
    const Value column = call(lookup("GTmodeProject::PresentRoot::makeColumn"), {Value::integer(p.id)});
    const Value rule = lookup("GTmodeProject::PresentRoot::" + p.kind);
    return rule.kind == Kind::Function && call(rule, {Value::integer(p.id), column}).truthy();
}

int SaveEditor::presents_waiting() {
    int n = 0;
    for (const Present& p : presents_) n += !p.ending && !p.car.empty() && present_earned(p);
    for (const EventList& l : events_)
        for (const Event& e : l.events) n += !e.prize.empty() && !e.prize_ending && prize_waiting(e);
    return n;
}

int SaveEditor::receive_presents(bool* full) {
    if (full) *full = false;
    int got = 0;
    // PresentRoot's get_car: the car as new (its paint the list's, else any), the diary's day, the car open in Arcade, the prize
    // cars' count.
    auto take = [&](const std::string& label, int paint) {
        const int paints = std::max(1, int(this->paints(label).size()));
        if (paint < 0) paint = int(natives().random.next01() * float(paints));
        paint = std::clamp(paint, 0, paints - 1);
        if (state().garage_count() >= int(GameState::kGarageSize)) {
            if (full) *full = true;
        } else {
            method(rt_->vm().attribute(game(), "garage"), "addNewCar", {Value::string(label), Value::integer(paint)});
            method(rt_->vm().attribute(game(), "calendar"), "putPresentCarEvent", {Value::string(label), Value::integer(paint)});
            ++got;
        }
        state().available_cars.insert(label);
        ++state().total_prize_cars;
    };
    for (const EventList& l : events_)
        for (const Event& e : l.events)
            if (!e.prize.empty() && !e.prize_ending && prize_waiting(e)) {
                // (What the present page's test leaves: the last results taken, shown on the map.)
                if (e.championship) unit(e.unit) |= 0xF00u;
                else
                    for (const EventRace& r : e.races) {
                        u32& w = unit(r.unit);
                        if (const u32 last = w >> 8 & 15; last != 15) {
                            if (last < (w >> 12 & 15)) w = (w & ~0xF000u) | last << 12;
                            w |= 0xF00u;
                        }
                    }
                take(e.prize, e.prize_paint);
            }
    for (const Present& p : presents_) {
        if (p.ending || !present_earned(p)) continue;
        set_present_given(p, true);
        if (!p.car.empty()) take(p.car, p.paint);
    }
    return got;
}

// ---- Records ------------------------------------------------------------------------------------------------------------------

std::vector<SaveEditor::CourseRecord> SaveEditor::course_records() {
    std::vector<CourseRecord> out;
    GameState& s = state();
    Database& d = natives().db();
    auto read = [&](const Bytes& b, size_t at, CourseRecord r) {
        r.at = at;
        for (size_t e = 0; e < kRankEntries; ++e) {
            const size_t o = at + 8 + e * 0x30;
            CourseRecord::Entry x;
            x.value = get32(b, o);
            if (r.speed ? x.value == 0 : x.value == kNoTime) break;
            x.car = car_name(d.label(get64(b, o + 8)));
            if (const u16 y = u16(b[o + 4] | b[o + 5] << 8)) {
                char t[32];
                std::snprintf(t, sizeof t, "%04d-%02d-%02d", y, b[o + 6], b[o + 7]);
                x.date = t;
            }
            r.entries.push_back(std::move(x));
        }
        int marks = 0;
        for (size_t i = 0; i < 8; ++i) marks += b[at + 0x228 + i];
        r.marks = (marks * 100 + 8 * 255 / 2) / (8 * 255);
        out.push_back(std::move(r));
    };
    for (size_t u = 0; u < kCourseUnits; ++u) {
        const u64 code = get64(s.course_record, u * kCourseUnit);
        if (code == kNoCode) continue;
        CourseRecord r;
        r.label = d.label(code);
        r.name = course_name(r.label);
        read(s.course_record, u * kCourseUnit, std::move(r));
    }
    // ❓ The machine tests' order (game_records.cpp): 400 m, 1,000 m, the top speed, a fourth whose test is not known.
    static const char* const kTests[3] = {"Machine test: 400 m", "Machine test: 1,000 m", "Machine test: top speed"};
    for (size_t u = 0; u < 3; ++u) {
        CourseRecord r;
        r.name = kTests[u], r.machine = true, r.speed = u == 2;
        read(s.machine_record, u * kCourseUnit, std::move(r));
    }
    return out;
}

void SaveEditor::delete_entry(const CourseRecord& r, size_t entry) {
    Bytes& b = r.machine ? state().machine_record : state().course_record;
    if (entry >= kRankEntries) return;
    // (deleteEntry's own: the ones after it move up, the last place empty.)
    std::memmove(b.data() + r.at + 8 + entry * 0x30, b.data() + r.at + 8 + (entry + 1) * 0x30, (kRankEntries - 1 - entry) * 0x30);
    const size_t o = r.at + 8 + (kRankEntries - 1) * 0x30;
    std::memset(b.data() + o, 0, 0x30);
    put32(b, o, r.speed ? 0 : kNoTime);
    std::memset(b.data() + o + 8, 0xFF, 8);
    std::memset(b.data() + o + 0x10, '-', 8);
}

void SaveEditor::clear_record(const CourseRecord& r) {
    for (size_t e = r.entries.size(); e-- > 0;) delete_entry(r, e);
}

// ---- Arcade, courses ----------------------------------------------------------------------------------------------------------

void SaveEditor::read_courses() {
    courses_.clear();
    Database& d = natives().db();
    auto add = [&](const std::string& label, bool photo) {
        if (label.empty() || d.code(kCourseTable, label) == kNoCode) return;
        for (const Course& c : courses_)
            if (c.label == label) return;
        std::string name = course_name(label);
        if (photo) {
            // (The photo places' names are the photo page's.)
            if (const std::string t = text("PhotoTravelRoot", "CourseName", label); t != label && !t.empty()) name = t;
            name += " (photo travel)";
        }
        courses_.push_back({label, name});
    };
    for (const char* list : {"RealCircuitList", "RoadTrackList", "CityCourseList", "DirtAndSnowList", "DrivingParkList", "PowerAndSpeedList"})
        if (const std::vector<Value>* rows = lookup(std::string("GTmodeProject::EventData::") + list).items())
            for (const Value& row : *rows)
                if (const std::vector<Value>* pair = row.items())
                    for (const Value& label : *pair) add(string_of(label), false);
    if (const std::vector<Value>* rows = lookup("GTmodeProject::EventData::PhotoStageList").items())
        for (const Value& row : *rows)
            if (const std::vector<Value>* r = row.items(); r && !r->empty()) add(string_of((*r)[0]), true);
}

void SaveEditor::set_course_open(const Course& c, bool on) {
    if (on) state().available_courses.insert(c.label);
    else state().available_courses.erase(c.label);
}

void SaveEditor::open_all_courses(bool on) {
    for (const Course& c : courses_) set_course_open(c, on);
}

void SaveEditor::set_car_open(const std::string& label, bool on) {
    if (on) state().available_cars.insert(label);
    else state().available_cars.erase(label);
}

void SaveEditor::open_all_cars(bool on) {
    if (on) {
        for (const CarKind& c : cars_) state().available_cars.insert(c.label);
        return;
    }
    state().available_cars.clear();
    for (const GarageEntry& e : state().garage)
        if (e.used) state().available_cars.insert(e.car);
}

int SaveEditor::used_sold() const {
    int n = 0;
    for (u32 w : state().used_sold) n += std::popcount(w);
    return n;
}

} // namespace gt4r::menu
