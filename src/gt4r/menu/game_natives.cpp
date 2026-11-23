// main::game and the classes of the game's state as the scripts see them: MGame, MOption,
// MGarage, MCarGarage, MCarData, MCourseData, MDatabase, MMemoryCardManager, MSound, MSystem
// and the smaller ones. The member names are the program's (its registration tables); the
// values behind them are the runtime's GameState and the disc's car database.
// docs/formats/menu.md ("The game's state") says what each answers and why.

#include "gt4r/menu/runtime_internal.h"

#include "gt4r/formats/db/specdb.h"
#include "gt4r/menu/program_tables.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

using Fn = script::NativeFunction;

Value text(const std::string& s) { return Value::string(s); }
Value number(s32 v) { return Value::integer(v); }

std::shared_ptr<script::Module> make_class(script::Module& into, const std::string& name) {
    auto m = std::make_shared<script::Module>();
    m->name = name, m->is_class = true;
    into.define(name, Value::of(Kind::Module, m));
    return m;
}

void static_fn(script::Module& cls, const std::string& name, Fn fn) { cls.define(name, Vm::native(cls.name + "::" + name, std::move(fn))); }

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

// The disc's car database, read when first asked.
struct Database {
    Runtime* rt;
    std::unique_ptr<db::SpecDb> db;
    bool tried = false;
    std::vector<db::CarEntry> cars;
    std::map<std::string, size_t> by_label;
    std::vector<db::CourseEntry> courses;
    ProgramTables names; // the makers, tuners, countries and categories by number

    // A car's row of the car table (GENERIC_CAR); empty when there is none.
    ByteSpan car_row(const std::string& label) {
        const db::Labels* labels = db ? db->labels("GENERIC_CAR") : nullptr;
        const db::Table* table = db ? db->table("GENERIC_CAR") : nullptr;
        const std::optional<u32> id = labels ? labels->id(label) : std::nullopt;
        return table && id ? table->row(*id) : ByteSpan{};
    }

    // What the menus show of a car, from its rows of the car database:
    //   GENERIC_CAR: +0x00 u32 the car's row of DEFAULT_PARTS (its parts as sold);
    //   ENGINE (the car's own engine, DEFAULT_PARTS' entry of table 12): +0x00 .. +0x14 six
    //   u32 texts of the unit pool (displacement, layout, valve gear, aspiration, the engine
    //   speeds of the power and the torque), +0x1A u16 PS, +0x1C u16 kgf.m x 100;
    //   DRIVETRAIN (table 10): +0x10 u8 0 FR, 1 FF, 2 4WD, 3 MR, 4 RR;
    //   VARIATION<language> (a row per paint): +0x0C u32 the paint's name, +0x24 its colour.
    struct Paint {
        std::string name;
        u8 rgb[3] = {};
    };
    struct Spec {
        bool engine = false;
        int ps = 0;
        float torque = 0; // kgf.m
        std::string displacement, layout, valves, aspiration, power_rpm, torque_rpm;
        int drivetrain = -1;
        std::vector<Paint> paints;
    };
    std::map<std::string, Spec> specs;
    const Spec& spec(const std::string& label) {
        const auto it = specs.find(label);
        if (it != specs.end()) return it->second;
        Spec& sp = specs[label];
        if (!db) return sp;
        auto u16at = [](ByteSpan r, size_t at) { return r.size() >= at + 2 ? int(u16(r[at]) | u16(r[at + 1]) << 8) : 0; };
        auto u32at = [](ByteSpan r, size_t at) { return r.size() >= at + 4 ? u32(r[at]) | u32(r[at + 1]) << 8 | u32(r[at + 2]) << 16 | u32(r[at + 3]) << 24 : 0u; };
        const db::Strings* units = db->strings("UnistrDB");
        // The car's parts as sold: the car table's row starts with their row of DEFAULT_PARTS
        // (pairs of a part's row and its table).
        auto part = [&](u32 table_id) -> std::optional<u32> {
            const db::Table* parts = db->table("DEFAULT_PARTS");
            const ByteSpan car = car_row(label);
            if (!parts || car.size() < 4) return std::nullopt;
            const ByteSpan row = parts->row(u32at(car, 0));
            for (size_t o = 0; o + 8 <= row.size(); o += 8)
                if (u32at(row, o + 4) == table_id && u32at(row, o) != 0xFFFFFFFFu) return u32at(row, o);
            return std::nullopt;
        };
        if (const std::optional<u32> id = part(12))
            if (const db::Table* engines = db->table("ENGINE"))
                if (const ByteSpan r = engines->row(*id); r.size() >= 0x1E) {
                    sp.engine = true;
                    sp.ps = u16at(r, 0x1A), sp.torque = float(u16at(r, 0x1C)) / 100.0f;
                    if (units) {
                        sp.displacement = units->at(u32at(r, 0)), sp.layout = units->at(u32at(r, 4)), sp.valves = units->at(u32at(r, 8));
                        sp.aspiration = units->at(u32at(r, 12)), sp.power_rpm = units->at(u32at(r, 16)), sp.torque_rpm = units->at(u32at(r, 20));
                    }
                }
        if (const std::optional<u32> id = part(10))
            if (const db::Table* trains = db->table("DRIVETRAIN"))
                if (const ByteSpan r = trains->row(*id); r.size() > 0x10) sp.drivetrain = r[0x10];
        const db::Labels* cars_ = db->labels("GENERIC_CAR");
        const db::Table* car_paints = db->table("CAR_VARIATION_american");
        const db::Table* paints = db->table("VARIATIONamerican");
        const db::Strings* names_ = db->strings("american_StrDB");
        const std::optional<u32> car_id = cars_ ? cars_->id(label) : std::nullopt;
        if (car_id && car_paints && paints)
            if (const ByteSpan row = car_paints->row(*car_id); row.size() >= 4) {
                size_t first = 0;
                const size_t n = paints->rows(u32at(row, 0), first);
                for (size_t i = 0; i < n; ++i) {
                    const ByteSpan r = paints->row_at(first + i);
                    Paint paint;
                    if (names_) paint.name = names_->at(u32at(r, 0x0C));
                    if (r.size() >= 0x27) paint.rgb[0] = r[0x24], paint.rgb[1] = r[0x25], paint.rgb[2] = r[0x26];
                    sp.paints.push_back(std::move(paint));
                }
            }
        return sp;
    }

