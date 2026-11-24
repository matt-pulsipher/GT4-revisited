#include "gt4r/sim/ai/pit_stops.h"

#include "gt4r/sim/session.h"

namespace gt4r::sim::ai {

PitStops::PitStops(const Field& field, const Program& program, size_t cars, PitHooks hooks, PitRace race)
    : flow_(program), hooks_(std::move(hooks)), race_(race), cars_(cars), last_d_(cars), last_lap_(cars, 0), stops_(cars, 0), records_(cars) {
    if (program.known()) race_.refuel_rate = program.b(0x620E25);
    for (const LinePoint& p : field.lines().line(CourseLines::kPitStop)) line_.dist.push_back(F(p.dist)), line_.heading.push_back(F(p.heading));
    // A car's box (0x363B10 by 0x46BDA0): the stop line's point 8 less its place on the grid (none without the line).
    for (size_t i = 0; i < cars; ++i) cars_[i].box = line_.dist.empty() ? 0 : gt3r::s8(8 - int(i));
}

void PitStops::before(const Field& field, std::span<race::CarInput> inputs) {
    (void)field;
    for (size_t i = 0; i < cars_.size() && i < inputs.size(); ++i) {
        PitCar& c = cars_[i];
        if (c.hold == 0) continue;
        // The stop's own timing (0x35C0B8, phase 4): the counter, the tires.
        const u16 svc = c.service;
        const u8 chose_f = c.tire_front, chose_r = c.tire_rear;
        const HoldEvents e = pit_hold_step(c, std::vector<gt3r::s8>(axle_));
        // At 645 with tires served: the stop's record takes the tires fitted (0xFE: the car's own kind again; a choice the
        // kind chosen).
        if (c.hold == 0x285 && (svc & 0xC0)) {
            const std::pair<int, int> own = hooks_.tires ? hooks_.tires(i) : std::pair<int, int>{-1, -1};
            StopRecord& r = records_[i][0];
            if (chose_f == 0xFE) r.front = own.first;
            else if (chose_f != 0xFF && e.chosen_tires) r.front = chose_f;
            if (chose_r == 0xFE) r.rear = own.second;
            else if (chose_r != 0xFF && e.chosen_tires) r.rear = chose_r;
        }
        for (int w : e.new_tires)
            if (hooks_.new_tire) hooks_.new_tire(i, w);
        // Standing in the box: the hold counter keeps the car (the preparation's hold, 0x35C0B8 / 0x357700).
        race::CarInput& in = inputs[i];
        in.throttle = 0, in.brake = 1, in.steer_angle = 0;
    }
}

void PitStops::after(const Session& session, Field& field) {
    const F L(field.lines().lap());
    for (size_t i = 0; i < cars_.size() && i < session.cars().size(); ++i) {
        PitCar& c = cars_[i];
        const race::CarState& st = session.cars()[i];
        const F d(st.course_v);
        const int control = std::max(field.control(i), 0);
        // 0x343C20 (the race clock in ms: the session's steps; the car's laps as the race flow counts them).
        LapStateInputs ls;
        ls.finished = race_.finished && race_.finished(i);
        ls.clock_ms = u32(session.time() * 1000.0);
        ls.limit_minutes = race_.limit_minutes, ls.laps = race_.laps, ls.lap = st.lap, ls.d = d, ls.lap_length = L;
        const int lap_state = sim::ai::lap_state(ls);
        // The wear and fuel phase's request (0x353218).
        PitNeed need;
        need.allowed = race_.pits;
        need.fuel_on = hooks_.fuel_on && hooks_.fuel_on(i);
        if (need.fuel_on) {
            need.fuel = hooks_.fuel(i), need.fuel_distance = hooks_.fuel_distance(i), need.fuel_used = hooks_.fuel_used(i);
        }
        need.d = d, need.lap = L, need.pit_marker = line_.marker(1);
        need.wear_on = hooks_.wear_on && hooks_.wear_on();
        if (need.wear_on) {
            need.wear_limit = hooks_.wear_limit ? hooks_.wear_limit() : F();
            need.wear = hooks_.tread ? hooks_.tread(i) : std::vector<F>{};
            need.burst = hooks_.burst ? hooks_.burst(i) : std::vector<F>{};
        }
        need.lap_state = lap_state, need.control = gt3r::s8(control);
        pit_need(c, need);
        // The pass (0x359008 → 0x35B060).
        PitInputs in;
        in.d = d, in.d_prev = last_d_[i], in.lap = L;
        in.speed = F(st.speed);
        in.side_speed = F(st.velocity.x * st.right.x + st.velocity.y * st.right.y + st.velocity.z * st.right.z);
        in.control = gt3r::s8(control), in.kind = u8(field.kind(i)), in.index = int(i);
        in.lap_state = lap_state;
        // The collision body's hits this step (0x45CC08: +0x43C a wall or +0x440 a car; 0x45CC30: +0x43C).
        const collide::Body& body = session.body(i);
        in.contact = body.wall_hit != 0 || body.car_hit != 0;
        in.contact_43C = body.wall_hit != 0;
        in.lines = !line_.dist.empty();
        in.crossed = st.lap > last_lap_[i];
        // On the pit lane (phase 18, 0x369888): a wheel on the ground on a triangle flagged bit 0, in a race with pit stops.
        bool lane = false;
        for (const race::WheelState& w : st.wheels) lane = lane || (w.on_ground && (w.surface & 1));
        c.flags = u8((c.flags & ~0x10) | (lane && race_.pits ? 0x10 : 0));
        std::vector<PitAction> acts;
        ServiceInputs sv;
        sv.pits_on = race_.pits, sv.lap_state = lap_state, sv.fuel_on = need.fuel_on, sv.control = gt3r::s8(control);
        flow_.step(c, in, line_, acts, [&] { return service_of(c, sv); });
        for (const PitAction& a : acts) {
            switch (a.kind) {
            case PitAction::Enter: // (0x3453A0 also marks the car as asked for the pits)
                if (field.pit_enter(i)) c.flags |= 8;
                break;
            case PitAction::Stop: field.pit_stop(i); break;
            case PitAction::Leave: field.pit_leave(i); break;
            case PitAction::Return: field.pit_return(i); break;
            case PitAction::EndRun: field.finish(i, 3); break;
            case PitAction::PlaceInBox:
                if (hooks_.place_in_box) hooks_.place_in_box(i, line_.box_heading(c.box));
                break;
            case PitAction::Heading:
                if (hooks_.place_in_box) hooks_.place_in_box(i, a.value);
                break;
            case PitAction::CountStop: { // 0x35AD10
                if (hooks_.display_event) hooks_.display_event(i, 1);
                ++stops_[i];
                auto& rec = records_[i];
                for (size_t k = 3; k > 0; --k) rec[k] = rec[k - 1];
                // 0x343B50: the car's lap (less one when F +0x4B2), at least 1
                const int lap = st.lap > 0 ? (c.x4B2 ? st.lap - 1 : st.lap) : 1;
                rec[0] = {lap, -1, -1};
                break;
            }
            case PitAction::Recount:
                if (hooks_.display_event) hooks_.display_event(i, 6);
                break;
            case PitAction::Refuel:
                if (hooks_.fuel && hooks_.set_fuel && hooks_.tank) {
                    F fuel = hooks_.fuel(i);
                    pit_refuel(c, fuel, hooks_.tank(i), F(1.0f / 60.0f), race_.refuel_rate);
                    hooks_.set_fuel(i, fuel);
                } else {
                    c.service &= u16(~0x100); // (no fuel counted: nothing to put in)
                }
                break;
            default: break;
            }
        }
        last_d_[i] = d, last_lap_[i] = st.lap;
    }
}

} // namespace gt4r::sim::ai
