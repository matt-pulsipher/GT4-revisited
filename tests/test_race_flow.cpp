// The race's flow (start hold, laps, finish, order, results) on made-up cars, and the start's camera
// motions on the courses of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/start_camera.h"

#include <cmath>

using namespace gt4r;

namespace {

constexpr float kLap = 1000;

// Cars moving along a lap at fixed speeds (metres a step), starting `behind` metres before the line.
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

TEST(race_grid_places) {
    const std::vector<int> p = race::grid_places(6, 0);
    CHECK(p == std::vector<int>({5, 0, 1, 2, 3, 4}));
    CHECK(race::grid_places(1, 0) == std::vector<int>({0}));
    CHECK(race::grid_places(3, 1) == std::vector<int>({0, 2, 1}));
    CHECK(race::start_hold_steps(race::StartType::Ten, 0) == 600);
    CHECK(race::start_hold_steps(race::StartType::Eight, 0) == 480);
    CHECK(race::start_hold_steps(race::StartType::Given, 3) == 180);
    CHECK(race::start_hold_steps(race::StartType::Rolling, 5) == 0);
    CHECK(race::start_hold_steps(race::StartType::None, 5) == 0);
}

TEST(race_flow_standing_start) {
    // Car 0 (the player) is the slowest; car 1 the fastest.
    Field f({1.0f, 2.0f, 1.5f}, 10);
    race::FlowSetup s;
    s.start = race::StartType::Ten, s.laps = 2, s.lap_length = kLap;
    race::RaceFlow flow(s, f.cars);
    CHECK(flow.holding() && flow.hold_left() == 600);
    // The hold: three beeps (180, 120 and 60 steps before the start), then the start; the clock stands.
    int beeps = 0, starts = 0, beep_at[3] = {};
    for (int i = 0; i < 600; ++i) {
        const race::RaceFlow::Events ev = flow.step(f.cars);
        if (ev.beep && beeps < 3) beep_at[beeps] = flow.hold_left();
        beeps += ev.beep, starts += ev.start;
    }
    CHECK(beeps == 3 && starts == 1 && !flow.holding() && flow.clock() == 0);
    CHECK(beep_at[0] == 180 && beep_at[1] == 120 && beep_at[2] == 60);
    CHECK(flow.lap_shown(0) == 1);
    // The race: car 1 drives 2 laps first and finishes; the others finish as they next cross the line.
    std::vector<size_t> finish_order;
    int laps_done_by_1 = 0;
    for (int i = 0; i < 4000 && !flow.everyone_finished(); ++i) {
        f.step();
        const race::RaceFlow::Events ev = flow.step(f.cars);
        for (size_t c : ev.finished) finish_order.push_back(c);
        laps_done_by_1 += ev.lap_done == 1;
    }
    CHECK(flow.everyone_finished());
    CHECK(finish_order == std::vector<size_t>({1, 0, 2}));
    // Car 1: 2010 m at 2 m a step = 1005 steps; the first lap from the start signal (1010 m incl. the
    // run to the line: 505 steps), the second 500 steps.
    const race::RaceFlow::CarRecord& r1 = flow.car(1);
    CHECK(r1.laps_done == 2);
    CHECK(std::llabs(r1.finish_units - 1005 * race::kClockPerStep) <= race::kClockPerStep);
    CHECK(std::llabs(r1.best_lap_ms - 500 * 1000 / 60) <= 17);
    // Car 0 crossed the line just after the leader finished: it finished there, a lap down; car 2
    // finished its second lap later and is placed ahead of it.
    CHECK(flow.car(0).finished && flow.car(0).laps_done == 1 && flow.car(2).laps_done == 2);
    const std::vector<race::RaceFlow::ResultRow> rows = flow.results();
    CHECK(rows.size() == 3 && rows[0].car == 1 && rows[2].car == 0 && rows[0].total_ms < rows[1].total_ms);
    CHECK(flow.place(0) == 3 && flow.place(1) == 1);
}

TEST(race_flow_leader_ends_the_race) {
    // A car a lap down finishes at its next crossing once the leader has finished.
    Field f({3.0f, 1.0f}, 5);
    race::FlowSetup s;
    s.start = race::StartType::None, s.laps = 3, s.lap_length = kLap;
    race::RaceFlow flow(s, f.cars);
    CHECK(!flow.holding() && flow.steps_since_start() == 0);
    for (int i = 0; i < 5000 && !flow.everyone_finished(); ++i) f.step(), flow.step(f.cars);
    CHECK(flow.car(0).finished && flow.car(0).laps_done == 3);
    CHECK(flow.car(1).finished && flow.car(1).laps_done == 1);
    const auto rows = flow.results();
    CHECK(rows[0].car == 0 && rows[1].car == 1);
}

TEST(race_flow_crossing_time) {
    // A crossing between two steps is timed at its share of the step.
    Field f({4.0f}, 2); // over the line half-way through the first step
    race::FlowSetup s;
    s.start = race::StartType::None, s.laps = 1, s.lap_length = kLap;
    race::RaceFlow flow(s, f.cars);
    for (int i = 0; i < 400 && !flow.player_finished(); ++i) f.step(), flow.step(f.cars);
    // 1002 m at 4 m a step: 250.5 steps.
    CHECK(flow.player_finished());
    CHECK(flow.car(0).finish_units == 250 * race::kClockPerStep + race::kClockPerStep / 2);
    // A rolling start: the start signal is the player's first crossing.
    Field g({4.0f}, 6);
    s.start = race::StartType::Rolling;
    race::RaceFlow rolling(s, g.cars);
    bool started = false;
    for (int i = 0; i < 3; ++i) g.step(), started |= rolling.step(g.cars).start;
    CHECK(started && rolling.steps_since_start() >= 0);
}

TEST(race_start_motion_curves) {
    race::MotionChannel c;
    c.first = 0;
    c.pieces = {{1, 2, 1, 3}, {3, 3, 2, 3}};
    CHECK(std::fabs(c.at(0) - 0) < 1e-5f && std::fabs(c.at(1) - 3) < 1e-5f);
    CHECK(std::fabs(c.at(0.5f) - 1.5f) < 1e-5f); // controls on the straight: linear
    CHECK(std::fabs(c.at(1.5f) - 3) < 1e-5f && std::fabs(c.at(9) - 3) < 1e-5f);
}

TEST(race_start_motion_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t courses = 0;
    for (const auto& disc : test::discs()) {
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 4, "crs/") != 0 || e.path.find('/', 4) != std::string::npos || e.path.find('.') != std::string::npos) continue;
            const Bytes data = disc->volume().read(e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const ByteSpan a = file.section(data, race::kSlotStartMotion), b = file.section(data, race::kSlotReplayStartMotion);
            if (!race::CameraMotion::is_motion(a) || !race::CameraMotion::is_motion(b)) continue;
            ++courses;
            const race::CameraMotion start = race::CameraMotion::parse(a), replay = race::CameraMotion::parse(b);
            // The race's: a 7 s clip, then two of a second; the replay's: 7 s and four of a second.
            CHECK(start.clips.size() == 3 && replay.clips.size() == 5);
            if (start.clips.size() != 3) continue;
            CHECK(start.clips[0].name.rfind("start_jingle", 0) == 0 && start.clips[1].name == "start_countdown_B1" && start.clips[2].name == "start_countdown_A1");
            CHECK(std::fabs(start.clips[0].length - 7) < 0.01f);
            float sum = 0;
            for (const race::MotionClip& c : start.clips) {
                sum += c.length;
                CHECK(c.channels.size() == 10);
                const race::MotionPose p = race::motion_pose(c, c.length * 0.5f);
                const float up = std::sqrt(p.up.x * p.up.x + p.up.y * p.up.y + p.up.z * p.up.z);
                CHECK(std::fabs(up - 1) < 0.05f && p.fov > 0.2f && p.fov < 1.6f);
            }
            CHECK(std::fabs(sum - start.total) < 0.01f);
            // The camera: 9 s of a 10 s hold; the long clip at the grid, then the two short ones.
            race::StartCamera cam;
            race::CarState car;
            car.forward = {0, 0, 1};
            course::Runway::Place grid;
            cam.begin(&start, false, 600, grid, car);
            int steps = 0, clips[3] = {};
            race::ViewPose pose;
            while (cam.active()) {
                CHECK(cam.pose(pose));
                ++clips[std::clamp(cam.clip(), 0, 2)];
                cam.step(), ++steps;
            }
            CHECK(steps == 540 && std::abs(clips[0] - 420) <= 1 && clips[1] == 60 && std::abs(clips[2] - 60) <= 1);
        }
    }
    if (!courses) SKIP("no Gran Turismo 4 disc");
    CHECK(courses >= 20);
}
