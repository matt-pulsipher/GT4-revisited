// The mods' cars on the makers' lists (formats/db/mod_cars.h): Gran Turismo mode's dealers sell the cars of the script
// module NewCarData (projects/gtmode/<zone>_carlist: `<maker>_carlist`, the labels in the order shown, and `<maker>_carnum`,
// how many), which the dealer page hands to BuyCarRoot (gtmode's DealerRoot: NewCarData[dealer + "_carlist"]); Arcade's
// makers' pages show the cars of the module CarData (projects/arcade/<zone>_carlist), alike. A mod car joins the end of its
// maker's list as the file is read: Arcade's always, a dealer's when it is for sale.

#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/menu/game_internal.h"

namespace gt4r::menu {

using script::Kind;
using script::Module;
using script::Value;

namespace {

void add_to(GameNatives& n, const std::shared_ptr<Module>& lists, bool for_sale_only) {
    Database& d = n.db();
    if (!d.db) return;
    const db::ModCars& mods = d.db->mod_cars();
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

} // namespace

void add_mod_cars_to_lists(GameNatives& n, const std::shared_ptr<Module>& into) {
    if (!into) return;
    // Gran Turismo mode's dealers (the cars for sale), Arcade's makers' pages (every car).
    for (const auto& [name, for_sale_only] : {std::pair<const char*, bool>{"NewCarData", true}, {"CarData", false}})
        if (const std::shared_ptr<script::Variable> data = into->find(name))
            if (const std::shared_ptr<Module> lists = data->value.as<Module>()) add_to(n, lists, for_sale_only);
}

} // namespace gt4r::menu
