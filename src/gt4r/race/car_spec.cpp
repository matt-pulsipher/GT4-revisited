#include "gt4r/race/car_spec.h"

#include <algorithm>

namespace gt4r::race {

namespace {

// A car's row of a parts table: the one it has as sold, else its own by label
// ("<prefix><car label>_a": the first of them).
ByteSpan part_row(db::SpecDb& db, const std::string& car, const char* table, const char* prefix) {
    const db::Table* t = db.table(table);
    const db::Labels* l = db.labels(table);
    if (!t || !l) return {};
    std::optional<u32> id = db.default_part(car, l->table_id);
    if (!id) {
        const std::string start = prefix + car;
        std::optional<std::pair<std::string, u32>> best;
        for (const auto& [label, row] : l->entries)
            if (label.compare(0, start.size(), start) == 0 && (label.size() == start.size() || label[start.size()] == '_') &&
                (!best || label < best->first))
                best.emplace(label, row);
        if (best) id = best->second;
    }
    return id ? t->row(*id) : ByteSpan{};
}

} // namespace

CarSpec read_car_spec(db::SpecDb& db, const std::string& car) {
    CarSpec s;
    const ByteSpan chassis = part_row(db, car, "CHASSIS", "ch_");
    const ByteSpan modify = part_row(db, car, "RACINGMODIFY", "rm_");
    const ByteSpan suspension = part_row(db, car, "SUSPENSION", "su_");
    const ByteSpan engine = part_row(db, car, "ENGINE", "en_");
    const db::Table* sizes = db.table("TIRESIZE");
    bool ok = chassis.size() >= 0x1A && sizes;
    if (chassis.size() >= 6 && rd16(chassis, 0x04)) s.wheelbase = float(rd16(chassis, 0x04)) * 0.001f;
    for (int axle = 0; axle < 2; ++axle) {
        const ByteSpan tire = part_row(db, car, axle == 0 ? "FRONTTIRE" : "REARTIRE", axle == 0 ? "ft_" : "rt_");
        const ByteSpan size = tire.size() >= 4 && sizes ? sizes->row(rd32(tire, 0)) : ByteSpan{};
        if (size.size() >= 4 && size[3] && size[2]) {
            // Width in 5 mm steps, the rim in inches (in tenths when above 60), the aspect in 5% steps.
            const float width = float(size[3]) * 0.005f;
            const float inches = size[2] > 60 ? float(size[2]) * 0.1f : float(size[2]);
            s.tire_width[axle] = width;
            s.rim_radius[axle] = inches * 0.0127f;
            s.tire_radius[axle] = s.rim_radius[axle] + width * float(size[0]) * 0.05f;
        } else {
            ok = false;
        }
        if (modify.size() >= 0x15 && rd16(modify, 0x02 + 2 * size_t(axle))) {
            const float tread = float(rd16(modify, 0x02 + 2 * size_t(axle))) * 0.001f;
            const float offset = float(std::int8_t(modify[0x13 + size_t(axle)])) * 0.001f;
            s.half_track[axle] = (tread - 2 * offset) * 0.5f;
        } else if (chassis.size() >= 0x16 && rd16(chassis, 0x12 + 2 * size_t(axle))) {
            // The width across the tires' outer faces, less a tire's width.
            s.half_track[axle] = (float(rd16(chassis, 0x12 + 2 * size_t(axle))) * 0.001f - s.tire_width[axle]) * 0.5f;
            ok = false;
        } else {
            ok = false;
        }
        const size_t at = axle == 0 ? 0x04 : 0x0A;
        if (suspension.size() >= at + 2 && rd16(suspension, at)) s.ride_height[axle] = float(rd16(suspension, at)) * 0.001f;
        else ok = false;
    }
    if (engine.size() >= 0x6E && engine[0x6D]) {
        s.red_line = float(engine[0x6D]) * 100.0f;
        s.rev_limit = engine[0x51] ? float(engine[0x51]) * 100.0f : s.red_line + 500.0f;
        if (s.rev_limit < s.red_line) s.rev_limit = s.red_line + 500.0f;
    }
    s.from_database = ok;
    return s;
}

std::vector<u32> car_paints(db::SpecDb& db, const std::string& car) {
    std::vector<u32> out;
    const std::string language = "american";
    const db::Labels* cars = db.labels("GENERIC_CAR");
    const db::Table* link = db.table("CAR_VARIATION_" + language);
    const db::Table* rows = db.table("VARIATION" + language);
    if (!cars || !link || !rows) return out;
    const std::optional<u32> id = cars->id(car);
    if (!id) return out;
    const ByteSpan l = link->row(*id);
    if (l.size() < 4) return out;
    size_t first = 0;
    const size_t n = rows->rows(rd32(l, 0), first);
    for (size_t i = 0; i < n; ++i) {
        const ByteSpan r = rows->row_at(first + i);
        if (r.size() >= 8 && rd32(r, 4) >= 1) out.push_back(rd32(r, 4) - 1);
    }
    // The game lists a car's paints by their number (the Ford Ka's Capri-Blau, paint 1, first),
    // not in the rows' order.
    std::sort(out.begin(), out.end());
    return out;
}

std::array<u32, 4> paint_chip(db::SpecDb& db, const std::string& car, u32 paint) {
    std::array<u32, 4> out{};
    const db::Labels* cars = db.labels("GENERIC_CAR");
    const db::Table* link = db.table("CAR_VARIATION_american");
    const db::Table* rows = db.table("VARIATIONamerican");
    const std::optional<u32> id = cars ? cars->id(car) : std::nullopt;
    if (!id || !link || !rows) return out;
    const ByteSpan l = link->row(*id);
    if (l.size() < 4) return out;
    size_t first = 0;
    const size_t n = rows->rows(rd32(l, 0), first);
    for (size_t i = 0; i < n; ++i) {
        const ByteSpan r = rows->row_at(first + i);
        if (r.size() < 0x27 || rd32(r, 4) != paint + 1) continue;
        for (size_t k = 0; k < 4 && r.size() >= 0x34; ++k) out[k] = rd32(r, 0x24 + 4 * k);
        if (!(out[0] & 0x1000000u)) out[0] = u32(r[0x24]) | u32(r[0x25]) << 8 | u32(r[0x26]) << 16 | 0x1000000u; // (Prologue's: one color)
    }
    return out;
}

RailCar rail_car(const CarSpec& spec) {
    RailCar c;
    c.wheelbase = spec.wheelbase;
    for (int a = 0; a < 2; ++a) c.half_track[a] = spec.half_track[a], c.tire_radius[a] = spec.tire_radius[a];
    c.rev_limit = spec.rev_limit;
    return c;
}

} // namespace gt4r::race
