// The save editor's garage: cars made, tuned and set up by the game's own natives (MGarage, MCarGarage) and the tuning shop's
// own script (which parts go together), on the saved game's records.

#include "gt4r/menu/save_editor.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/sim/showroom.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gt4r::menu {

using script::Kind;
using script::Value;

namespace {

// The parts types the pages list, in the settings page's order, with the page's keys of their names (0 the tuning shop's
// permanent work).
struct TypeRow {
    int type;
    const char* key;
    bool permanent;
};
const TypeRow kTypeRows[] = {
    {25, "Front tires", false}, {26, "Rear tires", false}, {20, "Muffler", false}, {18, "ComputerType", false}, {13, "NATune", false},
    {14, "TurbineKit", false}, {19, "InterCooler", false}, {28, "SuperCharger", false}, {27, "NOSType", false},
    {1, "Brake", false}, {2, "BrakeController", false}, {3, "Suspension", false},
    {11, "Gear", false}, {21, "Clutch", false}, {22, "FlyWheel", false}, {23, "PropellerShaftType", false}, {24, "LSD", false}, {10, "DriveTrain", false},
    {8, "Weight reduction", true}, {15, "PORTPOLISH", true}, {16, "ENGINEBALANCE", true}, {17, "DISPLACEMENT", true}, {31, "STIFFNESS", true},
};

s32 int_of(const Value& v) { return v.kind == Kind::Int ? v.i : v.is_number() ? s32(v.as_float()) : 0; }

std::vector<s32> ints(const Value& v) {
    std::vector<s32> out;
    if (const std::vector<Value>* items = v.items())
        for (const Value& x : *items) out.push_back(int_of(x));
    return out;
}

} // namespace

void SaveEditor::read_cars() {
    cars_.clear(), car_index_.clear();
    Database& d = natives().db();
    for (const db::CarEntry& c : d.cars) {
        CarKind k;
        k.label = c.label, k.name = c.name.empty() ? c.label : c.name;
        for (char& ch : k.name)
            if (ch == '`') ch = '\'';
        const ByteSpan row = d.car_row(c.label);
        if (row.size() > 0x10) {
            k.price = u32(row[8]) | u32(row[9]) << 8 | u32(row[10]) << 16 | u32(row[11]) << 24;
            k.year = s16(u16(row[0x0C] | row[0x0D] << 8));
            const auto it = d.names.tuners.find(row[0x10]);
            if (it != d.names.tuners.end()) {
                k.maker = text("NationRoot", "TunerName", it->second);
                if (k.maker.empty()) k.maker = it->second;
            }
        }
        car_index_[k.label] = cars_.size();
        cars_.push_back(std::move(k));
    }
}

void SaveEditor::read_wheels() {
    wheels_.clear(), wings_.clear();
    Database& d = natives().db();
    if (const db::Labels* labels = d.db ? d.db->labels("WHEEL") : nullptr)
        for (const auto& [l, id] : labels->entries) {
            const std::string name = text("TopRoot", "WheelName", l);
            if (name != l && !name.empty()) wheels_.push_back({l, name});
        }
    std::sort(wheels_.begin(), wheels_.end(), [](const Extra& a, const Extra& b) { return a.name < b.name; });
    if (const db::Labels* labels = d.db ? d.db->labels("WING") : nullptr)
        for (const auto& [l, id] : labels->entries) {
            const std::string name = text("WingShopRoot", "", l);
            if (name != l && !name.empty()) wings_.push_back({l, name});
        }
    std::sort(wings_.begin(), wings_.end(), [](const Extra& a, const Extra& b) { return a.label < b.label; });
}

std::string SaveEditor::car_name(const std::string& label) const {
    const auto it = car_index_.find(label);
    return it == car_index_.end() ? label : cars_[it->second].name;
}

