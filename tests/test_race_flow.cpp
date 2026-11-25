// The race's flow (start hold, laps, finish, order, results) on made-up cars, and the start's camera
// motions on the courses of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/car_pose.h"
#include "gt4r/race/drive_camera.h"
#include "gt4r/race/start_camera.h"
#include "gt4r/race/start_rule.h"
#include "gt4r/race/replay_script.h"
#include "gt4r/race/replay_file.h"
#include "gt4r/race/gadgets.h"
#include "gt4r/race/gadget_bodies.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/race/picture_settings.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/menu/race_natives.h"

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
    CHECK(beeps == 3 && starts == 1 && !flow.holding() && flow.clock() == race::kClockPerStep); // (the step that counts the hold to 0 is the race's first)
    CHECK(beep_at[0] == 180 && beep_at[1] == 120 && beep_at[2] == 60);
    CHECK(flow.lap_shown(0) == 1);
    // The race: car 1 drives 2 laps first and finishes; the others finish as they next cross the line.
    std::vector<size_t> finish_order;
    for (int i = 0; i < 4000 && !flow.everyone_finished(); ++i) {
        f.step();
        const race::RaceFlow::Events ev = flow.step(f.cars);
        for (size_t c : ev.finished) finish_order.push_back(c);
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

TEST(race_flow_rolling_clock) {
    // A rolling start: the clock stands until the first car takes the line, then runs; the player's own
    // crossing is his start signal, later; his first lap is timed from the clock's start.
    Field f({4.0f, 4.0f}, 10);
    f.at[1] = -2, f.publish(1); // car 1 ahead, 8 m before the player
    race::FlowSetup s;
    s.start = race::StartType::Rolling, s.laps = 2, s.lap_length = kLap;
    race::RaceFlow flow(s, f.cars);
    CHECK(!flow.clock_running());
    int clock_step = -1, start_step = -1;
    for (int i = 0; i < 10; ++i) {
        f.step();
        const race::RaceFlow::Events ev = flow.step(f.cars);
        if (ev.clock_started) clock_step = i;
        if (ev.start) start_step = i;
    }
    CHECK(clock_step == 0 && start_step == 2);
    CHECK(flow.clock_running() && flow.clock() == 10 * race::kClockPerStep); // (from the step of the crossing)
    CHECK(flow.lap_ms(0) == race::clock_ms(flow.clock()));
}

TEST(race_result_board) {
    // Three cars, the player (0) the slowest; the page up when the leader finishes.
    Field f({2.0f, 3.0f, 2.5f}, 5);
    race::FlowSetup s;
    s.start = race::StartType::None, s.laps = 1, s.lap_length = kLap;
    race::RaceFlow flow(s, f.cars);
    race::ResultBoard board;
    board.begin(3, 0, "Lap", "Laps");
    CHECK(board.rows().size() == 3 && board.rows()[0].car == -1);
    bool page = false;
    for (int i = 0; i < 2000 && !flow.everyone_finished(); ++i) {
        f.step();
        const race::RaceFlow::Events ev = flow.step(f.cars);
        if (!ev.finished.empty()) board.finished(flow, ev.finished), page = true;
        if (page) {
            board.follow(flow);
            // While car 2 and 0 run, their rows follow the order with no time.
            if (!flow.car(2).finished) {
                CHECK(board.rows()[1].car == 2 && board.rows()[1].time.empty() && !board.rows()[1].fixed);
                CHECK(board.rows()[2].car == 0 && board.player_row() == 2);
            }
        }
    }
    CHECK(board.all_fixed());
    const auto& r = board.rows();
    CHECK(r[0].car == 1 && r[1].car == 2 && r[2].car == 0);
    // The winner's whole time (" M'SS.mmm"), the others' gaps ("+M'SS.mmm").
    CHECK(r[0].time == race::result_time(race::clock_ms(flow.car(1).finish_units), false));
    CHECK(r[0].time[0] == ' ' && r[1].time[0] == '+' && r[2].time[0] == '+');
    CHECK(r[2].time == race::result_time(race::clock_ms(flow.car(0).finish_units) - race::clock_ms(flow.car(1).finish_units), true));
    // The changed mark is taken once.
    CHECK(board.take_changed(0) && !board.take_changed(0));
    CHECK(race::result_time(184462, false) == " 3'04.462" && race::result_time(491, true) == "+0'00.491");
    // A car a lap down: "+1Lap".
    Field g({3.0f, 1.0f}, 5);
    s.laps = 3;
    race::RaceFlow f2(s, g.cars);
    race::ResultBoard b2;
    b2.begin(2, 0, "Lap", "Laps");
    for (int i = 0; i < 5000 && !f2.everyone_finished(); ++i) {
        g.step();
        const auto ev = f2.step(g.cars);
        if (!ev.finished.empty()) b2.finished(f2, ev.finished);
    }
    CHECK(b2.rows()[1].time == "+2Laps");
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

// --- The race's movies (STRB): the pause menu played with its scripts ---

#include "gt4r/race/strobe.h"

TEST(strobe_pause_movie_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t movies = 0;
    for (const auto& disc : test::discs()) {
        const gt3r::Volume::Entry* e = disc->volume().find("menu/common/pause.strb");
        if (!e) continue;
        ++movies;
        auto movie = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(disc->volume().read(*e)));
        CHECK(movie->fps == 30 && movie->width == 640 && movie->height == 480);
        CHECK(movie->sprites.size() >= 20 && !movie->textures.empty() && !movie->fonts.empty());
        CHECK(movie->sprites[0].label("menu_normal") >= 0 && movie->sprites[0].label("CONFIRM") >= 0);
        size_t triangles = 0;
        for (const auto& s : movie->shapes)
            for (const auto& g : s.groups) triangles += g.triangles.size();
        CHECK(triangles > 0 && triangles % 3 == 0);
        // The single race's pause: no retry, no confirmation, the screen settings.
        for (int retry = 0; retry < 2; ++retry) {
            std::vector<std::string> calls;
            std::string exit;
            race::StrobePlayer* self = nullptr;
            auto host = [&](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
                using V = race::StrobePlayer::Value;
                std::string n = name;
                for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
                calls.push_back(n);
                if (n == "RETRYENABLE") return V::number(retry);
                if (n == "SCREENSETTINGENABLE") return V::number(1);
                if (n == "CONFIRMEXIT") return V::number(0);
                if (n == "EXIT_MENU" && !args.empty()) exit = args[0].to_string();
                if (n == "NAVIGATEVERTICAL" && self) self->navigate(true, false);
                if (n == "NAVIGATEALL" && self) self->navigate(true, true);
                if (n == "DEFAULTVAL") return V::number(2);
                return V{};
            };
            race::StrobePlayer p(movie, host);
            self = &p;
            p.restart();
            p.advance(10.0f / 30);
            CHECK(p.state() == (retry ? "MENU_RETRY" : "MENU_NORMAL"));
            CHECK(p.focus() == "DEFAULT");
            const size_t drawn = p.draw_list().size();
            CHECK(drawn > 10);
            // Down to EXIT (the second row), accept: the row's animation, then EXIT_MENU("exit").
            p.key(0x28);
            if (retry) p.key(0x28);
            CHECK(p.focus() != "DEFAULT");
            p.key(0x20);
            for (int i = 0; i < 20 && exit.empty(); ++i) p.advance(1.0f / 30);
            CHECK(exit == "exit");
            if (!retry) {
                // Screen Setting: its page, the brightness slider from its stored value, right steps it.
                exit.clear();
                p.restart();
                p.advance(10.0f / 30);
                p.key(0x28), p.key(0x28), p.key(0x20);
                for (int i = 0; i < 20; ++i) p.advance(1.0f / 30);
                CHECK(p.state() == "SCREEN_SETTING");
                CHECK(p.variable("BRIGHTNESS.v").to_number() == 2);
                p.key(0x27);
                CHECK(p.variable("BRIGHTNESS.v").to_number() == 3);
                CHECK(std::find(calls.begin(), calls.end(), "SETVAL") != calls.end());
            }
        }
    }
    if (!movies) SKIP("no Gran Turismo 4 disc");
}

TEST(strobe_result_movie_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t movies = 0;
    for (const auto& disc : test::discs()) {
        const gt3r::Volume::Entry* e = disc->volume().find("menu/common/result.strb");
        if (!e) continue;
        ++movies;
        auto movie = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(disc->volume().read(*e)));
        bool exit = false;
        // Six rows; the first three finished.
        auto host = [&](const std::string& name, const std::vector<race::StrobePlayer::Value>& args) {
            using V = race::StrobePlayer::Value;
            std::string n = name;
            for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
            const int i = args.empty() ? -1 : int(args[0].to_number());
            if (n == "CB_ISVISIBLE") return V::boolean(i >= 0 && i < 6);
            if (n == "CB_STABLE") return V::boolean(i >= 0 && i < 3);
            if (n == "CB_PLAYERRANK") return V::number(1);
            if (n == "CB_GETNAME") return V::string("Car " + std::to_string(i + 1));
            if (n == "CB_GETTIME") return V::string("1'0" + std::to_string(i) + ".000");
            if (n == "CB_ENABLEEXIT") return V::number(1);
            if (n == "CB_RACEEXIT") exit = true;
            return V::number(0);
        };
        race::StrobePlayer p(movie, host);
        p.play();
        p.advance(2.0f);
        std::vector<std::string> words;
        for (const race::StrobePlayer::Draw& d : p.draw_list())
            if (d.text) words.push_back(d.words);
        if (std::getenv("GT4R_TEST_VERBOSE")) {
            for (const std::string& w : words) std::printf("  text [%s]\n", w.c_str());
        }
        CHECK(std::find(words.begin(), words.end(), "Car 1") != words.end());
        CHECK(std::find(words.begin(), words.end(), "1'02.000") != words.end());
        CHECK(std::find(words.begin(), words.end(), "Car 5") == words.end()); // not finished: its row not filled
        // The car icons are the program's pictures (kind 7), one per visible row, not shapes.
        size_t icons = 0;
        for (const race::StrobePlayer::Draw& d : p.draw_list())
            if (d.external.rfind("_EXT_CARICON", 0) == 0) ++icons;
        CHECK(icons == 6);
        // Accept leaves the page.
        p.key(0x20);
        for (int i = 0; i < 90 && !exit; ++i) p.advance(1.0f / 30);
        CHECK(exit);
    }
    if (!movies) SKIP("no Gran Turismo 4 disc");
}

