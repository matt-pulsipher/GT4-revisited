#include "gt4r/race/recorded_race.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/race_start.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/pit_stops.h"
#include "gt4r/sim/ai/pit_tire_change.h"
#include "gt4r/sim/race_rules.h"
#include "gt4r/sim/record_image.h"
#include "gt4r/sim/rules_start.h"
#include "gt4r/sim/rules_step.h"
#include "gt4r/sim/rules_wear.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gt4r::race {

struct RecordedRace::Parts {
    std::shared_ptr<const sim::Program> program;
    std::unique_ptr<StartCourse> starts;
    std::unique_ptr<sim::Session> session;
    std::unique_ptr<sim::ai::Field> field;
    std::unique_ptr<sim::ai::PitStops> pits;
    std::vector<CarInput> inputs;
    bool bspec = false;
    int pace = 3;
};

RecordedRace::~RecordedRace() = default;
sim::Session& RecordedRace::session() { return *p_->session; }
sim::ai::Field* RecordedRace::field() { return p_->field.get(); }
s64 RecordedRace::total_ms() const { return flow_.car_count() && flow_.car(0).finished ? flow_.run_ms(0) : -1; }

RecordedRace::RecordedRace(Disc& disc, db::SpecDb& sdb, sim::ai::DemoRun run) : run_(std::move(run)), p_(std::make_unique<Parts>()) {
    Parts& p = *p_;
    const replay::Demonstration& d = run_.demo;
    if (d.entries.empty() || d.streams.empty()) throw std::runtime_error("the recording has no entries or no controls");
    p.program = std::make_shared<const sim::Program>(sim::Program::load(disc));
    const sim::Program& program = *p.program;
    gt3r::Volume* vol = &disc.volume();
    const auto read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* f = vol->find(path);
        return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
    };
    const db::RaceEvents events(sdb, read);
    std::optional<db::RaceEvent> ev = events.event(u32(d.code_b0));
    if (!ev) throw std::runtime_error("the recording's event is not in the database");
    for (const db::CourseEntry& c : sdb.courses())
        if (c.id == u32(d.code_b8)) course_ = c.label;
    const std::optional<Bytes> data = read("crs/" + course_);
    if (!data) throw std::runtime_error("no course " + course_);
    const course::CourseFile file = course::CourseFile::parse(*data);
    const std::optional<course::Runway> road = course::course_road(file, *data);
    if (!road) throw std::runtime_error("the course has no road");
    u32 laps = 0, rules[2]; // (the race information's laps, penalty and tire damage)
    std::memcpy(&laps, d.information.data(), 4), std::memcpy(rules, d.information.data() + 4, 8);
    if (d.race_class == "RaceArcadeSingle") arcade_rules(ev->bytes, int(rules[0]), int(rules[1]));
    const bool solo = d.race_class == "RaceFreeRun" || d.race_class == "RaceSolitaire";
    p.bspec = d.race_mode == 1 || d.race_mode == 3;

    // The start (race/start_rule.h).
    CourseStart cs = course_start(sdb, course_);
    if (const std::optional<Bytes> ad = read("crs/" + course_ + ".ad")) apply_drivers_file(cs, *ad);
    StartType start = cs.rolling ? StartType::Rolling : StartType::Ten;
    float speed = cs.speed(), back = cs.distance_m;
    int start_kind = 0;
    if (solo) {
        start = cs.free_speed_kmh > 0 ? StartType::Rolling : StartType::Ten;
        speed = cs.free_speed_kmh / 3.6f, back = cs.free_distance_m, start_kind = 3;
    }
    p.starts = std::make_unique<StartCourse>(*road, file.section(*data, course::kSlotRunway), cs.way);
    const StartRace& race = p.starts->set_race(Conductor::Other, ev->bytes, -1);
    const std::optional<course::AiData> ad = sim::ai::Field::read(disc, course_);
    if (ad && (start == StartType::Rolling || p.starts->may_need_lines(d.entries.size()))) p.starts->add_lines(*ad, sim::ai::Tables::load(program));

    std::vector<sim::Entry> entries = run_.entries();
    std::vector<PlannedStart> planned;
    for (size_t i = 0; i < entries.size(); ++i) {
        sim::Entry& e = entries[i];
        if (e.grid_place < 0) e.grid_place = int(i);
        e.control = i > 0 ? 2 : p.bspec ? 1 : 0;
        e.fuel = ev->bytes.size() > 0x43 && ev->bytes[0x43] != 0 && !solo && cs.fuel;
        std::optional<CarStart> rolling;
        float distance = 0;
        if (start == StartType::Rolling && speed > 0) {
            distance = float(sim::rules::rolling_start_distance(-int(std::lround(back)), int(std::lround(cs.spacing_m)), int(entries.size()), e.grid_place));
            rolling.emplace();
            rolling->kind = start_kind ? start_kind : distance < 0 ? 2 : 1;
            rolling->distance = int(distance), rolling->speed = int(std::lround(speed * 3.6f));
        }
        planned.push_back(plan_start(race, i, e.grid_place, rolling));
        e.start_speed = planned.back().line || !rolling ? 0.0f : speed;
        put_start(e, race, planned.back(), i == 0 && !p.bspec, e.grid_place, program);
        if (rolling && !planned.back().line) e.start_distance = distance;
    }
    sim::Session::Options so;
    so.hold_steps = 0;
    so.rules = event_rule_block(ev->bytes);
    so.rules[0x40] = u8(entries.size() - 1);
    p.session = std::make_unique<sim::Session>(sdb, program, std::make_shared<const course::Runway>(*road), entries, sim::course_row(sdb, course_), &so);
    sim::Session& session = *p.session;
    const sim::RaceRules rr = sim::race_rules(ev->bytes, program);
    session.set_race_rules(rr.hits, rr.speed_cap, rr.penalty_cap);
    std::array<gt3r::s8, 4> grip{};
    if (ev->bytes.size() > 0x56)
        for (size_t k = 0; k < 4; ++k) grip[k] = gt3r::s8(ev->bytes[0x53 + k]);
    session.set_class_grip(sim::rules::single_race_class_grip(grip));
    if (ev->bytes.size() >= 0x74) {
        u8 rule_block[0x80] = {};
        sim::rules::event_wear_bytes(ev->bytes.data(), rule_block);
        if (!cs.fuel) std::memset(rule_block, 0, sizeof rule_block);
        session.set_wear_rules(rule_block, !session.loose(), program);
    }
    if (ad) {
        sim::ai::Pace pace = sim::ai::pace_for(*ev);
        pace.solo = solo, pace.random_spread = true, pace.strip = cs.strip, pace.start_look = u8(std::max(0, start_code_digit(cs.code, 3)));
        std::vector<size_t> cars(entries.size());
        for (size_t i = 0; i < cars.size(); ++i) cars[i] = i;
        p.field = std::make_unique<sim::ai::Field>(session, *ad, program, cars, pace);
        sim::ai::Field& field = *p.field;
        if (p.bspec) {
            // The record of the B-spec driver's races as the recording keeps it (the race information's +0x20).
            field.set_bspec(0);
            std::array<u8, 0x40> rec{};
            std::memcpy(rec.data(), d.information.data() + 0x20, rec.size());
            const gt3r::ee::F rating(session.rating(0));
            field.set_bspec_record(0, rec, sim::ai::bspec_class(rating));
            if (const auto sk = sim::ai::bspec_skill(sim::ai::BSpecTables::load(program), sim::ai::BSpecRecord::from_bytes(rec.data()), rating, start_code_digit(cs.code, 1) > 0))
                field.set_bspec_skill(0, sk->corner, sk->brake, sk->alongside);
        } else field.set_shadow(0);
        for (size_t i : cars) session.set_driver_aids(i, field.params(i).aids, field.params(i).grip);
        for (size_t i : cars) {
            field.start_line(i, first_line(race, planned[i], i == 0 && !p.bspec, entries[i].grid_place, program));
            if (session.cars()[i].rolling) field.hold_rolling(i);
        }
        field.apply_start_delays(session);
        sim::ai::PitHooks hooks;
        if (program.known()) {
            hooks = sim::ai::session_pit_hooks(session);
            sim::ai::add_tire_change(hooks, session, sdb, program, read);
        }
        sim::ai::PitRace pr;
        pr.laps = int(laps);
        p.pits = std::make_unique<sim::ai::PitStops>(field, program, entries.size(), hooks, pr);
        p.pits->set_recorded(0);
    }

    FlowSetup fs;
    fs.start = start;
    fs.laps = int(laps);
    fs.lap_length = road->length;
    fs.counted = true;
    fs.goal = race.goal, fs.sectors = race.sectors, fs.section_start = race.section_start;
    fs.no_lap = race.way == 1;
    for (size_t i = 0; i < entries.size(); ++i) {
        fs.crossings.push_back(planned_crossings(race, planned[i], session.cars()[i].course_v));
        fs.marks.push_back(planned[i].start.mark);
    }
    flow_.begin(fs, session.cars());
    for (size_t i = 0; i < entries.size(); ++i) session.set_crossings(i, flow_.car(i).counter), session.set_rank(i, flow_.place(i));
    p.inputs.resize(entries.size());
    // A free run's recording starts at its lap's line: the car's record as the recorder kept it, the clock two steps on, its times
    // from the line (0x359D40).
    if (const std::optional<replay::RecorderState> st = replay::recorder_state(d.streams[0]); solo && st) {
        sim::Record r;
        r.car = session.record(0), r.x = session.gt4_record(0), r.layout = session.wheel_layout(0);
        sim::load_car_record(r, st->car);
        if (p.field) p.field->release_rolling(0);
        session.load_moment(0, r.car, r.x);
        flow_.resume(0, 2, (100 - s64(st->units) + s64(st->remainder) - 1) / 3, 100, session.cars()[0].course_v);
        session.set_crossings(0, flow_.car(0).counter);
    }
}

