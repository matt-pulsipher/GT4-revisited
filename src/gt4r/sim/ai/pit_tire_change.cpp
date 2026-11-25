#include "gt4r/sim/ai/pit_tire_change.h"

#include "gt4r/sim/session.h"

#include <memory>

namespace gt4r::sim::ai {

void add_tire_change(PitHooks& hooks, Session& session, db::SpecDb& db, const Program& program, const FileReader& read) {
    // 0x35C4D8: each car's pit tires from its entry's equipment and fitting parts.
    auto list = std::make_shared<std::vector<PitTires>>();
    for (size_t i = 0; i < session.cars().size(); ++i) {
        const Equipment& eq = session.equipment(i);
        list->push_back(pit_tires(db, program, eq, fitting_parts(read, db, eq.part(Equipment::Car))));
    }
    Session* s = &session;
    db::SpecDb* d = &db;
    const Program* p = &program;
    hooks.tires = [list](size_t i) {
        if (i >= list->size()) return std::pair<int, int>{-1, -1};
        return std::pair<int, int>{(*list)[i].fitted[0], (*list)[i].fitted[1]};
    };
    // 0x35C0B8 at 645: a copy of the entry's equipment with the chosen parts, its spec (0x3F3B68 on the course's kind) and
    // 0x361570 for the axles served, whether or not a part was found.
    hooks.change_tires = [list, s, d, p](size_t i, u8 front, u8 rear, u8 axles) {
        if (i >= list->size()) return;
        Equipment eq = s->equipment(i);
        fit_chosen_tires(eq, (*list)[i], front, rear);
        Spec spec;
        build_spec(*d, *p, eq, s->loose() ? 2 : 0, spec);
        s->change_tires(i, spec, *p, axles);
    };
    hooks.retune_tires = [s, p](size_t i, u8 axles) { s->retune_tires(i, *p, axles); };
}

} // namespace gt4r::sim::ai