TEST(race_course_start_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    CHECK(race::start_code_digit("1ff0", 0) == 1 && race::start_code_digit("1ff0", 1) == 15 && race::start_code_digit("x", 0) == -1);
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        gt3r::Volume* vol = &disc->volume();
        db::SpecDb sdb(
            [vol](const std::string& path) -> std::optional<Bytes> {
                const gt3r::Volume::Entry* e = vol->find(path);
                return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
            },
            [vol](const std::string& dir) { return vol->list_dir(dir); });
        // The original's arcade races: Tsukuba and Route 5 start rolling at 50 mph, Grand Canyon from a
        // standing count.
        const race::CourseStart ts = race::course_start(sdb, "tsukuba"), r5 = race::course_start(sdb, "route5"), gc = race::course_start(sdb, "grandcanyon");
        CHECK(ts.known && ts.rolling && ts.code == "1ff0");
        CHECK(r5.known && r5.rolling);
        CHECK(gc.known && !gc.rolling && gc.code == "0000");
        CHECK(std::fabs(ts.speed() * 2.23694f - 49.7f) < 0.1f); // 80 km/h
        CHECK(!race::course_start(sdb, "no_such_course").known);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}

TEST(race_picture_settings) {
    race::PictureSettings p;
    CHECK(p.v[0] == 3 && p.v[1] == 0 && p.v[2] == 0 && p.v[3] == 0);
    // Brightness +3: a gain of 2^0.3 on every channel.
    race::PictureSettings::Correction c = p.correction();
    CHECK(std::fabs(c.gain[1] - std::pow(2.0f, 0.3f)) < 1e-5f && c.gain[0] == c.gain[1] && c.gain[2] == c.gain[1]);
    CHECK(c.contrast == 1 && c.saturation == 1);
    auto g = p.grade();
    CHECK(g.on && std::fabs(g.white - 1 / std::pow(2.0f, 0.3f)) < 1e-5f && std::fabs(g.black) < 1e-6f);
    // All zero: no correction.
    race::PictureSettings z{race::PictureSettings::kReplayDefault};
    CHECK(!z.grade().on);
    // Balance +5: 6500 K; red down, blue up by 0.1. Contrast +5: the term 0.7. Saturation -5: 0.5.
    race::PictureSettings q;
    q.v = {0, 5, -5, 5};
    c = q.correction();
    CHECK(std::fabs(c.gain[0] - 0.9f) < 1e-5f && std::fabs(c.gain[2] - 1.1f) < 1e-5f);
    CHECK(std::fabs(c.contrast - 0.7f) < 1e-5f && std::fabs(c.saturation - 0.5f) < 1e-5f);
    g = q.grade();
    CHECK(std::fabs(g.black - 0.15f) < 1e-5f && std::fabs(g.white - 0.85f) < 1e-5f);
}

