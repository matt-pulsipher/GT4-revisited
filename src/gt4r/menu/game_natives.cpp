// main::game and the classes of the game's state as the scripts see them: MGame, MOption,
// MGarage, MCarData, MCourseData, MDatabase, MMemoryCardManager, MSound, MSystem and the
// smaller ones (MCarGarage: game_car.cpp; the calendar and the records: game_records.cpp;
// MRaceData: game_race.cpp). The member names are the program's (its registration tables); the
// values behind them are the runtime's GameState and the disc's car database.
// docs/formats/menu.md ("The game's state") says what each answers and why.

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/game_network.h"
#include "gt4r/menu/game_bspec.h"
#include "gt4r/formats/sound/sound_index.h"
#include "gt4r/menu/game_opponents.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/save/card_image.h"

#include "gt4r/save/memory_card.h"
#include "disc/core_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <ctime>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

// An attribute of an object that is a field of the state.
void field(Object& o, const std::string& name, std::string* to) {
    o.property(name, [to] { return Value::string(*to); }, [to](const Value& v) { *to = v.is_nil() ? std::string() : v.to_string(); });
}
void field(Object& o, const std::string& name, int* to) {
    o.property(name, [to] { return Value::integer(*to); }, [to](const Value& v) {
        *to = v.kind == Kind::Int ? v.i : v.kind == Kind::Float ? int(v.f) : v.truthy();
    });
}

Value option_value(const OptionValue& v) {
    if (const s32* i = std::get_if<s32>(&v)) return Value::integer(*i);
    if (const float* f = std::get_if<float>(&v)) return Value::number(*f);
    return Value::string(std::get<std::string>(v));
}

u32 r32(ByteSpan r, size_t o) { return r.size() >= o + 4 ? u32(r[o]) | u32(r[o + 1]) << 8 | u32(r[o + 2]) << 16 | u32(r[o + 3]) << 24 : 0; }

} // namespace

// ---- helpers shared by the game natives' files

script::Value fixed_for(const std::string& format, float value) {
    for (size_t at = format.find('%'); at != std::string::npos; at = format.find('%', at + 1)) {
        size_t j = at + 1;
        while (j < format.size() && std::strchr("-+ 0123456789.", format[j])) ++j;
        if (j >= format.size() || format[j] != 't') continue;
        const size_t dot = format.find('.', at);
        const int places = dot < j ? std::atoi(format.c_str() + dot + 1) : 0;
        return script::Value::integer(s32(std::lround(double(value) * std::pow(10.0, places))));
    }
    return script::Value::number(value);
}

script::Value format_with(Vm& vm, const Value& format, std::vector<Value> args) { return vm.call(vm.attribute(format, "format"), args); }

std::shared_ptr<script::Module> make_class(script::Module& into, const std::string& name) {
    auto m = std::make_shared<script::Module>();
    m->name = name, m->is_class = true;
    into.define(name, Value::of(Kind::Module, m));
    return m;
}

void static_fn(script::Module& cls, const std::string& name, Fn fn) { cls.define(name, Vm::native(cls.name + "::" + name, std::move(fn))); }

