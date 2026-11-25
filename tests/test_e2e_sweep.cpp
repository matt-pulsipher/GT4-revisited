// The playtest's sweeps as checks, headless (gt4tool sweep runs them in full, with a table): every
// course file of the disc set up with its computer drivers, every car of the database driven a little,
// and the player's car driven through the pad's path in each gearbox with the aids on and off.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/line_driver.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace gt4r;

namespace {

Disc* sweep_disc() {
    for (const auto& disc : test::discs())
        if (disc->info().game == Game::GT4 && sim::Program::load(*disc).known()) return disc.get();
    return nullptr;
}

std::shared_ptr<const course::Runway> road_of(Disc& disc, const std::string& course) {
    const gt3r::Volume::Entry* file = disc.volume().find("crs/" + course);
    if (!file) return nullptr;
    const Bytes data = disc.volume().read(*file);
    if (!course::CourseFile::looks_like_course(data)) return nullptr;
    const course::CourseFile parsed = course::CourseFile::parse(data);
    std::optional<course::Runway> road = course::course_road(parsed, data);
    return road ? std::make_shared<const course::Runway>(std::move(*road)) : nullptr;
}

bool finite(const race::CarState& s) { return std::isfinite(s.position.x) && std::isfinite(s.position.y) && std::isfinite(s.position.z) && std::isfinite(s.speed); }

// `entries` on `course`, the course's computer drivers at every wheel, `seconds` after the start's hold: the metres each
// car covered (negative: its state stopped being a number).
std::vector<double> drive(Disc& disc, db::SpecDb& db, const sim::Program& program, const std::string& course,
                          const std::shared_ptr<const course::Runway>& road, const std::vector<sim::Entry>& entries, double seconds) {
    sim::Session session(db, program, road, entries);
    std::vector<size_t> computer(entries.size());
    for (size_t i = 0; i < computer.size(); ++i) computer[i] = i;
    std::unique_ptr<sim::ai::Field> field = sim::ai::Field::open(disc, course, session, computer, {});
    for (size_t c : computer) session.set_driver_aids(c, field->params(c).aids, field->params(c).grip);
    std::vector<race::CarInput> in(entries.size());
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) {
        field->hold(in);
        session.hold_once(in);
    }
    std::vector<double> metres(entries.size(), 0.0);
    for (int step = 0; step < int(seconds * 60); ++step) {
        field->drive(in);
        field->apply_time_scales(session);
        session.step_once(in);
        for (size_t i = 0; i < entries.size(); ++i) {
            const race::CarState& s = session.cars()[i];
            if (metres[i] < 0) continue;
            metres[i] = finite(s) ? metres[i] + std::abs(double(s.speed)) / 60.0 : -1.0;
        }
    }
    return metres;
}

} // namespace

// Every course file: its road read, a session of two cars set up on it, and where the course has a
// driver file, 10 s of racing: both cars' states stay numbers, and where it has a main driving line the
// field gets going.
TEST(disc_e2e_sweep_every_course) {
    Disc* disc = sweep_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    db::SpecDb db = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    std::vector<sim::Entry> entries(2);
    CHECK(sim::stock_car(db, program, "nsx_r_02", entries[0].equipment));
    CHECK(sim::stock_car(db, program, "civic_r_98", entries[1].equipment));
    int courses = 0, raced = 0;
    for (const std::string& name : disc->volume().list_dir("crs/")) {
        if (name.find('.') != std::string::npos || name.find('/') != std::string::npos) continue;
        const std::shared_ptr<const course::Runway> road = road_of(*disc, name);
        if (!road) continue;
        ++courses;
        try {
            const std::optional<course::AiData> ai = sim::ai::Field::read(*disc, name);
            if (!ai) {
                sim::Session session(db, program, road, entries); // (set up only)
                continue;
            }
            const std::vector<double> m = drive(*disc, db, program, name, road, entries, 10.0);
            ++raced;
            // (A course without a main driving line, the photo locations and the gymkhanas, is not raced:
            // its states are checked only.)
            const bool raced_course = !ai->lines[0].empty();
            const bool ok = m[0] >= 0 && m[1] >= 0 && (!raced_course || std::max(m[0], m[1]) > 20.0);
            if (!ok) std::printf("  %s: %.0f m, %.0f m in 10 s\n", name.c_str(), m[0], m[1]);
            CHECK(ok);
        } catch (const std::exception& e) {
            std::printf("  %s: %s\n", name.c_str(), e.what());
            CHECK(false);
        }
    }
    std::printf("  %d course files, %d raced\n", courses, raced);
    CHECK(courses >= 100 && raced >= 100);
}