    db::SpecDb* get(const Runtime::Config& config) {
        if (tried) return db.get();
        tried = true;
        if (config.program) {
            try {
                names = ProgramTables::from_core(config.program());
            } catch (const std::exception& e) {
                rt->note(std::string("the program's name lists were not read: ") + e.what());
            }
        }
        if (!config.list) return nullptr;
        try {
            // The menus' lister takes a folder with its slash; the database names it without.
            const FolderLister list = config.list;
            db = std::make_unique<db::SpecDb>(rt->reader(), [list](const std::string& dir) { return list(!dir.empty() && dir.back() == '/' ? dir : dir + "/"); },
                                              "american");
            cars = db->cars();
            for (size_t i = 0; i < cars.size(); ++i) by_label[cars[i].label] = i;
            courses = db->courses();
        } catch (const std::exception& e) {
            rt->note(std::string("no car database: ") + e.what());
            db.reset();
        }
        return db.get();
    }
    const db::CarEntry* car(const std::string& label) const {
        const auto it = by_label.find(label);
        return it == by_label.end() ? nullptr : &cars[it->second];
    }
};

// The game day's date: day 0 is the first day of a new game. ❓ The program's own calendar
// (which day of which year a game starts on) is not read.
void day_to_date(int day, int& y, int& m, int& d) {
    std::tm t{};
    t.tm_year = 105, t.tm_mon = 3, t.tm_mday = 1 + day; // 2005-04-01
    t.tm_hour = 12;
    std::mktime(&t);
    y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
}

} // namespace

struct GameNatives {
    Runtime& rt;
    Runtime::Config config;
    std::shared_ptr<Database> database;
    std::shared_ptr<Object> game, option, sound;

    GameState& state() { return rt.game(); }