std::string money_text(s64 v) {
    if (v <= 0) return "--";
    const std::string digits = std::to_string(v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string time_text(u32 ms) {
    if (ms >= kNoTime) return "--'--.---";
    char b[40];
    const u32 h = ms / 3'600'000, m = ms / 60'000 % 60, s = ms / 1000 % 60, f = ms % 1000;
    if (h) std::snprintf(b, sizeof b, "%u:%02u'%02u.%03u", h, m, s, f);
    else std::snprintf(b, sizeof b, "%u'%02u.%03u", ms / 60'000, s, f);
    return b;
}

namespace {
bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
const int kBefore[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
} // namespace

u32 date_to_day(int y, int m, int d) {
    // A month out of 1 .. 12 carries into the year.
    y += (m - 1) >= 0 ? (m - 1) / 12 : -((12 - m) / 12);
    m = ((m - 1) % 12 + 12) % 12 + 1;
    const s64 p = y - 1;
    s64 days = 1'721'425 + 365 * p + p / 4 - p / 100 + p / 400 + kBefore[m - 1] + (m > 2 && leap(y)) + (d - 1);
    return u32(days);
}

void day_to_date(u32 day, int& y, int& m, int& d) {
    s64 n = s64(day) - 1'721'425; // days since 0001-01-01
    s64 p = n / 146097 * 400;
    n %= 146097;
    s64 c = std::min<s64>(n / 36524, 3);
    p += c * 100, n -= c * 36524;
    p += n / 1461 * 4, n %= 1461;
    s64 q = std::min<s64>(n / 365, 3);
    p += q, n -= q * 365;
    y = int(p + 1);
    m = 12;
    while (m > 1 && n < kBefore[m - 1] + (m > 2 && leap(y))) --m;
    d = int(n - kBefore[m - 1] - (m > 2 && leap(y))) + 1;
}

// next01 (0x578598): the draw made into a float in [0, 1) by hand: exponent 126 - z and the
// bits below the leading one shifted by z + 2 (z: the leading zeros): one more than a plain
// conversion (the bit under the leading one is dropped). Equal to the program's (gt4r_diff picker).
float ProgramRandom::next01() {
    const u32 x = step();
    if (!x) return 0;
    int z = 0;
    while (!(x & (0x80000000u >> z))) ++z;
    const u32 bits = (u32(126 - z) << 23) | ((x << ((z + 2) & 31)) >> 9);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// ---- Database

db::SpecDb* Database::get(const RuntimeConfig& config) {
    if (tried) return db.get();
    tried = true;
    if (config.program || config.program_image) {
        try {
            const std::shared_ptr<const gt3r::CoreImage> image = config.image();
            names = ProgramTables::find(image->program, image->load_address);
            program = sim::Program::from_core(image);
        } catch (const std::exception& e) {
            rt->note(std::string("the program's tables were not read: ") + e.what());
        }
    }
    if (!config.list) return nullptr;
    try {
        // The menus' lister takes a folder with its slash; the database names it without.
        const FolderLister list = config.list;
        // The names in the menus' language: Prologue (Japan) shows its cars' Japanese names
        // (CAR_NAME_japanese: "フェアレディ 240ZG '71"), the US disc the American ones.
        language = config.language == "JP" ? "japanese" : "american";
        db = std::make_unique<db::SpecDb>(rt->reader(), [list](const std::string& dir) { return list(!dir.empty() && dir.back() == '/' ? dir : dir + "/"); }, language);
        cars = db->cars();
        for (size_t i = 0; i < cars.size(); ++i) by_label[cars[i].label] = i;
        courses = db->courses();
    } catch (const std::exception& e) {
        rt->note(std::string("no car database: ") + e.what());
        db.reset();
    }
    return db.get();
}

const db::CarEntry* Database::car(const std::string& label) const {
    const auto it = by_label.find(label);
    return it == by_label.end() ? nullptr : &cars[it->second];
}

ByteSpan Database::row(const std::string& table, const std::string& label) {
    const db::Labels* l = db ? db->labels(table) : nullptr;
    const db::Table* t = db ? db->table(table) : nullptr;
    const std::optional<u32> id = l ? l->id(label) : std::nullopt;
    return t && id ? t->row(*id) : ByteSpan{};
}

const db::Labels* Database::labels_of(u32 table) {
    if (!db) return nullptr;
    switch (table) {
    case kCarTable: return db->labels("GENERIC_CAR");
    case kCourseTable: return db->labels("COURSE");
    case kRaceTable: return db->labels("RACE");
    case kEnemyTable: return db->labels("ENEMY_CARS");
    default: {
        const char* name = db::part_table_name(table);
        return name ? db->labels(name) : nullptr;
    }
    }
}

u64 Database::code(u32 table, const std::string& label) {
    const db::Labels* l = labels_of(table);
    const std::optional<u32> id = l && !label.empty() ? l->id(label) : std::nullopt;
    return id ? make_code(table, *id) : kNoCode;
}

std::string Database::label(u64 c) {
    if (c == kNoCode) return {};
    const db::Labels* l = labels_of(u32(c >> 32));
    const std::string* s = l ? l->label(u32(c)) : nullptr;
    return s ? *s : std::string();
}

int Database::race_unit(const std::string& label) {
    const db::Table* t = db ? db->table("RACE") : nullptr;
    const db::Labels* l = labels_of(kRaceTable);
    if (!t || !l || label.empty()) return -1;
    for (const std::string& tried : {label, "us" + label, "jp" + label}) {
        const std::optional<u32> id = l->id(tried);
        if (!id) continue;
        const std::vector<u32>& ids = t->ids();
        const auto it = std::lower_bound(ids.begin(), ids.end(), *id);
        if (it == ids.end() || *it != *id) return -1;
        const int unit = int(it - ids.begin());
        return unit < 768 ? unit : -1;
    }
    return -1;
}

std::string Database::race_unit_label(int unit) {
    const db::Table* t = db ? db->table("RACE") : nullptr;
    const db::Labels* l = labels_of(kRaceTable);
    if (!t || !l || unit < 0 || size_t(unit) >= t->ids().size()) return {};
    const std::string* s = l->label(t->ids()[size_t(unit)]);
    return s ? *s : std::string();
}

const sim::Showroom* Database::showroom() {
    if (!db || !program.known()) return nullptr;
    if (!showroom_) showroom_ = std::make_unique<sim::Showroom>(*db, program);
    return showroom_.get();
}

std::optional<sim::ShowroomFigures> Database::stock_showroom(const std::string& label) {
    if (const auto it = stock_figures_.find(label); it != stock_figures_.end()) return it->second;
    const sim::Showroom* s = showroom();
    std::optional<sim::ShowroomFigures> f = s ? s->stock(label) : std::nullopt;
    stock_figures_[label] = f;
    return f;
}

const Database::Spec& Database::spec(const std::string& label) {
    const auto it = specs.find(label);
    if (it != specs.end()) return it->second;
    Spec& sp = specs[label];
    if (!db) return sp;
    auto u16at = [](ByteSpan r, size_t at) { return r.size() >= at + 2 ? int(u16(r[at]) | u16(r[at + 1]) << 8) : 0; };
    const db::Strings* units = db->strings("UnistrDB");
    // The car's parts as sold: its row of DEFAULT_PARTS (pairs of a part's row and its table).
    auto part = [&](u32 table_id) -> std::optional<u32> {
        const db::Table* parts = db->table("DEFAULT_PARTS");
        const ByteSpan car = car_row(label);
        if (!parts || car.size() < 4) return std::nullopt;
        const ByteSpan row = parts->row(r32(car, 0));
        for (size_t o = 0; o + 8 <= row.size(); o += 8)
            if (r32(row, o + 4) == table_id && r32(row, o) != 0xFFFFFFFFu) return r32(row, o);
        return std::nullopt;
    };
    // ENGINE: six texts of the unit pool (displacement, layout, valve gear, aspiration, the
    // engine speeds of the power and the torque), +0x1A PS, +0x1C kgf.m x 100.
    if (const std::optional<u32> id = part(12))
        if (const db::Table* engines = db->table("ENGINE"))
            if (const ByteSpan r = engines->row(*id); r.size() >= 0x1E) {
                sp.engine = true;
                sp.ps = u16at(r, 0x1A), sp.torque = float(u16at(r, 0x1C)) / 100.0f;
                if (units) {
                    sp.displacement = units->at(r32(r, 0)), sp.layout = units->at(r32(r, 4)), sp.valves = units->at(r32(r, 8));
                    sp.aspiration = units->at(r32(r, 12)), sp.power_rpm = units->at(r32(r, 16)), sp.torque_rpm = units->at(r32(r, 20));
                }
            }
    // CHASSIS: +0x0C the catalog's weight; +0x08, +0x0A its length and height.
    if (const std::optional<u32> id = part(6))
        if (const db::Table* chassis = db->table("CHASSIS"))
            if (const ByteSpan r = chassis->row(*id); r.size() >= 0x0E) sp.weight = u16at(r, 0x0C), sp.length = u16at(r, 0x08), sp.height = u16at(r, 0x0A);
    if (const std::optional<u32> id = part(10))
        if (const db::Table* trains = db->table("DRIVETRAIN"))
            if (const ByteSpan r = trains->row(*id); r.size() > 0x10) sp.drivetrain = r.size() > 0x0F && r[0x0F] == 1 ? r[0x10] : -1;
    // The paints: the car's rows of VARIATION<language>; paint c is the row whose +0x04 is
    // c + 1 (0x4458D0).
    const db::Labels* cars_ = db->labels("GENERIC_CAR");
    const db::Table* car_paints = db->table("CAR_VARIATION_american");
    const db::Table* paints = db->table("VARIATIONamerican");
    const db::Strings* names_ = db->strings("american_StrDB");
    const std::optional<u32> car_id = cars_ ? cars_->id(label) : std::nullopt;
    if (car_id && car_paints && paints)
        if (const ByteSpan row = car_paints->row(*car_id); row.size() >= 4) {
            size_t first = 0;
            const size_t n = paints->rows(r32(row, 0), first);
            sp.paints.resize(n);
            for (size_t i = 0; i < n; ++i) {
                const ByteSpan r = paints->row_at(first + i);
                const u32 at = r32(r, 0x04);
                Paint& paint = sp.paints[at >= 1 && at <= n ? at - 1 : i];
                if (names_) paint.name = names_->at(r32(r, 0x0C));
                if (r.size() >= 0x2C) std::memcpy(paint.flags, r.data() + 0x24, 8);
                if (r.size() >= 0x34) std::memcpy(paint.chip, r.data() + 0x24, 16);
                if (r.size() >= 0x27) paint.rgb[0] = r[0x24], paint.rgb[1] = r[0x25], paint.rgb[2] = r[0x26];
                // (Rows without the words, Prologue's: the one colour.)
                if (!(paint.chip[0] & 0x1000000u)) paint.chip[0] = u32(paint.rgb[0]) | u32(paint.rgb[1]) << 8 | u32(paint.rgb[2]) << 16 | 0x1000000u;
            }
        }
    return sp;
}

CarHandle* car_handle(const script::Value& v) {
    const std::shared_ptr<CarObject> o = std::dynamic_pointer_cast<CarObject>(v.p);
    return o ? o->car.get() : nullptr;
}

// ---- MSound: every call goes to Runtime::sound, the one place a sound player is wired in.
std::shared_ptr<Object> GameNatives::make_music(const std::string& object) {
    auto m = std::make_shared<Object>("MMusic");
    Runtime* r = &rt;
    for (const char* name : {"load", "unload", "play", "pause", "playex"})
        m->method(name, [r, object, name](Vm&, const Value&, Args a) {
            r->sound(object, name, a);
            return Value{};
        });
    m->method("isPlaying", [](Vm&, const Value&, Args) { return number(0); });
    m->set("volume", Value::number(1)), m->set("repeat", number(0)), m->set("number", number(0));
    return m;
}

Value GameNatives::make_sound() {
    if (sound) return object_value(sound);
    sound = std::make_shared<Object>("MSound");
    Runtime* r = &rt;
    for (const char* name : {"load", "unload", "play", "keepMusic", "startMusic", "stopMusic", "fadeOutMusic", "fadeInMusic", "startStream",
                             "startStreamEx", "stopStream", "pauseStream"})
        sound->method(name, [r, name](Vm&, const Value&, Args a) {
            r->sound("sound", name, a);
            return Value{};
        });
    sound->method("isPlayingStream", [](Vm&, const Value&, Args) { return number(0); });
    sound->set("midi", object_value(make_music("sound.midi")));
    sound->set("stream", object_value(make_music("sound.stream")));
    return object_value(sound);
}

// ---- MOption
Value GameNatives::make_option() {
    if (option) return object_value(option);
    option = std::make_shared<Object>("MOption");
    GameState* s = &state();
    for (const auto& [name, value] : s->options) {
        const std::string n = name;
        option->property(n, [s, n] { return option_value(s->options[n]); }, [s, n](const Value& v) {
            if (v.kind == Kind::String) s->options[n] = v.as_string();
            else if (v.kind == Kind::Float) s->options[n] = v.f;
            else s->options[n] = v.kind == Kind::Int ? v.i : s32(v.truthy());
        });
    }
    // The volumes as the scripts pass them on: the raw option / 128 (menu_bgm_volume's 0x180AA8 and its six likes 0x180DA0,
    // 0x181098, 0x181390, 0x181688, 0x181980, 0x181B80, the same routine but for the field); written, x 128 truncated.
    for (const char* name : {"menu_bgm_volume", "menu_se_volume", "race_bgm_volume", "race_se_volume", "replay_bgm_volume", "replay_se_volume", "slide_bgm_volume"}) {
        const std::string raw = std::string(name) + "_raw";
        option->property(name, [s, raw] { return Value::number(option_value(s->options[raw]).as_float() / 128.0f); },
                         [s, raw](const Value& v) { s->options[raw] = s32(v.as_float() * 128.0f); });
    }
    // The screen settings: [brightness, contrast, saturation, color balance] as the Picture Quality
    // page reads and writes them (the program's four numbers at Option +0x1184, +0x1194).
    for (const char* name : {"race_color_correction", "replay_color_correction"}) {
        const std::string n = name;
        option->property(n, [s, n] {
            const std::array<int, 4> v = picture_from_text(s->option_text(n), {0, 0, 0, 0});
            return Value::array({number(v[0]), number(v[1]), number(v[2]), number(v[3])});
        }, [s, n](const Value& v) {
            const std::vector<Value>* items = v.items();
            if (!items || items->size() < 4) return;
            std::array<int, 4> p{};
            for (size_t i = 0; i < 4; ++i) p[i] = int((*items)[i].as_float());
            s->options[n] = picture_text(p);
        });
    }
    Runtime* r = &rt;
    // apply(), save(): the settings take effect (Runtime::on_options) and are kept (under the
    // data folder).
    GameNatives* np = this;
    auto save = [r, np](Vm&, const Value&, Args) {
        apply_monitor(*np);
        r->save_options();
        r->options_changed();
        return number(1);
    };
    option->method("apply", save);
    option->method("save", save);
    option->method("load", [](Vm&, const Value&, Args) { return number(1); });
    option->method("setDefault", [s, r](Vm&, const Value&, Args) {
        const std::string zone = s->option_text("game_zone");
        const std::array<std::string, 2> controllers{s->option_text("controllers_1p"), s->option_text("controllers_2p")};
        s->option_defaults(zone.empty() ? r->region() : zone);
        reset_controllers(*s, controllers);
        r->options_changed();
        return Value{};
    });
    // setGameZoneDefault (0x435D48): the zone by the program's region.
option->method("setGameZoneDefault", [s, r](Vm&, const Value&, Args) {
    const std::string region = r->region();
    s->options["game_zone"] = std::string(region == "us" ? "US" : region == "uk" ? "EU" : region == "kr" ? "KR" : "JP");
    return Value{};
});
for (const char* nothing : {"applyRaceDisplayMode", "applyMenuDisplayMode"})
        option->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    install_controllers(*this, *option);
    // The music and slide-show play lists (GameState::PlayList): `play_data` is a row a piece of
    // the streamed `racing` group, each [place in the order, played, enabled, the place's copy]
    // (the configuration's init_raceplaylist writes them: places for the region's pieces, -1 and
    // disabled for the others). A list not filled yet has the group's count of rows (the disc's
    // bgm/<region>/ads.inf, its third group; ❓ the slide show's list as long, as a saved game
    // has it). ❓ initShuffle is taken as the program's making of the order again: position 0,
    // every enabled row unplayed, "none" when no row is enabled.
    for (const auto& [list_name, list] : {std::pair<const char*, GameState::PlayList*>{"bgm_playlist", &state().bgm_playlist}, {"slide_playlist", &state().slide_playlist}}) {
        auto p = std::make_shared<Object>("MPlayList");
        GameState::PlayList* l = list;
        Runtime* r = &rt;
        auto rows = [l, r]() -> u32 {
            if (!l->rows) {
                for (const std::string& path : {"bgm/" + r->region() + "/ads.inf", std::string("bgm/us/ads.inf")}) {
                    const std::optional<Bytes> inf = r->reader()(path);
                    if (!inf || inf->size() < 0x14 + 3 * 8) continue;
                    auto w = [&inf](size_t o) { return u32((*inf)[o]) | u32((*inf)[o + 1]) << 8 | u32((*inf)[o + 2]) << 16 | u32((*inf)[o + 3]) << 24; };
                    if (w(0x0C) >= 3) l->rows = std::min<u32>(w(0x14 + 2 * 8 + 4), 128);
                    break;
                }
            }
            return std::min<u32>(l->rows, 128);
        };
        p->property("play_data", [l, rows] {
            std::vector<Value> out;
            for (u32 i = 0; i < rows(); ++i) {
                const GameState::PlayList::Row& row = l->row[i];
                out.push_back(Value::array({number(row.order), number(row.played), number(row.enabled), number(row.order2)}));
            }
            return Value::array(std::move(out));
        }, [l, rows](const Value& v) {
            const std::vector<Value>* items = v.items();
            if (!items) return;
            const u32 n = std::min<u32>(rows(), u32(items->size()));
            for (u32 i = 0; i < n; ++i) {
                const std::vector<Value>* d = (*items)[i].items();
                if (!d || d->size() < 4) continue;
                GameState::PlayList::Row& row = l->row[i];
                row.order = gt3r::s8((*d)[0].as_float()), row.played = u8((*d)[1].as_float() != 0), row.enabled = u8((*d)[2].as_float() != 0), row.order2 = gt3r::s8((*d)[3].as_float());
            }
            // ❓ "none" follows the rows given (a saved game after the configuration's list has 0).
            l->none = 1;
            for (u32 i = 0; i < rows(); ++i)
                if (l->row[i].enabled) l->none = 0;
        });
        p->method("initShuffle", [l, rows](Vm&, const Value&, Args) {
            bool any = false;
            for (u32 i = 0; i < rows(); ++i) l->row[i].played = !l->row[i].enabled, any |= l->row[i].enabled != 0;
            l->none = !any, l->position = 0;
            return Value{};
        });
        p->property("shuffle", [l] { return number(l->shuffle); }, [l](const Value& v) { l->shuffle = v.truthy(); });
        option->set(list_name, object_value(p));
    }
    return object_value(option);
}

// ---- The garage (GranTurismo4::GarageScratch; save.md "The garage") of a game's state: the game played's,
// or another card's for trading cars (loadInstance).
std::shared_ptr<Object> GameNatives::make_garage(GameState* s) {
    auto garage = std::make_shared<Object>("MGarage");
    GameNatives* self = this;
    auto valid = [s](s32 i) { return i >= 0 && size_t(i) < s->garage.size() && s->garage[size_t(i)].used; };
    garage->property("count", [s] { return number(s->garage_count()); });
    garage->method("isFull", [s](Vm&, const Value&, Args) { return number(s->garage_count() >= int(GameState::kGarageSize)); });
    garage->method("hasRidingCar", [s, valid](Vm&, const Value&, Args) { return number(valid(s->riding_car)); });
    // The car driven: an object on the garage's own record (a vacant car when there is none).
    garage->property("riding_car", [self, s, valid] {
        if (!valid(s->riding_car)) {
            auto c = std::make_shared<CarHandle>();
            c->state = s;
            return make_car_value(*self, c);
        }
        auto c = car_from_entry(self->db(), *s, s->riding_car, true);
        car_figures(self->db(), c->record(), c->power100, c->torque100, c->weight);
        return make_car_value(*self, c);
    });
    // addCar(car): the car's record at the first free place (0x435780); addNewCar(label,
    // paint): a new car (0x435800, the day it was got); addUsedCar(car): a used car's. The
    // place, -1 when the garage is full.
    auto add = [self, s](bool used) {
        return [self, s, used](Vm& vm, const Value&, Args a) {
            GarageEntry e;
            e.day = s->today;
            if (CarHandle* h = a.empty() ? nullptr : car_handle(a[0])) {
                e.car = h->label, e.record = h->record();
                entry_from_record(self->db(), e);
                if (!used) e.odometer = 0;
            } else {
                e.car = arg_text(a, 0);
                CarRecord r;
                if (!set_car(self->db(), r, e.car, arg_int(a, 1))) {
                    if (e.car.empty()) return number(-1);
                    e.paint = arg_int(a, 1);
                } else {
                    // The entry's car (0x434E38): as sold, with the odometer's condition (0x440F78: a new car's engine
                    // at 0 m, run in), its figures (0x434FD0).
                    make_used_condition(self->db(), r, 0);
                    e.record = r;
                    entry_from_record(self->db(), e);
                    e.record.reset(); // a new car is the car as sold until it is driven
                }
            }
            (void)vm;
            if (e.car.empty()) return number(-1);
            return number(s->add_car(std::move(e)));
        };
    };
    garage->method("addCar", add(false));
    garage->method("addNewCar", add(false));
    garage->method("addUsedCar", add(true));
    // delCar(place): sold; the place stays (undelCar puts it back). 1 when it was the car driven.
    garage->method("delCar", [s, valid](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0, -1);
        if (!valid(i)) return number(0);
        s->garage[size_t(i)].used = false;
        if (s->riding_car != i) return number(0);
        s->riding_car = -1;
        return number(1);
    });
    garage->method("undelCar", [s](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0, -1);
        if (i < 0 || size_t(i) >= s->garage.size()) return Value{};
        s->garage[size_t(i)].used = true;
        if (arg_int(a, 1)) s->riding_car = i;
        return Value{};
    });
    garage->method("delAllCarExceptRidingCar", [s](Vm&, const Value&, Args) {
        for (size_t i = 0; i < s->garage.size(); ++i)
            if (int(i) != s->riding_car) s->garage[i].used = false;
        return Value{};
    });
    // A change of the car driven (0x435388): its record (the car's given, or, from the card,
    // the place's own: one never driven is made as sold, with its odometer, 0x435568, and the
    // change is marked as such), the place has a record, and the order of driving: the place
    // first, every car driven more recently than it one further (0x435690).
    auto ride = [self, s, valid](s32 i, CarHandle* with) {
        if (!valid(i)) return false;
        GarageEntry& e = s->garage[size_t(i)];
        if (with) e.record = with->record();
        else {
            s->riding_fresh = !e.record;
            if (!e.record) e.record = car_from_entry(self->db(), *s, i, false)->own;
        }
        if (const int old = e.ride_rank & 0x3FF; old != 0) {
            for (GarageEntry& o : s->garage)
                if ((o.ride_rank & 0x3FF) < old) o.ride_rank = (o.ride_rank + 1) & 0x3FF;
            e.ride_rank = 0;
        }
        s->riding_car = i;
        return true;
    };
    // setRidingCar(place, car): the car given is driven from that place (0x16E108 -> 0x435388).
    // setRidingCar(place, label, paint): the car driven is that car as sold, from that place,
    // the place's marks and the order left as they are (0x435490).
    garage->method("setRidingCar", [self, s, ride, valid](Vm&, const Value&, Args a) {
        if (a.size() >= 3) {
            const s32 i = arg_int(a, 0, -1);
            if (!valid(i)) return Value{};
            CarRecord r;
            if (set_car(self->db(), r, arg_text(a, 1), arg_int(a, 2))) s->garage[size_t(i)].record = r;
            s->riding_car = i;
            return Value{};
        }
        ride(arg_int(a, 0, -1), a.size() > 1 ? car_handle(a[1]) : nullptr);
        return Value{};
    });
    // cancelRidingCar(place, car): only after a change to a car never driven (0x435418): that
    // car's record is dropped again and the car given is driven from its place.
    garage->method("cancelRidingCar", [s, ride](Vm&, const Value&, Args a) {
        if (!s->riding_fresh) return Value{};
        if (s->riding_car >= 0 && size_t(s->riding_car) < s->garage.size()) s->garage[size_t(s->riding_car)].record.reset();
        ride(arg_int(a, 0, -1), a.size() > 1 ? car_handle(a[1]) : nullptr);
        s->riding_fresh = false;
        return Value{};
    });
    // changeRidingCar(place, manager), pushRidingCar(manager), popGarageCar(place, car,
    // manager): the program writes and reads the car's slot of the garage file here; the
    // cars' records are in the state and go to the card when the game is saved. They answer as
    // a card operation does.
    garage->method("changeRidingCar", [ride](Vm&, const Value&, Args a) { return text(ride(arg_int(a, 0, -1), nullptr) ? "OK" : "INVALID_FILE"); });
    garage->method("pushRidingCar", [](Vm&, const Value&, Args) { return text("OK"); });
    garage->method("popGarageCar", [self, s, valid](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0, -1);
        if (!valid(i)) return text("INVALID_FILE");
        if (CarHandle* into = a.size() > 1 ? car_handle(a[1]) : nullptr) {
            auto c = car_from_entry(self->db(), *s, i, false);
            *into = *c;
            car_figures(self->db(), into->record(), into->power100, into->torque100, into->weight);
        }
        return text("OK");
    });
    // pickup(country, tuner, drivetrain): the garage's cars in place order whose country and
    // tuner (the car's columns, by their names' numbers) and the drivetrain of the sheet in use
    // are the ones named ("all" or a name not known: any), each a car object of its place
    // (0x16EE90 -> 0x170088; the car driven's list columns refreshed first, 0x435630).
    // pickupCountry(), pickupTuner(country): the countries and tuners the garage has cars of,
    // in the order of their numbers (0x170278, 0x1705E0).
    auto of_label = [](Vm& vm, const char* fn, const std::string& label) {
        const Value arg[1] = {text(label)};
        return vm.call(vm.attribute(vm.attribute(vm.lookup("menu"), "MCarData"), fn), arg).to_string();
    };
    auto named = [](Args a, size_t i) { return i < a.size() && a[i].kind == Kind::String && !a[i].as_string().empty() && a[i].as_string() != "all"; };
    garage->method("pickup", [self, s, of_label, named](Vm& vm, const Value&, Args a) {
        if (s->riding_car >= 0 && size_t(s->riding_car) < s->garage.size() && s->garage[size_t(s->riding_car)].record)
            entry_from_record(self->db(), s->garage[size_t(s->riding_car)]);
        std::vector<Value> out;
        for (size_t i = 0; i < s->garage.size(); ++i) {
            const GarageEntry& e = s->garage[i];
            if (!e.used) continue;
            if (named(a, 0) && of_label(vm, "GetCountry", e.car) != a[0].as_string()) continue;
            if (named(a, 1) && of_label(vm, "GetTuner", e.car) != a[1].as_string()) continue;
            auto c = car_from_entry(self->db(), *s, int(i), false);
            const Value car = make_car_value(*self, c);
            if (named(a, 2) && vm.call(vm.attribute(car, "getDTrainType"), {}).to_string() != a[2].as_string()) continue;
            out.push_back(car);
        }
        return Value::array(std::move(out));
    });
    auto distinct = [self, s, of_label, named](const char* fn, bool by_country) {
        return [self, s, of_label, named, fn, by_country](Vm& vm, const Value&, Args a) {
            const std::map<int, std::string>& numbers = by_country ? self->db().names.tuners : self->db().names.countries;
            auto number_of = [&numbers](const std::string& name) {
                for (const auto& [n, v] : numbers)
                    if (v == name) return n;
                return 1 << 30;
            };
            std::vector<std::pair<int, std::string>> seen;
            for (const GarageEntry& e : s->garage) {
                if (!e.used) continue;
                if (by_country && named(a, 0) && of_label(vm, "GetCountry", e.car) != a[0].as_string()) continue;
                const std::string v = of_label(vm, fn, e.car);
                if (std::none_of(seen.begin(), seen.end(), [&v](const auto& x) { return x.second == v; })) seen.emplace_back(number_of(v), v);
            }
            std::stable_sort(seen.begin(), seen.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
            std::vector<Value> out;
            for (const auto& [n, v] : seen) out.push_back(text(v));
            return Value::array(std::move(out));
        };
    };
    garage->method("pickupCountry", distinct("GetCountry", false));
    garage->method("pickupTuner", distinct("GetTuner", true));
    return garage;
}

Value GameNatives::make_game() {
    if (game) return object_value(game);
    game = std::make_shared<Object>("MGame");
    GameState* s = &state();
    Object& g = *game;
    Runtime* r = &rt;
    field(g, "major_menu_project", &s->major_menu_project);
    field(g, "major_menu_page", &s->major_menu_page);
    field(g, "major_menu_widget", &s->major_menu_widget);
    g.property("last_sequence", [s] { return text(s->last_sequence); });
    // Setting a race's sequence is the scripts asking for a race: the sequence starts when the
    // project finishes (Runtime::finish).
    g.property("next_sequence", [s] { return text(s->next_sequence); }, [s](const Value& v) {
        s->next_sequence = v.is_nil() ? std::string() : v.to_string();
        s->sequence_asked = true;
    });
    g.property("last_menu_project", [s] { return text(s->last_menu_project); });
    field(g, "next_menu_project", &s->next_menu_project);
    g.property("last_menu_arg", [s] { return text(s->last_menu_arg); });
    field(g, "next_menu_arg", &s->next_menu_arg);
    field(g, "menu_result", &s->menu_result);
    // The race being set up.
    RaceRequest* q = &s->race;
    field(g, "course_code", &q->course), field(g, "race_code", &q->race), field(g, "game_code", &q->game);
    field(g, "max_entry", &q->max_entry), field(g, "laps", &q->laps), field(g, "difficulty", &q->difficulty);
    // ❓ "NORMAL" to begin with: the scripts expect one of their three names.
    field(g, "enemy_level", &q->enemy_level), field(g, "overwrite_enemy_num", &q->overwrite_enemy_num);
    field(g, "car_code", &q->car), field(g, "car_color", &q->paint), field(g, "assist_type", &q->assist_type);
    field(g, "assist_asm", &q->assist_asm), field(g, "assist_tcs", &q->assist_tcs), field(g, "manual", &q->manual);
    field(g, "vibration", &q->vibration), field(g, "tire_idx", &q->tire);
    field(g, "car_code_2p", &q->car_2p), field(g, "car_color_2p", &q->paint_2p), field(g, "assist_type_2p", &q->assist_type_2p);
    field(g, "assist_asm_2p", &q->assist_asm_2p), field(g, "assist_tcs_2p", &q->assist_tcs_2p), field(g, "manual_2p", &q->manual_2p);
    field(g, "vibration_2p", &q->vibration_2p), field(g, "tire_idx_2p", &q->tire_2p);
    g.set("selected_course", Value{}), g.set("selected_car", Value{});
    g.set("car_garage", Value{}), g.set("car_garage_2p", Value{});
    g.set("last_record", Value{}), g.set("labo_folder", number(-1)), g.set("labo_list_index", number(-1)); // ❓ none (what the theater's exit leaves)
    // The race's entry table (MGame getEntryCarCode / setEntryCarCode): [car, paint] by the
    // entry's number; entry 0 is the player's car (car_code, car_color), the opponents the scripts
    // chose from 1 (arcade's doGame reads and writes 0 .. overwrite_enemy_num alike).
    g.method("getEntryCarCode", [q](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0, -1);
        if (i == 0) return Value::array({text(q->car), number(q->paint)});
        if (i < 0 || size_t(i) >= q->entries.size() || q->entries[size_t(i)].empty()) return Value::array({Value{}, number(0)});
        return Value::array({text(q->entries[size_t(i)]), number(size_t(i) < q->entry_paints.size() ? q->entry_paints[size_t(i)] : 0)});
    });
    g.method("setEntryCarCode", [q](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0, -1);
        if (i < 0 || i > 64) return Value{};
        if (q->entries.size() <= size_t(i)) q->entries.resize(size_t(i) + 1);
        if (q->entry_paints.size() <= size_t(i)) q->entry_paints.resize(size_t(i) + 1);
        const std::string car = a.size() > 1 && !a[1].is_nil() ? a[1].to_string() : std::string();
        q->entries[size_t(i)] = car, q->entry_paints[size_t(i)] = arg_int(a, 2);
        if (i == 0 && !car.empty()) q->car = car, q->paint = arg_int(a, 2);
        return Value{};
    });
    // The pages the menus came through: a stack of names (the title page pushes the mode).
    // The menu stack (0x42FD00 on): the names one after another, each ended by a 0, in 0x100
    // bytes; a name that would reach the last byte is not pushed.
    auto stack_bytes = [s] {
        size_t n = 0;
        for (const std::string& name : s->menu_stack) n += name.size() + 1;
        return n;
    };
    g.method("menuPush", [s, stack_bytes](Vm&, const Value&, Args a) {
        const std::string name = a.empty() ? std::string() : a[0].to_string();
        if (stack_bytes() + name.size() < 0xFF) s->menu_stack.push_back(name);
        return Value{};
    });
    g.method("menuPop", [s](Vm&, const Value&, Args) {
        if (s->menu_stack.empty()) return Value{};
        const std::string top = s->menu_stack.back();
        s->menu_stack.pop_back();
        return text(top);
    });
    // menuRef(n): the name n places below the top (0 the top).
g.method("menuRef", [s](Vm&, const Value&, Args a) {
    const s32 n = arg_int(a, 0);
    if (n < 0 || size_t(n) >= s->menu_stack.size()) return Value{};
    return text(s->menu_stack[s->menu_stack.size() - 1 - size_t(n)]);
});
    g.method("menuClearStack", [s](Vm&, const Value&, Args) {
        s->menu_stack.clear();
        return Value{};
    });
    // loadShutterSound(type): the camera whose shutter the "shutter" effect is ("typea" ..): told to the sound player.
    g.method("loadShutterSound", [r = &rt](Vm&, const Value&, Args a) {
        r->sound("sound", "loadShutter", a);
        return Value{};
    });
    for (const char* nothing : {"setReturnToLastMenuProject", "dumpAvailable"})
        g.method(nothing, [](Vm&, const Value&, Args) { return Value{}; });

    // The menus' place kept in the save (Context: the major project and page, the stack).
    // saveMenuStack keeps a copy of the stack and its length in bytes (0x42FE88); loadMenuStack
    // puts it back and answers 1, or 0 when none was kept (the length -1, a new game's;
    // 0x42FF48).
    g.method("saveMenuStack", [s, stack_bytes](Vm&, const Value&, Args) {
        Bytes stack(0x100, 0);
        size_t at = 0;
        for (const std::string& name : s->menu_stack) std::memcpy(stack.data() + at, name.data(), name.size()), at += name.size() + 1;
        s->saved_menu_depth = int(stack_bytes());
        s->saved_menu_stack = stack;
        return Value{};
    });
    g.method("loadMenuStack", [s](Vm&, const Value&, Args) {
        if (s->saved_menu_depth < 0) return number(0);
        s->menu_stack.clear();
        const size_t end = std::min<size_t>(size_t(s->saved_menu_depth), s->saved_menu_stack.size());
        for (size_t at = 0; at < end;) {
            const char* p = reinterpret_cast<const char*>(s->saved_menu_stack.data() + at);
            const std::string name(p, strnlen(p, end - at));
            s->menu_stack.push_back(name), at += name.size() + 1;
        }
        return number(1);
    });
    // Credits: kept as a number, given to the scripts as the money text ("10,000"; "--" for
    // none, 0x42E6B8). No cap: the scripts keep their own.
    g.property("money", [s] { return text(money_text(s->money)); }, [s](const Value& v) { s->money = v.kind == Kind::Int ? s64(v.i) : s64(v.as_float()); });
    g.method("incMoney", [s](Vm&, const Value&, Args a) {
        s->money += a.empty() ? 0 : a[0].kind == Kind::Int ? s64(a[0].i) : s64(a[0].as_float());
        return Value{};
    });
    g.method("setMoneyZero", [s](Vm&, const Value&, Args) {
        s->money = 0;
        return Value{};
    });
    g.method("isEnoughMoney", [s](Vm&, const Value&, Args a) { return number(s->money >= s64(arg_number(a, 0))); });
    g.method("getTotalPrizeMoney", [s](Vm&, const Value&, Args) { return text(money_text(s->total_prize_money)); });
    g.method("incTotalPrizeMoney", [s](Vm&, const Value&, Args a) {
        s->total_prize_money += s64(arg_number(a, 0));
        return Value{};
    });
    // The score with a comma every three digits.
    g.method("getScore", [s](Vm&, const Value&, Args) { return text(s->score > 0 ? money_text(s->score) : std::string("0")); });
    g.method("incScore", [s](Vm&, const Value&, Args a) {
        s->score += arg_int(a, 0);
        return Value{};
    });
    g.method("getTotalPrizeCars", [s](Vm&, const Value&, Args) { return number(s32(s->total_prize_cars)); });
    g.method("incTotalPrizeCars", [s](Vm&, const Value&, Args a) {
        s->total_prize_cars += u32(arg_int(a, 0, 1));
        return Value{};
    });
    g.method("getTotalRankCount", [s](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0);
        return number(i >= 0 && i < 7 ? s32(s->rank_counts[size_t(i)]) : 0);
    });
    g.method("incTotalRankCount", [s](Vm&, const Value&, Args a) {
        const s32 i = arg_int(a, 0);
        if (i >= 0 && i < 7) ++s->rank_counts[size_t(i)];
        return Value{};
    });
    g.method("getTotalRaceCount", [s](Vm&, const Value&, Args) {
        u32 n = 0;
        for (u32 c : s->rank_counts) n += c;
        return number(s32(n));
    });
    // The distances driven (meters ❓), in the player's unit with commas and the unit's name after them.
    auto distance = [s](const s64* m) {
        return [s, m](Vm&, const Value&, Args) {
            const bool miles = s->option_text("unit_velocity") == "MPH";
            const s64 v = s64(double(*m) * (miles ? 0.00062139 : 0.001));
            return text((v > 0 ? money_text(v) : std::string("0")) + (miles ? "miles" : "km")); // "31miles", as the original's Status page
        };
    };
    g.method("getAspecDistance", distance(&s->aspec_distance));
    g.method("getBspecDistance", distance(&s->bspec_distance));
    // The B-spec driver's experience from its book (0x468B58; game_bspec.h): the total 0 .. 10000, the car's, the
    // courses' and the battles' 0 .. 100, weighed by the program's table 0x623E40.
    {
        GameNatives* self = this;
        auto figure = [s, self](int (*fn)(const bspec::Book&, const bspec::Weights&)) {
            return [s, self, fn](Vm&, const Value&, Args) {
                const sim::ai::BSpecTables t = sim::ai::BSpecTables::load(self->db().program);
                return number(fn(s->bspec_book, t.weight));
            };
        };
        g.method("getBspecTotalExperience", figure(&bspec::total_experience));
        g.method("getBspecCarExperience", figure(&bspec::car_experience));
        g.method("getBspecCourseExperience", figure(&bspec::course_experience));
        g.method("getBspecBattleExperience", figure(&bspec::battle_experience));
    }
    field(g, "username", &s->username), field(g, "password", &s->password), field(g, "last_entry_name", &s->last_entry_name);
    g.property("withdrawn", [s] { return number(s->withdrawn); }, [s](const Value& v) { s->withdrawn = v.truthy(); });
    g.property("withdrawnGT4p", [s] { return number(s->withdrawn_gt4p); }, [s](const Value& v) { s->withdrawn_gt4p = v.truthy(); });
    field(g, "met_type", &s->met_type);
    // A championship left in the middle.
    g.method("isSuspended", [s](Vm&, const Value&, Args) { return number(s->suspended != 0); });
    field(g, "suspended_racecode", &s->suspended_racecode);
    g.property("suspended_session", [s] { return number(s->suspended_session); }, [s](const Value& v) { s->suspended_session = v.kind == Kind::Int ? v.i : int(v.as_float()); });
    g.method("abandonRace", [s](Vm&, const Value&, Args) {
        s->suspended = 0;
        return Value{};
    });
    // What arcade mode has unlocked: a car or a course by its label.
    g.method("isCarAvailable", [s](Vm&, const Value&, Args a) { return number(s->all_available || s->available_cars.count(arg_text(a, 0)) > 0); });
    g.method("setCarAvailable", [s](Vm&, const Value&, Args a) {
        if (arg_int(a, 1, 1)) s->available_cars.insert(arg_text(a, 0));
        else s->available_cars.erase(arg_text(a, 0));
        return Value{};
    });
    g.method("isCourseAvailable", [s](Vm&, const Value&, Args a) { return number(s->all_available || s->available_courses.count(arg_text(a, 0)) > 0); });
    g.method("setCourseAvailable", [s](Vm&, const Value&, Args a) {
        if (arg_int(a, 1, 1)) s->available_courses.insert(arg_text(a, 0));
        else s->available_courses.erase(arg_text(a, 0));
        return Value{};
    });
    g.method("setAllAvailable", [s](Vm&, const Value&, Args a) {
        s->all_available = a.empty() || a[0].truthy();
        return Value{};
    });
    // isNeedSave answers whether something changed and clears it (0x43AC68); setNeedSave sets it
    // (0x43ACA8, no argument).
    g.method("isNeedSave", [s](Vm&, const Value&, Args) {
        const bool need = s->need_save;
        s->need_save = false;
        return number(need);
    });
    g.method("setNeedSave", [s](Vm&, const Value&, Args) {
        s->need_save = true;
        return Value{};
    });
    g.method("isDuringQuickMenu", [s](Vm&, const Value&, Args) { return number(s->during_quick_menu); });
    g.method("setDuringQuickMenu", [s](Vm&, const Value&, Args a) {
        s->during_quick_menu = a.empty() || a[0].truthy();
        return Value{};
    });
    // getAllMusicList() (0x165798): the streamed "racing" pieces, [title, artist, genre, number] each.
    g.method("getAllMusicList", [r](Vm&, const Value&, Args) {
        std::vector<Value> out;
        for (const std::string& path : {"bgm/" + r->region() + "/ads.inf", std::string("bgm/us/ads.inf")}) {
            const std::optional<Bytes> inf = r->reader()(path);
            if (!inf || inf->size() < 0x14) continue;
            try {
                const sound::MusicIndex index = sound::MusicIndex::parse(*inf);
                const int racing = sound::stream_group("racing");
                if (racing < 0 || size_t(racing) >= index.groups.size()) continue;
                for (const sound::MusicRecord& m : index.groups[size_t(racing)])
                    out.push_back(Value::array({Value::string(m.title), Value::string(m.artist), Value::string(m.genre), number(s32(out.size()))}));
            } catch (const std::exception&) {
                continue;
            }
            break;
        }
        return Value::array(std::move(out));
    });
    // The LAN battle's settings (0x161F28, 0x162018: the words as a list, where the program gives its blob).
    g.method("getBattleSettingRace", [s](Vm&, const Value&, Args) {
        const net::RaceRules r = rules_from_options(*s);
        return Value::array({number(r.laps), number(r.handicap), number(r.boost), number(r.tire_wear), number(r.heavy), number(r.penalty)});
    });
    g.method("getBattleSettingEntry", [s](Vm&, const Value&, Args) {
        const RaceRequest& q = s->race;
        const GameSettings o = settings_of(*s);
        return Value::array({number(q.manual == 0), number(q.assist_asm), number(q.assist_tcs), number(q.vibration), number(o.active_steering), number(q.tire),
                             number(o.view == "BONNET" ? 1 : o.view == "OVERLOOK" ? 2 : 0), number(o.display.map ? 1 : 0)});
    });
    g.property("option", [this] { return make_option(); });
    g.method("getOption", [this](Vm&, const Value&, Args) { return make_option(); });
    g.method("setOption", [](Vm&, const Value&, Args) { return Value{}; });

    g.set("garage", object_value(make_garage(s)));

    // The demonstrations the title page falls back to.
    auto demo = std::make_shared<Object>("MDemonstration");
    for (const char* nothing : {"initialize", "resetPlayListCounter", "resetMovieCount"}) demo->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    demo->set("type", text("")), demo->set("name", text("")), demo->set("arg", text(""));
    demo->set("repeat_time", number(0)), demo->set("skip_time", number(0)), demo->set("restart_from_beginning", number(0));
    g.set("demonstration", object_value(demo));

    // The calendar, the records, the favorites, the presents, the used cars (game_records.cpp).
    install_records(*this, g, *rt.vm().lookup("menu").as<script::Module>());
    install_prologue_license(g, &config);
    (void)r;
    return object_value(game);
}

