// The values the race's start makes of Prologue's tables for its step (docs/formats/prologue.md "What the step reads"):
// the program's conductor set up in the interpreter on a RACE row, then what its start wrote (the engine sound's scales
// 0x714904 .., the tire wear's rules at the conductor + 0x84, the rule block's class grips at the conductor + 0x10798)
// against ours (rules_prologue.h tables / race_rules).

#include "prologue_race.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_prologue.h"

#include <cstdio>

namespace gt4r::diff::prologue {

namespace {

std::vector<u8> raw_row(Disc& disc, const std::string& name, u32 id) {
    const gt3r::Volume::Entry* e = disc.volume().find("specdb/GT4_PROLOGUE1000/" + name + ".dbt");
    if (!e) return {};
    const Bytes b = disc.volume().read(*e);
    const u32 rows = rd32(b, 8), size = rd32(b, 12);
    const size_t base = 16 + size_t(rows) * 8;
    for (u32 k = 0; k < rows; ++k)
        if (rd32(b, 16 + 8 * k) == id) {
            const size_t at = base + rd32(b, 20 + 8 * k);
            return {b.begin() + std::ptrdiff_t(at), b.begin() + std::ptrdiff_t(at + size)};
        }
    return {};
}

} // namespace

StartTablesStats start_tables_check(Disc& disc, bool print) {
    namespace ex = gt3r::sim::exact;
    StartTablesStats st;
    const sim::Program program = sim::Program::load(disc);
    const sim::prologue::Tables t = sim::prologue::tables(program);
    if (!t.found) return st.err = "not Prologue's program", st;
    gt3r::Volume* vol = &disc.volume();
    const auto read = [vol](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* f = vol->find(p);
        return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
    };
    db::SpecDb sdb(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const db::RaceEvents events(sdb, read);
    // The rows: a single race's (its class grips 20), a lesson's (0), each also with made-up tire wear rules (+0x51 .. +0x57;
    // every row of the disc has none) so that the wear's arithmetic is compared.
    struct Case {
        const char* label;
        u32 conductor;
        bool wear;
    };
    static const Case kCases[] = {{"normal0001", 0x3B5468, false}, {"normal0001", 0x3B5468, true}, {"l0p0001", 0x3B4750, false},
                                  {"l0p0001", 0x3B4750, true}};
    static const u8 kWear[7] = {30, 50, 10, 5, 20, 20, 40}; // (+0x2C .. +0x30, +0x33, +0x34 of the rule block)
    for (const Case& k : kCases) {
        const auto ev = events.event(k.label);
        if (!ev) return st.err = std::string("no RACE row ") + k.label, st;
        Lesson lesson;
        lesson.conductor = k.conductor;
        lesson.row = raw_row(disc, "RACE", ev->id);
        if (lesson.row.size() < 0x58) return st.err = "a short RACE row", st;
        std::vector<u8> mapped(ev->bytes.begin(), ev->bytes.end());
        if (k.wear)
            for (size_t i = 0; i < 7; ++i) lesson.row[0x51 + i] = kWear[i], mapped[0x6B + i] = kWear[i];
        Ee ee;
        boot(ee, disc);
        Race r;
        std::string err;
        if (!setup_race(ee, disc, sdb, ev->course, {"civic_r_98"}, 1, r, err, true, &lesson)) return st.err = err, st;

        ex::RaceInputs ours;
        sim::prologue::race_inputs(ours, t);
        const std::array<u8, 0x80> rules = race::event_rule_block(mapped);
        // (the program's wear rules are written when its conductor has them on: ours with them on wherever it wrote any)
        const bool on = ee.r32(r.cond + 0x84 + 0x18) != 0;
        sim::prologue::race_rules(ours, t, rules.data(), on);
        auto check = [&](const char* what, u32 want, u32 got) {
            ++st.checks;
            if (want == got) ++st.equal;
            else if (print) std::printf("  %s %s: the program's %08X, ours %08X\n", k.label, what, want, got);
        };
        for (u32 i = 0; i < 4; ++i) check("sound scale", ee.r32(0x714904 + 4 * i), ours.sound_scales[i].bits);
        const ex::WearParams& w = ours.prep.wear;
        const gt3r::ee::F* wear[10] = {&w.fresh, &w.fresh_loss, &w.knee1, &w.loss1, &w.knee2, &w.loss2, &w.max, &w.max_loss, &w.worn, &w.slip_cap};
        for (u32 i = 0; i < 10; ++i) check("tire wear", ee.r32(r.cond + 0x84 + 4 * i), wear[i]->bits);
        for (u32 i = 0; i < 4; ++i) check("class grip", ee.r8(r.cond + 0x10798 + i), u8(ours.class_grip[i]));
        if (k.wear) (k.conductor == 0x3B5468 ? st.single_wear : st.license_wear) = on;
        if (print)
            std::printf("  %s (conductor %06X)%s: wear %s, class grips %d %d %d %d\n", k.label, k.conductor, k.wear ? " with wear rules" : "",
                        on ? "on" : "off", ours.class_grip[0], ours.class_grip[1], ours.class_grip[2], ours.class_grip[3]);
    }
    return st;
}

} // namespace gt4r::diff::prologue
