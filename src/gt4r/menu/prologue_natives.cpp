// What Gran Turismo 4 Prologue's scripts ask of the program that Gran Turismo 4's do not: the
// game's license results (main::game.license), read by its school's pages and its presents
// (config/prologue/present.adc, projects/prologue). docs/formats/prologue.md "The menus".
//
// A unit per license test, by its number (0 ..) or its race code ("l0p0001": the number the
// scripts' own main::prologue::getLicenseID gives it; another code has none, nil). The main
// script writes each unit's category. The results of a game not yet played: no test passed, no
// time, no rank, no category passed, no points. ❓ A result kept when a test is run, and the save
// of them.

#include "gt4r/menu/game_internal.h"

#include <map>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

void install_prologue_license(Object& game) {
    if (game.fields.count("license")) return;
    auto license = std::make_shared<Object>("MPrologueLicense");
    auto units = std::make_shared<std::map<int, Value>>();
    auto unit = [units](int i) {
        Value& u = (*units)[i];
        if (u.kind == script::Kind::Nil) {
            auto o = std::make_shared<Object>("MPrologueLicenseUnit");
            o->set("category", Value::string(""));
            for (const char* none : {"isPassed", "getBestTime", "getRankNum"}) o->method(none, [](Vm&, const Value&, Args) { return Value::integer(0); });
            o->method("getPassCode", [](Vm&, const Value&, Args) { return Value::string(""); });
            u = object_value(o);
        }
        return u;
    };
    license->method("getUnit", [unit](Vm& vm, const Value&, Args a) -> Value {
        if (a.empty()) return {};
        if (a[0].kind == script::Kind::Int) return a[0].i >= 0 ? unit(a[0].i) : Value{};
        if (a[0].kind != script::Kind::String || a[0].as_string().empty()) return {};
        const Value find = vm.lookup("prologue::getLicenseID");
        if (find.kind != script::Kind::Function) return {};
        const Value id = vm.call(find, Args(&a[0], 1));
        return id.kind == script::Kind::Int && id.i >= 0 ? unit(id.i) : Value{};
    });
    for (const char* none : {"isPassedCategory", "isCompletePassedCategory", "getDetailCount", "getTotalPoint"})
        license->method(none, [](Vm&, const Value&, Args) { return Value::integer(0); });
    license->method("forcePassedAll", [](Vm&, const Value&, Args) { return Value{}; });
    game.set("license", object_value(license));
}

} // namespace gt4r::menu