std::vector<SaveEditor::Paint> SaveEditor::paints(const std::string& label) {
    std::vector<Paint> out;
    Database& d = natives().db();
    const auto& all = d.spec(label).paints;
    // getColorNum: the last one is left out when the car's row hides it.
    const ByteSpan row = d.car_row(label);
    size_t n = all.size();
    if (n > 1 && row.size() > 0x12 && (row[0x12] & 2)) --n;
    for (size_t i = 0; i < n; ++i) out.push_back({all[i].name, {all[i].rgb[0], all[i].rgb[1], all[i].rgb[2]}});
    return out;
}

int SaveEditor::add_car(const std::string& label, int paint) {
    if (state().garage_count() >= int(GameState::kGarageSize)) return -1;
    const int n = std::max(1, int(paints(label).size()));
    const Value r = method(rt_->vm().attribute(game(), "garage"), "addNewCar", {Value::string(label), Value::integer(std::clamp(paint, 0, n - 1))});
    const int slot = int_of(r);
    if (slot >= 0) method(game(), "setCarAvailable", {Value::string(label)});
    return slot;
}

void SaveEditor::remove_car(int slot) { method(rt_->vm().attribute(game(), "garage"), "delCar", {Value::integer(slot)}); }

void SaveEditor::set_active(int slot) {
    method(rt_->vm().attribute(game(), "garage"), "changeRidingCar", {Value::integer(slot)});
    refresh(slot);
}

// The record of a car: one never driven has none (it is the car as sold in its paint with its odometer); a change of what only
// a record holds makes it, as driving the car would.
CarRecord* SaveEditor::record(int slot, bool make) {
    if (slot < 0 || size_t(slot) >= state().garage.size() || !state().garage[size_t(slot)].used) return nullptr;
    GarageEntry& e = state().garage[size_t(slot)];
    if (!e.record && make) e.record = car_from_entry(natives().db(), state(), slot, false)->own;
    return e.record ? &*e.record : nullptr;
}

void SaveEditor::refresh(int slot) {
    if (slot < 0 || size_t(slot) >= state().garage.size()) return;
    entry_from_record(natives().db(), state().garage[size_t(slot)]);
}

script::Value SaveEditor::car(int slot) {
    if (!record(slot, true)) return {};
    return make_car_value(natives(), car_from_entry(natives().db(), state(), slot, true));
}

SaveEditor::Figures SaveEditor::figures(int slot) {
    const GarageEntry& e = state().garage[size_t(slot)];
    const GameState::Unit unit = state().power_unit();
    Figures f;
    f.hp = int(std::lround(double(e.power) / 100.0 * double(unit.factor))), f.kg = int(e.weight), f.power_unit = unit.name;
    return f;
}

int SaveEditor::car_paint(int slot) {
    const GarageEntry& e = state().garage[size_t(slot)];
    return e.record ? int(e.record->sheet(0)[0x18]) : e.paint;
}

void SaveEditor::set_car_paint(int slot, int paint) {
    GarageEntry& e = state().garage[size_t(slot)];
    const int n = std::max(1, int(paints(e.car).size()));
    paint = ((paint % n) + n) % n;
    e.paint = paint;
    if (e.record)
        for (int s = 0; s < 3; ++s) e.record->sheet(s)[0x18] = u8(paint);
}

// (The odometer's native divides the meters by 160 for tenths of a mile.)
const char* SaveEditor::distance_unit() const { return state().option_text("unit_velocity") == "MPH" ? "miles" : "km"; }
static double meters_per_unit(const GameState& s) { return s.option_text("unit_velocity") == "MPH" ? 1600.0 : 1000.0; }

u32 SaveEditor::max_odometer() const { return u32(100'000'000.0 / meters_per_unit(state())); }

u32 SaveEditor::odometer(int slot) {
    const GarageEntry& e = state().garage[size_t(slot)];
    return u32(double(e.record ? e.record->odometer() : e.odometer) / meters_per_unit(state()));
}

