// The program's objects the race's menus call: MQuickWork (the program's class registered at
// 0x1255F8; its fields and defaults at 0x127E60), docs/formats/menu.md "The race's menus".

#include "gt4r/menu/race_natives.h"

#include "gt4r/menu/game.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/menu/native.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {


// A time in ms as the quick pages write it: M'SS.mmm, with hours H:MM'SS.mmm; no time: the
// font's dashes (character 0x7F).
std::string quick_time_text(s32 ms) {
    if (ms < 0 || ms == QuickWork::kNoTime) return "\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F";
    char b[40];
    if (ms >= 3600000) std::snprintf(b, sizeof b, "%d:%02d'%02d.%03d", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    else std::snprintf(b, sizeof b, "%d'%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return b;
}

// A grid car's power and weight as the race's car is built (ShowRoomCar 0x364E90: the built car's
// power float, truncated; 0x364FA8 its weight): the car as sold. The power is 0 (written "--")
// when the car's row hides its figures (GENERIC_CAR +0x12 bit 3, 0x4489A0).
struct RowFigures {
    int power = 0, weight = 0;
    bool known = false;
};
RowFigures row_figures(Runtime& rt, const std::string& label) {
    static std::map<std::string, RowFigures> cache; // ❓ one disc a run: the cars' figures do not change
    if (const auto it = cache.find(label); it != cache.end()) return it->second;
    RowFigures f;
    try {
        if (std::shared_ptr<GameNatives> n = natives_of(rt)) {
            Database& d = n->db();
            sim::Equipment eq;
            if (d.db && sim::stock_car(*d.db, d.program, label, eq)) {
                sim::Spec spec;
                sim::build_spec(*d.db, d.program, eq, 0, spec);
                const sim::SpecFigures fig = sim::spec_figures(spec);
                f.power = int(fig.ps); // toward zero, as the program's conversion
                f.weight = spec.weight_kg();
                f.known = true;
                const db::Table* t = d.db->table("GENERIC_CAR");
                const ByteSpan car = t ? t->row(u32(eq.bytes[0]) | u32(eq.bytes[1]) << 8 | u32(eq.bytes[2]) << 16 | u32(eq.bytes[3]) << 24) : ByteSpan{};
                if (car.size() > 0x12 && (car[0x12] & 8)) f.power = 0;
            }
        }
    } catch (const std::exception&) {
    }
    cache[label] = f;
    return f;
}

} // namespace

void fill_player_car(Runtime& rt, QuickWork& w, const std::string& label) {
    w.player_car = label;
    try {
        std::shared_ptr<GameNatives> n = natives_of(rt);
        if (!n) return;
        Database& d = n->db();
        const ByteSpan car = d.row("GENERIC_CAR", label);
        if (car.size() > 0x11) {
            w.set("carCategory", s32(car[0x11] % 100));
            w.set("carYear", s32(s16(u16(car[0x0C] | car[0x0D] << 8))));
        }
        const int t = d.spec(label).drivetrain;
        w.set("driveTrainType", t >= 0 && t < 5 ? t : -1);
    } catch (const std::exception&) {
    }
}

const std::vector<std::string>& QuickWork::command_names() {
    static const std::vector<std::string> names = {
        "COMMAND_NOTHING",     "COMMAND_START",       "COMMAND_EXIT",        "COMMAND_REPLAY",      "COMMAND_DEMONSTRATION", "COMMAND_RECORDS",
        "COMMAND_LOAD_GAME",   "COMMAND_SAVE_GAME",   "COMMAND_LOAD_REPLAY", "COMMAND_SAVE_REPLAY", "COMMAND_LOAD_GHOST",    "COMMAND_SAVE_GHOST",
        "COMMAND_CANCEL",      "COMMAND_CONTINUE",    "COMMAND_SPECTATOR",   "COMMAND_SETTINGS",    "COMMAND_QUICK_TUNE",    "COMMAND_RETURN_MENU",
        "COMMAND_NEXT_RACE",   "COMMAND_RETIRE_CHAMPIONSHIP", "COMMAND_FREE_QUALIFY", "COMMAND_LOGGER", "COMMAND_OPTION",
    };
    return names;
}

QuickWork::QuickWork() {
    // The plain integer attributes and the defaults the program gives them.
    for (const char* zero : {"canReplay", "canLoadGhost", "canSaveGhost", "numberOfEntries", "cursorPosition", "IsSessionFinished", "SessionNumber",
                             "IsQuickTuned", "IsDryCourse", "IsASpec", "IsBSpec", "BestTime", "BestMaxSpeed", "prize", "GrandPrize", "CanLogger",
                             "DisableLogger", "IsFinalSession", "carYear", "CourseLength", "Laps", "QuickTuneWeightLevel", "QuickTunePowerLevel",
                             "QuickTuneGearMaxSpeed", "QuickTuneGearMaxSpeedMin", "QuickTuneGearMaxSpeedMax", "QuickTuneTransmission",
                             "QuickTuneDrivingAssist",
                             // Prologue's: whether its license page opens on the test's guide (GuidePage, a demonstration
                             // in a scene view, ❓ not played here) or on the test's page (LicenseRoot).
                             "dispGuidePage"})
        ints[zero] = 0;
    for (const char* minus : {"QuickTuneFrontTireType", "QuickTuneRearTireType", "playerGridNumber", "SeriesRank", "PsValue", "TorqueValue",
                              "driveTrainType", "carCategory"})
        ints[minus] = -1;
    for (const char* none : {"goldTime", "silverTime", "bronzeTime"}) ints[none] = kNoTime;
}

s32 QuickWork::get(const std::string& name) const {
    const auto it = ints.find(name);
    return it == ints.end() ? 0 : it->second;
}

std::string QuickWork::take_command() {
    std::string c = command;
    command = "COMMAND_NOTHING";
    return c;
}

QuickWork& quick_work(GameState& game) {
    if (!game.quick_work) game.quick_work = std::make_shared<QuickWork>();
    return *game.quick_work;
}

std::string quick_project(const std::string& sequence) {
    static const std::map<std::string, std::string> by_sequence = {
        {"single_race", "quick-arcade"},       {"time_trial", "quick-tt"},       {"solo_event", "quick-event"}, {"championship", "quick-championship"},
        {"machinetest", "quick-mt"},           {"freerun", "quick-freerun"},     {"practice", "quick-practice"}, {"license", "quick-license"},
        {"mission", "quick-mission"},          {"photo_drive", "quick-freerun"},
        {"license_race", "quick"}, // (Prologue's license tests: its project "quick", LicenseRoot)
    };
    const auto it = by_sequence.find(sequence);
    return it == by_sequence.end() ? std::string() : it->second;
}

void install_race_natives(Runtime& rt, script::Module& menu) {
    auto cls = std::make_shared<script::Module>();
    cls->name = "MQuickWork", cls->is_class = true;
    Runtime* r = &rt;
    cls->construct = [r](Vm&, Args) {
        // Every script object reads and writes the one quick work of the game.
        auto o = std::make_shared<Object>("MQuickWork");
        auto work = [r]() -> QuickWork& { return quick_work(r->game()); };
        for (const auto& [name, v] : QuickWork().ints) {
            const std::string n = name;
            const bool times = n == "goldTime" || n == "silverTime" || n == "bronzeTime";
            if (times) {
                o->property(n, [work, n] { return text(quick_time_text(work().get(n))); });
                continue;
            }
            o->property(n, [work, n] { return number(work().get(n)); }, [work, n](const Value& v) {
                work().set(n, v.kind == Kind::Int ? v.i : v.kind == Kind::Float ? s32(v.f) : s32(v.truthy()));
            });
        }
        // selectedCommand: a command's name (an unknown one is COMMAND_NOTHING).
        o->property("selectedCommand", [work] { return text(work().command); }, [work](const Value& v) {
            const std::string s = v.is_nil() ? std::string() : v.to_string();
            const std::vector<std::string>& names = QuickWork::command_names();
            work().command = std::find(names.begin(), names.end(), s) != names.end() ? s : names[0];
        });
        o->property("raceLabel", [work] { return text(work().race_label); }, [work](const Value& v) { work().race_label = v.to_string().substr(0, 0x3F); });
        o->property("courseLabel", [work] { return text(work().course_label); });
        auto row = [work](Args a) -> const QuickRow* {
            const s32 i = arg_int(a, 0, -1);
            const QuickWork& w = work();
            return i >= 0 && size_t(i) < w.rows.size() ? &w.rows[size_t(i)] : nullptr;
        };
        o->method("getGridCarName", [r, row](Vm& vm, const Value&, Args a) {
            const QuickRow* q = row(a);
            if (!q || q->car.empty()) return text("");
            return text(vm.attribute(make_car_object(*r, q->car, q->paint), "name").to_string());
        });
        o->property("licenseCarName", [r, work]() {
            const QuickWork& w = work();
            return text(w.rows.empty() ? std::string() : w.rows[0].car);
            (void)r;
        });
        // getColorChipInfo(i): the paint's chip words; ❓ only the first (the paint's colour,
        // 0x00BBGGRR) is filled.
        o->method("getColorChipInfo", [r, row](Vm& vm, const Value&, Args a) {
            const QuickRow* q = row(a);
            std::vector<Value> words(4, number(0));
            if (q && !q->car.empty()) {
                const Value car = make_car_object(*r, q->car, q->paint);
                const Value colors = vm.call(vm.attribute(car, "getColors"), {});
                if (const std::vector<Value>* list = colors.items(); list && q->paint >= 0 && size_t(q->paint) < list->size()) {
                    const Value& c = (*list)[size_t(q->paint)];
                    auto byte = [&](const char* k) { return u32(std::clamp(vm.attribute(c, k).as_float(), 0.0f, 1.0f) * 255.0f + 0.5f); };
                    words[0] = number(s32(byte("r") | byte("g") << 8 | byte("b") << 16));
                }
            }
            return Value::array(std::move(words));
        });
        // The catalog's power (PS) and weight of a row's car when the sequence gave none.
        // The power (PS, whole) and weight of a row's car as the race builds it, when the sequence
        // gave none.
        o->method("getPower", [r, row](Vm&, const Value&, Args a) {
            const QuickRow* q = row(a);
            if (!q) return number(0);
            if (q->power >= 0 || q->car.empty()) return number(std::max(0, q->power));
            return number(row_figures(*r, q->car).power);
        });
        o->method("getWeight", [r, row](Vm&, const Value&, Args a) {
            const QuickRow* q = row(a);
            if (!q) return number(0);
            if (q->weight >= 0 || q->car.empty()) return number(std::max(0, q->weight));
            return number(row_figures(*r, q->car).weight);
        });
        o->method("getTireType", [row](Vm&, const Value&, Args a) { const QuickRow* q = row(a); return number(q ? q->tire : 0); });
        o->method("getGridTime", [row](Vm&, const Value&, Args a) { const QuickRow* q = row(a); return number(q ? s32(q->time_ms) : QuickWork::kNoTime); });
        // getCatPs(format), getCatTq(format): the player's car's catalog line.
        for (const char* m : {"getCatPs", "getCatTq"}) {
            const std::string method = m;
            o->method(method, [r, work, method](Vm& vm, const Value&, Args a) {
                const QuickWork& w = work();
                if (w.player_car.empty()) return text("----");
                return vm.call(vm.attribute(make_car_object(*r, w.player_car, 0), method), std::vector<Value>(a.begin(), a.end()));
            });
        }
        return object_value(o);
    };
    menu.define("MQuickWork", Value::of(Kind::Module, cls));
}

} // namespace gt4r::menu