// ---- MMemoryCardManager(mode[, slot]): the card in slot 1 as the game's own files under the
// data folder (save/memory_card.h); without a data folder, a stand-in (GameState::card) that
// answers as a console whose card has a game or none.
save::GameSave& GameNatives::card() {
    if (!card_save) {
        const std::string dir = config.data_dir.empty() ? std::string() : config.data_dir + "/memorycard";
        Database* d = database.get();
        GameNatives* self = this;
        save::Codes codes{[self](u32 table, const std::string& label) { return self->db().code(table, label); },
                          [self](u64 code) { return self->db().label(code); }};
        (void)d;
        // Prologue's folder is its own product's, "BI" and "SCPS-15055" (its 0x19BCB0), so its
        // saves stand apart from Gran Turismo 4's under the same data folder.
        Database& base = db();
        base.get(config);
        const std::string product = base.db && base.db->prologue() ? "BISCPS-15055" : "BASCUS-97328";
        // A card image in the card's folder stands for the card when the folder has no game.
        if (const save::CardImageResult taken = save::import_dropped_card(dir, product); taken.ok) rt.note("memory card: " + taken.message);
        card_save = std::make_shared<save::GameSave>(dir, codes, rt.reader(), product);
    }
    return *card_save;
}

save::GameSave& GameNatives::card2() {
    if (!card2_save) {
        std::string dir = config.data_dir.empty() ? std::string() : config.data_dir + "/memorycard2";
        std::error_code ec;
        if (!dir.empty() && !std::filesystem::is_directory(dir, ec)) dir.clear();
        save::Codes codes{[this](u32 table, const std::string& label) { return db().code(table, label); }, [this](u64 code) { return db().label(code); }};
        card().connected(); // (the first card's set-up: the product)
        Database& base = db();
        const std::string product = base.db && base.db->prologue() ? "BISCPS-15055" : "BASCUS-97328";
        card2_save = std::make_shared<save::GameSave>(dir, codes, rt.reader(), product);
    }
    return *card2_save;
}

