// The Car database page's facts: menu/car_database.h.

#include "gt4r/menu/car_database.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/save_editor.h"

#include "base/config.h"
#include "base/log.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/showroom.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>

namespace gt4r::menu {

using script::Kind;
using script::Value;

namespace {

u16 r16(ByteSpan r, size_t o) { return r.size() >= o + 2 ? u16(r[o] | r[o + 1] << 8) : 0; }
u32 r32(ByteSpan r, size_t o) { return r.size() >= o + 4 ? u32(r[o]) | u32(r[o + 1]) << 8 | u32(r[o + 2]) << 16 | u32(r[o + 3]) << 24 : 0; }

std::string string_of(const Value& v) { return v.kind == Kind::String ? v.as_string() : std::string(); }
s32 int_of(const Value& v) { return v.kind == Kind::Int ? v.i : v.is_number() ? s32(v.as_float()) : 0; }

std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    for (size_t at; (at = s.find("  ")) != std::string::npos;) s.erase(at, 1);
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The name tables of a menus' language (CAR_NAME_<language>; as the menus' database picks them).
std::string database_language(const std::string& code) {
    if (code == "JP") return "japanese";
    if (code == "GB" || code == "PT") return "british";
    if (code == "FR") return "french";
    if (code == "DE") return "german";
    if (code == "IT") return "italian";
    if (code == "ES") return "spanish";
    if (code == "KR") return "korean";
    if (code == "CN" || code == "TW") return "big5";
    return "american";
}

const char* const kDrivetrains[] = {"FR", "FF", "4WD", "MR", "RR"};

std::string fmt(const char* f, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
std::string fmt(const char* f, ...) {
    char b[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

// Whether the app's font draws every letter of `s` (UTF-8): Latin and its accents, the general punctuation.
bool drawable_text(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const u8 c = u8(s[i]);
        u32 cp = c;
        int n = 0;
        if (c >= 0xF0) cp = c & 7, n = 3;
        else if (c >= 0xE0) cp = c & 15, n = 2;
        else if (c >= 0xC0) cp = c & 31, n = 1;
        for (int k = 1; k <= n && i + size_t(k) < s.size(); ++k) cp = cp << 6 | (u8(s[i + size_t(k)]) & 63);
        i += size_t(n) + 1;
        if (cp >= 0x250 && !(cp >= 0x2000 && cp < 0x2070) && cp != 0x20AC) return false;
    }
    return true;
}

// A name's letters of the Japanese character set that have a Latin form: the middle dots (U+30FB, U+FF65: "V・spec") as the Latin
// one, the stars ("FALKEN☆GT-R") as an asterisk, full-width letters and the ideographic space as the plain ones.
std::string latin(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const u8 c = u8(s[i]);
        if (c == 0xE3 && i + 2 < s.size() && u8(s[i + 1]) == 0x83 && u8(s[i + 2]) == 0xBB) out += "\xC2\xB7", i += 2;
        else if (c == 0xEF && i + 2 < s.size() && u8(s[i + 1]) == 0xBD && u8(s[i + 2]) == 0xA5) out += "\xC2\xB7", i += 2; // (U+FF65)
        else if (c == 0xE2 && i + 2 < s.size() && u8(s[i + 1]) == 0x98 && (u8(s[i + 2]) == 0x85 || u8(s[i + 2]) == 0x86)) out += '*', i += 2; // (the stars)
        else if (c == 0xE3 && i + 2 < s.size() && u8(s[i + 1]) == 0x80 && u8(s[i + 2]) == 0x80) out += ' ', i += 2;
        else if (c == 0xEF && i + 2 < s.size() && (u8(s[i + 1]) == 0xBC || u8(s[i + 1]) == 0xBD)) {
            const u32 cp = 0xF000 | u32(u8(s[i + 1]) & 0x3F) << 6 | (u8(s[i + 2]) & 0x3F);
            if (cp >= 0xFF01 && cp <= 0xFF5E) out += char(cp - 0xFEE0), i += 2;
            else out += char(c);
        } else out += char(c);
    }
    return out;
}

// A text key as words: "SPORTS_MUFFLER" -> "Sports muffler".
std::string humanize(std::string key) {
    for (size_t i = 0; i < key.size(); ++i) {
        char& c = key[i];
        c = c == '_' ? ' ' : i ? char(std::tolower(static_cast<unsigned char>(c))) : char(std::toupper(static_cast<unsigned char>(c)));
    }
    return key;
}

// "%.2t": a number of hundredths as the program writes it.
std::string hundredths(s32 v) { return fmt("%d.%02d", v / 100, std::abs(v % 100)); }

} // namespace

CarDatabase::CarDatabase(FileReader read, RuntimeConfig config) {
    // The database and the program alone first: Prologue's whole page, and what tells the games apart.
    own_ = std::make_unique<Database>();
    Database& d = *own_;
    d.tried = true;
    // The names in the menus' language when the app's font draws it, else the American ones (the Japanese discs have both).
    d.language = database_language(config.language);
    if (d.language == "japanese" || d.language == "korean" || d.language == "big5") d.language = "american";
    const FolderLister list = config.list;
    try {
        d.db = std::make_unique<db::SpecDb>(read, [list](const std::string& dir) { return list ? list(!dir.empty() && dir.back() == '/' ? dir : dir + "/") : std::vector<std::string>{}; },
                                            d.language);
    } catch (const std::exception& e) {
        error_ = std::string("This disc has no car database: ") + e.what();
        return;
    }
    d.cars = d.db->cars();
    for (size_t i = 0; i < d.cars.size(); ++i) d.by_label[d.cars[i].label] = i;
    for (const db::CarEntry& c : d.cars) names_[c.label] = c.name;
    d.courses = d.db->courses();
    try {
        if (const std::shared_ptr<const gt3r::CoreImage> image = config.image()) {
            d.names = ProgramTables::find(image->program, image->load_address);
            d.program = sim::Program::from_core(image);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Car database: the program's tables were not read: %s", e.what());
    }
    prologue_ = d.db->prologue();
    GameState state;
    state.option_defaults(config.region);
    if (!prologue_) {
        // Gran Turismo mode's scripts and text on a new game (nothing of it is kept).
        config.data_dir.clear();
        editor_ = std::make_unique<SaveEditor>(read, config);
        GameState fresh;
        fresh.option_defaults(config.region);
        fresh.money = 0;
        editor_->start(fresh);
        state = editor_->state();
        own_.reset();
    }
    const GameState::Unit p = state.power_unit(), t = state.torque_unit();
    power_unit_ = p.name, torque_unit_ = t.name;
    power_factor_ = p.factor, torque_factor_ = t.factor;
    read_cars();
}

CarDatabase::~CarDatabase() = default;

Database& CarDatabase::db() { return editor_ ? editor_->natives().db() : *own_; }

std::string CarDatabase::text(const std::string& page, const std::string& group, const std::string& key) {
    return editor_ ? editor_->text(page, group, key) : key;
}

std::string CarDatabase::credits(s64 v) { return v > 0 ? "Cr. " + money_text(v) : "Cr. --"; }

const CarDatabase::Car* CarDatabase::car(const std::string& label) const {
    for (const Car& c : cars_)
        if (c.label == label) return &c;
    return nullptr;
}

bool CarDatabase::drawable(const std::string& s) { return drawable_text(s); }

void CarDatabase::read_cars() {
    cars_.clear();
    std::map<size_t, int> tuner; // car -> its tuner's number
    Database& d = db();
    if (!d.db) return;
    for (const db::CarEntry& e : d.cars) {
        Car c;
        c.label = e.label;
        const auto named = names_.find(e.label);
        c.name = named != names_.end() && !named->second.empty() ? named->second : e.name.empty() ? e.label : e.name;
        c.name = one_line(latin(c.name));
        if (c.name.empty()) c.name = e.label;
        for (char& ch : c.name)
            if (ch == '`') ch = '\''; // (the database's apostrophe)
        const ByteSpan row = d.car_row(e.label);
        if (row.size() >= 0x13) {
            c.price = r32(row, 0x08);
            c.year = s16(r16(row, 0x0C));
            c.category = row[0x11] % 100;
            c.for_sale = !(row[0x12] & 1);
            tuner[cars_.size()] = row[0x10];
            if (const auto it = d.names.tuners.find(row[0x10]); it != d.names.tuners.end()) {
                // The dealers' name of the tuner (TunerName, as the save editor reads it).
                c.maker = text("NationRoot", "TunerName", it->second);
                if (c.maker == it->second || !drawable(c.maker)) c.maker.clear();
            }
            if (const auto it = d.names.countries.find(row[0x0E]); it != d.names.countries.end()) c.country = country_name(it->second);
        }
        const Database::Spec& sp = d.spec(e.label);
        if (sp.engine && !sp.power_rpm.empty() && sp.power_rpm[0] != '-') c.power = gt3r::ee::to_int(sim::F(float(sp.ps)) * sim::F(power_factor_));
        c.weight = sp.weight;
        c.drivetrain = sp.drivetrain >= 0 && sp.drivetrain < 5 ? kDrivetrains[sp.drivetrain] : "--";
        cars_.push_back(std::move(c));
    }
    // A tuner without a name the app can draw (the Japanese discs' are in kana): the words its cars' names begin with, all of
    // them (the database's names are "<maker> <model>"); a tuner of one car: its name less the short name (CAR_NAME's second).
    std::map<int, std::vector<size_t>> by_tuner;
    for (const auto& [i, t] : tuner) by_tuner[t].push_back(i);
    for (const auto& [t, list] : by_tuner) {
        if (!cars_[list[0]].maker.empty() || prologue_) continue; // (Prologue's database names no makers)
        std::string common;
        if (list.size() > 1) {
            common = cars_[list[0]].name;
            for (size_t i : list) {
                const std::string& n = cars_[i].name;
                size_t k = 0;
                while (k < common.size() && k < n.size() && common[k] == n[k]) ++k;
                common.resize(k);
            }
            // (whole words)
            if (const size_t sp = common.rfind(' '); sp != std::string::npos && !(common.size() == cars_[list[0]].name.size())) common.resize(sp);
            else if (common.size() != cars_[list[0]].name.size() || common.find(' ') == std::string::npos) common.clear();
        }
        for (size_t i : list) {
            std::string m = common;
            if (m.empty()) {
                const auto it = d.names.tuners.find(t);
                m = it == d.names.tuners.end() ? std::string("--") : it->second;
                if (!m.empty()) m[0] = char(std::toupper(static_cast<unsigned char>(m[0])));
                if (const size_t sp = cars_[i].name.find(' '); list.size() == 1 && sp != std::string::npos) m = cars_[i].name.substr(0, sp);
            }
            cars_[i].maker = m;
        }
    }
}

std::string CarDatabase::country_name(const std::string& code) {
    static const std::pair<const char*, const char*> kCountries[] = {
        {"JP", "Japan"}, {"US", "USA"}, {"GB", "UK"}, {"UK", "UK"}, {"DE", "Germany"}, {"FR", "France"}, {"IT", "Italy"}, {"ES", "Spain"},
        {"SE", "Sweden"}, {"NL", "Netherlands"}, {"BE", "Belgium"}, {"KR", "Korea"}, {"AU", "Australia"}, {"AT", "Austria"}, {"PDI", "--"},
    };
    for (const auto& [k, name] : kCountries)
        if (code == k) return name;
    return code;
}

std::vector<CarDatabase::Row> CarDatabase::specs(const std::string& label) {
    std::vector<Row> out;
    Database& d = db();
    const Car* c = car(label);
    if (!d.db || !c) return out;
    const Database::Spec& sp = d.spec(label);
    std::optional<db::CarParts> parts = db::car_parts(*d.db, label);
    auto part_row = [&](db::PartTable t) { return parts ? db::part_row(*d.db, parts->part(t)) : ByteSpan{}; };

    // The dealer's spec sheet, as its rows write the car's natives (getCatEngineType, getCatPs, ...).
    out.push_back({"Engine", "", true});
    out.push_back({"Type", sp.layout.empty() && sp.valves.empty() ? std::string("----") : sp.layout + ":" + sp.valves});
    out.push_back({"Displacement", sp.displacement.empty() ? std::string("----") : sp.displacement});
    // getCatPs (0x445EE8): the PS times the unit's factor, truncated; getCatTq (0x445F70): the hundredths of a kgf m times
    // the factor, truncated.
    if (!sp.engine || sp.power_rpm.empty() || sp.power_rpm[0] == '-') out.push_back({"Power", "----"});
    else out.push_back({"Power", fmt("%d%s/%srpm", gt3r::ee::to_int(sim::F(float(sp.ps)) * sim::F(power_factor_)), power_unit_.c_str(), sp.power_rpm.c_str())});
    if (!sp.engine || sp.torque_rpm.empty() || sp.torque_rpm[0] == '-') out.push_back({"Torque", "----"});
    else {
        const s32 v = gt3r::ee::to_int(sim::F(float(std::lround(sp.torque * 100.0f))) * sim::F(torque_factor_));
        out.push_back({"Torque", hundredths(v) + torque_unit_ + "/" + sp.torque_rpm + "rpm"});
    }
    out.push_back({"Dimensions & Weight", "", true});
    // getCatLength / getCatWidth / getCatHeight: CHASSIS +0x08, RACINGMODIFY +0x08, CHASSIS +0x0A ("-" for none).
    const ByteSpan chassis = part_row(db::PartTable::Chassis), body = part_row(db::PartTable::RacingModify);
    auto size = [](int v) { return v ? std::to_string(v) : std::string("-"); };
    out.push_back({"Dimensions", size(r16(chassis, 0x08)) + "x" + size(r16(body, 0x08)) + "x" + size(r16(chassis, 0x0A))});
    out.push_back({"Weight", sp.weight ? std::to_string(sp.weight) + "kg" : std::string("----")});
    out.push_back({"Chassis", "", true});
    out.push_back({"Drivetrain", c->drivetrain});
    if (!prologue_) { // (Prologue's database holds no prices)
        out.push_back({"Price", "", true});
        out.push_back({"Price", c->for_sale ? credits(c->price) : credits(0)});
    }

    // What the database holds besides.
    out.push_back({"More", "", true});
    if (!c->maker.empty()) out.push_back({"Maker", c->maker});
    if (!c->country.empty()) out.push_back({"Country", c->country});
    if (c->year > 0) out.push_back({"Year", std::to_string(c->year)});
    static const char* const kCategories[] = {"Production car", "Race car", "Tuned car", "Concept car"};
    if (!prologue_) out.push_back({"Category", c->category >= 0 && c->category < 4 ? kCategories[c->category] : "--"});
    if (!c->for_sale && c->price > 0) out.push_back({"List price", credits(c->price) + " (not sold by the dealers)"});
    if (!sp.aspiration.empty()) out.push_back({"Aspiration", sp.aspiration});
    if (const std::optional<db::Chassis> ch = db::Chassis::read(chassis)) {
        if (ch->wheelbase_mm) out.push_back({"Wheelbase", std::to_string(ch->wheelbase_mm) + "mm"});
        if (ch->front_weight_pct) out.push_back({"Weight on the front", std::to_string(ch->front_weight_pct) + "%"});
    }
    if (body.size() >= 6 && r16(body, 2)) out.push_back({"Tread F / R", std::to_string(r16(body, 2)) + " / " + std::to_string(r16(body, 4)) + "mm"});
    if (c->power > 0 && c->weight > 0) out.push_back({"Weight / power", fmt("%.2fkg/%s", double(c->weight) / c->power, power_unit_.c_str())});
    if (const std::optional<db::Gear> g = db::Gear::read(part_row(db::PartTable::Gear)); g && g->gears > 0) {
        if (g->kind != 0) out.push_back({"Gearbox", "Continuously variable"});
        else {
            out.push_back({"Gearbox", fmt("%d-speed, final %.3f", g->gears, g->final_drive / 1000.0)});
            std::string ratios;
            for (int i = 0; i < g->gears && i < 11; ++i) ratios += fmt(i ? " %.3f" : "%.3f", g->ratio[size_t(i)] / 1000.0);
            if (g->ratio[0]) out.push_back({"Ratios", ratios}); // (a gearbox whose ratios are made from its slider keeps none)
        }
    }
    auto tire = [&](db::PartTable t) -> std::string {
        const ByteSpan r = part_row(t);
        if (r.size() < 8) return {};
        const std::optional<db::TireSize> s = db::TireSize::read(db::part_row(*d.db, db::PartTable::TireSize, r32(r, 0)));
        if (!s || !s->width_mm) return {};
        return s->rim_inches == std::floor(s->rim_inches) ? fmt("%d/%dR%d", s->width_mm, s->aspect, int(s->rim_inches)) : fmt("%d/%dR%.1f", s->width_mm, s->aspect, double(s->rim_inches));
    };
    if (const std::string f = tire(db::PartTable::FrontTire); !f.empty()) out.push_back({"Tires, front", f});
    if (const std::string r = tire(db::PartTable::RearTire); !r.empty()) out.push_back({"Tires, rear", r});
    if (const std::optional<db::Engine> e = db::Engine::read(part_row(db::PartTable::Engine)); e && e->rev_limit)
        out.push_back({"Rev limit", std::to_string(e->rev_limit * 100) + "rpm"});
    return out;
}

std::optional<CarDatabase::Curve> CarDatabase::curve(const std::string& label) {
    Database& d = db();
    if (!d.db) return std::nullopt;
    Curve c;
    c.power_unit = power_unit_, c.torque_unit = torque_unit_;
    const Database::Spec& sp = d.spec(label);
    c.power_rpm = std::atoi(sp.power_rpm.c_str()), c.torque_rpm = std::atoi(sp.torque_rpm.c_str());
    // The settings pages' chart (buildEngineCurveData's figures: sim::Showroom::charts, a new engine).
    if (const sim::Showroom* sr = d.showroom()) {
        sim::Equipment eq;
        if (!sim::stock_car(*d.db, d.program, label, eq)) return std::nullopt;
        const std::optional<sim::Charts> ch = sr->charts(eq, sim::F(1.0f));
        if (!ch || ch->engine.rpm.empty()) return std::nullopt;
        const sim::EngineFigures& e = ch->engine;
        for (size_t i = 0; i < e.rpm.size(); ++i) {
            c.rpm.push_back(e.rpm[i]);
            c.power.push_back(gt3r::ee::to_host((i < e.point_ps.size() ? e.point_ps[i] : sim::F::raw(0)).bits) * power_factor_);
            c.torque.push_back(gt3r::ee::to_host((i < e.point_kgfm.size() ? e.point_kgfm[i] : sim::F::raw(0)).bits) * torque_factor_);
        }
        c.peak_power = gt3r::ee::to_host(e.ps.bits) * power_factor_, c.peak_torque = gt3r::ee::to_host(e.kgfm.bits) * torque_factor_;
        c.rev_limit = e.rev_limit;
        return c;
    }
    // Prologue: the engine row's own curve (ENGINE +0x1E, +0x55) with its output factor.
    const std::optional<db::CarParts> parts = db::car_parts(*d.db, label);
    const std::optional<db::Engine> e = parts ? db::Engine::read(db::part_row(*d.db, parts->part(db::PartTable::Engine))) : std::nullopt;
    if (!e || e->points <= 0) return std::nullopt;
    for (int i = 0; i < e->points && i < 24; ++i) {
        const int rpm = e->rpm[size_t(i)] * 100;
        const float kgfm = e->torque[size_t(i)] / 100.0f * e->output_pct / 100.0f;
        c.rpm.push_back(rpm);
        c.torque.push_back(kgfm * torque_factor_);
        c.power.push_back(kgfm * float(rpm) / 716.2f * power_factor_);
        c.peak_power = std::max(c.peak_power, c.power.back()), c.peak_torque = std::max(c.peak_torque, c.torque.back());
    }
    c.rev_limit = e->rev_limit * 100;
    return c;
}

std::vector<CarDatabase::PartKind> CarDatabase::upgrades(const std::string& label) {
    std::vector<PartKind> out;
    if (!editor_) return out;
    SaveEditor& ed = *editor_;
    Database& d = db();
    // The car as sold, as the tune shop's car object has it.
    auto handle = std::make_shared<CarHandle>();
    handle->state = &ed.state(), handle->label = label;
    if (!set_car(d, handle->own, label, 0)) return out;
    const Value car = make_car_value(ed.natives(), handle);
    ed.method(car, "loadPartsInfo");
    // The tune shop's groups and their parts types (TuningRoot: Muffler, Brake, Engine, DriveTrain, Turbo, Suspension, Tire,
    // Others; the tires' front and rear are one kind there).
    struct Group {
        const char *key, *name;
        std::vector<int> types;
    };
    static const Group kGroups[] = {
        {"Muffler", "Exhaust", {20}}, {"Brake", "Brakes", {1, 2}}, {"Engine", "Engine", {12, 13, 15, 16, 17, 18, 27}},
        {"DriveTrain", "Drivetrain", {11, 21, 22, 24, 23}}, {"Turbo", "Turbo", {14, 19, 28}}, {"Suspension", "Suspension", {3}},
        {"Tire", "Tires", {25}}, {"Others", "Other", {10, 7, 8, 31, 32}},
    };
    const Value condition = ed.lookup("GTmodeProject::TuningApplyRoot::getPartsCondition");
    const Value shop_name = ed.lookup("GTmodeProject::getPartsShopName");
    for (const Group& g : kGroups)
        for (int type : g.types) {
            PartKind k;
            k.group = text("TuningRoot", "", g.key);
            if (k.group.empty() || k.group == g.key || !drawable(k.group)) k.group = g.name;
            const Value t = Value::integer(type);
            const int n = type == 32 ? 1 : int_of(ed.method(car, "getPartsTypeMax", {t}));
            const int stock = type >= 32 ? -1 : int_of(ed.method(car, "getEquipedParts", {t}));
            // (Category 0 is the part as sold or none, not sold by the shop; the tires' 0 .. 2 are its standard tires.)
            for (int cat = type == 25 || type == 32 ? 0 : 1; cat < std::max(n, 1); ++cat) {
                // getPartsCondition(car, type, category): [exists, owned, allowed, price].
                const Value r = ed.call(condition, {car, t, Value::integer(cat)});
                const std::vector<Value>* v = r.items();
                if (!v || v->size() < 4 || !(*v)[0].truthy()) continue;
                Part p;
                p.price = int_of((*v)[3]);
                p.stock = cat == stock;
                // The tune shop's name of the part (getPartsShopName: TuningApplyRoot's text, "Exhaust & Air Filter: Sports").
                const std::string key = string_of(ed.call(shop_name, {t, Value::integer(cat)}));
                p.name = key.empty() ? std::string() : text("TuningApplyRoot", "", key);
                if (p.name.empty() || p.name == key || !drawable(p.name)) p.name = key.empty() ? "Stage " + std::to_string(cat) : humanize(key);
                p.name = one_line(p.name);
                k.parts.push_back(std::move(p));
            }
            if (k.parts.empty()) continue;
            k.name = std::to_string(type);
            out.push_back(std::move(k));
        }
    return out;
}

std::vector<CarDatabase::Event> CarDatabase::events(const std::string& label) {
    std::vector<Event> out;
    if (!editor_) return out;
    SaveEditor& ed = *editor_;
    Database& d = db();
    auto handle = std::make_shared<CarHandle>();
    handle->state = &ed.state(), handle->label = label;
    if (!set_car(d, handle->own, label, 0)) return out;
    const Value car = make_car_value(ed.natives(), handle);
    const Value check = ed.lookup("GTmodeProject::getViolatedRegulatoin");
    const Value race_class = ed.lookup("main::menu::MRaceData");
    script::Vm& vm = ed.rt_->vm();
    // The event's page's words (EventInfoDialog), or ours where the disc's cannot be drawn.
    auto words = [&](const std::string& key, const std::string& ours) {
        const std::string t = text("EventInfoDialog", "", key);
        return t.empty() || t == key || !drawable(t) ? ours : t;
    };
    for (const SaveEditor::EventList& list : ed.events())
        for (const SaveEditor::Event& e : list.events) {
            if (e.races.empty()) continue;
            Event ev;
            // (A disc whose words the app cannot draw: the hall's place in the lists and the event's code as words,
            // "jprjp_70_0000" -> "Jp 70".)
            ev.hall = drawable(list.title) ? list.title : "Event hall " + std::to_string(&list - ed.events().data() + 1);
            if (drawable(e.name)) ev.name = e.name;
            else {
                std::string key = e.code.size() > 5 ? e.code.substr(0, e.code.size() - 5) : e.code;
                if (key.size() > 3 && key[2] == '_') key = key.substr(3);
                else if (key.size() > 5 && key.compare(2, 1, "r") == 0) key = key.substr(5);
                ev.name.clear();
                for (std::string_view w : gt3r::split(key, '_')) { // ("ff" -> "FF", "sunday" -> "Sunday")
                    std::string word(w);
                    for (size_t i = 0; i < word.size(); ++i)
                        if (word.size() <= 3 || i == 0) word[i] = char(std::toupper(static_cast<unsigned char>(word[i])));
                    ev.name += (ev.name.empty() ? "" : " ") + word;
                }
            }
            ev.code = e.code, ev.level = drawable(e.level) ? e.level : std::string();
            // The event hall's check (GTmodeProject's onInitialize of the event's page): the regulation of its first race.
            const std::string& race_code = e.races[0].label;
            const Value reg = ed.call(check, {car, Value::string(race_code)});
            ev.allowed = !vm.attribute(reg, "violated").truthy();
            const Value race = ed.call(race_class, {Value::string(race_code)});
            auto need = [&](const char* method) { return int_of(ed.method(race, method)); };
            auto broken = [&](const char* field) { return !vm.attribute(reg, field).is_nil(); };
            // The rows of the event's page whose restriction is on, in its order.
            struct Kind {
                const char *method, *key, *field, *ours;
                std::vector<const char*> values;
            };
            static const Kind kKinds[] = {
                {"getNeedCarType", "CarTypeSpec", "cartype", "Car type", {"", "Production cars", "Race cars", "Convertibles"}},
                {"getNeedDrivetrain", "DrivetrainSpec", "drivetrain", "Drivetrain", {"", "FR", "FF", "4WD", "MR", "RR"}},
                {"getNeedAspiration", "AspirationSpec", "aspiration", "Engine type", {"", "Naturally aspirated", "Turbo", "Supercharged"}},
                {"getNeedTire", "TireRequired", "tiretype", "Tires", {"", "Standard", "Standard or sports", "Dirt", "Snow"}},
            };
            for (const Kind& k : kKinds) {
                const int n = need(k.method);
                if (n <= 0) continue;
                const std::string value = n < int(k.values.size()) ? k.values[size_t(n)] : std::to_string(n);
                ev.rules.push_back({words(k.key, k.ours), words(std::string(k.key) + "_" + std::to_string(n), value), false, broken(k.field)});
            }
            auto with_number = [](std::string f, const std::string& v) {
                const size_t at = f.find_first_of('%');
                if (at != std::string::npos && at + 1 < f.size()) f.replace(at, 2, v);
                return f;
            };
            if (const int ps = need("getNeedPs")) {
                const std::string v = std::to_string(std::lround(float(ps) * power_factor_)) + " " + power_unit_;
                ev.rules.push_back({words("NeedPower", "Power"), with_number(words("NeedPowerDoc", "%s or over"), v), false, broken("need_ps")});
            }
            if (const int price = need("getNeedPrice"))
                ev.rules.push_back({words("NeedPrice", "Price"), with_number(words("NeedPriceDoc", "Cr. %d or more"), std::to_string(price)), false, broken("need_price")});
            if (const int kg = need("getLimitedWeight"))
                ev.rules.push_back({words("LimitedWeight", "Weight"), with_number(words("LimitedWeightDoc", "%d kg or less"), std::to_string(kg)), false, broken("weight")});
            if (const int mm = need("getLimitedLength"))
                ev.rules.push_back({words("LimitedLength", "Length"), with_number(words("LimitedLengthDoc", "%d mm or less"), std::to_string(mm)), false, broken("length")});
            if (const int n = need("getNeedCountryID")) {
                static const char* const kNations[] = {"", "Japan", "USA", "UK", "Italy", "France", "Germany", "Europe"};
                ev.rules.push_back({words("NeedNation", "Country"), words("NeedNation_" + std::to_string(n), n < 8 ? std::string(kNations[n]) + " only" : std::to_string(n)), false,
                                    broken("country")});
            }
            const Value year_range = ed.method(race, "getNeedYearRange"), allowed = ed.method(race, "getAllowedCarList");
            if (const std::vector<Value>* years = year_range.items(); years && years->size() >= 2) {
                const int from = int_of((*years)[0]), to = int_of((*years)[1]);
                std::string v;
                if (from > 0 && to > 0) v = with_number(with_number(words("NeedYearDoc", "From %d to %d"), std::to_string(from)), std::to_string(to));
                else if (to > 0) v = with_number(words("NeedYearDocMax", "Up to %d"), std::to_string(to));
                else if (from > 0) v = std::to_string(from) + " or later";
                if (!v.empty()) ev.rules.push_back({words("NeedYear", "Model year"), v, false, broken("year")});
            }
            if (const std::vector<Value>* cars = allowed.items(); cars && !cars->empty()) {
                std::string names;
                for (size_t i = 0; i < cars->size() && i < 5; ++i) {
                    const Car* c = this->car(string_of((*cars)[i]));
                    names += (i ? ", " : "") + (c ? c->name : string_of((*cars)[i]));
                }
                if (cars->size() > 5) names += ", " + words("and the others", "and others");
                ev.rules.push_back({words("CarsAllowed", "Cars"), names, false, broken("carlist")});
            }
            if (const int n = need("getNeedLicense")) {
                static const char* const kLicenses[] = {"", "National B", "National A", "International B", "International A", "Super"};
                ev.license = words("NeedLicense_" + std::to_string(n), n < 6 ? std::string(kLicenses[n]) + " License" : std::to_string(n));
            }
            for (Row& r : ev.rules) {
                r.label = one_line(r.label), r.value = one_line(r.value);
                if (!r.label.empty() && r.label.back() == ':') r.label.pop_back(); // ("Cars Permitted:", the page's label beside its list)
            }
            ev.license = one_line(ev.license);
            out.push_back(std::move(ev));
        }
    return out;
}

} // namespace gt4r::menu
