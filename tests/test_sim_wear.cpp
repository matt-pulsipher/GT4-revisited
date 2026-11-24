// The tire wear: the program's routines against ours at every call, the events' wear rules, and the wear of a long race on
// our session.

#include "test_framework.h"

#include "diff.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_wear.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace gt4r;

namespace {

Disc* wear_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

db::SpecDb::Reader wear_reader(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
}

// The wear rules a race of the event runs with (0x35E108's bytes, then 0x367C90).
sim::rules::WearRules event_rules(const db::RaceEvent& e, const sim::Program& program) {
    u8 block[0x80] = {};
    sim::rules::event_wear_bytes(e.bytes.data(), block);
    const auto b = sim::rules::wear_rules_block(block, true, program);
    return sim::rules::wear_rules(b.data());
}

} // namespace

TEST(sim_program_wear_equal) {
    Disc* disc = wear_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The tread wear's curve in the rule block (its switch on) and both wears' made-up rules with spread wears, so the
    // sliding measures, the tire wear and the wear's grip run every branch.
    ::setenv("GT4R_STEP_TREAD", "1", 1);
    ::setenv("GT4R_STEP_WEAR", "7", 1);
    for (const auto& [car, course] : {std::pair{"nsx_r_02", "tsukuba"}, std::pair{"r34_gtr_v2_nur_02", "tsukuba"}, std::pair{"focus_wrc_99", "grandcanyon"}}) {
        const diff::StepResult r = diff::run_step(*disc, false, course, car, 900);
        if (!r.ran) std::printf("  %s: %s\n", car, r.error.c_str());
        CHECK(r.ran);
        for (const diff::StepTap& t : r.taps) {
            if (t.name != "sliding measures" && t.name != "tire wear" && t.name != "wear grip" && t.name != "sliding measures' tables (ours)") continue;
            if (t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", car, t.name.c_str(), t.equal, t.calls);
            CHECK(t.calls > 0 && t.equal == t.calls);
        }
    }
    ::unsetenv("GT4R_STEP_TREAD");
    ::unsetenv("GT4R_STEP_WEAR");
}

TEST(sim_disc_event_wear_rules) {
    Disc* disc = wear_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    const db::RaceEvents ev(sdb, wear_reader(*disc));
    // The original's Family Cup at Tsukuba and Tuning Car Grand Prix race 1 (its saved states' conductor + 4): new tires at
    // -10,000 losing 5 % of their grip, full grip from 0 to 40,000, then down 5 % to 50,000 and to 40 % at 80,000; new tires
    // wanted under 88 %; the sliding measure at most 1, a driven or braked wheel's slip doubling it.
    for (const char* label : {"family", "pr_tuning_0500"}) {
        const std::optional<db::RaceEvent> e = ev.event(label);
        CHECK(e.has_value());
        if (!e) continue;
        const sim::rules::WearRules k = event_rules(*e, program);
        CHECK(k.tread_on == 1 && k.second_on == 0);
        CHECK(k.tread_neg.host() == -10000.0f && std::fabs(k.tread_neg_loss.host() - 0.05f) < 1e-6f);
        CHECK(k.tread_knee1.host() == 40000.0f && k.tread_loss1.host() == 0.0f);
        CHECK(k.tread_knee2.host() == 50000.0f && std::fabs(k.tread_loss2.host() - 0.05f) < 1e-6f);
        CHECK(k.tread_max.host() == 80000.0f && std::fabs(k.tread_max_loss.host() - 0.4f) < 1e-6f);
        CHECK(std::fabs(k.tread_pit.host() - 0.88f) < 1e-6f && k.tread_cap.host() == 1.0f && k.tread_skid.host() == 1.0f);
    }
    // Practice and free runs have none.
    for (const char* label : {"practice", "freerun"}) {
        const std::optional<db::RaceEvent> e = ev.event(label);
        if (e) CHECK(event_rules(*e, program).tread_on == 0);
    }
}

TEST(sim_disc_long_race_wear) {
    Disc* disc = wear_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    const db::RaceEvents ev(sdb, wear_reader(*disc));
    const std::optional<db::RaceEvent> e = ev.event("pr_tuning_0500");
    CHECK(e.has_value());
    if (!e) return;
    u8 block[0x80] = {};
    sim::rules::event_wear_bytes(e->bytes.data(), block);
    sim::ai::FieldCar car;
    car.name = "r34_gtr_v2_nur_02";
    CHECK(sim::stock_car(sdb, program, car.name, car.entry.equipment));
    // Tuning Car Grand Prix race 1 (Apricot Hill Reverse, 3 laps) for the R34 alone, our session with the computer driver.
    bool set = false;
    std::array<float, 4> start{}, end{};
    float grip_start = 0, grip_end = 0;
    const sim::ai::FieldRun run = sim::ai::run_field(
        *disc, "r_apricot", std::vector<sim::ai::FieldCar>{car}, 3, {}, 0,
        [&](const sim::Session& s, const sim::ai::Field&) {
            for (int w = 0; w < 4; ++w) end[size_t(w)] = s.tread(0, w);
            grip_end = s.wear_grip(0, 0);
        },
        [&](sim::Session& s, sim::ai::Field&) {
            if (set) return;
            s.set_wear_rules(block, !s.loose(), program);
            for (int w = 0; w < 4; ++w) start[size_t(w)] = s.tread(0, w);
            grip_start = s.wear_grip(0, 0);
            set = true;
        });
    CHECK(set && !run.cars.empty() && run.cars[0].finished);
    std::printf("  R34 at Apricot Hill Reverse, 3 laps (%.1f s): tread %.0f %.0f %.0f %.0f from %.0f; grip %.4f from %.4f\n", run.seconds,
                double(end[0]), double(end[1]), double(end[2]), double(end[3]), double(start[0]), double(grip_end), double(grip_start));
    // New tires at the rules' least, their grip 95 %; the original's R34 in this race (B-spec, Push) ended its third lap
    // with the treads at 6,231 / 3,886 / 6,184 / 3,621 (the left tires wearing more): each of ours within 20 % of the wear,
    // and the grip back to 1 (scrubbed in). (Ours: 5,948 / 3,213 / 5,699 / 2,988.)
    const float original[4] = {6231.0f, 3886.0f, 6184.0f, 3621.0f};
    for (int w = 0; w < 4; ++w) {
        CHECK(start[size_t(w)] == -10000.0f);
        const float worn = end[size_t(w)] - start[size_t(w)], want = original[w] + 10000.0f;
        CHECK(std::fabs(worn - want) < want * 0.2f);
    }
    CHECK(end[0] > end[1] && end[2] > end[3]);
    CHECK(std::fabs(grip_start - 0.95f) < 1e-4f && grip_end > 0.999f);
}