TEST(replay_camera_springs) {
    // A spring settles on its target; the lens spring may fall 150 degrees a second, rise 100.
    race::CameraSpring s{100, 20, 0, -150, 100};
    s.reset(30);
    for (int i = 0; i < 600; ++i) s.step(10);
    CHECK(std::fabs(s.x - 10) < 1e-3f);
    // The slew: the wanted value moves at most 2 a second.
    race::CameraSpring o{70, 20, 2, -150, 150};
    o.reset(0);
    o.step(1);
    CHECK(std::fabs(o.want - 2.0f / 60) < 1e-6f);
    // The aim damper: starts on its target, follows a turning target a frame behind, wraps at 360.
    race::AimDamper d;
    d.reset(350, 7);
    CHECK(d.value() == 350);
    float target = 350;
    for (int i = 0; i < 240; ++i) {
        target = std::fmod(target + 0.5f, 360.0f);
        d.step(target, 2.0f); // follow over 1: no noise
    }
    float err = std::fabs(d.value() - target);
    err = std::min(err, 360 - err);
    CHECK(err < 1.0f);
    // A follow of 0 holds the aim where it is.
    race::AimDamper h;
    h.reset(100, 3);
    for (int i = 0; i < 60; ++i) h.step(140, 0.0f);
    CHECK(std::fabs(h.value() - 100) < 1e-3f);
}