void SaveEditor::set_odometer(int slot, u32 units) {
    GarageEntry& e = state().garage[size_t(slot)];
    const u32 m = u32(std::min(100'000'000.0, double(units) * meters_per_unit(state())));
    if (!e.record) e.odometer = m;
    else e.record->set_odometer(m), refresh(slot);
}

void SaveEditor::set_got_on_day(int slot, int day) { state().garage[size_t(slot)].day = GameState::kFirstDay + u32(std::clamp(day, 0, days())); }

bool SaveEditor::no_wear(int slot) {
    if (const CarRecord* r = record(slot, false)) return r->no_wear();
    const ByteSpan row = natives().db().car_row(state().garage[size_t(slot)].car);
    return row.size() > 0x11 && row[0x11] >= 100;
}

namespace {
// What a car with no record has (a new one's: made_used_condition of its odometer).
CarRecord as_listed(Database& d, GameState& s, int slot) { return car_from_entry(d, s, slot, false)->record(); }
} // namespace

SaveEditor::Engine SaveEditor::engine(int slot) {
    const CarRecord r = as_listed(natives().db(), state(), slot);
    const sim::ConditionLimits l = sim::condition_limits(natives().db().program);
    const float e = r.condition(1), fresh = l.new_engine.host(), worn = l.worn.host();
    if (e < fresh / 2) return kNewEngine;
    return e > worn / 2 ? kWorn : kRunIn;
}

void SaveEditor::set_engine(int slot, Engine e) {
    CarRecord* r = record(slot, true);
    if (!r || r->no_wear()) return;
    const sim::ConditionLimits l = sim::condition_limits(natives().db().program);
    r->set_condition(1, e == kNewEngine ? l.new_engine.host() : e == kRunIn ? 0.0f : l.worn.host());
    refresh(slot);
}

// The oil as getOilLifeRate reads it: below 0 fresh oil's run-in (from -300,000 m), then up to 6,000,000 m.
int SaveEditor::oil_used(int slot) {
    const CarRecord r = as_listed(natives().db(), state(), slot);
    const float o = r.condition(2);
    const sim::ConditionLimits l = sim::condition_limits(natives().db().program);
    return std::clamp(int(std::lround((o >= 0 ? o : 0.0f) / l.old_oil.host() * 100.0f)), 0, 100);
}

void SaveEditor::set_oil_used(int slot, int percent) {
    CarRecord* r = record(slot, true);
    if (!r || r->no_wear()) return;
    const sim::ConditionLimits l = sim::condition_limits(natives().db().program);
    percent = std::clamp(percent, 0, 100);
    if (percent == 0) method(car(slot), "replaceOil");
    else r->set_condition(2, l.old_oil.host() * float(percent) / 100.0f);
    refresh(slot);
}

int SaveEditor::dirt(int slot) { return std::clamp(int(std::lround(as_listed(natives().db(), state(), slot).condition(3) / 0.8f * 100.0f)), 0, 100); }

void SaveEditor::set_dirt(int slot, int percent) {
    if (CarRecord* r = record(slot, true)) r->set_condition(3, 0.8f * float(std::clamp(percent, 0, 100)) / 100.0f);
}

