// What Gran Turismo 4 Prologue's scripts ask of the program that Gran Turismo 4's do not: the
// game's license results (main::game.license), read by its school's pages and its presents
// (config/prologue/present.adc, projects/prologue). docs/formats/prologue.md "The menus".
//
// A unit per license test, by its number (0 ..) or its race code ("l0p0001": the number the
// scripts' own main::prologue::getLicenseID gives it; another code has none, nil). The main
// script writes each unit's category. The results of a game not yet played: no test passed, no
// time, no rank, no category passed, no points; a run's result is kept for the session (its race's end
// records it: Runtime::race_finished). ❓ The save of them.

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/runtime_internal.h"

#include "disc/core_image.h"
#include "gt4r/sim/program_prologue.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

namespace {

// A test's record (the program's license unit, 0x2E8 bytes, 48 of them from the license object's +8; the unit's natives
// 0x1582C0): +0 its category, +4 its medal (0 none, 1 bronze, 2 silver, 3 gold: the names at 0x505008; a result keeps
// the best, 0x3FE1A0), +8 its points (the best kept, 0x3FE430), from +0x20 ten ranking entries of 0x48 bytes in time
// order (+0x20 the time, ms; 359,999,999 none; 0x3FE3B0 finds a time's place).
struct LicenseUnit {
    int medal = 0;
    s32 points = 0;
    std::vector<u32> times; // at most 10, fastest first
};
constexpr u32 kNoTimeMs = 359'999'999;
constexpr const char* kMedals[4] = {"none", "bronze", "silver", "gold"};

} // namespace

void install_prologue_license(Object& game) {
    if (game.fields.count("license")) return;
    auto license = std::make_shared<Object>("MPrologueLicense");
    auto units = std::make_shared<std::map<int, Value>>();
    auto records = std::make_shared<std::map<int, LicenseUnit>>();
    auto unit = [units, records](int i) {
        Value& u = (*units)[i];
        if (u.kind == script::Kind::Nil) {
            auto o = std::make_shared<Object>("MPrologueLicenseUnit");
            o->set("category", Value::string(""));
            o->method("isPassed", [records, i](Vm&, const Value&, Args) { return Value::integer((*records)[i].medal > 0 ? 1 : 0); });
            o->method("isCompletePassed", [records, i](Vm&, const Value&, Args) { return Value::integer((*records)[i].medal == 3 ? 1 : 0); });
            o->method("getBestTime", [records, i](Vm&, const Value&, Args) {
                const LicenseUnit& r = (*records)[i];
                return Value::integer(s32(r.times.empty() ? kNoTimeMs : r.times.front()));
            });
            o->method("getRankNum", [records, i](Vm&, const Value&, Args) { return Value::integer(s32((*records)[i].times.size())); });
            o->method("getPoint", [records, i](Vm&, const Value&, Args) { return Value::integer((*records)[i].points); });
            o->method("getPassCode", [](Vm&, const Value&, Args) { return Value::string(""); });
            u = object_value(o);
        }
        return u;
    };
    // A unit by its number or its race code (the scripts' prologue::getLicenseID); -1 for another.
    auto unit_number = [](Vm& vm, const Value& v) -> int {
        if (v.kind == script::Kind::Int) return v.i >= 0 ? int(v.i) : -1;
        if (v.kind != script::Kind::String || v.as_string().empty()) return -1;
        const Value find = vm.lookup("prologue::getLicenseID");
        if (find.kind != script::Kind::Function) return -1;
        const Value id = vm.call(find, Args(&v, 1));
        return id.kind == script::Kind::Int && id.i >= 0 ? int(id.i) : -1;
    };
    license->method("getUnit", [unit, unit_number](Vm& vm, const Value&, Args a) -> Value {
        if (a.empty()) return {};
        const int n = unit_number(vm, a[0]);
        return n >= 0 ? unit(n) : Value{};
    });
    // A unit's category as the main script wrote it.
    auto category = [units](int i) -> std::string {
        const auto it = units->find(i);
        if (it == units->end() || !it->second.p) return "";
        const Value c = it->second.as<Object>()->get("category");
        return c.kind == script::Kind::String ? c.as_string() : "";
    };
    // 0x3FF000 / 0x3FF080: every test of the category passed (a medal) / every one gold.
    auto every = [units, records, category](const std::string& name, int least) {
        for (const auto& [i, u] : *units)
            if (category(i) == name && (*records)[i].medal < least) return 0;
        return 1;
    };
    license->method("isPassedCategory", [every](Vm&, const Value&, Args a) {
        return Value::integer(!a.empty() && a[0].kind == script::Kind::String ? every(a[0].as_string(), 1) : 0);
    });
    license->method("isCompletePassedCategory", [every](Vm&, const Value&, Args a) {
        return Value::integer(!a.empty() && a[0].kind == script::Kind::String ? every(a[0].as_string(), 3) : 0);
    });
    // 0x3FEF78: the tests with the medal of that name; 0x3FEFA8: the points of every test.
    license->method("getDetailCount", [records](Vm&, const Value&, Args a) {
        int medal = -1;
        for (int k = 0; k < 4; ++k)
            if (!a.empty() && a[0].kind == script::Kind::String && a[0].as_string() == kMedals[k]) medal = k;
        int n = 0;
        for (const auto& [i, r] : *records) n += r.medal == medal;
        return Value::integer(n);
    });
    license->method("getTotalPoint", [records](Vm&, const Value&, Args) {
        s32 n = 0;
        for (const auto& [i, r] : *records) n += r.points;
        return Value::integer(n);
    });
    license->method("forcePassedAll", [](Vm&, const Value&, Args) { return Value{}; });
    // A run's result (the license race's end, 0x19ED58): (race code, result name, time ms, points): the medal kept if
    // better, the points if more, the time into the ranking.
    license->method("recordResult", [records, unit, unit_number](Vm& vm, const Value&, Args a) -> Value {
        if (a.size() < 4) return {};
        const int n = unit_number(vm, a[0]);
        if (n < 0) return {};
        unit(n);
        LicenseUnit& r = (*records)[n];
        const std::string name = a[1].kind == script::Kind::String ? a[1].as_string() : "";
        for (int k = 1; k < 4; ++k)
            if (name == kMedals[k] && r.medal < k) r.medal = k;
        if (a[3].kind == script::Kind::Int && s32(a[3].i) > r.points) r.points = s32(a[3].i);
        if (a[2].kind == script::Kind::Int && a[2].i > 0 && u32(a[2].i) < kNoTimeMs) {
            r.times.push_back(u32(a[2].i));
            std::sort(r.times.begin(), r.times.end());
            if (r.times.size() > 10) r.times.resize(10);
        }
        return Value::integer(1);
    });
    game.set("license", object_value(license));
}