TEST(race_replay_file) {
    race::ReplayFile f;
    f.course = "tsukuba", f.sequence = "single_race", f.race = "", f.laps = 2;
    f.cars = {{"_117_coupe_68", 3}, {"nsx_r_02", 0}};
    for (int s = 0; s < 120; ++s) {
        std::vector<race::CarState> st(2);
        for (int c = 0; c < 2; ++c) {
            st[size_t(c)].position = {float(s) * 0.5f, 1, float(c)};
            st[size_t(c)].speed = float(s), st[size_t(c)].gear = c + 1, st[size_t(c)].lap = s / 60;
            st[size_t(c)].wheels[2].spin = float(s) * 0.1f, st[size_t(c)].wheels[3].surface = 7;
            st[size_t(c)].brake_lamps = s % 2;
        }
        f.steps.push_back(st);
    }
    const Bytes b = f.encode();
    CHECK(race::ReplayFile::is_replay(b));
    const race::ReplayFile g = race::ReplayFile::decode(b);
    CHECK(g.course == "tsukuba" && g.sequence == "single_race" && g.laps == 2 && g.cars.size() == 2 && g.cars[0].paint == 3 && g.cars[1].label == "nsx_r_02");
    CHECK(g.steps.size() == 120);
    CHECK(g.steps[119][1].position.x == 59.5f && g.steps[119][1].gear == 2 && g.steps[119][0].lap == 1 && g.steps[61][0].brake_lamps);
    CHECK(g.steps[50][0].wheels[2].spin == f.steps[50][0].wheels[2].spin && g.steps[50][1].wheels[3].surface == 7);
    // A cut file.
    bool threw = false;
    try {
        race::ReplayFile::decode(ByteSpan(b).first(b.size() / 2));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(race_gadgets_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0, courses = 0, placed = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 4, "crs/") != 0 || e.path.find('/', 4) != std::string::npos || e.path.find('.') != std::string::npos) continue;
            const Bytes data = disc->volume().read(e);
            if (!course::CourseFile::looks_like_course(data)) continue;
            const course::CourseFile file = course::CourseFile::parse(data);
            const race::Gadgets g = race::Gadgets::parse(file.section(data, course::kSlotGadgets), file.section(data, course::kSlotRunway));
            ++courses, placed += g.placements.size();
            for (const race::Gadgets::Placement& p : g.placements) CHECK(p.kind >= 0 && size_t(p.kind) < std::max<size_t>(g.kinds.size(), 1));
            // Every course with gadgets has their models, where the header's +0x10 says.
            if (!g.kinds.empty() && !g.placements.empty()) CHECK(!g.models.empty());
            if (e.path == "crs/testcourse_100m") {
                // The stopping box's row of cones: one kind, its models at +0xC0 (after the single kind).
                CHECK(g.kinds.size() == 1 && g.placements.size() == 12 && !g.models.empty());
            }
            if (e.path == "crs/tsukuba") {
                // Tsukuba: 18 cones along its pit lane's ends; six kinds, the cone's a five-point hull.
                CHECK(g.kinds.size() == 6 && g.placements.size() == 18);
                CHECK(g.kinds[5].hull_points == 5 && std::fabs(g.kinds[5].radius - 0.593f) < 1e-3f && g.kinds[5].mass == 15);
                CHECK(g.placements[0].kind == 5 && std::fabs(g.placements[0].position.x + 243.2f) < 0.1f);
                CHECK(!g.models.empty());
                ++ran;
            }
        }
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
    CHECK(courses > 100 && placed > 18);
}

