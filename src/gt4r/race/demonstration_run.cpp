#include "gt4r/race/demonstration_run.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/gadget_bodies.h"
#include "gt4r/race/gadgets.h"
#include "gt4r/race/license_rules.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/race_start.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/race_rules.h"
#include "gt4r/sim/rules_wear.h"

namespace gt4r::race {

DemonstrationRun run_demonstration(Disc& disc, db::SpecDb& sdb, const std::string& label, const DemonstrationOptions& opt) {
    DemonstrationRun r;
    try {
        const sim::Program program = sim::Program::load(disc);
        std::string error;
        const std::optional<sim::ai::DemoRun> demo = opt.run ? std::optional<sim::ai::DemoRun>(*opt.run) : sim::ai::DemoRun::load(disc, program, label, &error);
        if (!demo) return r.error = error, r;
        r.recorded_ms = demo->recorded_ms();
        gt3r::Volume* vol = &disc.volume();
        const db::RaceEvents events(sdb, [vol](const std::string& p) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* f = vol->find(p);
            return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
        });
        const std::optional<db::RaceEvent> ev = events.event(label);
        if (!ev) return r.error = "no event " + label, r;
        r.course = ev->course;
        const gt3r::Volume::Entry* cf = vol->find("crs/" + ev->course);
        if (!cf) return r.error = "no course " + ev->course, r;
        const Bytes data = vol->read(*cf);
        const course::CourseFile file = course::CourseFile::parse(data);
        const std::optional<course::Runway> road = course::course_road(file, data);
        if (!road) return r.error = "no road", r;
        const ByteSpan runway = file.section(data, course::kSlotRunway);

        const LicenseRules rules = LicenseRules::from_row(ev->bytes);
        r.kind = rules.kind;
        const std::string code = label.size() > 2 ? label.substr(2) : label;
        const Conductor conductor = conductor_for("license", code);
        const CourseStart cs = course_start(sdb, ev->course);
        StartCourse starts(*road, runway, cs.way);
        const StartRace& race = starts.set_race(conductor, ev->bytes, rules.section);
        const std::optional<course::AiData> ad = sim::ai::Field::read(disc, ev->course);
        if (ad && starts.may_need_lines(demo->entries().size())) starts.add_lines(*ad, sim::ai::Tables::load(program));

        std::vector<sim::Entry> entries = demo->entries();
        std::vector<PlannedStart> planned;
        for (size_t i = 0; i < entries.size(); ++i) {
            sim::Entry& e = entries[i];
            planned.push_back(plan_start(race, i, e.grid_place >= 0 ? e.grid_place : int(i)));
            put_start(e, race, planned.back(), i == 0, e.grid_place >= 0 ? e.grid_place : int(i), program);
            e.control = i > 0 ? 2 : 0;
        }
        sim::Session::Options so;
        so.hold_steps = 0;
        so.rules = event_rule_block(ev->bytes);
        sim::Session session(sdb, program, std::make_shared<const course::Runway>(*road), entries, sim::course_row(sdb, ev->course), &so);
        std::array<gt3r::s8, 4> grip{};
        for (size_t k = 0; k < 4; ++k) grip[k] = gt3r::s8(ev->bytes[0x53 + k]);
        session.set_class_grip(grip);
        {
            const sim::RaceRules rr = sim::race_rules(ev->bytes, program);
            session.set_race_rules(rr.hits, rr.speed_cap, rr.penalty_cap);
        }
        {
            u8 rule_block[0x80] = {};
            sim::rules::event_wear_bytes(ev->bytes.data(), rule_block);
            session.set_wear_rules(rule_block, !session.loose(), program);
        }
        std::unique_ptr<sim::ai::Field> field;
        if (ad) {
            sim::ai::Pace pace = sim::ai::pace_for(*ev);
            pace.solo = conductor == Conductor::License, pace.random_spread = false, pace.strip = cs.strip, pace.start_look = u8(std::max(0, start_code_digit(cs.code, 3)));
            std::vector<size_t> cars(entries.size());
            for (size_t i = 0; i < cars.size(); ++i) cars[i] = i;
            field = std::make_unique<sim::ai::Field>(session, *ad, program, cars, pace);
            field->set_shadow(0);
            for (size_t i : cars) session.set_driver_aids(i, field->params(i).aids, field->params(i).grip);
            for (size_t i : cars) {
                field->start_line(i, first_line(race, planned[i], i == 0, entries[i].grid_place >= 0 ? entries[i].grid_place : int(i), program));
                if (session.cars()[i].rolling) field->hold_rolling(i);
            }
        }
        r.car = std::to_string(entries.size());

        FlowSetup fs;
        fs.start = race.row.moving(conductor) ? StartType::Moving : StartType::Eight;
        fs.laps = std::max(1, ev->laps);
        fs.lap_length = road->length;
        fs.counted = true;
        fs.sectors = race.sectors, fs.goal = race.goal, fs.section_start = race.section_start;
        for (size_t i = 0; i < entries.size(); ++i) {
            fs.crossings.push_back(planned_crossings(race, planned[i], session.cars()[i].course_v));
            fs.marks.push_back(planned[i].start.mark);
        }
        fs.test_kind = rules.kind;
        RaceFlow flow(fs, session.cars());
        for (size_t i = 0; i < entries.size(); ++i) session.set_crossings(i, flow.car(i).counter), session.set_back_crossings(i, flow.car(i).back), session.set_rank(i, flow.place(i));
        LicenseJudge judge(rules);
        std::array<u32, 32> soft{};
        for (u32 m = 0; m < soft.size(); ++m) soft[m] = program.w(sim::Program::kSoft + 4 * m);
        Gadgets gadgets;
        GadgetBodies bodies;
        if (opt.cones) {
            gadgets = Gadgets::parse(file.section(data, course::kSlotGadgets), runway, rules.section);
            bodies.reset(gadgets, *road);
        }

        std::vector<CarInput> in(entries.size());
        int met = 0;
        r.ran = true;
        for (size_t step = 0; step < demo->steps() + size_t(std::max(0, opt.extra)); ++step) {
            for (CarInput& c : in) c = {};
            in[0] = demo->input(step);
            const bool hold = flow.holding();
            if (field) field->step(session, in, -1, hold ? u32(flow.hold_left() - 1) : 0);
            else if (hold) session.hold_once(in);
            else session.step_once(in);
            const int cones = met; // (the cones move after the race's step: what they met is judged in the next)
            if (!gadgets.placements.empty()) {
                std::vector<GadgetBodies::Car> boxes;
                for (size_t i = 0; i < entries.size(); ++i) boxes.push_back(gadget_box(session.record(i), session.cars()[i]));
                bodies.step(gadgets, *road, boxes, sim::Session::kStep);
                met = bodies.contacts().empty() ? 0 : bodies.contacts()[0];
            }
            const RaceFlow::Events e = flow.step(session.cars());
            for (const size_t i : e.started)
                if (session.cars()[i].rolling && (!field || field->release_rolling(i))) session.release_start(int(i));
            for (size_t i = 0; i < entries.size(); ++i) session.set_crossings(i, flow.car(i).counter), session.set_back_crossings(i, flow.car(i).back), session.set_rank(i, flow.place(i));
            if (field)
                for (const size_t i : e.finished) field->finish_run(session, i);
            if (!hold && !flow.player_finished()) {
                const CarState& c = session.cars()[0];
                if (const std::optional<float> share = rules.kind == 1 ? stopped_in_box(c, program.release()) : std::nullopt) flow.finish_now(0, *share);
                else if (const LicenseFail f = judge.step(license_step(c, [&](u8 m) { return soft[m & 31] != 0; }, cs.kind, session.record(0).p.mass.host(), cones)); f != LicenseFail::None)
                    r.fail = int(f), flow.finish_now(0);
                else if (rules.kind == 6 && entries.size() > 1 && flow.place(0) == 1) r.fail = int(LicenseFail::Kind6), flow.finish_now(0);
                else if (judge.cones_done()) flow.finish_now(0, 1);
            }
            r.steps = step + 1;
            if (opt.on_step) opt.on_step(step, session);
            if (flow.player_finished()) break;
        }
        if (flow.player_finished() && r.fail == 0) r.ended_ms = int(flow.run_ms(0));
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

} // namespace gt4r::race
