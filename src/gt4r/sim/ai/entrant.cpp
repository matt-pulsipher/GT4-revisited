#include "gt4r/sim/ai/entrant.h"

#include "base/ee_float.h"

#include <algorithm>

namespace gt4r::sim::ai {

namespace {

constexpr u32 kEnemyTable = 38; // ENEMY_CARS

std::optional<u32> label_id(db::SpecDb& db, const char* table, const std::string& label) {
    const db::Labels* l = db.labels(table);
    return l ? l->id(label) : std::nullopt;
}

} // namespace

std::optional<EntrySettings> EntrySettings::read(ByteSpan r) {
    if (r.size() < 0x25) return std::nullopt;
    EntrySettings s;
    s.output = rd16(r, 0x00), s.final_drive = rd16(r, 0x02);
    s.ride[0] = rd16(r, 0x04), s.ride[1] = rd16(r, 0x06);
    s.spring[0] = r[0x08], s.spring[1] = r[0x0A];
    s.downforce[0] = r[0x0C], s.downforce[1] = r[0x0D];
    for (u32 i = 0; i < 6; ++i) s.lsd[i] = r[0x0E + i];
    s.gear_slider = r[0x14];
    s.camber[0] = r[0x15], s.camber[1] = r[0x16];
    s.toe[0] = r[0x17], s.toe[1] = r[0x18];
    for (u32 i = 0; i < 8; ++i) s.dampers[i] = r[0x19 + i];
    s.anti_roll[0] = r[0x21], s.anti_roll[1] = r[0x22];
    s.ascc = r[0x23], s.tcs = r[0x24];
    return s;
}

void EntrySettings::apply(Equipment& eq, bool entered) const {
    eq.set_h(Equipment::FinalDrive, final_drive);
    eq.set_b(Equipment::GearSlider, gear_slider);
    // The differentials: the row's front three, then the rear three, into the record's front / rear pairs.
    static constexpr u32 kLsd[6] = {0, 2, 4, 1, 3, 5};
    for (u32 i = 0; i < 6; ++i) eq.set_b(Equipment::LsdSettings + kLsd[i], lsd[i]);
    eq.set_b(Equipment::DownforceFront, downforce[0]), eq.set_b(Equipment::DownforceRear, downforce[1]);
    for (u32 i = 0; i < 8; ++i) eq.set_b(Equipment::Dampers + i, dampers[i]);
    eq.set_b(Equipment::CamberFront, camber[0]), eq.set_b(Equipment::CamberRear, camber[1]);
    eq.set_b(Equipment::ToeFront, toe[0]), eq.set_b(Equipment::ToeRear, toe[1]);
    eq.set_b(Equipment::AntiRollFront, anti_roll[0]), eq.set_b(Equipment::AntiRollRear, anti_roll[1]);
    eq.set_h(Equipment::RideFront, ride[0]), eq.set_h(Equipment::RideRear, ride[1]);
    eq.set_b(Equipment::SpringFront, spring[0]), eq.set_b(Equipment::SpringRear, spring[1]);
    eq.set_b(Equipment::TcscIndex, tcs);
    eq.set_b(Equipment::AsccIndices, ascc);
    // 0x445B78: the record names an entered car (its +0x08 is not none).
    if (entered) eq.set_h(Equipment::OutputFactor, output);
}

int paint_count(db::SpecDb& db, const Equipment& eq) {
    const db::PartId v = eq.part(Equipment::Variation);
    if (v == db::kNoPart) return 0;
    const db::Table* rows = db.table("VARIATIONamerican");
    if (!rows) return 0;
    size_t first = 0;
    return int(rows->rows(db::part_row_id(v), first));
}

bool opponent_equipment(db::SpecDb& db, const Program& program, const db::Opponent& o, Equipment& out) {
    const db::Table* cars = db.table("GENERIC_CAR");
    const db::Table* sets = db.table("DEFAULT_PARTS");
    if (!cars || !sets) return false;
    db::CarParts car;
    car.car_id = o.car_id;
    const ByteSpan car_row = cars->row(o.car_id);
    if (car_row.size() < 0x18) return false;
    car.car_row.assign(car_row.begin(), car_row.end());
    car.parts.fill(db::kNoPart);
    const ByteSpan parts = sets->row(o.parts_id);
    if (parts.size() < 26 * 8) return false;
    for (size_t k = 0; k < 26; ++k) {
        const u32 r = rd32(parts, 8 * k), t = rd32(parts, 8 * k + 4);
        if (t >= 1 && t <= 26 && r != 0xFFFFFFFFu) car.parts[t - 1] = db::part_id(t, r);
    }
    if (!parts_equipment(db, car, out)) return false;
    out.set_part(0x08, db::part_id(kEnemyTable, o.enemy_id));
    if (out.part(Equipment::Variation) == db::kNoPart) return false;
    // The row's settings (DEFAULT_PARAM, none: the parts' defaults), then a computed gearbox's ratios from them.
    if (const db::Table* params = db.table("DEFAULT_PARAM"))
        if (const std::optional<EntrySettings> s = EntrySettings::read(params->row(o.param_id))) s->apply(out, true);
    computed_gears(db, program, out);
    // 0x445880: a paint the car has (-1 as well: 0xFF, the race's choice).
    if (o.paint < paint_count(db, out)) out.set_b(0x18, u8(o.paint));
    return true;
}

bool entry_equipment(db::SpecDb& db, const Program& program, const db::RaceEvents& events, const GridEntry& g, Equipment& out) {
    if (!g.enemy.empty())
        if (const std::optional<u32> id = label_id(db, "ENEMY_CARS", g.enemy))
            if (std::optional<db::Opponent> o = events.opponent(*id)) {
                o->paint = g.paint;
                return opponent_equipment(db, program, *o, out);
            }
    if (!stock_car(db, program, g.car, out)) return false;
    if (g.paint >= 0 && g.paint < paint_count(db, out)) out.set_b(0x18, u8(g.paint));
    return true;
}

using gt3r::ee::F;

PaintColour paint_colour(db::SpecDb& db, const Equipment& eq, int paint) {
    PaintColour c;
    const db::PartId v = eq.part(Equipment::Variation);
    const db::Table* rows = v == db::kNoPart ? nullptr : db.table("VARIATIONamerican");
    size_t first = 0;
    if (!rows || paint < 0 || size_t(paint) >= rows->rows(db::part_row_id(v), first)) return c;
    const ByteSpan row = rows->row_at(first + size_t(paint));
    if (row.size() < 0x28) return c;
    const u32 word = rd32(row, 0x24);
    const F k255 = F::raw(0x437F0000);
    const F r = gt3r::ee::int_to_f(s32(word & 0xFF)) / k255, g = gt3r::ee::int_to_f(s32((word >> 8) & 0xFF)) / k255,
            b = gt3r::ee::int_to_f(s32((word >> 16) & 0xFF)) / k255;
    F len = gt3r::ee::sqrt((r * r + g * g) + b * b);
    if (len == F()) len = F::raw(0x3F800000);
    c.length = len, c.x = r / len, c.y = g / len, c.z = b / len;
    return c;
}

namespace {

// 0x40A338: how alike two paints are, 0 .. 1: their directions' agreement less their colours' distance over the
// largest (the root of 3); 1 for the same colour.
F likeness(const PaintColour& a, const PaintColour& b) {
    const F one = F::raw(0x3F800000), zero;
    if (a.x == b.x && a.y == b.y && a.z == b.z && a.length == b.length) return one;
    const F root3 = F::raw(0x3FDDB3D7);
    const F dx = a.x * a.length - b.x * b.length, dy = a.y * a.length - b.y * b.length, dz = a.z * a.length - b.z * b.length;
    const F dist = gt3r::ee::sqrt((dx * dx + dy * dy) + dz * dz);
    const F dot = (a.x * b.x + a.y * b.y) + a.z * b.z;
    const F l = ((root3 - dist) / root3) * dot;
    if (l < zero) return zero;
    return one < l ? one : l;
}

struct Candidate {
    int paint = 0;
    PaintColour colour;
    F weight;
};

} // namespace

void choose_paints(db::SpecDb& db, std::vector<Equipment>& grid, const std::function<float()>& random) {
    const F one = F::raw(0x3F800000);
    for (size_t e = 0; e < grid.size(); ++e) {
        if (grid[e].b(0x18) != 0xFF) continue;
        // 0x40A6F0: every car's candidates: its paint when fixed, else each of its paints weighed down by 0.984375 a step.
        std::vector<std::vector<Candidate>> cands(grid.size());
        for (size_t j = 0; j < grid.size(); ++j) {
            const u8 fixed = grid[j].b(0x18);
            if (fixed != 0xFF) {
                cands[j].push_back({int(fixed), paint_colour(db, grid[j], fixed), one});
                continue;
            }
            const int n = paint_count(db, grid[j]);
            F w = one, sum;
            for (int i = 0; i < n; ++i) {
                cands[j].push_back({i, paint_colour(db, grid[j], i), w});
                sum = sum + w;
                w = w * F::raw(0x3F7C0000);
            }
            for (Candidate& c : cands[j]) c.weight = c.weight / sum;
        }
        std::vector<Candidate>& mine = cands[e];
        if (mine.empty()) continue;
        F total;
        for (size_t k = 0; k < mine.size(); ++k) {
            F against = one;
            for (size_t j = 0; j < grid.size(); ++j)
                for (size_t c = 0; c < cands[j].size(); ++c) {
                    if (j == e && c == k) continue;
                    F l = likeness(mine[k].colour, cands[j][c].colour);
                    if (cands[j].size() == 1) {
                        for (int p = 0; p < 6; ++p) l = l * l;
                        l = l * F::raw(0x461C4000);
                    }
                    against = against + l;
                }
            mine[k].weight = mine[k].weight * (one / against);
            total = total + mine[k].weight;
        }
        // 0x40A5A0: in rising weight (the equal kept in order), then each its share.
        for (size_t i = 1; i < mine.size(); ++i) {
            Candidate x = mine[i];
            for (size_t j = 0; j < i; ++j)
                if (x.weight < mine[j].weight) std::swap(x, mine[j]);
            mine[i] = x;
        }
        for (Candidate& c : mine) c.weight = c.weight / total;
        // A fresh draw against each candidate's share in turn, the last taken when none is.
        size_t pick = mine.size() - 1;
        for (size_t i = 0; i + 1 < mine.size(); ++i)
            if (F(random()) < mine[i].weight) {
                pick = i;
                break;
            }
        grid[e].set_b(0x18, u8(mine[pick].paint));
    }
}

std::vector<Equipment> grid_equipment(db::SpecDb& db, const Program& program, const db::RaceEvents& events, const std::vector<GridEntry>& grid,
                                      u32 seed, const Equipment* player) {
    std::vector<Equipment> out(grid.size());
    for (size_t i = 0; i < grid.size(); ++i) {
        if (grid[i].player && player) out[i] = *player;
        else entry_equipment(db, program, events, grid[i], out[i]);
        // A car a script entered without a paint: the race's to choose (0x445880 with a paint word of 0), as a pool's.
        if (!grid[i].player && grid[i].enemy.empty() && grid[i].paint < 0 && paint_count(db, out[i]) > 0) out[i].set_b(0x18, 0xFF);
    }
    u32 state = seed ? seed : 1;
    choose_paints(db, out, [&state]() {
        state = state * 1664525u + 1013904223u;
        return float(state >> 8) / float(1u << 24);
    });
    return out;
}

} // namespace gt4r::sim::ai