namespace {

// Gran Turismo 4 Prologue's save (save/prologue_data.h): the license record's units and the profile's name
// (the license object's default name, the profile's +0x1133C) into the state before a save, and back after a load.
bool prologue_game(GameNatives& n) {
    Database& d = n.db();
    return d.db && d.db->prologue() && n.game;
}
void prologue_into_state(GameNatives& n) {
    if (!prologue_game(n)) return;
    GameState& s = n.state();
    s.prologue_license = prologue_license_units(*n.game);
    Vm& vm = n.rt.vm();
    const Value license = vm.attribute(object_value(n.game), "license");
    const Value get = license.p ? vm.attribute(license, "getDefaultName") : Value{};
    if (get.kind == script::Kind::Function) s.username = vm.call(get, {}).to_string();
}
void prologue_from_state(GameNatives& n) {
    if (!prologue_game(n)) return;
    GameState& s = n.state();
    if (!s.prologue_license.empty()) prologue_license_load(*n.game, ByteSpan(s.prologue_license.data(), s.prologue_license.size()));
    Vm& vm = n.rt.vm();
    const Value license = vm.attribute(object_value(n.game), "license");
    const Value set = license.p ? vm.attribute(license, "setDefaultName") : Value{};
    const Value name[1] = {Value::string(s.username)};
    if (set.kind == script::Kind::Function) vm.call(set, name);
}

} // namespace

