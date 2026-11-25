#include "gt4r/race/demonstration_run_prologue.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/replay/demonstration_prologue.h"
#include "gt4r/race/gadget_bodies.h"
#include "gt4r/race/gadgets.h"
#include "gt4r/race/license_prologue.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/race_start.h"
#include "gt4r/race/sections.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/race_rules.h"
#include "gt4r/sim/rules_wear.h"

namespace gt4r::race::prologue {

DemonstrationRun run_demonstration(Disc& disc, db::SpecDb& sdb, const std::string& label, const Options& opt) {
    DemonstrationRun r;
    try {
        const sim::Program program = sim::Program::load(disc);
        gt3r::Volume* vol = &disc.volume();
        const auto read = [vol](const std::string& p) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* f = vol->find(p);
            return f ? std::optional<Bytes>(vol->read(*f)) : std::nullopt;
        };
        const std::optional<Bytes> file = read(replay::prologue::run_path(label));
        if (!file) return r.error = "no recorded run " + label, r;
        const std::optional<replay::prologue::Run> run = replay::prologue::Run::parse(*file, replay::prologue::delta_bases(program), &r.error);
        if (!run) return r;
        r.recorded_ms = run->time_ms;
        const db::RaceEvents events(sdb, read);
        const std::optional<db::RaceEvent> ev = events.event(label);
        if (!ev) return r.error = "no event " + label, r;
        r.course = ev->course;
        const std::optional<Bytes> data = read("crs/" + ev->course);
        if (!data) return r.error = "no course " + ev->course, r;
        const course::CourseFile cf = course::CourseFile::parse(*data);
        const std::optional<course::Runway> road = course::course_road(cf, *data);
        if (!road || !road->gtrw) return r.error = "no road", r;
        const ByteSpan section = cf.section(*data, course::kSlotRoadGtrw);

        const LicenseRules rules = LicenseRules::from_row(ev->bytes);
        r.kind = rules.kind;
        const CourseStart cs = course_start(sdb, ev->course);
        const sim::RaceRules rr = sim::race_rules(ev->bytes, program);
        const bool rolling = rr.start_speed[0] != 0 && rr.start_distance[0] < 0;

        // The cars: the event's pool in its order (the lesson's own first), with the file's places and aids.
        const std::vector<db::Opponent> pool = events.pool(*ev);
        std::vector<sim::Entry> entries(run->entries.size());
        FlowSetup fs;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i >= pool.size()) return r.error = "the event's pool is shorter than the file's entries", r;
            sim::ai::GridEntry g;
            g.car = pool[i].car, g.paint = pool[i].paint, g.parts = pool[i].parts, g.param = pool[i].param, g.enemy = pool[i].enemy;
            sim::Entry& e = entries[i];
            if (!sim::ai::entry_equipment(sdb, program, events, g, e.equipment)) return r.error = "no equipment", r;
            { // (the file's own parts and settings beside the pool's: Prologue's record has Gran Turismo 4's up to +0xF0, its settings 0x20 before)
                const replay::prologue::Entry& f = run->entries[i];
                const auto differs = [&](const char* what, u32 at, u32 theirs, u32 ours) {
                    if (theirs == ours) return;
                    char t[96];
                    std::snprintf(t, sizeof t, " car %zu %s +%X file %X ours %X;", i, what, at, theirs, ours);
                    r.equipment += t;
                };
                for (u32 at = 0x20; at < 0xF0; at += 8) differs("part", at, u32(f.code(at)), db::part_row_id(e.equipment.part(sim::Equipment::Slot(at))));
                for (u32 k = 0; k < 8; ++k) differs("gear", k, f.equipment[0xF8 + 2 * k] | f.equipment[0xF9 + 2 * k] << 8, e.equipment.bytes[0x110 + 2 * k] | e.equipment.bytes[0x111 + 2 * k] << 8);
                for (u32 at = 0x128; at < 0x15B; ++at) differs("setting", at, f.equipment[at - 0x20], e.equipment.bytes[at]);
                if (u32(f.code(0)) != db::part_row_id(e.equipment.part(sim::Equipment::Car))) r.equipment += " another car;";
            }
            e.grid_place = run->entries[i].grid_place;
            e.automatic = i > 0 || run->entries[i].pedal_style != 1;
            if (i == 0) e.pedal_style = run->entries[i].pedal_style;
            e.asm_on = run->entries[i].asm_on != 0, e.tcs_on = run->entries[i].tcs_on != 0;
            e.control = i > 0 ? 2 : 0;
        }
        const std::optional<course::AiData> ad = sim::ai::Field::read(disc, ev->course);
        const sim::RoadGround ground(*road);
        std::optional<sim::ai::CourseLines> lines;
        if (ad) lines.emplace(*ad, ground, sim::ai::Tables::load(program));
        StartRace sr;
        sr.road = &*road, sr.lines = lines ? &*lines : nullptr, sr.way = cs.way;
        std::vector<PlannedStart> planned(entries.size());
        if (rolling)
            for (size_t i = 0; i < entries.size() && i < rr.start_speed.size(); ++i) {
                CarStart s;
                s.distance = rr.start_distance[i], s.speed = rr.start_speed[i];
                s.kind = s.distance < 0 && s.speed > 0 ? 2 : s.distance != 0 ? 1 : 0;
                planned[i] = plan_start(sr, i, entries[i].grid_place, s);
                put_start(entries[i], sr, planned[i], i == 0, entries[i].grid_place, program);
                fs.past_line.push_back(s.distance >= 0);
            }
        r.car = pool[0].car;
        sim::Session::Options so;
        so.hold_steps = 0;
        so.rules = event_rule_block(ev->bytes);
        so.rules[0x40] = u8(entries.size() - 1);
        sim::Session session(sdb, program, std::make_shared<const course::Runway>(*road), entries, sim::course_row(sdb, ev->course), &so);
        session.set_race_rules(rr.hits, rr.speed_cap, rr.penalty_cap);
        {
            std::array<gt3r::s8, 4> grip{};
            for (size_t k = 0; k < 4; ++k) grip[k] = gt3r::s8(ev->bytes[0x53 + k]);
            session.set_class_grip(grip);
            u8 rule_block[0x80] = {};
            sim::rules::event_wear_bytes(ev->bytes.data(), rule_block);
            session.set_wear_rules(rule_block, !session.loose(), program);
        }
        PitZone zone;
        std::unique_ptr<sim::ai::Field> field;
        if (ad) {
            zone = pit_zone(*lines);
            sim::ai::Pace pace = sim::ai::pace_for(*ev);
            std::vector<size_t> cars(entries.size());
            for (size_t i = 0; i < cars.size(); ++i) cars[i] = i;
            field = std::make_unique<sim::ai::Field>(session, *ad, program, cars, pace);
            field->set_shadow(0);
            for (size_t i : cars) session.set_driver_aids(i, field->params(i).aids, field->params(i).grip);
            for (size_t i : cars)
                if (planned[i].line) {
                    field->start_line(i, planned[i].start.kind >= 2 ? sim::ai::CourseLines::kMain : i == 0 ? 7 : sim::ai::CourseLines::kMain);
                    if (planned[i].start.kind == 2) field->hold_rolling(i);
                }
        }

        std::vector<float> splits = road_list(section, 0);
        if (splits.size() > 3) splits.resize(3);
        fs.start = rolling ? StartType::Rolling : StartType::Given;
        fs.start_seconds = opt.hold >= 0 ? opt.hold : 4; // (the race information's start type 1: 4 seconds, 0x37A7B8)
        fs.laps = std::max(1, ev->laps);
        fs.lap_length = road->length;
        fs.player_clock = rolling;
        fs.counted = !splits.empty();
        fs.sectors = splits;
        RaceFlow flow(fs, session.cars());
        for (size_t i = 0; i < entries.size(); ++i) session.set_crossings(i, flow.car(i).counter), session.set_rank(i, flow.place(i));
        for (size_t i = 0; i < fs.past_line.size(); ++i)
            if (fs.past_line[i] && session.record(i).driver_kind == 1) session.release_start(int(i));
        LicenseJudge judge(rules);
        Gadgets gadgets;
        GadgetBodies bodies;
        if (opt.cones) {
            gadgets = Gadgets::parse(cf.section(*data, course::kSlotGadgets), section);
            bodies.reset(gadgets, *road);
        }
        bool stop_area = false;
        for (const course::Runway::Triangle& t : road->triangles) stop_area = stop_area || (t.kind & 4) != 0;
        const float goal = road->goal_v > 0 && road->goal_v < road->length - 1 ? road->goal_v : -1;
        float goal_prev = -1;

        std::vector<PadRecord> pads;
        for (const replay::PadRecord& f : run->streams[0].frames) pads.push_back({f.bytes});
        static const PadRecord kEmpty{};
        std::vector<CarInput> in(entries.size());
        r.ran = true;
        const size_t steps = pads.size() + size_t(std::max(0, opt.extra));
        for (size_t step = 0; step < steps; ++step) {
            const bool hold = flow.holding();
            for (CarInput& c : in) c = {};
            const size_t at = size_t(std::max(0, int(step) + opt.shift));
            in[0] = sim::ai::pad_input(replay::PadRecord{(at < pads.size() ? pads[at] : kEmpty).bytes});
            in[0].pad = at < pads.size() ? &pads[at] : &kEmpty;
            if (field) field->step(session, in, session.record(0).driver_kind == 1 ? -1 : 0, hold ? u32(std::max(1, flow.hold_left() - 1)) : 0);
            else if (hold) session.hold_once(in);
            else session.step_once(in);
            int cones = 0;
            if (!gadgets.placements.empty()) {
                std::vector<GadgetBodies::Car> boxes;
                for (size_t i = 0; i < entries.size(); ++i) boxes.push_back(gadget_box(session.record(i), session.cars()[i]));
                bodies.step(gadgets, *road, boxes, sim::Session::kStep);
                cones = bodies.contacts().empty() ? 0 : bodies.contacts()[0];
            }
            const RaceFlow::Events e = flow.step(session.cars());
            for (const size_t i : e.started)
                if (session.record(i).driver_kind == 1 && (!field || field->release_rolling(i))) session.release_start(int(i));
            for (size_t i = 0; i < entries.size(); ++i) session.set_crossings(i, flow.car(i).counter), session.set_rank(i, flow.place(i));
            const CarState& c = session.cars()[0];
            if (!hold && !flow.player_finished() && flow.steps_since_start() >= 0) {
                if (const std::optional<float> share = (rules.kind == 1 || !rules.any()) && stop_area ? stopped_in_box(c) : std::nullopt) flow.finish_now(0, *share);
                else if (const LicenseFail f = judge.step(license_step(c, zone, cs.kind, session.record(0).p.mass.host(), cones)); f != LicenseFail::None && opt.judge)
{
                    r.fail = int(f), flow.finish_now(0);
                    char t[160];
                    std::snprintf(t, sizeof t, "(wall %.2f m/s of %d, car %.2f of %d, cones %d at %.1f m)", double(c.wall_hit / session.record(0).p.mass.host()), rules.wall_limit, double(c.car_hit),
                                  rules.car_limit, cones, double(c.course_v));
                    r.error = t;
                } else if (judge.cones_done()) flow.finish_now(0, 1); // (the clock + 50)
                else if (goal > 0 && !stop_area) {
                    if (flow.car(0).counter >= fs.laps && goal_prev >= 0 && goal_prev < goal && goal <= c.course_v && c.course_v - goal_prev < 100)
                        flow.finish_now(0, (goal - goal_prev) / std::max(c.course_v - goal_prev, 1e-4f));
                    goal_prev = flow.car(0).counter >= 1 ? c.course_v : -1;
                }
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

} // namespace gt4r::race::prologue