TEST(race_next_license_test) {
    // NEXT_RACE in the license sequence (0x10FCD0): the next number, the coffee break test after test 8,
    // the license's test 9 after its coffee break test, none after test 16.
    CHECK(menu::next_license_test("l0b0001") == "l0b0002");
    CHECK(menu::next_license_test("usl0a0015") == "usl0a0016");
    CHECK(menu::next_license_test("l0b0016").empty());
    CHECK(menu::next_license_test("l0b0008") == "l0c0001");
    CHECK(menu::next_license_test("lia0008") == "l0c0004");
    CHECK(menu::next_license_test("usl0s0008") == "usl0c0005");
    CHECK(menu::next_license_test("l0c0003") == "lib0009");
    CHECK(menu::next_license_test("l0c0005") == "l0s0009");
    CHECK(menu::next_license_test("l0c0006").empty());
    CHECK(menu::next_license_test("am_sunday_0001").empty());
    CHECK(menu::next_license_test("usl0m0025").empty()); // a mission
}

TEST(race_frame_blend) {
    // A frame between two steps: the car halfway, its axes square; the camera halfway, a cut not blended.
    race::CarState a, b;
    a.position = {0, 0, 0}, b.position = {2, 0, 4};
    const float c = std::cos(0.2f), s = std::sin(0.2f);
    b.forward = {s, 0, c}, b.right = {c, 0, -s};
    a.wheels[0].spin = 1, b.wheels[0].spin = 3;
    const race::CarState m = race::blend_states(a, b, 0.5f);
    CHECK(std::fabs(m.position.x - 1) < 1e-5f && std::fabs(m.position.z - 2) < 1e-5f && std::fabs(m.wheels[0].spin - 2) < 1e-5f);
    CHECK(std::fabs(m.forward.x * m.forward.x + m.forward.z * m.forward.z - 1) < 1e-4f && std::fabs(m.forward.x * m.right.x + m.forward.z * m.right.z) < 1e-4f);
    CHECK(m.right.x > 0.9f && m.up.y > 0.99f);
    race::ViewPose p, q;
    p.view = gt3r::render::Mat4::identity(), q.view = gt3r::render::Mat4::identity();
    p.eye = {0, 1, 0}, q.eye = {1, 1, 0};
    q.view.at(0, 3) = -1; // the eye at x 1
    const race::ViewPose h = race::blend_poses(p, q, 0.5f);
    CHECK(std::fabs(h.eye.x - 0.5f) < 1e-5f && std::fabs(h.view.at(0, 3) + 0.5f) < 1e-5f && std::fabs(h.view.at(0, 0) - 1) < 1e-5f);
    q.eye = {10, 1, 0};
    CHECK(race::blend_poses(p, q, 0.5f).eye.x == 10); // a cut
}