Value GameNatives::make_card_manager(Args a) {
    auto m = std::make_shared<Object>("MMemoryCardManager");
    GameState* s = &state();
    GameNatives* self = this;
    const std::string mode = arg_text(a, 0);
    m->set("mode", text(mode));
    // The photos of a card (game_photos.cpp).
    if (install_photo_manager(*this, *m, mode, arg_int(a, 1))) return object_value(m);
    // MMemoryCardFile.loadInstance: a card's game loaded beside the one played, [result, a game object with its garage] (the
    // trade's, and the 2-Player Battle's garages of either slot).
    auto load_instance = [self](save::GameSave& card) {
        return [self, &card](Vm&, const Value&, Args) {
            if (!card.connected()) return Value::array({text("DEVICE_ERROR"), Value{}});
            if (card.exists() != 0) return Value::array({text("FILE_NOT_FOUND"), Value{}});
            auto other = std::make_shared<GameState>();
            const save::Result r = card.load(*other);
            if (r != save::Result::Ok) return Value::array({text(save::result_name(r)), Value{}});
            self->other_games.push_back(other);
            auto game = std::make_shared<Object>("MGame");
            game->set("garage", object_value(self->make_garage(other.get())));
            return Value::array({text("OK"), object_value(game)});
        };
    };
    // MMemoryCardManager(mode, 1): the card in slot 2, read for trading cars: its game is loaded beside the one played
    // (loadInstance: [result, a game object with that game's garage]).
    if (arg_int(a, 1) == 1) {
        save::GameSave& card = card2();
        auto file = std::make_shared<Object>("MMemoryCardFile");
        file->method("isExist", [&card](Vm&, const Value&, Args) { return number(!card.connected() ? -1 : card.exists() == 0 ? 0 : 1); });
        file->method("loadInstance", load_instance(card));
        for (const char* nothing : {"dump", "setNewFile"}) file->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        const Value file_value = object_value(file);
        m->method("getNewFile", [file_value](Vm&, const Value&, Args) { return file_value; });
        m->method("getFile", [file_value](Vm&, const Value&, Args) { return file_value; });
        m->method("isConnect", [&card](Vm&, const Value&, Args) { return number(card.connected()); });
        m->method("isFormat", [&card](Vm&, const Value&, Args) { return number(card.connected()); });
        m->method("isChanged", [](Vm&, const Value&, Args) { return number(0); });
        m->method("isNoFile", [&card](Vm&, const Value&, Args) { return number(card.exists() != 0); });
        for (const char* nothing : {"clearAutoSaveDisabled", "requestPause", "waitPause", "start", "restart", "clear"})
            m->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        auto progress = std::make_shared<Object>("MProgress");
        progress->set("value", Value::number(0));
        m->set("progress", object_value(progress));
        return object_value(m);
    }
    auto file = std::make_shared<Object>("MMemoryCardFile");
    // A real card: a data folder; and the stand-in's card "connected".
    auto real = [self, s] { return s->card.connected && self->card().connected(); };
    auto there = [self, s, real] { return real() && self->card().exists() == 0; };
    // isExist(manager): 0 the game's data is on the card, 1 it is not, -1 no card (0x1765C8).
    file->method("isExist", [s, there](Vm&, const Value&, Args) { return number(!s->card.connected ? -1 : there() || s->card.has_game_data ? 0 : 1); });
    // load(manager): "OK", "DEVICE_ERROR", "INVALID_FILE"; any other result "" (0x1756A0).
    file->method("load", [self, s, there](Vm&, const Value&, Args) {
        if (!s->card.connected) return text("DEVICE_ERROR");
        if (!there()) return text(s->card.has_game_data ? "OK" : "FILE_NOT_FOUND");
        const save::Result r = self->card().load(*s);
        if (r == save::Result::Ok) s->card.has_game_data = true, prologue_from_state(*self), self->rt.options_changed();
        return text(r == save::Result::Ok || r == save::Result::DeviceError || r == save::Result::InvalidFile ? save::result_name(r) : "");
    });
    // loadInstance(manager): [result, game] of the card's game beside the one played (the 2-Player Battle's garage, slot 1).
    file->method("loadInstance", [s, there, load = load_instance(card())](Vm& vm, const Value& self_value, Args a) {
        if (!there()) return Value::array({text(s->card.has_game_data ? "OK" : "FILE_NOT_FOUND"), Value{}});
        return load(vm, self_value, a);
    });
    // saveNew(manager, title): the save made anew; save(manager): written over.
    file->method("saveNew", [self, s, real](Vm&, const Value&, Args a) {
        if (!s->card.connected) return text("DEVICE_ERROR");
        apply_monitor(*self);
        prologue_into_state(*self);
        if (real()) {
            const save::Result r = self->card().save_new(*s, a.size() > 1 ? a[1].to_string() : std::string("Game Data"));
            if (r != save::Result::Ok) return text(r == save::Result::DifferentMemoryCard ? "DIFFERENT_MEMORYCARD" : "DEVICE_ERROR");
        }
        s->card.has_game_data = true, s->need_save = false;
        return text("OK");
    });
    file->method("save", [self, s, real, there](Vm&, const Value&, Args a) {
        if (!s->card.connected) return text("DEVICE_ERROR");
        apply_monitor(*self);
        prologue_into_state(*self);
        if (real()) {
            const save::Result r = there() ? self->card().save(*s) : self->card().save_new(*s, a.size() > 1 ? a[1].to_string() : std::string("Game Data"));
            if (r != save::Result::Ok) return text(save::result_name(r));
        }
        s->card.has_game_data = true, s->need_save = false;
        return text("OK");
    });
    file->method("remove", [self, s, real](Vm&, const Value&, Args) {
        if (real()) self->card().remove();
        s->card.has_game_data = false;
        return text("OK");
    });
    for (const char* ok : {"format", "unformat", "saveDnas", "writeInfo"}) file->method(ok, [](Vm&, const Value&, Args) { return text("OK"); });
    for (const char* nothing : {"dump", "setNewFile"}) file->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    file->method("readSize", [self, real](Vm&, const Value&, Args) { return number(real() ? self->card().used_kb() : 0); });
    for (const char* zero : {"dataFileSize", "isInfoValid", "isIconValid"}) file->method(zero, [](Vm&, const Value&, Args) { return number(0); });
    file->method("getIconTexture", [](Vm&, const Value&, Args) { return Value{}; });
    file->property("size", [self] { return number(self->card().needed_kb()); });
    file->set("objectName", text("Game Data")), file->set("title", text("")), file->set("carName", text(""));
    file->set("courseName", text("")), file->set("time", number(0)), file->set("date", number(0)), file->set("portrait", number(0));
    file->set("spectatorMode", number(0)), file->set("racemode", number(0)), file->set("racesubmode", number(0)), file->set("dispstep", number(0));
    file->set("list", Value::array({}));
    const Value file_value = object_value(file);
    m->method("getNewFile", [file_value](Vm&, const Value&, Args) { return file_value; });
    m->method("getFile", [file_value](Vm&, const Value&, Args) { return file_value; });
    m->method("isConnect", [s](Vm&, const Value&, Args) { return number(s->card.connected); });
    m->method("isFormat", [s](Vm&, const Value&, Args) { return number(s->card.connected && s->card.formatted); });
    m->method("isChanged", [](Vm&, const Value&, Args) { return number(0); });
    m->method("isUseNewFile", [](Vm&, const Value&, Args) { return number(1); });
    m->method("isNoFile", [s, there](Vm&, const Value&, Args) { return number(!there() && !s->card.has_game_data); });
    m->method("isAutoSaveDisabled", [](Vm&, const Value&, Args) { return number(0); });
    for (const char* nothing : {"clearAutoSaveDisabled", "requestPause", "waitPause", "start", "restart", "clear"})
        m->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
    for (const char* ok : {"test", "save"}) m->method(ok, [](Vm&, const Value&, Args) { return text("OK"); });
    // ❓ What the probes of the earlier games' saves read (0x1D4500 on) is not followed: none.
    for (const char* no : {"isExistGT4pData", "isExistGT3Data"}) m->method(no, [](Vm&, const Value&, Args) { return number(0); });
    for (const char* none : {"inheritGT4pData", "inheritGT3Data"}) m->method(none, [](Vm&, const Value&, Args) { return text("FILE_NOT_FOUND"); });
    // The card's free KB. The scripts take 8,000 for an unformatted card; the data folder's card
    // is formatted: 8,000 less a KB for its directory, the game's save and the replays kept
    // (❓ the program's count of a real card's clusters is not followed).
    auto free_kb = [self, s, real]() -> s32 {
        if (!real()) return s->card.free_kb;
        s32 used = 1 + std::max(0, self->card().used_kb());
        std::error_code ec;
        for (const std::string& f : saved_replays(self->rt.data_dir())) used += s32((std::filesystem::file_size(f, ec) + 1023) / 1024);
        used += photos_kb(self->rt);
        return std::max(0, 8000 - used);
    };
    m->property("free", [free_kb] { return number(free_kb()); });
    m->property("realFree", [free_kb] { return number(free_kb()); });
    auto progress = std::make_shared<Object>("MProgress");
    progress->set("value", Value::number(0));
    m->set("count", number(0)), m->set("cursorPos", number(0)), m->set("progress", object_value(progress)), m->set("list", Value::array({}));
    m->set("autoloadPlayList", Value{}), m->set("autoloadPlayListResult", Value{});
    // The Replay Theater's replays (game_replays.cpp).
    if (mode == "MODE_LOAD_REPLAY") install_replay_list(*this, *m);
    if (mode == "MODE_DEMONSTRATION") install_demo_list(*this, *m, false);
    if (mode == "MODE_DEMONSTRATION_SUB") install_demo_list(*this, *m, true);
    return object_value(m);
}

