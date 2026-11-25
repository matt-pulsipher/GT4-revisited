#include "gt4r/sim/pit_tires.h"

#include <cstdio>
#include <cstring>

namespace gt4r::sim {

namespace {

constexpr u32 kLeast = 0x620860, kMost = 0x620870; // the choosable kinds by the kind fitted (16 bytes each)

u64 r64(const u8* p) {
    u64 v;
    std::memcpy(&v, p, 8);
    return v;
}

} // namespace

std::vector<FittingPart> fitting_parts(const FileReader& read, const db::SpecDb& db, db::PartId car) {
    std::vector<FittingPart> out;
    if (!read || car == db::kNoPart) return out;
    char name[40];
    std::snprintf(name, sizeof name, "CARS/%016llX", static_cast<unsigned long long>(u64(car)));
    const std::optional<Bytes> file = read(db.folder() + name);
    if (!file || file->size() < 8) return out;
    const u64 n = r64(file->data());
    for (u64 i = 0; i < n && 8 + (i + 1) * 16 <= file->size(); ++i) {
        const u8* e = file->data() + 8 + i * 16;
        out.push_back({db::PartId(r64(e)), e[8]});
    }
    return out;
}

db::PartId fitting_part(const std::vector<FittingPart>& parts, u32 table, u8 kind) {
    for (const FittingPart& f : parts) {
        const u32 t = db::part_table(f.part);
        if (t == table && f.kind == kind) return f.part;
        if (table < t) break;
    }
    return db::kNoPart;
}

u8 tire_kind(db::SpecDb& db, const Equipment& eq, int axle) {
    const db::PartId id = eq.part(axle == 0 ? Equipment::FrontTire : Equipment::RearTire);
    if (id == db::kNoPart) return 0xFF;
    const ByteSpan row = db::part_row(db, id);
    const size_t at = axle == 0 ? 0x3A : 0x38;
    return at < row.size() ? row[at] : 0;
}

db::PartId PitTires::part(int axle, int kind) const {
    if (axle < 0 || axle > 1) return db::kNoPart;
    if (kind < int(least[size_t(axle)]) || int(most[size_t(axle)]) < kind) return db::kNoPart;
    const size_t k = size_t(kind - int(least[size_t(axle)]));
    return k < 5 ? parts[size_t(axle)][k] : db::kNoPart;
}

PitTires pit_tires(db::SpecDb& db, const Program& program, const Equipment& eq, const std::vector<FittingPart>& parts) {
    PitTires t;
    for (int a = 0; a < 2; ++a) {
        const u8 kind = tire_kind(db, eq, a);
        t.fitted[size_t(a)] = kind;
        t.least[size_t(a)] = program.b(kLeast + kind), t.most[size_t(a)] = program.b(kMost + kind);
        const u32 table = u32(a == 0 ? db::PartTable::FrontTire : db::PartTable::RearTire);
        for (int k = t.least[size_t(a)]; k <= int(t.most[size_t(a)]); ++k)
            if (size_t(k - t.least[size_t(a)]) < 5) t.parts[size_t(a)][size_t(k - t.least[size_t(a)])] = fitting_part(parts, table, u8(k));
    }
    return t;
}

u8 fit_chosen_tires(Equipment& eq, PitTires& t, u8 front, u8 rear) {
    u8 changed = 0;
    const u8 chosen[2] = {front, rear};
    for (int a = 0; a < 2; ++a) {
        if (chosen[a] == 0xFF) continue;
        const db::PartId id = t.part(a, chosen[a]);
        if (id == db::kNoPart) continue;
        eq.set_part(a == 0 ? Equipment::FrontTire : Equipment::RearTire, id);
        t.fitted[size_t(a)] = chosen[a];
        changed |= u8(1 << a);
    }
    return changed;
}

void change_tire_records(db::SpecDb& db, const Program& program, const Equipment& eq, int course_kind, ParamBlock& p, u32 axles, Spec* spec) {
    Spec s;
    build_spec(db, program, eq, course_kind, s);
    convert_tire_records(s, program, p, axles);
    if (spec) *spec = s;
}

} // namespace gt4r::sim