s32 prologue_license_points(ByteSpan row, const std::string& result, u32 t) {
    if (row.size() < 0x7C) return 0;
    auto target = [&row](size_t k) {
        const u32 ms = u32(row[0x76 + 2 * k]) * 60000u + u32(row[0x77 + 2 * k]) * 1000u + u32(row[0x1A + 2 * k] | row[0x1B + 2 * k] << 8);
        return ms ? ms : kNoTimeMs;
    };
    const int r = result == "gold" ? 2 : result == "silver" ? 3 : result == "bronze" ? 4 : 0;
    if (r == 0) return 0;
    const u32 g = target(0), sv = target(1), b = target(2);
    // 0x19F410: by the test's kind (Prologue's row +0x22, Gran Turismo 4's columns +0x3E) and its byte +0x5B (+0x73).
    int kind = row[0x3E] >= 4 ? 1 : 0;
    if (row[0x73] > 0 && b == kNoTimeMs) kind = 2;
    if (b == kNoTimeMs && kind == 0) kind = 2;
    auto ratio = [](u32 scale, u32 above, u32 span) { return span ? s32((scale * above) / span) : 0; };
    if (kind == 1) return r == 2 ? 100 : r == 3 ? 50 : 10; // 0x19F620
    if (kind == 0) { // 0x19F508
        if (r == 2) return ratio(100, sv - t, sv - g);
        if (r == 3) return ratio(50, b - t, b - sv);
        return 10;
    }
    if (r == 2) return ratio(100, sv - t, sv - g); // 0x19F670
    return r == 3 ? 50 : 10;
}

// ---- The switch actor's colours (MSwitchActor's update, 0x2EB848): its button's `base`,
// `text` and `ring` take a colour by the button's state each picture. The colours are the
// program's own, made by its startup (0x2EBC48: each a colour object at 0x70C218 + 0x10 n built from
// four floats); they are read here by following that routine's constant loads, not kept.
// docs/formats/prologue.md "The menus".