    // ---- MSound: every call goes to Runtime::sound, the one place a sound player is wired in.
    std::shared_ptr<Object> make_music(const std::string& object) {
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

    Value make_sound() {
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
    Value make_option() {
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
        Runtime* r = &rt;
        const std::string dir = config.data_dir;
        auto path = [dir] { return dir.empty() ? std::string() : dir + "/gt4r-options.txt"; };
        // apply(): the settings take effect; here they are kept (under the data folder).
        auto save = [s, path, r](Vm&, const Value&, Args) {
            const std::string file = path();
            if (file.empty()) return number(1);
            if (std::FILE* f = std::fopen(file.c_str(), "wb")) {
                const std::string t = s->options_text();
                std::fwrite(t.data(), 1, t.size(), f);
                std::fclose(f);
            } else r->note("the options could not be kept in " + file);
            return number(1);
        };
        option->method("apply", save);
        option->method("save", save);
        option->method("load", [](Vm&, const Value&, Args) { return number(1); });
        option->method("setDefault", [s, r](Vm&, const Value&, Args) {
            const std::string zone = s->option_text("game_zone");
            s->option_defaults(zone.empty() ? r->region() : zone);
            return Value{};
        });
        for (const char* nothing : {"setGameZoneDefault", "applyRaceDisplayMode", "applyMenuDisplayMode", "setDeviceConfig", "setDeviceFeedback"})
            option->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        option->method("getDeviceConfig", [](Vm&, const Value&, Args) { return Value{}; });
        option->method("getDeviceFeedback", [](Vm&, const Value&, Args) { return number(0); });
        option->set("bgm_playlist", Value{}), option->set("slide_playlist", Value{});
        // The options kept from the last time.
        if (const std::string file = path(); !file.empty())
            if (std::FILE* f = std::fopen(file.c_str(), "rb")) {
                std::string t;
                char buf[4096];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) t.append(buf, n);
                std::fclose(f);
                s->options_from_text(t);
            }
        return object_value(option);
    }

    // ---- MCarGarage: a car with its paint and parts (one of the garage's, or made by a script)
    Value make_car(const std::string& label, int paint, int slot) {
        auto car = std::make_shared<Object>("MCarGarage");
        auto data = std::make_shared<GarageCar>();
        data->car = label, data->paint = paint;
        std::shared_ptr<Database> d = database;
        Runtime::Config cfg = config;
        car->property("car_code", [data] { return text(data->car); }, [data](const Value& v) { data->car = v.to_string(); });
        car->method("getCarCode", [data](Vm&, const Value&, Args) { return text(data->car); });
        car->method("getCarLabel", [data](Vm&, const Value&, Args) { return text(data->car); });
        car->method("setCarCode", [data](Vm&, const Value&, Args a) {
            data->car = arg_text(a, 0);
            return Value{};
        });
        auto name = [data, d, cfg] {
            d->get(cfg);
            const db::CarEntry* e = d->car(data->car);
            return text(e ? e->name : data->car);
        };
        car->property("name", name);
        car->property("short_name", name);
        car->property("short_name_no_year", name);
        car->set("slot", number(slot));
        car->set("rideHistory", number(0));
        GameState* st = &state();
        auto spec = [data, d, cfg]() -> const Database::Spec& {
            d->get(cfg);
            return d->spec(data->car);
        };
        car->method("getColorNum", [spec](Vm&, const Value&, Args) { return number(s32(std::max<size_t>(spec().paints.size(), 1))); });
        // getColors(): a paint each, as its colour (0 to 1 a part).
        auto colors = [spec](Vm&, const Value&, Args) {
            std::vector<Value> out;
            for (const Database::Paint& p : spec().paints) {
                auto c = std::make_shared<Object>("MColorObject");
                c->set("r", Value::number(float(p.rgb[0]) / 255.0f)), c->set("g", Value::number(float(p.rgb[1]) / 255.0f));
                c->set("b", Value::number(float(p.rgb[2]) / 255.0f)), c->set("a", Value::number(1));
                out.push_back(object_value(c));
            }
            return Value::array(std::move(out));
        };
        car->method("getColors", colors);
        car->method("getAllColors", colors);
        car->method("getColorName", [spec, data](Vm&, const Value&, Args a) {
            const std::vector<Database::Paint>& paints = spec().paints;
            const s32 i = a.empty() ? data->paint : arg_int(a, 0);
            return text(i >= 0 && size_t(i) < paints.size() && !paints[size_t(i)].name.empty() ? paints[size_t(i)].name : std::string("-"));
        });
        // The drivetrain by its name in the text ("DriveTrainName": FR, FF, 4WD, MR, RR).
        auto train = [spec](Vm&, const Value&, Args) {
            static const char* const kinds[] = {"FR", "FF", "4WD", "MR", "RR"};
            const int t = spec().drivetrain;
            return t >= 0 && t < 5 ? text(kinds[t]) : Value{};
        };
        car->method("getDTrainType", train);
        car->method("getDTrain", train);
        // getCatPs(format), getCatTq(format): the catalog's power and torque as text; the
        // format takes the number, its unit, the engine speed and its unit ("%d%s/%s%s";
        // "%.2t": the torque with two decimals).
        car->method("getCatPs", [spec, st](Vm& vm, const Value&, Args a) {
            const Database::Spec& sp = spec();
            if (!sp.engine) return text("--");
            const bool hp = st->option_int("unit_power") != 0;
            const Value args[4] = {number(s32(std::lround(float(sp.ps) * (hp ? 0.98632f : 1.0f)))), text(hp ? "HP" : "ps"), text(sp.power_rpm), text("rpm")};
            return a.empty() ? args[0] : vm.call(vm.attribute(a[0], "format"), args);
        });
        car->method("getCatTq", [spec, st](Vm& vm, const Value&, Args a) {
            const Database::Spec& sp = spec();
            if (!sp.engine) return text("--");
            const bool ftlb = st->option_int("unit_torque") != 0;
            const float v = sp.torque * (ftlb ? 7.233f : 1.0f);
            if (a.empty()) return Value::number(v);
            std::string format = a[0].to_string();
            for (size_t at; (at = format.find('t')) != std::string::npos && at > 0 && (format[at - 1] == '%' || std::isdigit(u8(format[at - 1])));) format[at] = 'f';
            const Value args[4] = {Value::number(v), text(ftlb ? "ft.lb" : "kgf.m"), text(sp.torque_rpm), text("rpm")};
            return vm.call(vm.attribute(text(format), "format"), args);
        });
        car->method("getCatDisplacement", [spec](Vm&, const Value&, Args) { return text(spec().displacement); });
        car->method("getCatEngineType", [spec](Vm&, const Value&, Args) { return text(spec().layout); });
        car->method("getCatEngineCam", [spec](Vm&, const Value&, Args) { return text(spec().valves); });
        car->method("getAspiration", [spec](Vm&, const Value&, Args) { return text(spec().aspiration); });
        car->method("getCurrentPs", [spec](Vm&, const Value&, Args) { return number(spec().ps); });
        car->method("getCurrentPower", [spec](Vm&, const Value&, Args) { return number(spec().ps); });
        car->method("getCurrentTq", [spec](Vm&, const Value&, Args) { return Value::number(spec().torque); });
        car->method("isTurbo", [spec](Vm&, const Value&, Args) { return number(spec().aspiration == "TURBO"); });
        car->method("isSuperCharger", [spec](Vm&, const Value&, Args) { return number(spec().aspiration == "SUPERCHARGER"); });
        car->method("getOdometer", [data](Vm&, const Value&, Args) { return Value::number(data->odometer); });
        car->method("getDate", [data](Vm&, const Value&, Args) { return number(data->date); });
        for (const char* nothing : {"dump", "updateCurrentStatus", "loadPartsInfo", "setupPartsBuffer", "beginSetting", "endSetting", "syncWheel", "makeUsed"})
            car->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        car->method("getColorIndex", [data](Vm&, const Value&, Args) { return number(data->paint); });
        car->method("getGarageIndex", [slot](Vm&, const Value&, Args) { return number(slot); });
        car->method("isVacant", [data](Vm&, const Value&, Args) { return number(data->car.empty()); });
        // ❓ Which cars have no manual gears is not read: none.
        car->method("isAutomaticOnly", [](Vm&, const Value&, Args) { return number(0); });
        car->method("copyFrom", [](Vm&, const Value&, Args) { return Value{}; });
        return object_value(car);
    }

    // ---- MGame
    Value make_game() {
        if (game) return object_value(game);
        game = std::make_shared<Object>("MGame");
        GameState* s = &state();
        Object& g = *game;
        Runtime* r = &rt;
        field(g, "major_menu_project", &s->major_menu_project);
        field(g, "major_menu_page", &s->major_menu_page);
        field(g, "major_menu_widget", &s->major_menu_widget);
        g.property("last_sequence", [s] { return text(s->last_sequence); });
        field(g, "next_sequence", &s->next_sequence);
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
        g.set("last_record", Value{}), g.set("labo_folder", Value{}), g.set("labo_list_index", number(0));
        g.method("getEntryCarCode", [q](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            return text(i >= 0 && size_t(i) < q->entries.size() ? q->entries[size_t(i)] : std::string());
        });
        g.method("setEntryCarCode", [q](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            if (i < 0 || i > 64) return Value{};
            if (q->entries.size() <= size_t(i)) q->entries.resize(size_t(i) + 1);
            q->entries[size_t(i)] = arg_text(a, 1);
            return Value{};
        });
        // The pages the menus came through: a stack of names (the title page pushes the mode).
        g.method("menuPush", [s](Vm&, const Value&, Args a) {
            s->menu_stack.push_back(a.empty() ? std::string() : a[0].to_string());
            return Value{};
        });
        g.method("menuPop", [s](Vm&, const Value&, Args) {
            if (s->menu_stack.empty()) return Value{};
            const std::string top = s->menu_stack.back();
            s->menu_stack.pop_back();
            return text(top);
        });
        g.method("menuRef", [s](Vm&, const Value&, Args) { return s->menu_stack.empty() ? Value{} : text(s->menu_stack.back()); });
        g.method("menuClearStack", [s](Vm&, const Value&, Args) {
            s->menu_stack.clear();
            return Value{};
        });
        for (const char* nothing : {"saveMenuStack", "loadMenuStack", "setReturnToLastMenuProject", "abandonRace", "loadShutterSound", "dumpAvailable"})
            g.method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        // Credits.
        g.property("money", [s] { return number(s32(std::min<s64>(s->money, INT32_MAX))); }, [s](const Value& v) { s->money = s64(v.as_float()); });
        g.method("incMoney", [s](Vm&, const Value&, Args a) {
            s->money = std::clamp<s64>(s->money + s64(arg_number(a, 0)), 0, 999'999'999);
            return Value{};
        });
        g.method("setMoneyZero", [s](Vm&, const Value&, Args) {
            s->money = 0;
            return Value{};
        });
        g.method("isEnoughMoney", [s](Vm&, const Value&, Args a) { return number(s->money >= s64(arg_number(a, 0))); });
        g.method("getTotalPrizeMoney", [s](Vm&, const Value&, Args) { return number(s32(std::min<s64>(s->total_prize_money, INT32_MAX))); });
        g.method("incTotalPrizeMoney", [s](Vm&, const Value&, Args a) {
            s->total_prize_money += s64(arg_number(a, 0));
            return Value{};
        });
        g.method("getScore", [s](Vm&, const Value&, Args) { return number(s->score); });
        g.method("incScore", [s](Vm&, const Value&, Args a) {
            s->score += arg_int(a, 0);
            return Value{};
        });
        g.method("getTotalPrizeCars", [s](Vm&, const Value&, Args) { return number(s->total_prize_cars); });
        g.method("incTotalPrizeCars", [s](Vm&, const Value&, Args) {
            ++s->total_prize_cars;
            return Value{};
        });
        g.method("getTotalRaceCount", [s](Vm&, const Value&, Args) { return number(s->races); });
        for (const char* zero : {"getTotalRankCount", "getAspecDistance", "getBspecDistance", "getBspecTotalExperience", "getBspecCarExperience",
                                 "getBspecCourseExperience", "getBspecBattleExperience"})
            g.method(zero, [](Vm&, const Value&, Args) { return number(0); });
        g.method("incTotalRankCount", [](Vm&, const Value&, Args) { return Value{}; });
        field(g, "username", &s->username), field(g, "password", &s->password), field(g, "last_entry_name", &s->last_entry_name);
        field(g, "withdrawn", &s->withdrawn), field(g, "met_type", &s->met_type);
        g.set("withdrawnGT4p", number(0));
        // A race left unfinished: none.
        g.method("isSuspended", [s](Vm&, const Value&, Args) { return number(!s->suspended_racecode.empty()); });
        field(g, "suspended_racecode", &s->suspended_racecode);
        g.set("suspended_session", number(0));
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
        g.method("isNeedSave", [s](Vm&, const Value&, Args) { return number(s->need_save); });
        g.method("setNeedSave", [s](Vm&, const Value&, Args a) {
            s->need_save = a.empty() || a[0].truthy();
            return Value{};
        });
        g.method("isDuringQuickMenu", [s](Vm&, const Value&, Args) { return number(s->during_quick_menu); });
        g.method("setDuringQuickMenu", [s](Vm&, const Value&, Args a) {
            s->during_quick_menu = a.empty() || a[0].truthy();
            return Value{};
        });
        g.method("getAllMusicList", [](Vm&, const Value&, Args) { return Value::array({}); });
        g.method("getBattleSettingRace", [](Vm&, const Value&, Args) { return Value{}; });
        g.method("getBattleSettingEntry", [](Vm&, const Value&, Args) { return Value{}; });
        g.property("option", [this] { return make_option(); });
        g.method("getOption", [this](Vm&, const Value&, Args) { return make_option(); });
        g.method("setOption", [](Vm&, const Value&, Args) { return Value{}; });

        // The garage.
        auto garage = std::make_shared<Object>("MGarage");
        garage->property("count", [s] { return number(s32(s->garage.size())); });
        garage->method("hasRidingCar", [s](Vm&, const Value&, Args) { return number(s->riding_car >= 0 && size_t(s->riding_car) < s->garage.size()); });
        garage->property("riding_car", [this, s] {
            if (s->riding_car < 0 || size_t(s->riding_car) >= s->garage.size()) return Value{};
            const GarageCar& c = s->garage[size_t(s->riding_car)];
            return make_car(c.car, c.paint, s->riding_car);
        });
        garage->method("isFull", [s](Vm&, const Value&, Args) { return number(s->garage.size() >= 1000); });
        auto add = [s](Vm& vm, const Value&, Args a) {
            GarageCar c;
            if (!a.empty() && a[0].kind == Kind::String) c.car = a[0].as_string(), c.paint = arg_int(a, 1);
            else if (!a.empty()) c.car = vm.attribute(a[0], "car_code").to_string(), c.paint = arg_int(a, 1);
            c.date = s->day;
            if (c.car.empty() || s->garage.size() >= 1000) return number(-1);
            s->garage.push_back(std::move(c));
            return number(s32(s->garage.size()) - 1);
        };
        garage->method("addCar", add), garage->method("addNewCar", add), garage->method("addUsedCar", add);
        garage->method("delCar", [s](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            if (i < 0 || size_t(i) >= s->garage.size()) return Value{};
            s->garage.erase(s->garage.begin() + i);
            if (s->riding_car == i) s->riding_car = -1;
            else if (s->riding_car > i) --s->riding_car;
            return Value{};
        });
        garage->method("delAllCarExceptRidingCar", [s](Vm&, const Value&, Args) {
            if (s->riding_car >= 0 && size_t(s->riding_car) < s->garage.size()) {
                const GarageCar kept = s->garage[size_t(s->riding_car)];
                s->garage = {kept}, s->riding_car = 0;
            } else s->garage.clear(), s->riding_car = -1;
            return Value{};
        });
        garage->method("setRidingCar", [s](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            if (i >= 0 && size_t(i) < s->garage.size()) s->riding_car = i;
            return Value{};
        });
        garage->method("changeRidingCar", [s](Vm&, const Value&, Args a) {
            const s32 i = arg_int(a, 0, -1);
            if (i >= 0 && size_t(i) < s->garage.size()) s->riding_car = i;
            return Value{};
        });
        garage->method("cancelRidingCar", [s](Vm&, const Value&, Args) {
            s->riding_car = -1;
            return Value{};
        });
        for (const char* nothing : {"undelCar", "pushRidingCar", "popGarageCar"}) garage->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        for (const char* list : {"pickup", "pickupCountry", "pickupTuner"}) garage->method(list, [](Vm&, const Value&, Args) { return Value::array({}); });
        g.set("garage", object_value(garage));

        // The calendar: the game's day.
        auto calendar = std::make_shared<Object>("MCalendar");
        calendar->property("today", [s] { return number(s->day); }, [s](const Value& v) { s->day = int(v.as_float()); });
        calendar->method("getElapsedWeek", [s](Vm&, const Value&, Args) { return number(s->day / 7); });
        calendar->method("getElapsedDate", [s](Vm&, const Value&, Args) { return number(s->day); });
        calendar->method("incDate", [s](Vm&, const Value&, Args a) {
            s->day += std::max(arg_int(a, 0, 1), 0);
            return Value{};
        });
        calendar->method("getEvent", [](Vm&, const Value&, Args) { return Value{}; });
        for (const char* put : {"putBuyCarEvent", "putBuyNewCarEvent", "putBuyUsedCarEvent", "putBuyTradeCarEvent", "putPresentCarEvent", "putSellCarEvent",
                                "putBuyWheelEvent", "putBuyWingEvent", "putRunRaceEvent", "putRunLicenseEvent", "putRunMissionEvent",
                                "putRunFreeRunEvent", "putRunCourseEvent"})
            calendar->method(put, [](Vm&, const Value&, Args) { return Value{}; });
        g.set("calendar", object_value(calendar));

        // The demonstrations the title page falls back to.
        auto demo = std::make_shared<Object>("MDemonstration");
        for (const char* nothing : {"initialize", "resetPlayListCounter", "resetMovieCount"}) demo->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        demo->set("type", text("")), demo->set("name", text("")), demo->set("arg", text(""));
        demo->set("repeat_time", number(0)), demo->set("skip_time", number(0)), demo->set("restart_from_beginning", number(0));
        g.set("demonstration", object_value(demo));

        // Favorites.
        auto favorite = [](std::set<std::string>* set) {
            auto f = std::make_shared<Object>("MFavorite");
            f->method("clear", [set](Vm&, const Value&, Args) {
                set->clear();
                return Value{};
            });
            f->method("has", [set](Vm&, const Value&, Args a) { return number(set->count(arg_text(a, 0)) > 0); });
            f->method("append", [set](Vm&, const Value&, Args a) {
                set->insert(arg_text(a, 0));
                return Value{};
            });
            f->method("remove", [set](Vm&, const Value&, Args a) {
                set->erase(arg_text(a, 0));
                return Value{};
            });
            f->method("buildLabelList", [set](Vm&, const Value&, Args) {
                std::vector<Value> v;
                for (const std::string& l : *set) v.push_back(text(l));
                return Value::array(std::move(v));
            });
            f->method("getSize", [set](Vm&, const Value&, Args) { return number(s32(set->size())); });
            f->method("isFull", [set](Vm&, const Value&, Args) { return number(set->size() >= 100); });
            f->method("dump", [](Vm&, const Value&, Args) { return Value{}; });
            return object_value(f);
        };
        g.set("favorite_car", favorite(&s->favorite_cars));
        g.set("favorite_course", favorite(&s->favorite_courses));

        // The records: nothing run yet.
        auto unit = [] {
            auto u = std::make_shared<Object>("MRaceRecordUnit");
            u->set("type", number(0)), u->set("result", number(-1)), u->set("present_result", number(-1)), u->set("spot_result", number(-1));
            u->set("best_score", number(0));
            for (const char* nothing : {"refreshResult", "update", "updateBestScore"}) u->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
            for (const char* zero : {"getRankNum", "getRankNumMaxSpeed", "getBestTime", "getBestSpeed", "getDate"})
                u->method(zero, [](Vm&, const Value&, Args) { return number(0); });
            for (const char* none : {"getCarName", "getPassCode"}) u->method(none, [](Vm&, const Value&, Args) { return text(""); });
            for (const char* nothing : {"insertEntry", "insertEntryMaxSpeed", "deleteEntry", "deleteEntryMaxSpeed"})
                u->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
            return object_value(u);
        };
        auto race_record = std::make_shared<Object>("MRaceRecord");
        race_record->method("getUnit", [unit](Vm&, const Value&, Args) { return unit(); });
        for (const char* no : {"isLicensePerfect", "isLicenseAllSilver", "isLicenseAllBronze", "isLicensePassed", "canTryLastLicense", "isMissionClear"})
            race_record->method(no, [](Vm&, const Value&, Args) { return number(0); });
        g.set("race_record", object_value(race_record));
        g.set("license_record", object_value(race_record));
        auto course_record = std::make_shared<Object>("MCourseRecord");
        for (const char* get : {"getUnit", "getMTUnit", "create"}) course_record->method(get, [unit](Vm&, const Value&, Args) { return unit(); });
        g.set("course_record", object_value(course_record));
        g.set("machine_record", object_value(course_record));
        auto present = std::make_shared<Object>("MPresent");
        for (const char* zero : {"getByRace", "getByTime"}) present->method(zero, [](Vm&, const Value&, Args) { return number(0); });
        for (const char* nothing : {"setByRace", "setByTime", "dump"}) present->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        g.set("present", object_value(present));
        auto used = std::make_shared<Object>("MUsedCar");
        used->method("get", [](Vm&, const Value&, Args) { return number(0); });
        used->method("set", [](Vm&, const Value&, Args) { return Value{}; });
        used->set("week", number(0));
        g.set("used_car", object_value(used));
        (void)r;
        return object_value(game);
    }

    // ---- MMemoryCardManager(mode[, slot]): the memory card as a console with a card and no
    // saved game answers. A file's isExist gives 0 when it is there and 1 when the card has
    // none; load and save give a result's name.
    Value make_card_manager(Args a) {
        auto m = std::make_shared<Object>("MMemoryCardManager");
        GameState* s = &state();
        const std::string mode = arg_text(a, 0);
        m->set("mode", text(mode));
        auto file = std::make_shared<Object>("MMemoryCardFile");
        file->method("isExist", [s](Vm&, const Value&, Args) { return number(!s->card.connected ? -1 : s->card.has_game_data ? 0 : 1); });
        file->method("load", [s](Vm&, const Value&, Args) { return text(s->card.has_game_data ? "OK" : "FILE_NOT_FOUND"); });
        file->method("loadInstance", [s](Vm&, const Value&, Args) { return Value::array({text(s->card.has_game_data ? "OK" : "FILE_NOT_FOUND"), Value{}}); });
        // Saving: there is no card file to write; the game goes on as saved.
        auto save = [s](Vm&, const Value&, Args) {
            if (!s->card.connected) return text("NOT_CONNECTED");
            s->card.has_game_data = true, s->need_save = false;
            return text("OK");
        };
        for (const char* name : {"save", "saveNew", "saveDnas", "writeInfo"}) file->method(name, save);
        for (const char* ok : {"remove", "format", "unformat"}) file->method(ok, [](Vm&, const Value&, Args) { return text("OK"); });
        for (const char* nothing : {"dump", "setNewFile"}) file->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        for (const char* zero : {"readSize", "dataFileSize", "isInfoValid", "isIconValid"}) file->method(zero, [](Vm&, const Value&, Args) { return number(0); });
        file->method("getIconTexture", [](Vm&, const Value&, Args) { return Value{}; });
        file->set("size", number(0)), file->set("objectName", text("")), file->set("title", text("")), file->set("carName", text(""));
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
        m->method("isNoFile", [s](Vm&, const Value&, Args) { return number(!s->card.has_game_data); });
        m->method("isAutoSaveDisabled", [](Vm&, const Value&, Args) { return number(0); });
        for (const char* nothing : {"clearAutoSaveDisabled", "requestPause", "waitPause", "start", "restart", "clear"})
            m->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        for (const char* ok : {"test", "save"}) m->method(ok, [](Vm&, const Value&, Args) { return text("OK"); });
        for (const char* no : {"isExistGT4pData", "isExistGT3Data"}) m->method(no, [](Vm&, const Value&, Args) { return number(0); });
        for (const char* none : {"inheritGT4pData", "inheritGT3Data"}) m->method(none, [](Vm&, const Value&, Args) { return text("FILE_NOT_FOUND"); });
        m->property("free", [s] { return number(s->card.free_kb); });
        m->property("realFree", [s] { return number(s->card.free_kb); });
        m->set("count", number(0)), m->set("cursorPos", number(0)), m->set("progress", Value{}), m->set("list", Value::array({}));
        m->set("autoloadPlayList", Value{}), m->set("autoloadPlayListResult", Value{});
        return object_value(m);
    }
};

namespace {
std::weak_ptr<GameNatives> g_natives_of(Runtime& rt, std::shared_ptr<GameNatives> set = nullptr) {
    static std::map<Runtime*, std::weak_ptr<GameNatives>> all;
    if (set) all[&rt] = set;
    for (auto it = all.begin(); it != all.end();) it = it->second.expired() ? all.erase(it) : std::next(it);
    const auto it = all.find(&rt);
    return it == all.end() ? std::weak_ptr<GameNatives>() : it->second;
}
} // namespace

Value make_car_object(Runtime& rt, const std::string& label, int paint) {
    const std::shared_ptr<GameNatives> n = g_natives_of(rt).lock();
    return n ? n->make_car(label, paint, -1) : Value{};
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
    auto n = std::make_shared<GameNatives>(GameNatives{rt, {}, std::make_shared<Database>(), nullptr, nullptr, nullptr});
    n->database->rt = &rt;
    g_natives_of(rt, n);
    rt.keep_alive(n);
    Runtime* r = &rt;
    GameState* s = &rt.game();

    // The objects the main script makes.
    {
        auto cls = make_class(menu, "MGame");
        cls->construct = [n](Vm&, Args) { return n->make_game(); };
        // The configuration script the main script requires: the retail game's.
        static_fn(*cls, "getConfigScriptFile", [](Vm&, const Value&, Args) { return text("config-main"); });
    }
    make_class(menu, "MSound")->construct = [n](Vm&, Args) { return n->make_sound(); };
    make_class(menu, "MOption")->construct = [n](Vm&, Args) { return n->make_option(); };
    make_class(menu, "MShell")->construct = [](Vm&, Args) {
        auto shell = std::make_shared<Object>("MShell");
        for (const char* nothing : {"putMessage", "execute", "getCandidates"}) shell->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        shell->set("pipe", Value{});
        return object_value(shell);
    };
    // The network: none. What the scripts ask before they use it answers no.
    make_class(menu, "MNetwork")->construct = [](Vm&, Args) {
        auto net = std::make_shared<Object>("MNetwork");
        for (const char* no : {"isNetworkAdaptorAvailable", "isInterfaceInitialized", "isNetworkInitialized", "isRTIMEInitialized", "isLoggedIn",
                               "isMGCLworking", "amIhost", "isClientDisconnected", "isDisconnected", "imHaveMessage", "isEnableAbort",
                               "raceMenuAllConnected", "raceMenuChanged", "getInterfaceCount", "getStyleNumLAN", "getMemberNumLAN", "gameTime"})
            net->method(no, [](Vm&, const Value&, Args) { return number(0); });
        for (const char* nothing : {"inLobby", "setLobbyMessageEnable", "terminateNetwork", "terminateRTIME", "disconnect", "logout", "leaveGame",
                                    "raceMenuStart", "raceMenuEnd", "raceMenuForceEnd", "raceMenuNotifySequence", "raceMenuNotifyCourse",
                                    "raceMenuNotifyCar", "raceMenuNotifyEntrySetting", "raceMasterNotifyRaceSetting", "raceMasterNotifyCourse",
                                    "setGameClosed", "enterNetSync", "netSync", "leaveNetSync", "initCommunicator", "exitCommunicator",
                                    "beginEnableAbort", "endEnableAbort", "setCallbackModule", "imClose"})
            net->method(nothing, [](Vm&, const Value&, Args) { return Value{}; });
        net->method("getLastErrorCode", [](Vm&, const Value&, Args) { return text(""); });
        net->set("network_enable", number(0)), net->set("error_exit", number(0)), net->set("my_id", number(0)), net->set("my_name", text(""));
        net->set("language", text("")), net->set("pipe", Value{}), net->set("im_pipe", Value{});
        return object_value(net);
    };

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
        for (const char* nothing : {"SetDrawPerfMeter", "SetLoadingMode", "DumpMemoryBlock", "SyncUnitWithLanguage", "Reboot"})
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
        auto miles = [s] { return s->option_int("unit_velocity") != 0; };
        auto hp = [s] { return s->option_int("unit_power") != 0; };
        static_fn(*cls, "GetUnitOfVelocity", [miles](Vm&, const Value&, Args) { return text(miles() ? "mph" : "km/h"); });
        static_fn(*cls, "GetVelocityForDisplay", [miles](Vm&, const Value&, Args a) { return Value::number(arg_number(a, 0) * (miles() ? 0.621371f : 1.0f)); });
        static_fn(*cls, "GetVelocityString", [miles](Vm&, const Value&, Args a) {
            char b[32];
            std::snprintf(b, sizeof b, "%d", int(std::lround(arg_number(a, 0) * (miles() ? 0.621371f : 1.0f))));
            return text(b);
        });
        static_fn(*cls, "GetUnitOfPower", [hp](Vm&, const Value&, Args) { return text(hp() ? "HP" : "ps"); });
        static_fn(*cls, "GetPowerForDisplay", [hp](Vm&, const Value&, Args a) { return Value::number(arg_number(a, 0) * (hp() ? 0.98632f : 1.0f)); });
        static_fn(*cls, "GetUnitOfTorque", [s](Vm&, const Value&, Args) { return text(s->option_int("unit_torque") != 0 ? "ft.lb" : "kgf.m"); });
        static_fn(*cls, "GetTorqueForDisplay", [s](Vm&, const Value&, Args a) {
            return Value::number(arg_number(a, 0) * (s->option_int("unit_torque") != 0 ? 7.233f : 1.0f));
        });
        static_fn(*cls, "GetUnitOfMass", [miles](Vm&, const Value&, Args) { return text(miles() ? "lbs" : "kg"); });
        static_fn(*cls, "GetCourseLengthString", [miles](Vm&, const Value&, Args a) {
            char b[32];
            const float m = arg_number(a, 0);
            if (miles()) std::snprintf(b, sizeof b, "%.2f mi", double(m / 1609.344f));
            else std::snprintf(b, sizeof b, "%d m", int(m));
            return text(b);
        });
        static_fn(*cls, "GetCourseHeightString", [miles](Vm&, const Value&, Args a) {
            char b[32];
            const float m = arg_number(a, 0);
            if (miles()) std::snprintf(b, sizeof b, "%d ft", int(std::lround(m * 3.28084f)));
            else std::snprintf(b, sizeof b, "%d m", int(m));
            return text(b);
        });
        static_fn(*cls, "SetUnitSystemByLanguage", [](Vm&, const Value&, Args) { return Value{}; });
        static_fn(*cls, "IsCalendarTypeDayFirst", [r](Vm&, const Value&, Args) { return number(r->region() == "uk"); });
    }
    {
        auto cls = make_class(menu, "MUtility");
        // 1234567 -> "1,234,567"
        static_fn(*cls, "GetMoneyString", [](Vm&, const Value&, Args a) {
            const s64 v = s64(a.empty() ? 0 : a[0].kind == Kind::Int ? a[0].i : s64(a[0].as_float()));
            std::string digits = std::to_string(v < 0 ? -v : v), out;
            for (size_t i = 0; i < digits.size(); ++i) {
                if (i && (digits.size() - i) % 3 == 0) out += ',';
                out += digits[i];
            }
            return text((v < 0 ? "-" : "") + out);
        });
        // A lap time in thousandths of a second: 1'23.456
        static_fn(*cls, "GetTimeString", [](Vm&, const Value&, Args a) {
            const s32 ms = arg_int(a, 0);
            if (ms < 0) return text("--'--.---");
            char b[32];
            std::snprintf(b, sizeof b, "%d'%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
            return text(b);
        });
        static_fn(*cls, "GetFormatString", [](Vm& vm, const Value&, Args a) {
            if (a.empty()) return text("");
            return vm.call(vm.attribute(a[0], "format"), a.subspan(1));
        });
        static_fn(*cls, "GetPriceMagnification", [](Vm&, const Value&, Args) { return number(1); });
    }
    {
        auto cls = make_class(menu, "MRandom");
        auto roll = [](Vm&, const Value&, Args a) {
            // GetValue(from, to): a whole number from `from` up to, not including, `to`.
            static u32 seed = 0x2545F491u;
            seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
            const s32 lo = a.size() > 1 ? arg_int(a, 0) : 0, hi = arg_int(a, a.size() > 1 ? 1 : 0, 1);
            return number(hi > lo ? lo + s32(seed % u32(hi - lo)) : lo);
        };
        static_fn(*cls, "GetValue", roll);
        cls->construct = [roll](Vm&, Args) {
            auto o = std::make_shared<Object>("MRandom");
            o->method("getValue", roll);
            return object_value(o);
        };
    }
    {
        auto cls = make_class(menu, "MCalendar");
        static_fn(*cls, "dayToDate", [](Vm&, const Value&, Args a) {
            int y, m, d;
            day_to_date(arg_int(a, 0), y, m, d);
            return Value::array({number(y), number(m), number(d)});
        });
        static_fn(*cls, "dateToDay", [](Vm&, const Value&, Args a) {
            std::tm t{}, base{};
            t.tm_year = arg_int(a, 0) - 1900, t.tm_mon = arg_int(a, 1) - 1, t.tm_mday = arg_int(a, 2), t.tm_hour = 12;
            base.tm_year = 105, base.tm_mon = 3, base.tm_mday = 1, base.tm_hour = 12;
            return number(s32(std::lround(std::difftime(std::mktime(&t), std::mktime(&base)) / 86400.0)));
        });
        static_fn(*cls, "dayToWeekDay", [](Vm&, const Value&, Args a) { return number(((arg_int(a, 0) + 5) % 7 + 7) % 7); });
        static_fn(*cls, "dayToGameDay", [](Vm&, const Value&, Args a) { return number(arg_int(a, 0) + 1); });
        static_fn(*cls, "gameDayToDay", [](Vm&, const Value&, Args a) { return number(arg_int(a, 0) - 1); });
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
        static_fn(*cls, "getPrintList", [](Vm&, const Value&, Args) { return Value::array({}); });
        static_fn(*cls, "setAction", [](Vm&, const Value&, Args) { return Value{}; });
        static_fn(*cls, "getAction", [](Vm&, const Value&, Args) { return number(0); });
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
        static_fn(*cls, "PickUpEnemyCars", [](Vm&, const Value&, Args) { return Value::array({}); });
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
        static_fn(*cls, "GetID", [d, n](Vm&, const Value&, Args a) {
            db::SpecDb* db = d->get(n->config);
            const db::Labels* labels = db ? db->labels("COURSE") : nullptr;
            const std::optional<u32> id = labels ? labels->id(arg_text(a, 0)) : std::nullopt;
            return number(id ? s32(*id) : -1);
        });
    }
    {
        auto cls = make_class(menu, "MCarGarage");
        cls->construct = [n](Vm&, Args a) { return n->make_car(arg_text(a, 0), arg_int(a, 1), -1); };
    }
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

    // ---- small ones
    make_class(menu, "MColorObject")->construct = [](Vm&, Args a) {
        auto c = std::make_shared<Object>("MColorObject");
        c->set("r", a.size() > 0 ? a[0] : Value::number(1)), c->set("g", a.size() > 1 ? a[1] : Value::number(1));
        c->set("b", a.size() > 2 ? a[2] : Value::number(1)), c->set("a", a.size() > 3 ? a[3] : Value::number(1));
        return object_value(c);
    };
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
    n->config = rt.config();
}

} // namespace gt4r::menu