namespace {
std::weak_ptr<GameNatives> g_natives_of(Runtime& rt, std::shared_ptr<GameNatives> set = nullptr) {
    static std::map<Runtime*, std::weak_ptr<GameNatives>> all;
    if (set) all[&rt] = set;
    for (auto it = all.begin(); it != all.end();) it = it->second.expired() ? all.erase(it) : std::next(it);
    const auto it = all.find(&rt);
    return it == all.end() ? std::weak_ptr<GameNatives>() : it->second;
}
} // namespace

std::shared_ptr<GameNatives> natives_of(Runtime& rt) { return g_natives_of(rt).lock(); }

// MOption's monitor_size setter (0x436D60) copies the screen's figures (Option +0x24, 0x2C bytes)
// from one of three tables of the program: "subaru" 0x6A61E8, else 16:9 (wide_mode) 0x6A61B8,
// else 0x6A6188. A saved game made by the original has them; the 3D view reads them.
void apply_monitor(GameNatives& n) {
    Database& d = n.db();
    if (!d.program.known()) return;
    GameState& s = n.state();
    const u32 table = s.option_text("monitor_size") == "subaru" ? 0x6A61E8 : s.option_int("wide_mode") ? 0x6A61B8 : 0x6A6188;
    s.monitor_params.resize(0x2C);
    for (u32 i = 0; i < 0x2C; ++i) s.monitor_params[i] = d.program.b(table + i);
}

Value make_car_object(Runtime& rt, const std::string& label, int paint) {
    const std::shared_ptr<GameNatives> n = g_natives_of(rt).lock();
    if (!n) return Value{};
    auto c = std::make_shared<CarHandle>();
    c->state = &n->state(), c->label = label;
    set_car(n->db(), c->own, label, paint);
    return make_car_value(*n, c);
}

// The place of a course's record in the game's course records (0x433C10 on the label as 0x468690 gives it).
std::optional<size_t> bspec_course_unit(Database& d, const GameState& s, std::string label) {
    if (label.size() >= 2 && label.compare(label.size() - 2, 2, "24") == 0) label.resize(label.size() - 2);
    const u64 code = d.code(kCourseTable, label);
    if (code == kNoCode || s.course_record.size() < kCourseUnit * kCourseUnits) return std::nullopt;
    for (size_t i = 0; i < kCourseUnits; ++i)
        if (r32(s.course_record, i * kCourseUnit) == u32(code) && r32(s.course_record, i * kCourseUnit + 4) == u32(code >> 32)) return i * kCourseUnit;
    return std::nullopt;
}

std::string course_attribute(Database& d, const std::string& course) {
    const ByteSpan r = d.row("COURSE", course);
    db::SpecDb* db = d.db.get();
    const db::Strings* units = db ? db->strings("UnistrDB") : nullptr;
    if (r.size() < 0x0C || !units) return {};
    return units->at(u32(r[8]) | u32(r[9]) << 8 | u32(r[10]) << 16 | u32(r[11]) << 24);
}

void complete_race_request(Runtime& rt, RaceRequest& r) {
    const std::shared_ptr<GameNatives> n = g_natives_of(rt).lock();
    if (!n) return;
    GameState& s = rt.game();
    Database& d = n->db();
    // The race's own label: as given, or with the region's prefix (the license tests and
    // missions are "usl0b0001", the scripts say "l0b0001"; 0x430D20 tries "us" then "jp").
    if (r.sequence == "replay") r.replay_file = s.replay_file;
    std::string race = r.race;
    for (const std::string& tried : {r.race, "us" + r.race, "jp" + r.race})
        if (!r.race.empty() && d.code(kRaceTable, tried) != kNoCode) {
            race = tried;
            break;
        }
    const ByteSpan row = race.empty() ? ByteSpan{} : d.row("RACE", race);
    const bool test = r.sequence.find("license") != std::string::npos || r.sequence.find("mission") != std::string::npos;
    // The course the scripts set (Arcade's, the free run's, practice's, photo drive's, and an event's when they set one:
    // 0x447C78, 0x4471E0); a license test's, a mission's and the machine test's is always their event's (0x1151F0, 0x11D0F8,
    // 0x11BEC0, 0x115F58), whatever another mode left.
    if ((r.course.empty() || test || r.sequence == "machinetest") && row.size() >= 8)
        r.course = d.label(u64(r32(row, 0)) | u64(r32(row, 4)) << 32);
    // Prologue's arcade races are their events': the grid (RACE +0x3E, 1 on "gt4p_dirt", the run
    // without opponents) and the laps (+0x43, 2), which its scripts do not set.
    // docs/formats/prologue.md "The menus".
    if (d.db && d.db->prologue() && row.size() > 0x43) {
        if (row[0x3E]) r.max_entry = std::min(r.max_entry > 0 ? r.max_entry : 99, int(row[0x3E]));
        if (r.laps <= 0 && row[0x43]) r.laps = row[0x43];
    }
    // The B-spec driver's book and the course's marks (0x468690: a label ending in "24" is looked up without it;
    // 0x433C10 finds the course's record, none: no marks).
    // The race's course flag (cond + 0xCBE7, 0x468A40; set by 0x34C7E0 from the course's attribute text, 0x447F08: its
    // second character's value, '0' none): the marks are read and written back only when it is set ("1ff0" Tsukuba,
    // "0000" Grand Canyon).
    r.bspec_book = s.bspec_book;
    r.bspec_course_known = false;
    r.garage_slot = -1; // (only a garage car's request has one, below: not the request before's)
    const std::string attribute = course_attribute(d, r.course);
    if (const std::optional<size_t> at = attribute.size() > 1 && attribute[1] != '0' ? bspec_course_unit(d, s, r.course) : std::nullopt) {
        std::copy_n(s.course_record.data() + *at + 0x228, 8, r.bspec_course_marks.begin());
        r.bspec_course_known = true;
    }
    // Whose car: the garage's driven car in Gran Turismo mode's sequences (0x10F948: the event's 0x113B48, the championship's
    // 0x118EF0, the free run's 0x116E20, practice's 0x1181C8, the machine test's 0x115A90, photo drive's 0x11ABE0), the event's
    // own in a license test and a mission (0x10F880, 0x10F8E0), the scripts' car_code in Arcade's (0x10F7D8): a code left by
    // another mode is not taken. ❓ The photo shoot's and Preview's are not read: the scripts' car when they set one.
    static const std::set<std::string> garage_sequences = {"solo_event", "championship", "freerun", "practice", "machinetest", "photo_drive"};
    if (test || garage_sequences.count(r.sequence)) r.car.clear();
    if (!r.car.empty()) return;
    if (test) {
        // A test's grid is its event's (RACE +0x3E: 1 on the license tests).
        if (row.size() > 0x3E && row[0x3E]) r.max_entry = row[0x3E];
        // A test's car is the first of its pool (an ENEMY_CARS row: +0x00 the car).
        const auto entries = race_entries(d, race);
        if (!entries.empty()) {
            const ByteSpan enemy = d.row("ENEMY_CARS", entries[0].first);
            if (enemy.size() >= 4) r.car = d.label(make_code(kCarTable, r32(enemy, 0))), r.paint = std::max(entries[0].second, 0);
        }
        return;
    }
    if (s.riding_car >= 0 && size_t(s.riding_car) < s.garage.size() && s.garage[size_t(s.riding_car)].used) {
        const GarageEntry& e = s.garage[size_t(s.riding_car)];
        r.car = e.car, r.garage_slot = s.riding_car;
        r.paint = e.record ? s8(e.record->active()[0x18]) : e.paint;
    }
}

void install_fallback_world(Runtime& rt) {
    Vm& vm = rt.vm();
    // The program's module, as the main script would have asked for it.
    if (vm.lookup("menu").kind != Kind::Module) {
        auto module = std::make_shared<script::Module>();
        module->name = "menu", module->parent = vm.root();
        vm.root()->define("menu", Value::of(Kind::Module, module));
        vm.require("menu", module);
    }
    const Value menu = vm.lookup("menu");
    for (const char* name : {"sound", "game", "network", "shell"}) {
        if (vm.lookup(name).as<Object>()) continue;
        const std::string cls = name == std::string("sound") ? "MSound" : name == std::string("game") ? "MGame" : name == std::string("network") ? "MNetwork" : "MShell";
        const Value maker = vm.attribute(menu, cls);
        try {
            if (maker.p && maker.p->callable()) vm.root()->define(name, vm.call(maker, {}));
        } catch (const script::ScriptError&) {
        }
    }
}