int SaveEditor::body_worn(int slot) {
    const CarRecord r = as_listed(natives().db(), state(), slot);
    return std::clamp(int(std::lround((r.condition(4) - 500'000.0f) / 2'500'000.0f * 100.0f)), 0, 100);
}

void SaveEditor::set_body_worn(int slot, int percent) {
    CarRecord* r = record(slot, true);
    if (!r || r->no_wear()) return;
    percent = std::clamp(percent, 0, 100);
    if (percent == 0) method(car(slot), "refreshBody");
    else r->set_condition(4, 500'000.0f + 2'500'000.0f * float(percent) / 100.0f);
}

int SaveEditor::sheet(int slot) {
    const CarRecord* r = record(slot, false);
    return r ? r->active_sheet() : 0;
}

void SaveEditor::set_sheet(int slot, int s) {
    if (CarRecord* r = record(slot, true)) r->set_active_sheet(std::clamp(s, 0, 2)), refresh(slot);
}

std::vector<SaveEditor::PartKind> SaveEditor::parts(int slot) {
    std::vector<PartKind> out;
    // Read from a copy: looking does not make a record.
    const std::shared_ptr<CarHandle> handle = car_from_entry(natives().db(), state(), slot, false);
    const Value c = make_car_value(natives(), handle);
    method(c, "loadPartsInfo");
    CarRecord stock;
    const bool known = set_car(natives().db(), stock, state().garage[size_t(slot)].car, 0);
    auto stock_handle = std::make_shared<CarHandle>();
    stock_handle->state = &state(), stock_handle->label = handle->label, stock_handle->own = stock;
    const Value sold = make_car_value(natives(), stock_handle);
    for (const TypeRow& row : kTypeRows) {
        const Value type = Value::integer(row.type);
        PartKind k;
        k.type = row.type, k.permanent = row.permanent;
        k.name = std::strchr(row.key, ' ') ? std::string(row.key) : setting_label("SettingRoot", row.key);
        const int now = int_of(method(c, "getEquipedParts", {type})), as_sold = known ? int_of(method(sold, "getEquipedParts", {type})) : -1;
        const int n = int_of(method(c, "getPartsTypeMax", {type}));
        const bool removable = method(c, "isRemovableType", {type}).truthy(), normal = method(c, "isRemovedNormalType", {type}).truthy();
        for (int cat = 0; cat < n; ++cat) {
            PartOption o;
            o.category = cat;
            const bool exists = row.type == 31 ? cat == 1 : method(c, "isExistParts", {type, Value::integer(cat)}).truthy();
            const bool none = cat == 0 && (removable || row.permanent) && !exists;
            if (!exists && !none) continue;
            if (none && row.permanent && as_sold > 0) continue; // (work the car comes with is not undone)
            if (none && row.type == 20 && !method(c, "isDefaultNoParts", {type}).truthy()) continue; // (the settings page's own rule)
            if (row.permanent && cat > 0 && cat < as_sold) continue;
            if (none) o.name = row.permanent ? "Not done" : normal ? setting_label("SettingRoot", "normal") : setting_label("SettingRoot", "none");
            else {
                const Value key = call(lookup("GTmodeProject::getPartsName"), {type, Value::integer(cat)});
                const std::string s = key.kind == Kind::String ? key.as_string() : std::string();
                o.name = s.empty() ? (normal ? setting_label("SettingRoot", "normal") : cat == 0 ? setting_label("SettingRoot", "normal") : "Stage " + std::to_string(cat)) : setting_label("SettingRoot", s);
                if (o.name == "--") o.name = cat == 11 ? setting_label("SettingRoot", "dirt_tire") : setting_label("SettingRoot", "snow_tire");
                if ((row.type == 25 || row.type == 26) && cat < 11)
                    o.name = setting_label("SettingRoot", cat < 3 ? "normal_tire" : cat < 6 ? "sports_tire" : "racing_tire") + ": " + o.name;
                if (row.permanent && (row.type == 15 || row.type == 16 || row.type == 17 || row.type == 31)) o.name = "Done";
                if (o.name == "Equip") o.name = "Fitted";
            }
            o.owned = none || (cat == 0 && !removable && !row.permanent && int_of(method(c, "isHavingParts", {type, Value::integer(cat)})) == 0 && as_sold == 0) ||
                      method(c, "isHavingParts", {type, Value::integer(cat)}).truthy();
            o.fitted = now == cat || (none && now <= 0);
            if (o.fitted) k.fitted = int(k.options.size());
            k.options.push_back(std::move(o));
        }
        if (k.options.size() > 1) out.push_back(std::move(k));
    }
    return out;
}

void SaveEditor::fit_part(int slot, int type, int category) {
    const Value c = car(slot);
    if (c.is_nil()) return;
    method(c, "loadPartsInfo");
    CarRecord& r = *record(slot, true);
    const Value t = Value::integer(type);
    const bool permanent = type == 8 || type == 15 || type == 16 || type == 17 || type == 31;
    if (permanent) {
        // Work done in stages: the stage and the ones before it are had; undone, none is.
        const int n = int_of(method(c, "getPartsTypeMax", {t}));
        const int from = parts_owned_from(type, natives().db().program.release() == sim::Release::Japan);
        for (int k = 1; k < n; ++k) r.set_owned(from + k, k <= category);
        if (category > 0) method(c, "setParts", {t, Value::integer(category)});
        else
            for (int s = 0; s < 3; ++s) {
                if (type == 31) r.sheet(s)[0x168] = 0;
                else {
                    static constexpr u32 kSlots[4] = {0x88, 0x90, 0x98, 0};
                    const u32 at = type == 8 ? 0x78u : kSlots[type - 15];
                    std::memset(r.sheet(s) + at, 0xFF, 8);
                }
            }
        refresh(slot);
        return;
    }
    if (category > 0 || method(c, "isExistParts", {t, Value::integer(category)}).truthy()) method(c, "getParts", {t, Value::integer(category)});
    if (type == 25 || type == 26) {
        method(c, "getParts", {Value::integer(25), Value::integer(category)}), method(c, "getParts", {Value::integer(26), Value::integer(category)});
        method(c, "setParts", {t, Value::integer(category)});
        // Front and rear are of one kind on the settings page (road, sports, racing, dirt, snow).
        auto kind = [](int k) { return k < 3 ? 0 : k < 6 ? 1 : k < 11 ? 2 : k == 11 ? 3 : 4; };
        const Value other = Value::integer(type == 25 ? 26 : 25);
        if (kind(int_of(method(c, "getEquipedParts", {other}))) != kind(category)) method(c, "setParts", {other, Value::integer(category)});
        method(c, "syncWheel");
    } else {
        // What goes together, as the tuning shop has it (a turbine kit takes the other aspirations' parts off, ...).
        if (category > 0) call(lookup("GTmodeProject::TuningApplyRoot::set_parts_consistent"), {c, t, Value::integer(category)});
        else method(c, "setParts", {t, Value::integer(category)});
    }
    refresh(slot);
}

void SaveEditor::own_part(int slot, int type, int category, bool on) {
    const Value c = car(slot);
    if (c.is_nil()) return;
    // (the first owned bit of each type: 0x43F270's, by the release)
    const bool japan = natives().db().program.known() && natives().db().program.release() == sim::Release::Japan;
    auto owned_from = [japan](int t) { return parts_owned_from(t, japan); };
    if (type < 0 || type >= 32 || owned_from(type) < 0) return;
    const bool tires = type == 25 || type == 26;
    for (int t : tires ? std::vector<int>{25, 26} : std::vector<int>{type}) {
        if (!on && int_of(method(c, "getEquipedParts", {Value::integer(t)})) == category) return; // (fitted: it stays owned)
        for (int s = 0; s < 3; ++s)
            if (!on) {
                // (and one another sheet has fitted)
                CarRecord& r = *record(slot, true);
                const int was = r.active_sheet();
                r.set_active_sheet(s);
                const bool fitted = int_of(method(c, "getEquipedParts", {Value::integer(t)})) == category;
                r.set_active_sheet(was);
                if (fitted) return;
            }
    }
    for (int t : tires ? std::vector<int>{25, 26} : std::vector<int>{type}) record(slot, true)->set_owned(owned_from(t) + category, on);
}

void SaveEditor::own_all_parts(int slot) {
    const std::vector<PartKind> all = parts(slot);
    for (const PartKind& k : all) {
        if (k.permanent) continue;
        for (const PartOption& o : k.options)
            if (!o.owned) own_part(slot, k.type, o.category, true);
    }
}

std::vector<SaveEditor::Setting> SaveEditor::settings(int slot) {
    std::vector<Setting> out;
    const std::shared_ptr<CarHandle> handle = car_from_entry(natives().db(), state(), slot, false);
    const Value c = make_car_value(natives(), handle);
    method(c, "loadPartsInfo");
    auto fitted = [&](int type) { return int_of(method(c, "getEquipedParts", {Value::integer(type)})); };
    auto get = [&](const char* getter, int i) { return int_of(method(c, getter, {Value::integer(i)})); };
    auto label = [&](const char* page, const char* key, const char* end = "") { return setting_label(page, key) + end; };
    // (Grey on the page when there is nothing between the ends.)
    auto add = [&](const std::string& group, const std::string& name, const char* what, int index, s32 value, s32 lo, s32 hi, int decimals = 0, int offset = 0) {
        if (lo >= hi) return;
        Setting s;
        s.group = group, s.name = name, s.setter = what, s.index = index, s.value = std::clamp(value, lo, hi), s.lo = lo, s.hi = hi, s.decimals = decimals, s.offset = offset;
        out.push_back(std::move(s));
    };
    const std::string front = ", front", rear = ", rear";
    {
        const std::vector<s32> r = ints(method(c, "getSuspensionRange"));
        const std::string g = label("SettingRoot", "Suspension");
        if (r.size() >= 42) {
            const char* get_s = "getSuspensionSetting";
            add(g, label("SuspensionRoot", "spring rate") + front, "Suspension", 14, get(get_s, 14), r[12], r[13], 1);
            add(g, label("SuspensionRoot", "spring rate") + rear, "Suspension", 15, get(get_s, 15), r[14], r[15], 1);
            add(g, label("SuspensionRoot", "ride height") + front, "Suspension", 12, get(get_s, 12), r[0], r[1]);
            add(g, label("SuspensionRoot", "ride height") + rear, "Suspension", 13, get(get_s, 13), r[2], r[3]);
            auto level = [&](const std::string& name, const char* what, int index, s32 steps) {
                const s32 v = get(get_s, index);
                if (v > 0) add(g, name, what, index, v, 1, steps);
            };
            if (fitted(3) == 3) {
                // One level for both directions of a shock absorber.
                level(label("SuspensionRoot", "damper") + front, "Dampers", 0, r[16]);
                level(label("SuspensionRoot", "damper") + rear, "Dampers", 4, r[26]);
            } else {
                level(label("SuspensionRoot", "damper shrink") + front, "Damper", 0, r[16]);
                level(label("SuspensionRoot", "damper shrink") + rear, "Damper", 4, r[26]);
                level(label("SuspensionRoot", "damper expand") + front, "Damper", 2, r[21]);
                level(label("SuspensionRoot", "damper expand") + rear, "Damper", 6, r[31]);
            }
            add(g, label("SuspensionRoot", "camber") + front, "Suspension", 8, get(get_s, 8), r[4], r[5], 1);
            add(g, label("SuspensionRoot", "camber") + rear, "Suspension", 9, get(get_s, 9), r[6], r[7], 1);
            add(g, label("SuspensionRoot", "toe") + front, "Suspension", 10, get(get_s, 10), r[8], r[9], 0, -128);
            add(g, label("SuspensionRoot", "toe") + rear, "Suspension", 11, get(get_s, 11), r[10], r[11], 0, -128);
            level(label("SuspensionRoot", "stabilizer") + front, "Suspension", 16, r[36]);
            level(label("SuspensionRoot", "stabilizer") + rear, "Suspension", 17, r[39]);
        }
    }
    {
        const std::vector<s32> r = ints(method(c, "getBrakeControllerRange"));
        const std::string g = label("BrakeRoot", "brake");
        if (r.size() >= 6)
            for (int i = 0; i < 2; ++i)
                if (const s32 v = get("getBrakeControllerSetting", i); v > 0) add(g, label("BrakeRoot", "brake") + (i ? rear : front), "BrakeController", i, v, 1, r[size_t(3 * i)]);
    }
    {
        const std::vector<s32> r = ints(method(c, "getGearRange", {Value::integer(0)}));
        const std::string g = label("GearRoot", "Gear Ratio");
        if (r.size() >= 23 && r[2] < r[3]) {
            for (int i = 1; i <= std::min(r[4], 7); ++i) {
                // (A ratio set outside its page's ends is kept: the ends widen to it.)
                const s32 v = get("getGearSetting", i);
                add(g, label("GearRoot", ("gear" + std::to_string(i)).c_str()), "Gear", i, v, std::min(v, r[size_t(7 + 2 * i)]), std::max(v, r[size_t(8 + 2 * i)]), 3);
            }
            add(g, label("GearRoot", "final"), "Gear", 8, get("getGearSetting", 8), r[0], r[1], 3);
            add(g, label("GearRoot", "auto setting"), "GearAuto", 9, get("getGearSetting", 9), r[2], r[3], 0, 1 - r[2]);
        }
    }
    {
        const std::vector<s32> r = ints(method(c, "getLSDRange"));
        const std::string g = label("LSDRoot", "LSD");
        if (r.size() >= 12) {
            const bool ayc = fitted(24) == 5;
            add(g, label("LSDRoot", "initial torque") + front, "LSD", 0, get("getLSDSetting", 0), r[0], r[1]);
            if (ayc) add(g, label("AYCCRoot", "AYCC"), "LSD", 3, get("getLSDSetting", 3), r[6], r[7]);
            else add(g, label("LSDRoot", "initial torque") + rear, "LSD", 3, get("getLSDSetting", 3), r[6], r[7]);
            add(g, label("LSDRoot", "LSD accel") + front, "LSD", 1, get("getLSDSetting", 1), r[2], r[3]);
            add(g, label("LSDRoot", "LSD accel") + rear, "LSD", 4, get("getLSDSetting", 4), r[8], r[9]);
            add(g, label("LSDRoot", "LSD decel") + front, "LSD", 2, get("getLSDSetting", 2), r[4], r[5]);
            add(g, label("LSDRoot", "LSD decel") + rear, "LSD", 5, get("getLSDSetting", 5), r[10], r[11]);
        }
    }
    {
        const std::vector<s32> a = ints(method(c, "getASCCRange")), t = ints(method(c, "getTCSCRange"));
        const std::string g = label("AIDRoot", "DrivingAid");
        // The page's levels: ASM's word less one (none reads 0), TCS's counted from the other end.
        if (a.size() >= 9) {
            for (int i = 0; i < 2; ++i) {
                const s32 steps = a[size_t(6 * i)], v = std::max(1, get("getASCCSetting", i)) - 1;
                if (steps > 0) add(g, label("AIDRoot", i ? "ASM_VUC" : "ASM_VSC"), "ASCC", i, v, 0, steps - 1);
            }
        }
        if (!t.empty() && t[0] > 0) {
            const s32 v = get("getTCSCSetting", 0);
            add(g, label("AIDRoot", "TCSController"), "TCSC", 0, v ? t[0] - v + 1 : 0, 0, t[0]);
        }
    }
    {
        const std::vector<s32> r = ints(method(c, "getDriveTrainRange"));
        if (r.size() >= 2) add(label("DriveTrainRoot", "DriveTrain"), label("DriveTrainRoot", "VariableCenterDef"), "DriveTrain", 0, get("getDriveTrainSetting", 0), r[0], r[1]);
        const std::vector<s32> d = ints(method(c, "getRacingModifyRange"));
        const std::string g = label("AeroRoot", "aero");
        if (d.size() >= 4)
            for (int i = 0; i < 2; ++i) add(g, label("AeroRoot", "aero") + (i ? rear : front), "RacingModify", i, get("getRacingModifySetting", i), d[size_t(2 * i)], d[size_t(2 * i + 1)]);
        const std::vector<s32> n = ints(method(c, "getNOSRange"));
        if (n.size() >= 3) {
            // (The page's slider: from the least to the most of the row's three numbers.)
            const s32 v = get("getNOSSetting", 0);
            if (fitted(27) > 0) add(label("NosRoot", "NOS TITLE"), "Nitrous strength", "NOS", 0, v, n[1], n[2]);
        }
        const std::vector<s32> b = ints(method(c, "getBallastRange"));
        const std::string w = label("BallastRoot", "WeightBalance");
        if (b.size() >= 4) {
            add(w, label("BallastRoot", "Weight") + " (kg)", "Ballast", 0, get("getBallastSetting", 0), b[0], b[1]);
            add(w, label("BallastRoot", "Balance"), "Ballast", 1, get("getBallastSetting", 1), b[2], b[3]);
        }
    }
    return out;
}

void SaveEditor::set_setting(int slot, const Setting& s, s32 value) {
    const Value c = car(slot);
    if (c.is_nil()) return;
    method(c, "loadPartsInfo");
    value = std::clamp(value, s.lo, s.hi);
    auto set = [&](const std::string& setter, s32 index, s32 v) { method(c, setter, {Value::integer(index), Value::integer(v)}); };
    if (s.setter == "Dampers")
        for (int k = 0; k < 4; ++k) set("setSuspensionSetting", s.index + k, value);
    else if (s.setter == "Damper") set("setSuspensionSetting", s.index, value), set("setSuspensionSetting", s.index + 1, value);
    else if (s.setter == "GearAuto") set("setGearSetting", 9, value), method(c, "setGearAuto");
    else if (s.setter == "ASCC") set("setASCCSetting", s.index, value == 0 ? 0 : value + 1);
    else if (s.setter == "TCSC") {
        const std::vector<s32> t = ints(method(c, "getTCSCRange"));
        set("setTCSCSetting", 0, value && !t.empty() ? t[0] - value + 1 : 0);
    } else set("set" + s.setter + "Setting", s.index, value);
    refresh(slot);
}

std::string SaveEditor::setting_text(const Setting& s) const {
    const s32 v = s.value + s.offset;
    char b[32];
    if (s.decimals == 0) std::snprintf(b, sizeof b, "%d", int(v));
    else {
        int scale = 1;
        for (int i = 0; i < s.decimals; ++i) scale *= 10;
        std::snprintf(b, sizeof b, "%.*f", s.decimals, double(v) / scale);
    }
    return b;
}

bool SaveEditor::wheels_allowed(int slot) {
    const Value c = make_car_value(natives(), car_from_entry(natives().db(), state(), slot, false));
    return !method(c, "isChangeWheelDisabled").truthy();
}
bool SaveEditor::wing_allowed(int slot) {
    const Value c = make_car_value(natives(), car_from_entry(natives().db(), state(), slot, false));
    return !method(c, "isChangeWingDisabled").truthy();
}

std::string SaveEditor::wheel(int slot, int category) {
    const CarRecord* r = record(slot, false);
    if (!r) return {};
    const s64 id = r->wheel(category);
    return id == -1 ? std::string() : natives().db().label(u64(id));
}

void SaveEditor::set_wheel(int slot, int category, const std::string& label) {
    CarRecord* r = record(slot, true);
    if (!r) return;
    r->set_wheel(category, label.empty() ? -1 : s64(natives().db().code(kWheelTable, label)));
    method(car(slot), "syncWheel");
    refresh(slot);
}

std::string SaveEditor::wing(int slot) {
    const CarRecord* r = record(slot, false);
    if (!r) return {};
    u64 id;
    std::memcpy(&id, r->active() + 0x100, 8);
    return id == kNoCode ? std::string() : natives().db().label(id);
}

void SaveEditor::set_wing(int slot, const std::string& label) {
    const Value c = car(slot);
    if (c.is_nil()) return;
    if (label.empty()) method(c, "setWing", {Value{}});
    else method(c, "setWing", {Value::string(label)});
    refresh(slot);
}

} // namespace gt4r::menu
