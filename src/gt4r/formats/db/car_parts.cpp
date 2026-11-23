#include "gt4r/formats/db/car_parts.h"

namespace gt4r::db {

namespace {

const char* const kNames[] = {"GENERIC_CAR", "BRAKE",       "BRAKECONTROLLER", "SUSPENSION",     "ASCC",          "TCSC",         "CHASSIS",
                              "RACINGMODIFY", "LIGHTWEIGHT", "STEER",           "DRIVETRAIN",     "GEAR",          "ENGINE",       "NATUNE",
                              "TURBINEKIT",   "PORTPOLISH",  "ENGINEBALANCE",   "DISPLACEMENT",   "COMPUTER",      "INTERCOOLER",  "MUFFLER",
                              "CLUTCH",       "FLYWHEEL",    "PROPELLERSHAFT",  "LSD",            "FRONTTIRE",     "REARTIRE",     "NOS",
                              "SUPERCHARGER", "WHEEL",       "WING",            "TIRESIZE",       "TIRECOMPOUND",  "TIREFORCEVOL"};

} // namespace

const char* part_table_name(u32 table_id) { return table_id < u32(PartTable::Count) ? kNames[table_id] : nullptr; }

ByteSpan part_row(SpecDb& db, u32 table_id, u32 row_id) {
    const char* name = part_table_name(table_id);
    if (!name) return {};
    const Table* t = db.table(name);
    return t ? t->row(row_id) : ByteSpan();
}

std::optional<CarParts> car_parts(SpecDb& db, const std::string& car_label) {
    const Labels* cars = db.labels("GENERIC_CAR");
    const Table* car_table = db.table("GENERIC_CAR");
    if (!cars || !car_table) return std::nullopt;
    const std::optional<u32> id = cars->id(car_label);
    if (!id) return std::nullopt;
    const ByteSpan row = car_table->row(*id);
    if (row.size() < 0x18) return std::nullopt;
    CarParts out;
    out.car_id = *id;
    out.car_row.assign(row.begin(), row.end());
    out.parts.fill(kNoPart);
    // The car's row starts with its DEFAULT_PARTS row {row id, table id}.
    const Table* defaults = db.table("DEFAULT_PARTS");
    if (!defaults) return std::nullopt;
    const ByteSpan parts = defaults->row(rd32(row, 0));
    if (parts.size() < 26 * 8) return std::nullopt;
    for (size_t k = 0; k < 26; ++k) {
        const u32 r = rd32(parts, 8 * k), t = rd32(parts, 8 * k + 4);
        if (t >= 1 && t <= 26 && r != 0xFFFFFFFFu) out.parts[t - 1] = part_id(t, r);
    }
    return out;
}

std::optional<Chassis> Chassis::read(ByteSpan r) {
    if (r.size() < 0x25) return std::nullopt;
    Chassis c;
    c.length_mm = rd16(r, 0x00), c.height_mm = rd16(r, 0x02), c.wheelbase_mm = rd16(r, 0x04), c.weight_kg = rd16(r, 0x06);
    c.grip_front = rd16(r, 0x0E), c.grip_rear = rd16(r, 0x10);
    for (size_t a = 0; a < 2; ++a) c.outer_width_mm[a] = rd16(r, 0x12 + 2 * a), c.wheel_top_mm[a] = rd16(r, 0x16 + 2 * a);
    c.front_weight_pct = r[0x1C], c.yaw_inertia = r[0x1D], c.cog_height_mm = r[0x1E];
    return c;
}

std::optional<Engine> Engine::read(ByteSpan r) {
    if (r.size() < 0x71) return std::nullopt;
    Engine e;
    e.catalog_ps = rd16(r, 0x1A), e.catalog_torque = rd16(r, 0x1C);
    e.points = r[0x54] <= 24 ? r[0x54] : 24;
    for (size_t i = 0; i < 24; ++i) e.torque[i] = rd16(r, 0x1E + 2 * i), e.rpm[i] = r[0x55 + i];
    e.shift_limit = r[0x50], e.rev_limit = r[0x51], e.idle_rpm10 = r[0x52], e.clutch_rpm10 = r[0x53];
    e.output_pct = r[0x6F];
    return e;
}

Engine::Peak Engine::peak() const {
    Peak p;
    const float k = float(output_pct ? output_pct : 100) * 0.01f;
    for (int i = 0; i < points; ++i) {
        const float kgfm = float(torque[size_t(i)]) * 0.01f * k, n = float(rpm[size_t(i)]) * 100.0f;
        const float ps = kgfm * n / 716.2f;
        if (ps > p.ps) p.ps = ps, p.power_rpm = int(n);
        if (kgfm > p.kgfm) p.kgfm = kgfm, p.torque_rpm = int(n);
    }
    return p;
}

std::optional<Drivetrain> Drivetrain::read(ByteSpan r) {
    if (r.size() < 0x15) return std::nullopt;
    Drivetrain d;
    d.engine_inertia = rd16(r, 0), d.engine_friction = rd16(r, 2);
    d.wheel_inertia[0] = rd16(r, 4), d.wheel_inertia[1] = rd16(r, 6);
    d.shaft_inertia[0] = rd16(r, 8), d.shaft_inertia[1] = rd16(r, 0xA);
    d.drive = r[0x10], d.awd_system = r[0x11], d.front_torque_pct = r[0x14];
    return d;
}

const char* Drivetrain::layout() const {
    static const char* const k[] = {"FR", "FF", "4WD", "MR", "RR"};
    return drive < 5 ? k[drive] : "?";
}

std::optional<Gear> Gear::read(ByteSpan r) {
    if (r.size() < 0x2D) return std::nullopt;
    Gear g;
    g.gears = r[0x24];
    for (size_t i = 0; i < 11; ++i) g.ratio[i] = rd16(r, 2 * i);
    g.reverse = rd16(r, 0x16);
    g.final_min = rd16(r, 0x18), g.final_max = rd16(r, 0x1A), g.final_drive = rd16(r, 0x1C);
    g.kind = r[0x23], g.computed = r[0x25] != 0;
    g.slider_min = r[0x26], g.slider_max = r[0x27], g.slider = r[0x28];
    return g;
}

std::optional<TireSize> TireSize::read(ByteSpan r) {
    if (r.size() < 4) return std::nullopt;
    TireSize t;
    t.aspect = r[0] * 5, t.width_mm = r[3] * 5;
    // The rim: tenths of an inch; values below 61 are whole inches.
    t.rim_inches = r[2] < 61 ? float(r[2]) : float(r[2]) * 0.1f;
    return t;
}

} // namespace gt4r::db
