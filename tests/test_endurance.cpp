// Endurance races: the time limit's finish (DynamicsConductor::vf21 0x357B98, the limit 0x34C140 / 0x34C158), the
// events that carry one (the RACE row's +0x18).

#include "test_framework.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/race_display.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/pit_stops.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstdio>
#include <map>

using namespace gt4r;

TEST(endurance_time_limited_events) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Volume* vol = &disc->volume();
    const auto read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    db::SpecDb db(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const db::RaceEvents events(db, read);
    std::map<std::string, std::pair<int, int>> timed; // label: minutes, laps
    for (const std::string& label : events.labels())
        if (const std::optional<db::RaceEvent> e = events.event(label); e && e->time_limit > 0) {
            timed[label] = {e->time_limit, e->laps};
            std::printf("  %-28s %-20s %5d min  laps %d\n", label.c_str(), e->course.c_str(), e->time_limit, e->laps);
        }
    CHECK(timed.size() == 8);
}

namespace {

constexpr float kLap = 1000;

// Cars moving along a lap at fixed speeds (metres a step), from `behind` metres before the line.
struct Field {
    std::vector<race::CarState> cars;
    std::vector<float> speed, at;
    Field(std::vector<float> speeds, float behind) : speed(std::move(speeds)) {
        cars.resize(speed.size());
        at.assign(speed.size(), -behind);
        for (size_t i = 0; i < cars.size(); ++i) publish(i);
    }
    void publish(size_t i) {
        const float d = at[i];
        cars[i].course_v = std::fmod(std::fmod(d, kLap) + kLap, kLap);
        cars[i].lap = int(std::floor(d / kLap));
    }
    void step() {
        for (size_t i = 0; i < cars.size(); ++i) at[i] += speed[i], publish(i);
    }
};

} // namespace

TEST(endurance_time_limit_finish) {
    // No laps, a limit of 1 minute (3,600 steps): car 1 leads at 2 m a step (a lap every 500 steps), car 0 at 1.6, car 2 at 1.
    std::vector<u8> row(0x97, 0);
    row[0x18] = 1;
    CHECK(race::row_time_limit_ms(row) == 60000);
    row[0x18] = 0xA0, row[0x19] = 0x05; // (1440 minutes: the 24 hours)
    CHECK(race::row_time_limit_ms(row) == 1440LL * 60000);
    CHECK(race::row_time_limit_ms(std::vector<u8>(0x10, 0)) == 0);
    Field f({1.6f, 2.0f, 1.0f}, 2);
    race::FlowSetup s;
    s.start = race::StartType::None, s.laps = 0, s.lap_length = kLap, s.limit_ms = 60000;
    race::RaceFlow flow(s, f.cars);
    race::ResultBoard board;
    board.begin(3, 0, "Lap", "Laps", true);
    std::vector<size_t> order;
    s64 leader_at = -1;
    for (int i = 0; i < 20000 && !flow.everyone_finished(); ++i) {
        f.step();
        const race::RaceFlow::Events ev = flow.step(f.cars);
        for (size_t c : ev.finished) order.push_back(c);
        board.finished(flow, ev.finished);
        if (!ev.finished.empty() && leader_at < 0) leader_at = race::clock_ms(flow.car(order[0]).finish_units);
    }
    // The leader's first crossing at or past 60 s: 7 laps at 500 steps (58.4 s) and the eighth's at 66.7 s.
    CHECK(flow.everyone_finished());
    CHECK(order.size() == 3 && order[0] == 1);
    CHECK(flow.car(1).laps_done == 8 && leader_at >= 60000 && leader_at < 60000 + 500 * 1000 / 60 + 20);
    // The others at their next crossings after it, by laps.
    CHECK(flow.car(0).laps_done == 7 && flow.car(2).laps_done == 4);
    const auto& rows = board.rows();
    CHECK(rows[0].car == 1 && rows[0].time == " 8Laps");
    CHECK(rows[1].car == 0 && rows[1].time == " 7Laps");
    CHECK(rows[2].car == 2 && rows[2].time == " 4Laps");
    // Two on the winner's laps: the second its gap.
    Field g({2.0f, 1.99f}, 2);
    race::RaceFlow flow2(s, g.cars);
    board.begin(2, 0, "Lap", "Laps", true);
    for (int i = 0; i < 20000 && !flow2.everyone_finished(); ++i) {
        g.step();
        const race::RaceFlow::Events ev = flow2.step(g.cars);
        board.finished(flow2, ev.finished);
    }
    CHECK(board.rows()[0].time == " 8Laps" && board.rows()[1].time.rfind("+0'0", 0) == 0);
    // Without a limit the race runs on.
    s.limit_ms = 0;
    Field h({2.0f}, 2);
    race::RaceFlow open(s, h.cars);
    for (int i = 0; i < 20000; ++i) h.step(), open.step(h.cars);
    CHECK(!open.car(0).finished && open.car(0).laps_done > 30);
}