// Every car of the database, as sold, alone on the test course with its computer driver for 15 s: it is
// built, its state stays a number and it moves (the slowest, the Benz Patent Motor Wagen, covers
// about 30 m).
TEST(disc_e2e_sweep_every_car) {
    Disc* disc = sweep_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    db::SpecDb db = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = road_of(*disc, "testcourse");
    CHECK(road != nullptr);
    if (!road) return;
    int cars = 0, bad = 0;
    for (const db::CarEntry& c : db.cars()) {
        ++cars;
        std::vector<sim::Entry> one(1);
        double m = -2;
        try {
            if (sim::stock_car(db, program, c.label, one[0].equipment)) m = drive(*disc, db, program, "testcourse", road, one, 15.0)[0];
        } catch (const std::exception& e) {
            std::printf("  %s: %s\n", c.label.c_str(), e.what());
        }
        if (m < 10.0) {
            ++bad;
            std::printf("  %s: %s\n", c.label.c_str(), m == -2 ? "not built" : m < 0 ? "state not a number" : (std::to_string(int(m)) + " m in 15 s").c_str());
        }
    }
    std::printf("  %d cars, %d that did not move\n", cars, bad);
    CHECK(cars >= 700);
    CHECK(bad == 0);
}

// The player's car through the pad's path (an analog stick and pedals: steering to the road's middle,
// braking for the bends), in each gearbox, with the aids on and off: a lap of Tsukuba each.
TEST(disc_e2e_sweep_pad_lap) {
    Disc* disc = sweep_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    db::SpecDb db = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const std::shared_ptr<const course::Runway> road = road_of(*disc, "tsukuba");
    CHECK(road != nullptr);
    if (!road) return;
    for (int manual = 0; manual < 2; ++manual)
        for (int aids = 0; aids < 2; ++aids) {
            std::vector<sim::Entry> e(1);
            CHECK(sim::stock_car(db, program, "nsx_r_02", e[0].equipment));
            e[0].automatic = !manual, e[0].asm_on = e[0].tcs_on = aids != 0;
            sim::Session session(db, program, road, e);
            sim::LineDriver pad(session, 0);
            const sim::SpecFigures fig = sim::spec_figures(session.spec(0));
            std::vector<race::CarInput> in(1);
            for (int k = 0; k < sim::Session::kHoldSteps; ++k) session.hold_once(in);
            int last = session.cars()[0].lap, shifts = 0, hold = 0, gear_max = 1;
            double start = last >= 0 ? 0.0 : -1.0, lap = 0; // (a car ahead of the line times its lap from the start)
            for (int step = 0; step < 150 * 60 && lap == 0; ++step) {
                const race::CarState& me = session.cars()[0];
                race::CarInput p = pad.drive(1.0f, true);
                if (manual && hold-- <= 0) {
                    if (me.engine_rpm > float(fig.rev_limit_rpm - 300) && me.gear < session.spec(0).gears()) p.shift_up = true, hold = 20, ++shifts;
                    else if (me.engine_rpm < 0.55f * float(fig.power_rpm) && me.gear > 1) p.shift_down = true, hold = 20, ++shifts;
                }
                in[0] = p;
                session.step_once(in);
                const race::CarState& s = session.cars()[0];
                CHECK(finite(s));
                if (!finite(s)) break;
                gear_max = std::max(gear_max, s.gear);
                if (s.lap > last) {
                    if (start >= 0) lap = session.time() - start;
                    start = session.time();
                }
                last = s.lap;
            }
            std::printf("  %s, aids %s: lap %.3f s, %d shifts, top gear %d\n", manual ? "MT" : "AT", aids ? "on" : "off", lap, shifts, gear_max);
            CHECK(lap > 55 && lap < 100);
            CHECK(gear_max >= 4);
            if (manual) CHECK(shifts >= 6);
        }
}
