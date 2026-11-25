#include "gt4r/net/car_choice.h"

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/convert.h"
#include "gt4r/sim/race_tires.h"
#include "gt4r/sim/showroom.h"

#include <algorithm>
#include <cstring>

namespace gt4r::net {

namespace {

u64 fnv(ByteSpan b, u64 h = 0xCBF29CE484222325ull) {
    for (u8 v : b) h = (h ^ v) * 0x100000001B3ull;
    return h;
}

// The equipment's part slots and the parts table each takes (sim/equipment.h).
struct SlotTable {
    u32 slot;
    db::PartTable table;
};
constexpr SlotTable kSlots[] = {
    {sim::Equipment::Brake, db::PartTable::Brake},
    {sim::Equipment::BrakeController, db::PartTable::BrakeController},
    {sim::Equipment::Chassis, db::PartTable::Chassis},
    {sim::Equipment::Engine, db::PartTable::Engine},
    {sim::Equipment::Drivetrain, db::PartTable::Drivetrain},
    {sim::Equipment::Gear, db::PartTable::Gear},
    {sim::Equipment::Suspension, db::PartTable::Suspension},
    {sim::Equipment::Lsd, db::PartTable::Lsd},
    {sim::Equipment::FrontTire, db::PartTable::FrontTire},
    {sim::Equipment::RearTire, db::PartTable::RearTire},
    {sim::Equipment::Steer, db::PartTable::Steer},
    {sim::Equipment::Lightweight, db::PartTable::Lightweight},
    {sim::Equipment::RacingModify, db::PartTable::RacingModify},
    {sim::Equipment::PortPolish, db::PartTable::PortPolish},
    {sim::Equipment::EngineBalance, db::PartTable::EngineBalance},
    {sim::Equipment::Displacement, db::PartTable::Displacement},
    {sim::Equipment::Computer, db::PartTable::Computer},
    {sim::Equipment::NaTune, db::PartTable::NaTune},
    {sim::Equipment::TurbineKit, db::PartTable::TurbineKit},
    {sim::Equipment::Flywheel, db::PartTable::Flywheel},
    {sim::Equipment::Clutch, db::PartTable::Clutch},
    {sim::Equipment::PropellerShaft, db::PartTable::PropellerShaft},
    {sim::Equipment::Muffler, db::PartTable::Muffler},
    {sim::Equipment::Intercooler, db::PartTable::Intercooler},
    {sim::Equipment::Ascc, db::PartTable::Ascc},
    {sim::Equipment::Tcsc, db::PartTable::Tcsc},
    {sim::Equipment::Wheel, db::PartTable::Wheel},
    {sim::Equipment::Nos, db::PartTable::Nos},
    {sim::Equipment::Wing, db::PartTable::Wing},
    {sim::Equipment::Supercharger, db::PartTable::Supercharger},
};

} // namespace

std::string settings_problem(db::SpecDb& db, const sim::Program& program, const sim::Equipment& eq) {
    using E = sim::Equipment;
    // The fitted parts' defaults.
    db::CarParts parts;
    parts.car_id = db::part_row_id(eq.part(E::Car));
    for (const SlotTable& s : kSlots)
        if (u32(s.table) >= 1 && u32(s.table) <= 26) parts.parts[u32(s.table) - 1] = eq.part(s.slot);
    E def;
    if (!sim::parts_equipment(db, parts, def)) return "no such car";
    sim::computed_gears(db, program, def);
    auto row = [&](u32 slot) { return db::part_row(db, eq.part(slot)); };
    auto at = [](ByteSpan r, size_t o) { return o < r.size() ? int(r[o]) : 0; };
    auto word = [&](ByteSpan r, size_t o) { return at(r, o) | at(r, o + 1) << 8; };
    std::string why;
    auto byte = [&](u32 o, int lo, int hi, const char* what) {
        const int v = eq.b(o);
        bool ok = v == def.b(o) || (lo <= v && v <= hi);
        if (!ok && lo > hi) ok = gt3r::s8(lo) <= gt3r::s8(v) && gt3r::s8(v) <= gt3r::s8(hi); // (a signed range)
        if (!ok && why.empty()) why = std::string("a setting out of its range (") + what + ")";
    };
    auto half = [&](u32 o, int lo, int hi, const char* what) {
        const int v = eq.h(o);
        if (v != def.h(o) && (v < lo || v > hi) && why.empty()) why = std::string("a setting out of its range (") + what + ")";
    };
    const ByteSpan brake = row(E::BrakeController), train = row(E::Drivetrain), body = row(E::RacingModify), lsd = row(E::Lsd), susp = row(E::Suspension),
                   ascc = row(E::Ascc), tcsc = row(E::Tcsc), nos = row(E::Nos), gear = row(E::Gear), wing = row(E::Wing);
    byte(E::BrakeFront, 1, at(brake, 3), "brakes"), byte(E::BrakeRear, 1, at(brake, 7), "brakes");
    byte(E::TorqueSplit, at(train, 0x12), at(train, 0x13), "torque split");
    byte(E::DownforceFront, at(body, 0x0D) + at(wing, 3), at(body, 0x0E) + at(wing, 4), "downforce");
    byte(E::DownforceRear, at(body, 0x10) + at(wing, 3), at(body, 0x11) + at(wing, 4), "downforce");
    for (u32 i = 0; i < 6; ++i) byte(E::Turbo + i, 1, 0, "turbo");
    static constexpr u8 kLsd[6] = {0x04, 0x10, 0x08, 0x14, 0x0C, 0x18};
    for (u32 i = 0; i < 6; ++i) byte(E::LsdSettings + i, at(lsd, kLsd[i]), at(lsd, kLsd[i] + 1u), "differential");
    byte(E::CamberFront, at(susp, 0x0F), at(susp, 0x10), "camber"), byte(E::CamberRear, at(susp, 0x12), at(susp, 0x13), "camber");
    half(E::RideFront, word(susp, 0), word(susp, 2), "ride height"), half(E::RideRear, word(susp, 6), word(susp, 8), "ride height");
    byte(E::ToeFront, at(susp, 0x27), at(susp, 0x28), "toe"), byte(E::ToeRear, at(susp, 0x2A), at(susp, 0x2B), "toe");
    byte(E::SpringFront, at(susp, 0x34), at(susp, 0x35), "springs"), byte(E::SpringRear, at(susp, 0x37), at(susp, 0x38), "springs");
    byte(E::MotionFront, 1, 0, "suspension"), byte(E::MotionRear, 1, 0, "suspension");
    static constexpr u8 kDamper[8] = {0x48, 0x48, 0x4F, 0x4F, 0x56, 0x56, 0x5D, 0x5D};
    for (u32 i = 0; i < 8; ++i) byte(E::Dampers + i, 1, at(susp, kDamper[i]), "dampers");
    byte(E::AntiRollFront, 1, at(susp, 0x66), "anti-roll bars"), byte(E::AntiRollRear, 1, at(susp, 0x6A), "anti-roll bars");
    static constexpr u8 kAscc[4] = {0x04, 0x0A, 0x0F, 0x15};
    for (u32 i = 0; i < 4; ++i) byte(E::AsccIndices + i, ascc.empty() ? 1 : 1, ascc.empty() ? 0 : at(ascc, kAscc[i]), "ASM");
    byte(E::TcscIndex, 1, tcsc.empty() ? 0 : at(tcsc, 6), "TCS");
    byte(E::Ballast, 0, 200, "ballast");
    if (const int place = gt3r::s8(eq.b(E::BallastPlace)); (place < -50 || place > 50) && why.empty()) why = "a setting out of its range (ballast)";
    byte(E::NosSetting, 0, std::max({at(nos, 5), at(nos, 6), at(nos, 7)}), "nitrous");
    // What no page sets.
    half(E::OutputFactor, 1, 0, "output");
    for (u32 o : {u32(E::WeightPct), u32(E::PowerPct), 0x164u, u32(E::GripAdd)}) byte(o, 1, 0, "output");
    // The gears.
    const int gears = std::clamp(at(gear, 0x24), 0, 7);
    if (at(gear, 0x25)) {
        half(E::FinalDrive, word(gear, 0x18), word(gear, 0x1A), "gears"), half(E::FinalDrive2, word(gear, 0x18), word(gear, 0x1A), "gears");
        byte(E::GearSlider, at(gear, 0x26), at(gear, 0x27), "gears");
        E set = eq;
        sim::computed_gears(db, program, set);
        auto ratio = [&](int g) { return int(s16(set.h(E::GearRatios + 2 * u32(g)))); };
        for (int g = 0; g <= gears; ++g) {
            const int lo = g == 0 || g == gears ? ratio(g) * 7 / 8 : (2 * ratio(g) + ratio(g + 1)) / 3;
            const int hi = g <= 1 ? ratio(g) * 9 / 8 : (ratio(g - 1) + 2 * ratio(g)) / 3;
            half(E::GearRatios + 2 * u32(g), std::min(lo, ratio(g)) * 3 / 4, std::max(hi, ratio(g)) * 4 / 3, "gears");
        }
    } else {
        half(E::FinalDrive, 1, 0, "gears"), byte(E::GearSlider, 1, 0, "gears");
        for (int g = 0; g <= 11; ++g) half(E::GearRatios + 2 * u32(g), 1, 0, "gears");
    }
    return why;
}

bool CarChoice::operator==(const CarChoice& o) const {
    return label == o.label && paint == o.paint && tire == o.tire && garage == o.garage && equipment.bytes == o.equipment.bytes && condition == o.condition &&
           automatic == o.automatic && asm_on == o.asm_on && tcs_on == o.tcs_on && wheel_aid == o.wheel_aid && driver == o.driver && digest == o.digest;
}

void write_choice(gt3r::net::Writer& w, const CarChoice& c) {
    w.str(c.label);
    w.u8_(c.paint);
    w.u8_(u8(c.tire + 1));
    w.u8_(u8((c.automatic ? 1 : 0) | (c.asm_on ? 2 : 0) | (c.tcs_on ? 4 : 0) | (c.wheel_aid ? 8 : 0) | (c.garage ? 16 : 0)));
    w.u8_(c.driver);
    w.u32_(u32(c.digest)), w.u32_(u32(c.digest >> 32));
    if (!c.garage) return;
    w.bytes(c.equipment.bytes);
    w.bytes(c.condition);
}

bool read_choice(gt3r::net::Reader& r, CarChoice& c) {
    c.label = r.str();
    c.paint = r.u8_();
    c.tire = int(r.u8_()) - 1;
    const u8 flags = r.u8_();
    c.automatic = flags & 1, c.asm_on = flags & 2, c.tcs_on = flags & 4, c.wheel_aid = flags & 8, c.garage = flags & 16;
    c.driver = r.u8_();
    const u32 lo = r.u32_(), hi = r.u32_();
    c.digest = u64(lo) | u64(hi) << 32;
    if (c.garage) {
        const ByteSpan eq = r.bytes(c.equipment.bytes.size()), cond = r.bytes(c.condition.size());
        if (!r.ok()) return false;
        std::copy(eq.begin(), eq.end(), c.equipment.bytes.begin());
        std::copy(cond.begin(), cond.end(), c.condition.begin());
    }
    return r.ok() && c.label.size() < 0x40 && c.tire <= 12 && c.driver <= 2;
}

CarBuilder::CarBuilder(db::SpecDb& db, const sim::Program& program, sim::FileReader read)
    : db_(db), program_(program), read_(std::move(read)), showroom_(std::make_unique<sim::Showroom>(db_, program_)) {}
CarBuilder::~CarBuilder() = default;

std::optional<sim::Equipment> CarBuilder::raced(const CarChoice& c, int course_kind, std::string* why) const {
    sim::Equipment eq;
    if (!sim::stock_car(db_, program_, c.label, eq)) {
        if (why) *why = "this disc has no car " + c.label;
        return std::nullopt;
    }
    if (c.garage) {
        if (c.equipment.part(sim::Equipment::Car) != eq.part(sim::Equipment::Car)) {
            if (why) *why = "the car's parts are another car's";
            return std::nullopt;
        }
        eq = c.equipment;
    }
    sim::fit_race_tires(db_, read_, eq, course_kind, c.garage ? -1 : c.tire);
    eq.set_b(0x18, c.paint);
    return eq;
}

u64 CarBuilder::digest(const CarChoice& c) const {
    const std::optional<sim::Equipment> eq = raced(c, 0);
    if (!eq) return 0;
    sim::Spec spec;
    sim::Record rec;
    auto block = std::make_unique<sim::ParamBlock>();
    if (!showroom_->record(*eq, spec, rec, -1, block.get())) return 0;
    u64 h = fnv(block->bytes);
    if (c.garage) h = fnv(c.condition, h);
    return h ? h : 1;
}

std::string CarBuilder::problem(const CarChoice& c) const {
    std::string why;
    sim::Equipment stock;
    if (!sim::stock_car(db_, program_, c.label, stock)) return "this disc has no car " + c.label;
    if (c.garage) {
        if (c.equipment.part(sim::Equipment::Car) != stock.part(sim::Equipment::Car)) return "the car's parts are another car's";
        const std::vector<sim::FittingPart> fit = sim::fitting_parts(read_, db_, stock.part(sim::Equipment::Car));
        for (const SlotTable& s : kSlots) {
            const db::PartId id = c.equipment.part(s.slot);
            if (id == db::kNoPart || id == stock.part(s.slot)) continue;
            if (db::part_table(id) != u32(s.table)) return std::string("a part of the wrong kind (") + db::part_table_name(u32(s.table)) + ")";
            // (a wheel fits any car; the database's row must be there)
            if (db::part_row(db_, id).empty()) return std::string("a part this disc does not have (") + db::part_table_name(u32(s.table)) + ")";
            if (s.table == db::PartTable::Wheel || s.table == db::PartTable::Wing) continue;
            if (std::none_of(fit.begin(), fit.end(), [&](const sim::FittingPart& f) { return f.part == id; }))
                return std::string("a part that does not fit the car (") + db::part_table_name(u32(s.table)) + ")";
        }
        if (const std::string why = settings_problem(db_, program_, c.equipment); !why.empty()) return why;
    }
    const u64 here = digest(c);
    if (!here) return "the car cannot be built here";
    if (here != c.digest) return "the car is different on this disc";
    return {};
}

int CarBuilder::power(const CarChoice& c) const {
    const std::optional<sim::Equipment> eq = raced(c, 0);
    const std::optional<sim::ShowroomFigures> f = eq ? showroom_->figures(*eq) : std::nullopt;
    return f ? gt3r::ee::to_int(f->power) : 0;
}

int CarBuilder::weight(const CarChoice& c) const {
    const std::optional<sim::Equipment> eq = raced(c, 0);
    const std::optional<sim::ShowroomFigures> f = eq ? showroom_->figures(*eq) : std::nullopt;
    return f ? f->weight : 0;
}

} // namespace gt4r::net