TEST(race_gadget_bodies_disc) {
    // A car driven through Tsukuba's first cone at 15 m/s knocks it away (into the pit wall beside it);
    // it settles on the floor (the program's rest rule seldom puts a knocked body back to rest: it stays
    // awake, all but still); the cones left alone rest where they stand.
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    bool ran = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("crs/tsukuba");
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const course::CourseFile file = course::CourseFile::parse(data);
        const std::optional<course::Runway> road = course::course_road(file, data);
        const race::Gadgets g = race::Gadgets::parse(file.section(data, course::kSlotGadgets), file.section(data, course::kSlotRunway));
        if (!road || g.placements.size() < 2) continue;
        ran = true;
        race::GadgetBodies bodies;
        bodies.reset(g, *road);
        const course::Vec3 cone = g.placements[0].position;
        race::GadgetBodies::Car car;
        car.forward = {1, 0, 0}, car.right = {0, 0, -1}, car.vel = {15, 0, 0};
        // (The car's center half a metre over the cone's floor: Tsukuba's pit lane is not in the road file.)
        const float h = cone.y - 0.11f + 0.5f;
        car.pos = {cone.x - 5, h, cone.z};
        unsigned hits = 0;
        for (int i = 0; i < 60; ++i) {
            hits |= bodies.step(g, *road, std::span<const race::GadgetBodies::Car>(&car, 1), 1.0f / 60);
            car.pos.x += car.vel.x / 60;
        }
        const race::GadgetBodies::Body& b = bodies.bodies()[0];
        CHECK(hits == 1 && bodies.moved());
        CHECK(b.removed || std::hypot(b.pos.x - cone.x, b.pos.z - cone.z) > 0.5f);
        for (int i = 0; i < 600; ++i) bodies.step(g, *road, {}, 1.0f / 60);
        CHECK(b.removed || (std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y + b.vel.z * b.vel.z) < 0.5f && std::fabs(b.pos.y - b.floor) < 0.6f));
        const race::GadgetBodies::Body& other = bodies.bodies().back();
        CHECK(!other.awake && std::fabs(other.pos.x - g.placements.back().position.x) < 1e-4f);
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}

TEST(race_license_stop_box_disc) {
    // B-1's road (the 100 m test course) has its stopping box: triangles flagged bit 2, past the line.
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    bool ran = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        const gt3r::Volume::Entry* e = disc->volume().find("crs/testcourse_100m");
        if (!e) continue;
        const Bytes data = disc->volume().read(*e);
        const std::optional<course::Runway> road = course::course_road(course::CourseFile::parse(data), data);
        if (!road) continue;
        ran = true;
        size_t box = 0;
        for (const course::Runway::Triangle& t : road->triangles) box += (t.kind & 4) != 0;
        std::printf("  testcourse_100m: %zu of %zu triangles in the stopping box\n", box, road->triangles.size());
        CHECK(box > 0 && box < road->triangles.size());
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}

TEST(race_flow_finish_now) {
    // A run ended by its own rule: finished at the clock, a share of the step in.
    Field f({10.0f}, 5);
    race::FlowSetup fs;
    fs.start = race::StartType::None, fs.laps = 1, fs.lap_length = kLap;
    race::RaceFlow flow(fs, f.cars);
    for (int i = 0; i < 60; ++i) f.step(), flow.step(f.cars);
    flow.finish_now(0, 0.5f);
    CHECK(flow.player_finished() && flow.car(0).finish_units == 60 * race::kClockPerStep - race::kClockPerStep / 2);
}