namespace {

constexpr u32 kColorsMaker = 0x2EBC48, kColorsMakerEnd = 0x2EC600; // the routine that makes them
constexpr u32 kColorConstructor = 0x1D74B0;                       // (r, g, b, a) into the object at $a0

// The floats each colour object gets, by its address: the routine's lui / ori / addiu / mtc1 /
// mov.s followed in order, a constructor's call recorded with its delay slot done.
std::map<u32, std::array<float, 4>> colors_made(ByteSpan image, u32 load) {
    std::map<u32, std::array<float, 4>> out;
    if (kColorsMaker < load || kColorsMakerEnd - load > image.size()) return out;
    std::array<std::optional<u32>, 32> gpr{};
    std::array<std::optional<float>, 32> fpr{};
    gpr[0] = 0;
    auto word = [&](u32 at) {
        u32 w;
        std::memcpy(&w, image.data() + (at - load), 4);
        return w;
    };
    auto as_float = [](u32 bits) {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto step = [&](u32 w) {
        const u32 op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
        const u16 imm = u16(w);
        if (op == 0x0F) gpr[rt] = u32(imm) << 16;                                     // lui
        else if (op == 0x0D) gpr[rt] = gpr[rs] ? std::optional<u32>(*gpr[rs] | imm) : std::nullopt; // ori
        else if (op == 0x09) gpr[rt] = gpr[rs] ? std::optional<u32>(*gpr[rs] + u32(s32(s16(imm)))) : std::nullopt; // addiu
        else if (op == 0x11 && rs == 4) fpr[(w >> 11) & 31] = gpr[rt] ? std::optional<float>(as_float(*gpr[rt])) : std::nullopt; // mtc1
        else if (op == 0x11 && rs == 16 && (w & 63) == 6) fpr[(w >> 6) & 31] = fpr[(w >> 11) & 31];                 // mov.s
        else if (op == 0) gpr[(w >> 11) & 31].reset();                   // (a register operation: its result unknown)
        else if ((op >= 0x18 && op <= 0x27) || op == 0x37) gpr[rt].reset(); // (loads)
        gpr[0] = 0;
    };
    for (u32 at = kColorsMaker; at + 8 <= kColorsMakerEnd; at += 4) {
        const u32 w = word(at);
        if ((w >> 26) == 0x03 && ((w & 0x3FFFFFF) << 2) == kColorConstructor) {
            step(word(at + 4)); // the delay slot
            at += 4;
            if (gpr[4] && fpr[12] && fpr[13] && fpr[14] && fpr[15]) out[*gpr[4]] = {*fpr[12], *fpr[13], *fpr[14], *fpr[15]};
            continue;
        }
        step(w);
    }
    return out;
}

} // namespace

std::optional<SwitchColors> prologue_switch_colors(const RuntimeConfig& config) {
    if (!config.program) return std::nullopt;
    try {
        const gt3r::CoreImage core = gt3r::CoreImage::decode(config.program());
        if (!sim::PrologueProgram::locate(core.program, core.load_address).recognized()) return std::nullopt;
        const std::map<u32, std::array<float, 4>> made = colors_made(core.program, core.load_address);
        // The update's choices (0x2EB848): by the button's active (1) and insensitive (8) bits;
        // the ring's blink while it has the focus, and its colours without it.
        SwitchColors c;
        auto at = [&made](u32 address, std::array<float, 4>& into) {
            const auto it = made.find(address);
            if (it == made.end()) return false;
            into = it->second;
            return true;
        };
        const bool all = at(0x70C238, c.base_active) && at(0x70C248, c.base_idle) && at(0x70C278, c.base_insensitive_active) &&
                         at(0x70C288, c.base_insensitive) && at(0x70C258, c.text_active) && at(0x70C268, c.text_idle) &&
                         at(0x70C298, c.text_insensitive_active) && at(0x70C2A8, c.text_insensitive) && at(0x70C238, c.ring_blink_on) &&
                         at(0x70C228, c.ring_blink_off) && at(0x70C288, c.ring_insensitive) && at(0x70C248, c.ring_idle);
        if (!all) return std::nullopt;
        return c;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace gt4r::menu
