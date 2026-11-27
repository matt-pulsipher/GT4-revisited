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
// country page its button (image/piece/tuner_logo2/<key>.png): the mod's pictures (ModMakers::picture). Arcade's makers'
// page (ArcadeProject::MakerRoot) lists MakerData `<nation>_makers` that have cars: the key goes on its country's.

#include "gt4r/formats/db/mod_cars.h"
#include "gt4r/formats/db/mod_makers.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/runtime_internal.h"

#include "base/file.h"
#include "formats/png_reader.h"

#include <algorithm>
#include <map>
#include <set>

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
    // Arcade's makers' page (ArcadeProject::MakerRoot): MakerData `<nation>_makers` per nation, the nation of a country code by
    // the page's own pairs of lists (`<region>_nation_list` and `_nation_list2`: "usa" beside "US"), else the nation most of
    // whose makers' cars are of that country.
    if (const std::shared_ptr<Module> makers = vm.lookup("ArcadeProject::MakerData").as<Module>())
        if (const std::shared_ptr<Module> page = vm.lookup("ArcadeProject::MakerRoot").as<Module>())
            for (const db::ModMakerDef& m : d.db->mod_makers().makers()) {
                std::string nation;
                for (const auto& [name, v] : page->members) {
                    if (!nation.empty() || name.size() < 13 || name.compare(name.size() - 13, 13, "_nation_list2") != 0 || !v) continue;
                    const std::shared_ptr<script::Variable> names = page->find(name.substr(0, name.size() - 1));
                    const std::vector<Value>* codes = v->value.items();
                    const std::vector<Value>* keys = names ? names->value.items() : nullptr;
                    for (size_t i = 0; codes && keys && i < codes->size() && i < keys->size(); ++i)
                        if ((*codes)[i].kind == Kind::String && (*codes)[i].as_string() == m.country) nation = (*keys)[i].to_string();
                }
                if (nation.empty() && m.country_number >= 0) { // (the Japanese disc's page names its nations alone: the row whose makers' cars are mostly of the country)
                    std::map<std::string, int> country_of; // a maker's key -> its cars' country
                    for (const db::CarEntry& c : d.cars)
                        if (const ByteSpan row = d.car_row(c.label); row.size() > 0x0F)
                            if (const auto k = d.names.makers.find(row[0x0F]); k != d.names.makers.end()) country_of.emplace(k->second, row[0x0E]);
                    float best = 0;
                    for (const auto& [name, v] : makers->members) {
                        if (name.size() < 8 || name.compare(name.size() - 7, 7, "_makers") != 0 || !v || !v->value.items() || v->value.items()->empty()) continue;
                        int of = 0;
                        for (const Value& x : *v->value.items())
                            if (const auto it = country_of.find(x.to_string()); it != country_of.end() && it->second == m.country_number) ++of;
                        if (const float share = float(of) / float(v->value.items()->size()); share > best) best = share, nation = name.substr(0, name.size() - 7);
                    }
                }
                const std::shared_ptr<script::Variable> list = nation.empty() ? nullptr : makers->find(nation + "_makers");
                std::vector<Value>* items = list ? list->value.items() : nullptr;
                if (!items) continue;
                if (std::none_of(items->begin(), items->end(), [&](const Value& x) { return x.kind == Kind::String && x.as_string() == m.key; }))
                    items->push_back(Value::string(m.key));
            }
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

