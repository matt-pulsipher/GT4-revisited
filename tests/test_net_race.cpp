// The race of an online race between machines in one process (docs/specs/net.md "Race").

#include "net_machines.h"

#include <cstdlib>

using namespace gt4r;
using namespace gt4r::test::net;

namespace {

struct Run {
    Errors e;
    u32 warnings = 0;
    std::string warning;
};
enum class Kind { Own, Dead };
Run run(World& w, std::span<const std::string> labels, const Link& link, int seconds, bool smooth, Kind kind) {
    Race r(w, labels, link, 0, [&](std::vector<sim::Entry>& e) {
        if (!smooth) e[0].automatic = false;
    });
    for (size_t m = 0; m < r.machines.size(); ++m) {
        r.machines[m]->drive = smooth ? weaving(1.0f, 0.25f, double(m) * 1.3) : jerky(u32(77 + m * 1000), m == 0);
        for (size_t k = 1; k < labels.size(); ++k) r.machines[m]->field->remote(k).set_physics(kind == Kind::Own);
    }
    Run out;
    for (int k = 0; k < seconds * 60; ++k) {
        r.step_all();
        if (k < 30) continue;
        for (size_t m = 0; m < r.machines.size(); ++m)
            for (size_t c = 0; c < r.machines.size(); ++c)
                if (c != m) out.e.add(r.error(m, c));
    }
    for (auto& m : r.machines) out.warnings += m->warnings, out.warning += m->warning;
    return out;
}

} // namespace

// The remote car against its owner's, at several delays and losses (GT4R_NET_TABLE=1: every one of the specification's table).
TEST(net_race_remote_car_error) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    World w(*disc, "tsukuba");
    const std::vector<std::string> labels{"nsx_r_02", "r34_gtr_v2_nur_02"};
    const bool table = std::getenv("GT4R_NET_TABLE") != nullptr;
    struct Case {
        Link link;
        bool smooth;
        double rms_cm, worst_cm; // at most
    };
    std::vector<Case> cases{{{0, 0, 0}, true, 0.01, 0.1}, {{50, 10, 0.05}, true, 0.5, 8}, {{100, 20, 0.05}, true, 2, 20}, {{100, 20, 0.2}, false, 12, 150}};
    if (table) {
        cases.clear();
        for (bool smooth : {true, false})
            for (double ms : {0.0, 50.0, 100.0, 250.0})
                for (double loss : {0.0, 0.05, 0.2}) cases.push_back({{ms, ms * 0.2, loss}, smooth, 1e9, 1e9});
    }
    for (const Case& c : cases) {
        const Run own = run(w, labels, c.link, table ? 60 : 20, c.smooth, Kind::Own);
        const Run dead = table ? run(w, labels, c.link, 60, c.smooth, Kind::Dead) : Run{};
        std::printf("  %s %3.0f ms + %2.0f, loss %2.0f %%: rms %6.2f cm, worst %6.1f cm; dead reckoning rms %6.2f, worst %6.1f; warnings %u %s\n", c.smooth ? "smooth" : "jerky ", c.link.delay_ms,
                    c.link.jitter_ms, c.link.loss * 100, own.e.rms() * 100, own.e.worst * 100, dead.e.rms() * 100, dead.e.worst * 100, own.warnings, own.warning.c_str());
        CHECK(own.e.rms() * 100 < c.rms_cm && own.e.worst * 100 < c.worst_cm);
        CHECK(own.warnings == 0);
    }
}