TEST(strobe_race_monitor_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t movies = 0;
    for (const auto& disc : test::discs()) {
        const gt3r::Volume::Entry* e = disc->volume().find("menu/common/racemonitor.strb");
        if (!e) continue;
        ++movies;
        auto movie = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(disc->volume().read(*e)));
        race::StrobePlayer p(movie, [](const std::string&, const std::vector<race::StrobePlayer::Value>&) { return race::StrobePlayer::Value{}; });
        p.restart();
        // The instances the program fills by name: six rows, the current car, the total, the lap, the pace.
        const std::vector<std::string> tree = p.tree();
        auto has = [&](const std::string& path) {
            for (const std::string& l : tree)
                if (l.rfind(path + "  ", 0) == 0) return true;
            return false;
        };
        for (const char* path : {"BAR1.NAME", "BAR6.DIFF", "BAR3.GRID", "CURRENTCAR", "TOTAL", "LAP", "PACEPANEL.PACETEXT", "CARPOS_F.GRID", "GAP5.TEXT"}) CHECK(has(path));
        // Set by path: a text and a row's frame (the lit row is its second).
        CHECK(p.set("BAR2.NAME", "text", race::StrobePlayer::Value::string("Ford Ka")));
        CHECK(p.variable("BAR2.NAME").kind == race::StrobePlayer::Value::ClipRef);
        p.call("BAR2", "gotoAndStop", {race::StrobePlayer::Value::number(2)});
        bool named = false, lit = false;
        for (const std::string& l : p.tree()) {
            named = named || l == "BAR2.NAME  text 17 frame 0 \"Ford Ka\"";
            lit = lit || l.rfind("BAR2  sprite 26 frame 1", 0) == 0;
        }
        CHECK(named && lit);
        CHECK(!p.set("NO_SUCH", "text", race::StrobePlayer::Value::string("x")));
    }
    if (!movies) SKIP("no Gran Turismo 4 disc");
}

// Where the cars' crossings of the line start (0x363B10): 0 only in the lap's last sector, past the goal and half the lap.
TEST(race_flow_start_crossings) {
    const std::vector<float> tsukuba{505.883f, 1072.191f};
    CHECK(race::start_crossings(1872.7f, 2115.1f, tsukuba, -1) == 0); // the rolling run-up, behind the line
    CHECK(race::start_crossings(20.6f, 2115.1f, tsukuba, -1) == 1);   // past the line
    CHECK(race::start_crossings(800.0f, 2115.1f, tsukuba, -1) == 1);  // the second sector
    CHECK(race::start_crossings(1080.0f, 2115.1f, tsukuba, -1) == 0); // the last sector, past half the lap
    CHECK(race::start_crossings(1060.0f, 2115.1f, std::vector<float>{300.0f, 1000.0f}, -1) == 0);
    CHECK(race::start_crossings(1010.0f, 2115.1f, std::vector<float>{300.0f, 1000.0f}, -1) == 1); // ... in the lap's first half
    CHECK(race::start_crossings(3505.5f, 3576.6f, {}, 659.3f) == 0); // B-3: a section, one sector, past its goal
    CHECK(race::start_crossings(400.0f, 3576.6f, {}, 659.3f) == 1);
    // A standing grid past the line (Fuji, 200 m): the first crossing completes a lap; one behind it is the start line.
    for (const bool past : {true, false}) {
        race::FlowSetup fs;
        fs.start = race::StartType::None, fs.laps = 1, fs.lap_length = 1000, fs.counted = true, fs.sectors = {300, 600};
        std::vector<race::CarState> cars(1);
        cars[0].course_v = past ? 200.0f : 950.0f, cars[0].lap = 0;
        race::RaceFlow flow;
        flow.begin(fs, cars);
        int crossings = 0;
        for (int step = 0; step < 400 && !flow.car(0).finished; ++step) {
            float v = cars[0].course_v + 10;
            if (v >= 1000) v -= 1000, ++cars[0].lap, ++crossings;
            cars[0].course_v = v;
            flow.step(cars);
        }
        CHECK(flow.car(0).finished && crossings == (past ? 1 : 2));
    }
}