std::optional<std::pair<float, float>> mod_picture_size(Runtime& rt, const std::string& path) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n || !n->db().db) return std::nullopt;
    const std::optional<db::ModPicture> p = n->db().db->mod_picture(path);
    if (!p || p->png.empty() || !p->disc.empty()) return std::nullopt; // (a mod car's picture takes its stand-in's size)
    try {
        if (const std::optional<Bytes> file = gt3r::read_host_file(p->png)) {
            const gt3r::PngImage png = gt3r::decode_png(*file, 2048);
            if (png.width && png.height) return std::pair{float(png.width), float(png.height)};
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

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

static void garage_without(GameState& s, const std::map<std::string, u64>& gone) {
    for (size_t i = 0; i < s.garage.size(); ++i) {
        GarageEntry& e = s.garage[i];
        const auto it = gone.find(e.car);
        if (e.car.empty() || it == gone.end()) continue;
        // (its words as the save writes a car's, kept as a car the database lacks is: save/game_data.cpp)
        e.kept_flags = (e.kept_flags & ~(u64(0x7FFFFFF) | (u64(0x1FFF) << 32))) | u64(e.used) | u64(e.record.has_value()) << 1 | u64(e.paint & 0x7F) << 2 |
                       u64(e.power & 0x3FFFF) << 9 | u64(e.weight & 0x1FFF) << 32;
        e.kept_day = (e.kept_day & 0xFF000000u) | ((e.day - GameState::kFirstDay) & 0xFFFFFF);
        e.kept_rank = (e.kept_rank & ~u64(0x3FF)) | u64(e.ride_rank & 0x3FF);
        e.missing_code = it->second, e.car.clear(), e.used = false;
        if (s.riding_car == int(i)) s.riding_missing = int(i), s.riding_car = -1;
    }
    for (const auto& [label, code] : gone) s.available_cars.erase(label);
}

static void garage_with(GameState& s, Database& d) {
    const db::Labels* cars = d.db->labels("GENERIC_CAR");
    if (!cars) return;
    for (size_t i = 0; i < s.garage.size(); ++i) {
        GarageEntry& e = s.garage[i];
        if (!e.missing() || (e.missing_code >> 32) != 0) continue;
        const std::string* label = cars->label(u32(e.missing_code));
        if (!label) continue;
        e.car = *label, e.missing_code = ~u64(0);
        e.used = e.kept_flags & 1, e.paint = int((e.kept_flags >> 2) & 0x7F), e.power = u32((e.kept_flags >> 9) & 0x3FFFF), e.weight = u32((e.kept_flags >> 32) & 0x1FFF);
        e.day = GameState::kFirstDay + (e.kept_day & 0xFFFFFF), e.ride_rank = int(e.kept_rank & 0x3FF);
        if (s.riding_car < 0 && s.riding_missing == int(i)) s.riding_car = int(i), s.riding_missing = -1;
    }
}

// The labels out of the makers' lists of a script file (NewCarData's, CarData's), their counts down; a mod maker's list left
// empty goes (its dealer leaves its country's page).
static void remove_from_lists(Database& d, const std::shared_ptr<Module>& into, const std::set<std::string>& gone) {
    for (const char* name : {"NewCarData", "CarData"}) {
        const std::shared_ptr<script::Variable> data = into->find(name);
        const std::shared_ptr<Module> lists = data ? data->value.as<Module>() : nullptr;
        if (!lists) continue;
        std::vector<std::string> emptied;
        for (auto& [key, v] : lists->members) {
            if (key.size() < 8 || key.compare(key.size() - 8, 8, "_carlist") != 0 || !v) continue;
            std::vector<Value>* items = v->value.items();
            if (!items) continue;
            const size_t before = items->size();
            std::erase_if(*items, [&](const Value& x) { return x.kind == Kind::String && gone.count(x.as_string()) > 0; });
            if (items->size() == before) continue;
            const std::string maker = key.substr(0, key.size() - 8);
            if (const std::shared_ptr<script::Variable> count = lists->find(maker + "_carnum"); count && count->value.kind == Kind::Int)
                count->value = Value::integer(s32(items->size()));
            if (items->empty() && d.db->mod_makers().by_key(maker)) emptied.push_back(maker);
        }
        for (const std::string& m : emptied) lists->members.erase(m + "_carlist"), lists->members.erase(m + "_carnum");
    }
}

std::vector<std::string> refresh_mod_cars(Runtime& rt) {
    const std::shared_ptr<GameNatives> n = natives_of(rt);
    if (!n) return {};
    Database& d = n->db();
    if (!d.db) return {};
    // A mod switched off or removed: its cars out of the database, the makers' lists and the garage (kept in the save as a car
    // the database lacks is when the game is loaded).
    std::map<std::string, u64> codes;
    if (const db::Labels* cars = d.db->labels("GENERIC_CAR"))
        for (const std::string& label : d.db->mod_cars().labels())
            if (const std::optional<u32> id = cars->id(label)) codes[label] = u64(*id);
    if (const std::vector<std::string> removed = d.db->remove_mod_cars(); !removed.empty()) {
        std::map<std::string, u64> gone;
        for (const std::string& l : removed)
            if (const auto it = codes.find(l); it != codes.end()) gone[l] = it->second;
        const std::set<std::string> labels(removed.begin(), removed.end());
        d.cars = d.db->cars();
        d.by_label.clear();
        for (size_t i = 0; i < d.cars.size(); ++i) d.by_label[d.cars[i].label] = i;
        for (const std::string& l : removed) d.specs.erase(l), d.stock_figures_.erase(l);
        for (const std::weak_ptr<Module>& m : std::vector<std::weak_ptr<Module>>(d.car_lists))
            if (const std::shared_ptr<Module> into = m.lock()) remove_from_lists(d, into, labels);
        garage_without(rt.game(), gone);
        rt.note("mod cars taken out: " + std::to_string(removed.size()));
    }
    std::vector<std::string> added = d.db->add_mod_cars();
    if (added.empty()) return added;
    garage_with(rt.game(), d); // (a car of the garage the database lacked: there again)
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
