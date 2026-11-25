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