void install_game_natives(Runtime& rt, script::Module& menu) {
    auto n = std::make_shared<GameNatives>(GameNatives{rt, rt.config(), std::make_shared<Database>(), nullptr, nullptr, nullptr, nullptr});
    n->database->rt = &rt;
    g_natives_of(rt, n);
    rt.keep_alive(n);
    Runtime* r = &rt;
    GameState* s = &rt.game();

    // The objects the main script makes.
    {
        auto cls = make_class(menu, "MGame");
        cls->construct = [n](Vm&, Args) { return n->make_game(); };
        // The configuration script the main script requires. The program names it (the US
        // game's "config-main" is a text of its own, at 0x6A62C8); here it is the retail one
        // the disc has: config-main, or Prologue's config-prologue.
        static_fn(*cls, "getConfigScriptFile", [r](Vm&, const Value&, Args) {
            for (const char* name : {"config-main", "config-prologue"})
                if (r->reader()(std::string("config/") + name + ".adc")) return text(name);
            return text("config-main");
        });
    }
    make_class(menu, "MSound")->construct = [n](Vm&, Args) { return n->make_sound(); };
    make_class(menu, "MOption")->construct = [n](Vm&, Args) { return n->make_option(); };
    make_class(menu, "MShell")->construct = [](Vm&, Args) {
        auto shell = std::make_shared<Object>("MShell");
        for (const char* nothing : {"putMessage", "execute", "getCandidates"}) shell->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        shell->set("pipe", Value{});
        return object_value(shell);
    };
    // The network (game_network.h).
    make_class(menu, "MNetwork")->construct = [r](Vm&, Args) { return make_network(*r); };

    // ---- MSystem
    {
        auto cls = make_class(menu, "MSystem");
        static_fn(*cls, "GetRegionCode", [r](Vm&, const Value&, Args) { return text(r->region()); });
        static_fn(*cls, "GetLanguage", [r](Vm&, const Value&, Args) { return text(r->language()); });
        static_fn(*cls, "GetStaticLanguage", [r](Vm&, const Value&, Args) { return text(r->language()); });
        static_fn(*cls, "SetLanguage", [r](Vm&, const Value&, Args a) {
            r->note("language asked for: " + arg_text(a, 0));
            return Value{};
        });
        static_fn(*cls, "GetVideoSystem", [r](Vm&, const Value&, Args) { return text(r->region() == "uk" ? "PAL" : "NTSC"); });
        static_fn(*cls, "GetVersionString", [](Vm&, const Value&, Args) { return text(""); });
        static_fn(*cls, "GetVersionBranch", [](Vm&, const Value&, Args) { return text("gt4"); });
        static_fn(*cls, "GetBuildNumber", [](Vm&, const Value&, Args) { return number(0); });
        static_fn(*cls, "GetStaticWideMode", [](Vm&, const Value&, Args) { return number(0); });
        static_fn(*cls, "GetDnasCode", [](Vm&, const Value&, Args) { return number(0); });
        static_fn(*cls, "GetMaxMemorySize", [](Vm&, const Value&, Args) { return number(32 * 1024 * 1024); });
        static_fn(*cls, "IsOEM", [](Vm&, const Value&, Args) { return number(0); });
        static_fn(*cls, "IsDrawPerfMeter", [](Vm&, const Value&, Args) { return number(0); });
        static_fn(*cls, "SetLoadingMode", [r](Vm&, const Value&, Args a) {
            r->game().loading_mode = arg_text(a, 0);
            return Value{};
        });
        for (const char* nothing : {"SetDrawPerfMeter", "DumpMemoryBlock", "SyncUnitWithLanguage", "Reboot"})
            static_fn(*cls, nothing, [](Vm&, const Value&, Args) { return Value{}; });
        auto now = [] {
            const std::time_t t = std::time(nullptr);
            return *std::localtime(&t);
        };
        static_fn(*cls, "GetCurrentDateTime", [](Vm&, const Value&, Args) { return number(s32(std::time(nullptr) & 0x7FFFFFFF)); });
        auto stamp = [now](Vm&, const Value&, Args) {
            const std::tm t = now();
            char b[40];
            std::snprintf(b, sizeof b, "%04d/%02d/%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
            return text(b);
        };
        static_fn(*cls, "GetCurrentDateTimeString", stamp);
        static_fn(*cls, "GetGMTDateTimeString", stamp);
    }
    {
        auto cls = make_class(menu, "MLocale");
        static_fn(*cls, "get", [r](Vm&, const Value&, Args) { return text(r->language()); });
        static_fn(*cls, "set", [](Vm&, const Value&, Args) { return Value{}; });
        static_fn(*cls, "isPal", [r](Vm&, const Value&, Args) { return number(r->region() == "uk"); });
    }

    // ---- MUnit, MUtility: numbers as text in the player's units.
    {
        auto cls = make_class(menu, "MUnit");
        auto miles = [s] { return s->option_text("unit_velocity") == "MPH"; };
        static_fn(*cls, "GetUnitOfVelocity", [s](Vm&, const Value&, Args) { return text(s->velocity_unit().name); });
        static_fn(*cls, "GetVelocityForDisplay", [s](Vm&, const Value&, Args a) { return Value::number(arg_number(a, 0) * s->velocity_unit().factor); });
        static_fn(*cls, "GetVelocityString", [s](Vm&, const Value&, Args a) {
            char b[32];
            std::snprintf(b, sizeof b, "%d", int(std::lround(arg_number(a, 0) * s->velocity_unit().factor)));
            return text(b);
        });
        static_fn(*cls, "GetUnitOfPower", [s](Vm&, const Value&, Args) { return text(s->power_unit().name); });
        static_fn(*cls, "GetPowerForDisplay", [s](Vm&, const Value&, Args a) { return number(s32(std::lround(arg_number(a, 0) * s->power_unit().factor))); });
        static_fn(*cls, "GetUnitOfTorque", [s](Vm&, const Value&, Args) { return text(s->torque_unit().name); });
        static_fn(*cls, "GetTorqueForDisplay", [s](Vm&, const Value&, Args a) { return Value::number(arg_number(a, 0) * s->torque_unit().factor); });
        static_fn(*cls, "GetUnitOfMass", [miles](Vm&, const Value&, Args) { return text(miles() ? "lbs" : "kg"); });
        static_fn(*cls, "GetCourseLengthString", [miles](Vm&, const Value&, Args a) {
            char b[32];
            const float m = arg_number(a, 0);
            // 0x1AEE18: hundredths of the unit rounded, "%.2t%s" with "miles" or "km".
            const int v = int(m * (miles() ? 0.000621371f : 0.001f) * 100.0f + 0.5f);
            std::snprintf(b, sizeof b, "%d.%02d%s", v / 100, v % 100, miles() ? "miles" : "km");
            return text(b);
        });
        static_fn(*cls, "GetCourseHeightString", [miles](Vm&, const Value&, Args a) {
            char b[32];
            const float m = arg_number(a, 0);
            // 0x1AEFC0: tenths of the unit rounded, "%.1t%s" with "ft" or "m".
            const int v = int(m * (miles() ? 3.28084f : 1.0f) * 10.0f + 0.5f);
            std::snprintf(b, sizeof b, "%d.%d%s", v / 10, v % 10, miles() ? "ft" : "m");
            return text(b);
        });
        static_fn(*cls, "SetUnitSystemByLanguage", [](Vm&, const Value&, Args) { return Value{}; });
        // By the language (0x48EE00): the year first for Japanese, Korean and Taiwanese.
        static_fn(*cls, "IsCalendarTypeDayFirst", [r](Vm&, const Value&, Args) {
            const std::string l = r->language();
            return number(!(l == "JP" || l == "KR" || l == "TW"));
        });
    }
    {
        auto cls = make_class(menu, "MUtility");
        // The money: "--" for nothing, else with commas (0x42E6B8); a time in ms (0x42E478).
        static_fn(*cls, "GetMoneyString", [](Vm&, const Value&, Args a) {
            return text(money_text(a.empty() ? 0 : a[0].kind == Kind::Int ? s64(a[0].i) : s64(a[0].as_float())));
        });
        static_fn(*cls, "GetTimeString", [](Vm&, const Value&, Args a) { return text(time_text(u32(arg_int(a, 0, s32(kNoTime))))); });
        static_fn(*cls, "GetFormatString", [](Vm& vm, const Value&, Args a) {
            if (a.empty()) return text("");
            return vm.call(vm.attribute(a[0], "format"), a.subspan(1));
        });
        static_fn(*cls, "GetPriceMagnification", [r](Vm&, const Value&, Args) { return number(r->region() == "jp" ? 100 : 1); });
    }
    {
        // MRandom (0x2CB888): GetValue(lo, hi) from the program's generator (its state starts at
        // 0x12345678); GetValue(lo, hi, seed) one draw of a generator seeded so.
        auto cls = make_class(menu, "MRandom");
        GameNatives* global = n.get();
        auto roll = [global](Vm&, const Value&, Args a) {
            if (a.size() < 2) return Value{};
            const s32 lo = arg_int(a, 0), hi = arg_int(a, 1);
            ProgramRandom own{u32(arg_int(a, 2))};
            ProgramRandom& g = a.size() > 2 ? own : global->random;
            return number(s32(g.next01() * float(hi - lo) + float(lo)));
        };
        static_fn(*cls, "GetValue", roll);
        cls->construct = [roll](Vm&, Args) {
            auto o = std::make_shared<Object>("MRandom");
            o->method("getValue", roll);
            return object_value(o);
        };
    }
    // ---- MMemoryCardManager
    {
        auto cls = make_class(menu, "MMemoryCardManager");
        cls->construct = [n](Vm&, Args a) { return n->make_card_manager(a); };
        static_fn(*cls, "setExistValidGameData", [s](Vm&, const Value&, Args a) {
            s->valid_game_data = a.empty() || a[0].truthy();
            return Value{};
        });
        static_fn(*cls, "getExistValidGameData", [s](Vm&, const Value&, Args) { return number(s->valid_game_data); });
        install_photo_lists(*n, menu);
        static_fn(*cls, "getPrintList", [n](Vm&, const Value&, Args) { return n->photo_print_list; });
        static_fn(*cls, "setAction", [s](Vm&, const Value&, Args a) {
            s->card_action = arg_int(a, 0);
            return Value{};
        });
        static_fn(*cls, "getAction", [s](Vm&, const Value&, Args) { return number(s->card_action); });
    }

    // ---- The car database: MCarData, MCourseData, MDatabase, MCarGarage
    {
        auto cls = make_class(menu, "MCarData");
        std::shared_ptr<Database> d = n->database;
        Runtime::Config cfg = n->config;
        auto car = [d, n](const std::string& label) {
            d->get(n->config);
            return d->car(label);
        };
        static_fn(*cls, "IsExist", [car](Vm&, const Value&, Args a) { return number(car(arg_text(a, 0)) != nullptr); });
        static_fn(*cls, "GetCarLabel", [](Vm&, const Value&, Args a) { return a.empty() ? text("") : text(a[0].to_string()); });
        static_fn(*cls, "GetCarLabelCount", [d, n](Vm&, const Value&, Args) {
            d->get(n->config);
            return number(s32(d->cars.size()));
        });
        static_fn(*cls, "GetCarLabelByIndex", [d, n](Vm&, const Value&, Args a) {
            d->get(n->config);
            const s32 i = arg_int(a, 0, -1);
            return text(i >= 0 && size_t(i) < d->cars.size() ? d->cars[size_t(i)].label : std::string());
        });
        auto name = [car](Vm&, const Value&, Args a) {
            const db::CarEntry* e = car(arg_text(a, 0));
            return text(e ? e->name : arg_text(a, 0));
        };
        static_fn(*cls, "GetCarName", name);
        static_fn(*cls, "GetShortCarName", name);
        (void)cfg;
        // The car table's columns as the program's own accessors read them (0x448868 on):
        // +0x08 u32 price, +0x0C s16 model year, +0x0E country, +0x0F maker, +0x10 tuner,
        // +0x11 category (0 to 99; from 100 a car not shown in the lists), +0x12 flags (bit 0
        // not for sale, bit 2 not in races, bit 5 a test car), +0x14 the open-top kind.
        auto row = [d, n](Args a) {
            d->get(n->config);
            return d->car_row(arg_text(a, 0));
        };
        auto byte_at = [row](size_t at) { return [row, at](Vm&, const Value&, Args a) {
            const ByteSpan r = row(a);
            return number(r.size() > at ? r[at] : 0);
        }; };
        auto flag = [row](int bit) { return [row, bit](Vm&, const Value&, Args a) {
            const ByteSpan r = row(a);
            return number(r.size() > 0x12 ? (r[0x12] >> bit) & 1 : 0);
        }; };
        auto named = [row, d](size_t at, std::map<int, std::string> ProgramTables::*list, const char* none, int modulo) {
            return [row, d, at, list, none, modulo](Vm&, const Value&, Args a) {
                const ByteSpan r = row(a);
                if (r.size() <= at) return text(none);
                const std::map<int, std::string>& names = d->names.*list;
                const auto it = names.find(modulo ? r[at] % modulo : r[at]);
                return text(it == names.end() ? none : it->second);
            };
        };
        static_fn(*cls, "GetPrice", [row](Vm&, const Value&, Args a) {
            const ByteSpan r = row(a);
            return number(r.size() >= 0x0C ? s32(u32(r[8]) | u32(r[9]) << 8 | u32(r[10]) << 16 | u32(r[11]) << 24) : 0);
        });
        static_fn(*cls, "GetModelYear", [row](Vm&, const Value&, Args a) {
            const ByteSpan r = row(a);
            return number(r.size() >= 0x0E ? s16(u16(r[0x0C]) | u16(r[0x0D]) << 8) : 0);
        });
        static_fn(*cls, "GetMaker", named(0x0F, &ProgramTables::makers, "none", 0));
        static_fn(*cls, "GetTuner", named(0x10, &ProgramTables::tuners, "none", 0));
        static_fn(*cls, "GetCountry", named(0x0E, &ProgramTables::countries, "none", 0));
        static_fn(*cls, "GetCategory", named(0x11, &ProgramTables::categories, "no categorised", 100));
        static_fn(*cls, "GetCarLogoName", named(0x10, &ProgramTables::tuners, "none", 0));
        static_fn(*cls, "IsStrange", [row](Vm&, const Value&, Args a) {
            const ByteSpan r = row(a);
            return number(r.size() > 0x11 && r[0x11] >= 100);
        });
        static_fn(*cls, "GetOpenModelType", byte_at(0x14));
        static_fn(*cls, "NotForSell", flag(0));
        static_fn(*cls, "RaceForbidden", flag(2));
        static_fn(*cls, "IsTestCar", flag(5));
        // ❓ Not read from the program yet: what the tires and the surface checks answer
        // (0x448AD0 reads the car's tire rows), the dirt and snow versions of a car, the
        // selling price and the opponents picked for a car.
        // The car's row of ARCADEINFO_NORMAL: eight entries of {u32 row of RACE, u32 table}
        // and a byte. The program's GetRaceCode (0x12E150) takes entry `difficulty` (0 to 2)
        // on a dry course and entry 3 + `difficulty` on any other. ❓ Its tire and dirt checks
        // read a rearranged copy of the row: the last byte is taken as "road tires" and entry 6
        // as the dirt race.
        auto arcade = [d, n](Args a) -> ByteSpan {
            db::SpecDb* db = d->get(n->config);
            const db::Labels* labels = db ? db->labels("ARCADEINFO_NORMAL") : nullptr;
            const db::Table* table = db ? db->table("ARCADEINFO_NORMAL") : nullptr;
            const std::optional<u32> id = labels ? labels->id(arg_text(a, 0)) : std::nullopt;
            return table && id ? table->row(*id) : ByteSpan{};
        };
        auto entry = [](ByteSpan r, size_t i) -> s32 {
            return r.size() >= i * 8 + 4 ? s32(u32(r[i * 8]) | u32(r[i * 8 + 1]) << 8 | u32(r[i * 8 + 2]) << 16 | u32(r[i * 8 + 3]) << 24) : -1;
        };
        static_fn(*cls, "IsTireNormal", [arcade](Vm&, const Value&, Args a) {
            const ByteSpan r = arcade(a);
            return number(r.size() > 0x40 ? r[0x40] != 0 : 1);
        });
        static_fn(*cls, "IsDirtRunnable", [arcade, entry](Vm&, const Value&, Args a) {
            const ByteSpan r = arcade(a);
            return number(r.empty() ? 0 : entry(r, 6) != -1);
        });
        static_fn(*cls, "GetRaceCode", [arcade, entry, d](Vm&, const Value&, Args a) {
            const ByteSpan r = arcade(a);
            const s32 type = arg_int(a, 1), level = std::clamp(a.size() > 2 && a[2].kind == Kind::String ? s32(std::strtol(a[2].as_string().c_str(), nullptr, 0)) : arg_int(a, 2), 0, 2);
            const s32 id = entry(r, size_t(type == 0 ? level : 3 + level));
            const db::Labels* races = d->db && id != -1 ? d->db->labels("RACE") : nullptr;
            const std::string* label = races ? races->label(u32(id)) : nullptr;
            return label ? text(*label) : Value{};
        });
        static_fn(*cls, "GetCarNumericCode", [d, n](Vm&, const Value&, Args a) {
            db::SpecDb* db = d->get(n->config);
            const db::Labels* labels = db ? db->labels("GENERIC_CAR") : nullptr;
            const std::optional<u32> id = labels ? labels->id(arg_text(a, 0)) : std::nullopt;
            return number(id ? s32(*id) : -1);
        });
        static_fn(*cls, "GetCarNameByNumericCode", [d, n](Vm&, const Value&, Args a) {
            db::SpecDb* db = d->get(n->config);
            const db::Labels* labels = db ? db->labels("GENERIC_CAR") : nullptr;
            const std::string* label = labels ? labels->label(u32(arg_int(a, 0, -1))) : nullptr;
            return text(label ? *label : std::string());
        });
        static_fn(*cls, "GetSellPrice", [row](Vm&, const Value&, Args a) {
            const ByteSpan r = row(a);
            return number(r.size() >= 0x0C ? s32((u32(r[8]) | u32(r[9]) << 8 | u32(r[10]) << 16 | u32(r[11]) << 24) / 4) : 0);
        });
        for (const char* same : {"ShiftCarCode", "GetDirtCarCode", "GetSnowCarCode"})
            static_fn(*cls, same, [](Vm&, const Value&, Args a) { return a.empty() ? text("") : text(a[0].to_string()); });
        static_fn(*cls, "GetDescriptionID", [](Vm&, const Value&, Args a) { return a.empty() ? text("") : text(a[0].to_string()); });
        // PickUpEnemyCars(car, list, offset, range, count, dirt) (0x1308C8; game_opponents.h): [[car, paint], ...].
        GameNatives* np = n.get();
        static_fn(*cls, "PickUpEnemyCars", [d, np](Vm&, const Value&, Args a) {
            if (a.size() < 2) return Value{};
            CarHandle* player = car_handle(a[0]);
            if (!d->get(np->config) || !player) return Value::array({});
            opponents::PickInput in;
            in.player_label = player->label;
            const ByteSpan row = d->car_row(player->label);
            in.player_category = row.size() > 0x11 ? row[0x11] : 0;
            sim::Equipment eq;
            std::memcpy(eq.bytes.data(), player->record().active(), sim::Equipment::kSize);
            const sim::Showroom* sr = d->showroom();
            const std::optional<sim::ShowroomFigures> own = sr ? sr->figures(eq) : std::nullopt;
            in.player_score = own ? own->evaluation : opponents::F::raw(0);
            if (const std::vector<Value>* items = a[1].items())
                for (const Value& v : *items) in.list.push_back(v.to_string());
            in.offset = opponents::F(arg_number(a, 2, 0.0f));
            in.range = a.size() > 3 ? opponents::F(arg_number(a, 3)) : opponents::F::raw(0x3B03126E); // 0.002
            in.count = a.size() > 4 ? arg_int(a, 4) : 5;
            in.dirt = a.size() > 5 && arg_int(a, 5) != 0;
            in.category = [d](const std::string& label, int& c) {
                const ByteSpan r = d->car_row(label);
                if (r.size() <= 0x11) return false;
                c = r[0x11];
                return true;
            };
            in.score = [d](const std::string& label) {
                const std::optional<sim::ShowroomFigures> f = d->stock_showroom(label);
                return f ? f->evaluation : opponents::F::raw(0);
            };
            // The car's ARCADEINFO_NORMAL row by its GENERIC_CAR id (0x448AD0 -> 0x448B48): entry 6, the dirt race.
            in.dirt_race = [d](const std::string& label) {
                const db::Labels* labels = d->db->labels("GENERIC_CAR");
                const db::Table* table = d->db->table("ARCADEINFO_NORMAL");
                const std::optional<u32> id = labels ? labels->id(label) : std::nullopt;
                const ByteSpan r = table && id ? table->row(*id) : ByteSpan{};
                if (r.size() < 0x38) return -1;
                for (size_t i = 0x30; i < 0x38; ++i)
                    if (r[i] != 0xFF) return 1;
                return 0;
            };
            // getColorNum (0x445828): the paints, less the last when the car's row hides it.
            in.paints = [d](const std::string& label) {
                const ByteSpan r = d->car_row(label);
                return int(d->spec(label).paints.size()) - (r.size() > 0x12 && (r[0x12] & 2) ? 1 : 0);
            };
            std::vector<Value> out;
            for (const opponents::Pick& p : opponents::pick(in, np->random)) out.push_back(Value::array({text(p.car), number(p.paint)}));
            return Value::array(std::move(out));
        });
    }
    // MCourseData: a course's row of the COURSE table by its label. The program reads the same
    // row (0x1B2F10, 0x1B3030): +0x10 the surface (0 dry, 1 wet, 2 dirt, 3 snow), +0x11 the most
    // cars it takes.
    {
        auto cls = make_class(menu, "MCourseData");
        std::shared_ptr<Database> d = n->database;
        auto row = [d, n](const std::string& label) -> ByteSpan {
            db::SpecDb* db = d->get(n->config);
            const db::Labels* labels = db ? db->labels("COURSE") : nullptr;
            const db::Table* table = db ? db->table("COURSE") : nullptr;
            const std::optional<u32> id = labels ? labels->id(label) : std::nullopt;
            return table && id ? table->row(*id) : ByteSpan{};
        };
        static_fn(*cls, "GetMaxCarNum", [row](Vm&, const Value&, Args a) {
            const ByteSpan r = row(arg_text(a, 0));
            return number(r.size() > 0x11 ? r[0x11] : 0);
        });
        static_fn(*cls, "GetType", [row](Vm&, const Value&, Args a) {
            const ByteSpan r = row(arg_text(a, 0));
            static const char* const kinds[] = {"dry", "wet", "dirt", "snow"};
            return text(r.size() > 0x10 && r[0x10] < 4 ? kinds[r[0x10]] : "unknown");
        });
        // GetAttrString (0x1B3320): the course's attribute text, a string of UnistrDB by the row's +0x08
        // (0x447F08); its first character '1' a rolling start (Arcade's script reads it): "1ff0" Tsukuba, "0000"
        // Grand Canyon.
        static_fn(*cls, "GetAttrString", [row, d, n](Vm&, const Value&, Args a) {
            const ByteSpan r = row(arg_text(a, 0));
            db::SpecDb* db = d->get(n->config);
            const db::Strings* units = db ? db->strings("UnistrDB") : nullptr;
            if (r.size() < 0x0C || !units) return text("");
            return text(units->at(u32(r[8]) | u32(r[9]) << 8 | u32(r[10]) << 16 | u32(r[11]) << 24));
        });
        static_fn(*cls, "GetID", [d, n](Vm&, const Value&, Args a) {
            db::SpecDb* db = d->get(n->config);
            const db::Labels* labels = db ? db->labels("COURSE") : nullptr;
            const std::optional<u32> id = labels ? labels->id(arg_text(a, 0)) : std::nullopt;
            return number(id ? s32(*id) : -1);
        });
    }
    install_car_class(*n, menu);
    {
        auto cls = make_class(menu, "MDatabase");
        std::shared_ptr<Database> d = n->database;
        static_fn(*cls, "getCarLabelCount", [d, n](Vm&, const Value&, Args) {
            d->get(n->config);
            return number(s32(d->cars.size()));
        });
        static_fn(*cls, "getCarLabelByIndex", [d, n](Vm&, const Value&, Args a) {
            d->get(n->config);
            const s32 i = arg_int(a, 0, -1);
            return text(i >= 0 && size_t(i) < d->cars.size() ? d->cars[size_t(i)].label : std::string());
        });
        static_fn(*cls, "getCourseLabelCount", [d, n](Vm&, const Value&, Args) {
            d->get(n->config);
            return number(s32(d->courses.size()));
        });
        static_fn(*cls, "getCourseLabelByIndex", [d, n](Vm&, const Value&, Args a) {
            d->get(n->config);
            const s32 i = arg_int(a, 0, -1);
            return text(i >= 0 && size_t(i) < d->courses.size() ? d->courses[size_t(i)].label : std::string());
        });
    }

    install_race_classes(*n, menu);

    // Array(n): an array of n empty places.
    rt.vm().root()->define("Array", Vm::native("Array", [](Vm&, const Value&, Args a) {
        return Value::array(std::vector<Value>(size_t(std::clamp(arg_int(a, 0), 0, 1 << 20))));
    }));
    // What the configuration sets and the program's memory tools read.
    for (const char* name : {"MemoryDebug_address", "MemoryDebug_size", "MemoryDebug_count"})
        if (!rt.vm().root()->find(name)) rt.vm().root()->define(name, number(0));
    menu.define("Version", number(0));

    // ---- small ones
    {
        auto cls = make_class(menu, "MColorObject");
        cls->construct = [](Vm&, Args a) {
            auto c = std::make_shared<Object>("MColorObject");
            c->set("r", a.size() > 0 ? a[0] : Value::number(1)), c->set("g", a.size() > 1 ? a[1] : Value::number(1));
            c->set("b", a.size() > 2 ? a[2] : Value::number(1)), c->set("a", a.size() > 3 ? a[3] : Value::number(1));
            return object_value(c);
        };
        // interpolate(a, b, t) (0x2039E0): a x t + b x (1 - t), each part.
        static_fn(*cls, "interpolate", [](Vm& vm, const Value&, Args a) {
            if (a.size() < 3) return Value{};
            const float t = arg_number(a, 2);
            auto c = std::make_shared<Object>("MColorObject");
            for (const char* part : {"r", "g", "b", "a"})
                c->set(part, Value::number(vm.attribute(a[0], part).as_float() * t + vm.attribute(a[1], part).as_float() * (1 - t)));
            return object_value(c);
        });
    }
    make_class(menu, "MProgress")->construct = [](Vm&, Args) {
        auto p = std::make_shared<Object>("MProgress");
        p->set("value", Value::number(0));
        return object_value(p);
    };
    make_class(menu, "MGpb")->construct = [](Vm&, Args) {
        auto p = std::make_shared<Object>("MGpb");
        for (const char* nothing : {"load", "unload", "get"}) p->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        return object_value(p);
    };
}

} // namespace gt4r::menu
