// A pit stop's tire change (sim/pit_tires.h, sim/ai/pit_tire_change.h): the pit's tires per car (0x35C4D8), the tire
// records rebuilt on the car's block (0x35C0B8 at its 645th / 644th steps: 0x3F3B68, 0x361570, 0x361798) against the
// program's at every call in its own race, and a B-spec car's stop on our session with another kind chosen.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/disc/disc.h"
#include "gt4r/sim/ai/pit_tire_change.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/pit_tires.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

sim::FileReader reader(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
}

db::SpecDb open_db(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(reader(disc), [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

// A kind the pit has a part for on `axle` other than the one fitted (-1: none).
int other_kind(const sim::PitTires& t, int axle) {
    for (int k = t.least[size_t(axle)]; k <= int(t.most[size_t(axle)]); ++k)
        if (k != t.fitted[size_t(axle)] && t.part(axle, k) != db::kNoPart) return k;
    return -1;
}

} // namespace

TEST(sim_pittires_change_on_the_block) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb db = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::vector<std::string> labels{"s500_63", "ka_01", "nsx_r_02", "r34_gtr_v2_nur_02"};
    auto session = sim::Session::open(*disc, "tsukuba", labels);
    int changed = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
        const sim::Equipment& eq = session->equipment(i);
        const std::vector<sim::FittingPart> parts = sim::fitting_parts(reader(*disc), db, eq.part(sim::Equipment::Car));
        sim::PitTires t = sim::pit_tires(db, program, eq, parts);
        std::printf("  %s: front kind %u (%u .. %u), rear %u (%u .. %u), %zu fitting parts\n", labels[i].c_str(), t.fitted[0], t.least[0], t.most[0], t.fitted[1],
                    t.least[1], t.most[1], parts.size());
        CHECK(!parts.empty());
        std::printf("   parts");
        for (int a = 0; a < 2; ++a)
            for (int k = 0; k < 5; ++k) std::printf(" %llX", (unsigned long long)t.parts[size_t(a)][size_t(k)]);
        std::printf("\n");
        // The kind fitted is in its range and its part is the equipment's.
        for (int a = 0; a < 2; ++a) {
            CHECK(t.least[size_t(a)] <= t.fitted[size_t(a)] && t.fitted[size_t(a)] <= t.most[size_t(a)]);
            CHECK(t.part(a, t.fitted[size_t(a)]) == eq.part(a == 0 ? sim::Equipment::FrontTire : sim::Equipment::RearTire));
        }
        // The change with the kinds fitted leaves the block as the car's set-up built it (0x361570 + 0x361798 are the
        // converter's own tire part).
        const sim::ParamBlock before = session->params(i);
        {
            sim::Equipment e2 = eq;
            sim::PitTires t2 = t;
            CHECK(sim::fit_chosen_tires(e2, t2, t.fitted[0], t.fitted[1]) == 3);
            sim::Spec spec;
            sim::build_spec(db, program, e2, session->loose() ? 2 : 0, spec);
            session->change_tires(i, spec, program, 3);
            session->retune_tires(i, program, 3);
            CHECK(std::memcmp(before.bytes.data(), session->params(i).bytes.data(), sim::ParamBlock::kSize) == 0);
        }
        // Another kind on both axles: the records rebuilt equal those of a car built with those tires (the same tire sizes).
        const int kf = other_kind(t, 0), kr = other_kind(t, 1);
        if (kf < 0 || kr < 0) continue;
        sim::Equipment e2 = eq;
        CHECK(sim::fit_chosen_tires(e2, t, u8(kf), u8(kr)) == 3 && t.fitted[0] == kf && t.fitted[1] == kr);
        sim::Spec spec;
        sim::build_spec(db, program, e2, session->loose() ? 2 : 0, spec);
        session->change_tires(i, spec, program, 3);
        const float grip_645 = session->gt4_record(i).tires.tire[0].grip.host(); // (curve D's at no slip x 9.8, before the load curve)
        session->retune_tires(i, program, 3);
        sim::Record fresh;
        sim::ParamBlock fresh_block;
        sim::build_record(spec, program, sim::rules::tables(program, {}), sim::ConvertIn{}, fresh, &fresh_block);
        int differ = 0;
        for (u32 k = 0x410; k < 0x410 + 6 * 0x1EC; ++k) differ += fresh_block.bytes[k] != session->params(i).bytes[k];
        const float g0 = before.f(0x418).host(), g1 = session->params(i).f(0x418).host(); // (the front record's paved grip)
        std::printf("    kinds %d / %d: front paved grip %.3f -> %.3f (base grip %.2f at the 645th), %d bytes of the records unlike a car built so\n", kf, kr, double(g0), double(g1),
                    double(grip_645), differ);
        CHECK(differ == 0);
        CHECK(std::memcmp(before.bytes.data(), session->params(i).bytes.data(), sim::ParamBlock::kSize) != 0);
        ++changed;
    }
    CHECK(changed > 0);
}