void RecordedRace::step() {
    Parts& p = *p_;
    sim::Session& session = *p.session;
    for (CarInput& c : p.inputs) c = {};
    if (p.bspec) {
        const std::array<u8, 6> m = run_.mail(at_);
        if (p.field && m[4] == 4) p.pace = std::clamp(int(m[5]), 0, 4), p.field->command(0, {p.pace, -1});
        else if (p.field && m[4] == 5) p.field->command(0, {-1, m[5] ? 1 : 0});
        else if (p.pits && m[4] == 6) p.pits->command(0, m[5] != 0);
    } else p.inputs[0] = run_.input(at_);
    if (const std::array<u8, 6> m = run_.mail(at_); p.pits && m[0] != 0) p.pits->choose(0, m[1], m[2], m[3], m[0]);
    const bool hold = flow_.holding();
    std::vector<CarInput> driven = p.inputs;
    if (!hold && p.pits) p.pits->before(*p.field, driven);
    if (p.bspec && p.field) session.set_shift_factor(0, p.field->shift_factor(0));
    if (p.field) p.field->step(session, driven, -1, hold ? u32(std::max(1, flow_.hold_left() - 1)) : 0);
    else if (hold) session.hold_once(driven);
    else session.step_once(driven);
    if (!hold && p.pits) p.pits->after(session, *p.field);
    if (!hold && session.count_rolling(0)) {
        if (!p.field || p.field->release_rolling(0)) session.release_start(0);
        flow_.signal_start();
    }
    const RaceFlow::Events e = flow_.step(session.cars());
    for (const size_t i : e.started)
        if (session.cars()[i].rolling && (!p.field || p.field->release_rolling(i))) session.release_start(int(i));
    for (size_t i = 0; i < flow_.car_count(); ++i) session.set_crossings(i, flow_.car(i).counter), session.set_rank(i, flow_.place(i));
    if (p.field)
        for (const size_t i : e.finished) p.field->finish_run(session, i);
    if (e.lap_done == 0) laps_.push_back(flow_.car(0).last_lap_ms), laps_in_run_ = run_.over(at_) ? laps_in_run_ : laps_.size();
    ++at_;
}

bool RecordedRace::exact() const {
    if (laps_.empty()) return false;
    s64 best = laps_[0];
    for (s64 t : laps_) best = std::min(best, t);
    const s64 total = total_ms();
    if (s64(run_.demo.time_ms) == (total >= 0 ? total : laps_.back()) && (run_.demo.time2_ms < 0 || best == s64(run_.demo.time2_ms))) return true;
    s64 within = 0, best_within = laps_[0];
    for (size_t k = 0; k < laps_in_run_; ++k) within += laps_[k], best_within = std::min(best_within, laps_[k]);
    return laps_in_run_ > 0 && s64(run_.demo.time_ms) == within && (run_.demo.time2_ms < 0 || best_within == s64(run_.demo.time2_ms));
}

} // namespace gt4r::race
