// The mods' cars on the makers' lists (formats/db/mod_cars.h): Gran Turismo mode's dealers sell the cars of the script
// module NewCarData (projects/gtmode/<zone>_carlist: `<maker>_carlist`, the labels in the order shown, and `<maker>_carnum`,
// how many), which the dealer page hands to BuyCarRoot (gtmode's DealerRoot: NewCarData[dealer + "_carlist"]); Arcade's
// makers' pages show the cars of the module CarData (projects/arcade/<zone>_carlist), alike. A mod car joins the end of its
// maker's list as the file is read: Arcade's always, a dealer's when it is for sale.
//
// The mods' makers (formats/db/mod_makers.h) join the menus as the game's own dealers are made: their numbers on the program's
// makers' and tuners' lists (the cars' maker and brand), their names in the text "TunerName" (the pages' and the app's maker
// name; "TunerDoc" the country page's line about the maker, its description), a list of their cars beside the others' (`<key>_carlist`, `<key>_carnum`: made for a maker with cars), and in
// GTmodeProject's NationData each on its country's page: NationRoot (gtmode.adc) lists the dealers of NationData
// `<nation>_dealers` that have a list, the nation the one whose `<nation>2` is the maker's country code, `<key>` its hall
// (as `<nation>_hall`) and DealerData `<key>_subtuner` none; DealerRoot shows its logo (piece/tuner_logo/<key>.img), the
// country page its button (image/piece/tuner_logo2/<key>.png): the mod's pictures (ModMakers::picture).

#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/formats/db/mod_makers.h"
#include "gt4r/menu/game_internal.h"

#include <algorithm>

namespace gt4r::menu {

using script::Kind;
using script::Module;
using script::Value;

namespace {

void add_to(GameNatives& n, const std::shared_ptr<Module>& lists, bool for_sale_only) {
    Database& d = n.db();
    if (!d.db) return;
    const db::ModCars& mods = d.db->mod_cars();
    // A mod maker's list where it has a car to go on it.
    for (const db::ModMakerDef& m : d.db->mod_makers().makers()) {
        if (lists->find(m.key + "_carlist")) continue;
        bool any = false;
        for (const std::string& label : mods.labels()) {
            const db::ModCarDef* def = mods.def(label);
            const ByteSpan row = d.car_row(label);
            any = any || (def && (!for_sale_only || def->for_sale) && row.size() > 0x0F && row[0x0F] == m.maker);
        }
        if (any) lists->define(m.key + "_carlist", Value::array({})), lists->define(m.key + "_carnum", Value::integer(0));
    }
    for (const std::string& label : mods.labels()) {
        const db::ModCarDef* def = mods.def(label);
        const ByteSpan row = d.car_row(label);
        if (!def || (for_sale_only && !def->for_sale) || row.size() < 0x18) continue;
        const auto maker = d.names.makers.find(row[0x0F]);
        const std::shared_ptr<script::Variable> list = maker == d.names.makers.end() ? nullptr : lists->find(maker->second + "_carlist");
        std::vector<Value>* items = list ? list->value.items() : nullptr;
        if (!items) {
            n.rt.note("mod car " + label + ": no list of its maker's cars in " + lists->path());
            continue;
        }
        bool there = false;
        for (const Value& v : *items) there = there || v.as_string() == label;
        if (there) continue;
        items->push_back(Value::string(label));
        if (const std::shared_ptr<script::Variable> count = lists->find(maker->second + "_carnum"); count && count->value.kind == Kind::Int)
            count->value = Value::integer(count->value.i + 1);
    }
}

// The mods' makers on their countries' pages of GTmodeProject's NationData (and DealerData), once each.
void add_mod_makers_to_nations(GameNatives& n) {
    Database& d = n.db();
    if (!d.db || d.db->mod_makers().empty()) return;
    script::Vm& vm = n.rt.vm();
    const std::shared_ptr<Module> nations = vm.lookup("GTmodeProject::NationData").as<Module>();
    const std::shared_ptr<Module> dealers = vm.lookup("GTmodeProject::DealerData").as<Module>();
    if (!nations) return;
    for (const db::ModMakerDef& m : d.db->mod_makers().makers()) {
        if (nations->find(m.key)) continue;
        std::string nation;
        for (const auto& [name, v] : nations->members)
            if (name.size() > 1 && name.back() == '2' && v && v->value.kind == Kind::String && v->value.as_string() == m.country && nations->find(name.substr(0, name.size() - 1) + "_dealers"))
                nation = name.substr(0, name.size() - 1);
        if (nation.empty()) {
            n.rt.note("mod maker " + m.key + ": no country page of " + m.country);
            continue;
        }
        const std::shared_ptr<script::Variable> list = nations->find(nation + "_dealers");
        if (std::vector<Value>* items = list->value.items()) items->push_back(Value::string(m.key));
        const std::shared_ptr<script::Variable> hall = nations->find(nation + "_hall");
        nations->define(m.key, hall ? hall->value : Value{});
        if (dealers && !dealers->find(m.key + "_subtuner")) dealers->define(m.key + "_subtuner", Value{});
    }
}

} // namespace

void use_mod_makers(Database& d) {
    if (!d.db) return;
    for (const db::ModMakerDef& m : d.db->mod_makers().makers()) {
        d.names.makers[m.maker] = m.key, d.names.tuners[m.tuner] = m.key;
        if (d.rt) d.rt->add_text("TunerName", m.key, m.name), d.rt->add_text("TunerDoc", m.key, m.description.empty() ? m.name : m.description);
    }
}

void add_mod_cars_to_lists(GameNatives& n, const std::shared_ptr<Module>& into) {
    if (!into) return;
    std::vector<std::weak_ptr<Module>>& kept = n.db().car_lists;
    if (std::none_of(kept.begin(), kept.end(), [&](const std::weak_ptr<Module>& m) { return m.lock() == into; })) kept.push_back(into);
    // Gran Turismo mode's dealers (the cars for sale), Arcade's makers' pages (every car).
    for (const auto& [name, for_sale_only] : {std::pair<const char*, bool>{"NewCarData", true}, {"CarData", false}})
        if (const std::shared_ptr<script::Variable> data = into->find(name))
            if (const std::shared_ptr<Module> lists = data->value.as<Module>()) add_to(n, lists, for_sale_only);
}

void add_mod_makers_to_menus(GameNatives& n) { add_mod_makers_to_nations(n); }

std::vector<std::string> refresh_mod_cars(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n) return {};
    Database& d = n->db();
    if (!d.db) return {};
    std::vector<std::string> added = d.db->add_mod_cars();
    if (added.empty()) return added;
    use_mod_makers(d); // (a mod's makers installed since)
    add_mod_makers_to_nations(*n);
    d.cars = d.db->cars();
    d.by_label.clear();
    for (size_t i = 0; i < d.cars.size(); ++i) d.by_label[d.cars[i].label] = i;
    for (const std::string& l : added) d.specs.erase(l), d.stock_figures_.erase(l); // (asked for before: none then)
    std::vector<std::weak_ptr<Module>> lists = d.car_lists;
    for (const std::weak_ptr<Module>& m : lists)
        if (const std::shared_ptr<Module> into = m.lock()) add_mod_cars_to_lists(*n, into);
    rt.note("mod cars added: " + std::to_string(added.size()));
    return added;
}

} // namespace gt4r::menu