TEST(ai_pits_tire_change_equal_to_the_program) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Car 0 (an S500, kind 4: 3 .. 5) asks for the pits at 20 s and is given kinds 3 front, 5 rear to fit: the program's block
    // after its 645th and 644th steps against ours from the block before them, byte for byte, and the race's bytes.
    setenv("GT4R_AI_PITCHECK", "1", 1);
    setenv("GT4R_AI_PIT", "0,20", 1);
    setenv("GT4R_AI_PITTIRES", "3,5", 1);
    diff::AiRunOptions opt;
    opt.seconds = 190;
    const diff::AiRunResult r = diff::run_ai(*disc, false, "tsukuba", {"s500_63", "ka_01"}, 3, opt);
    unsetenv("GT4R_AI_PITCHECK");
    unsetenv("GT4R_AI_PIT");
    unsetenv("GT4R_AI_PITTIRES");
    CHECK(r.ran);
    CHECK(r.tire_change_calls == 2 && r.tire_change_equal == 2);
    CHECK(r.pit_tire_cars == 1 && r.pit_tire_equal == 1);
    CHECK(r.hold_calls >= 721 && r.hold_equal == r.hold_calls);
    CHECK(r.pit_equal == r.pit_calls && r.pit_stops == 1);
}

TEST(ai_pits_bspec_tire_change_on_our_session) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The B-spec car asked in, the pit menu's choice made before its wait runs out: another kind on both axles. At the stop's
    // 645th step from its end the kinds are fitted and recorded, its tire records rebuilt (the front's paved grip changes),
    // and the car goes on racing.
    db::SpecDb db = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::vector<std::string> labels{"ka_01", "s500_63"};
    auto session = sim::Session::open(*disc, "tsukuba", labels);
    std::vector<size_t> computer{0, 1};
    auto field = sim::ai::Field::open(*disc, "tsukuba", *session, computer);
    field->set_bspec(0);
    sim::ai::PitHooks hooks = sim::ai::session_pit_hooks(*session);
    sim::ai::add_tire_change(hooks, *session, db, program, reader(*disc));
    const std::pair<int, int> fitted = hooks.tires(0);
    const int want = fitted.first == 3 ? 5 : 3;
    sim::ai::PitStops pits(*field, program, 2, hooks);
    std::vector<race::CarInput> in(2);
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) field->hold(in), session->hold_once(in);
    const float grip0 = session->gt4_record(0).tires.tire[0].mu[0].host();
    bool chose = false, changed_at_645 = false;
    float lap_after = 0;
    for (int step = 0; step < 60 * 200; ++step) {
        if (step == 20 * 60) pits.request(0);
        const sim::ai::PitCar& c = pits.car(0);
        if (!chose && (c.service & 0x10) && c.hold == 721) pits.choose(0, u8(want), u8(want), 0xFE), chose = true;
        field->drive(in);
        pits.before(*field, in);
        if (c.hold == 0x285) changed_at_645 = session->gt4_record(0).tires.tire[0].mu[0].host() != grip0;
        session->step_once(in);
        pits.after(*session, *field);
        lap_after = session->cars()[0].course_v;
    }
    const float grip1 = session->gt4_record(0).tires.tire[0].mu[0].host();
    std::printf("  kinds %d / %d -> %d: fitted %d / %d, the stop's record %d / %d, front paved grip %.3f -> %.3f, at %.0f m after\n", fitted.first,
                fitted.second, want, hooks.tires(0).first, hooks.tires(0).second, pits.stop_record(0)[0].front, pits.stop_record(0)[0].rear, double(grip0),
                double(grip1), double(lap_after));
    CHECK(chose && changed_at_645);
    CHECK(hooks.tires(0) == (std::pair<int, int>{want, want}));
    CHECK(pits.stop_record(0)[0].front == want && pits.stop_record(0)[0].rear == want);
    CHECK(grip1 != grip0);
    CHECK(pits.stops(0) == 1 && field->kind(0) == 0);
}