// The single race's display with the total time (0x6A09A8 + 0x6A08F0) in every release's program: the race's time at 550, 58
// and the best lap at 550, 110, no last lap; shown for 10 laps or more or a time limit (RaceDisplay::vf9).
TEST(endurance_total_time_display) {
    CHECK(race::total_time_display(10, 0) && race::total_time_display(0, 60000) && !race::total_time_display(9, 0) && !race::total_time_display(0, 0));
    int ran = 0;
    for (const std::unique_ptr<Disc>& d : test::discs()) {
        if (d->info().game != Game::GT4) continue;
        ++ran;
        const std::shared_ptr<const gt3r::CoreImage> image = d->program_image();
        const race::DisplayLayout l = race::read_total_race_layout(image->program, image->load_address);
        CHECK(l.valid());
        const race::DisplayPlace* total = l.find(race::element::kTotalTime);
        const race::DisplayPlace* best = l.find(race::element::kBestTime);
        CHECK(total && total->x == 550 && total->y == 58 && best && best->y == 110 && !l.find(race::element::kLastTime));
        CHECK(l.find(race::element::kPosition) && l.find(race::element::kLapCount) && l.find(race::element::kMap));
    }
    if (!ran) SKIP("needs a Gran Turismo 4 disc (GT4R_DISC)");
}

// Free Qualify's grid (0x3EE808): by best laps, ties to the earlier entry, no time last.
TEST(endurance_qualify_places) {
    const std::vector<s64> best{65000, 62000, -1, 62000, 70000, 61000};
    CHECK(race::qualify_places(best) == std::vector<int>({3, 1, 5, 2, 4, 0}));
    const std::vector<s64> none{-1, -1, -1};
    CHECK(race::qualify_places(none) == std::vector<int>({0, 1, 2}));
    CHECK(race::qualify_start_distance(0) == -300 && race::qualify_start_distance(5) == -420);
}

// The arcade's "Passing pit lane" penalty: F +0x62E is one byte for the hits' penalty and the pit flow; the session's mark is
// the pit flow's pad visit (0x35FE18 sets it, 0x35B060 drives it), kept the same both ways.
TEST(endurance_pit_lane_penalty_mark) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d;
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::vector<std::string> labels{"s500_63", "ka_01"};
    auto session = sim::Session::open(*disc, "tsukuba", labels);
    std::vector<size_t> computer{0, 1};
    auto field = sim::ai::Field::open(*disc, "tsukuba", *session, computer);
    const sim::Program program = sim::Program::load(*disc);
    sim::ai::PitStops pits(*field, program, 2, sim::ai::session_pit_hooks(*session));
    std::vector<race::CarInput> in(2);
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) field->hold(in), session->hold_once(in);
    auto step = [&] {
        field->drive(in);
        pits.before(*field, in);
        session->step_once(in);
        pits.after(*session, *field);
    };
    step();
    CHECK(session->pit_mark(0) == 0 && pits.car(0).pad_state == 0);
    session->set_pit_mark(0, 1); // (the penalty given)
    step();
    CHECK(pits.car(0).pad_state == 1 && session->pit_mark(0) == 1);
    session->set_pit_mark(0, 0);
    step();
    CHECK(pits.car(0).pad_state == 0);
}
