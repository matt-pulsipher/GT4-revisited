#include "gt4r/sim/ai/pit_stops.h"

#include "gt4r/sim/session.h"

namespace gt4r::sim::ai {

PitHooks session_pit_hooks(Session& session) {
    PitHooks h;
    Session* s = &session;
    // 0x354E38: the fuel counted when the tank (car +0x5D0 = F +0x4CC) is above 0.
    h.fuel_on = [s](size_t i) { return s->tank(i) > 0.0f; };
    h.fuel = [s](size_t i) { return F(s->fuel(i)); };
    h.tank = [s](size_t i) { return F(s->tank(i)); };
    h.fuel_distance = [s](size_t i) { return F(s->fuel_distance(i)); };
    h.fuel_used = [s](size_t i) { return F(s->fuel_used(i)); };
    h.set_fuel = [s](size_t i, F f) { s->set_fuel(i, f); };
    // The wear rules: the switch (+0x68) and the limit (+0x20); each wheel's wear grip (+0x50) and tread (+0xAC).
    h.wear_on = [s] { return s->wear_rules().raw[0x68] != 0; };
    h.wear_limit = [s] {
        const auto& r = s->wear_rules().raw;
        return F::raw(u32(r[0x20]) | (u32(r[0x21]) << 8) | (u32(r[0x22]) << 16) | (u32(r[0x23]) << 24));
    };
    h.wear_grip = [s](size_t i) {
        std::vector<F> v;
        for (int w = 0; w < 4; ++w) v.push_back(F(s->wear_grip(i, w)));
        return v;
    };
    h.tread = [s](size_t i) {
        std::vector<F> v;
        for (int w = 0; w < 4; ++w) v.push_back(F(s->tread(i, w)));
        return v;
    };
    h.new_tire = [s](size_t i, int w) { s->new_tire(i, w); };
    // 0x35B060 → 0x363560: the car stood where it is, facing the box's heading (the record's frame: x, y, the ground's z).
    h.place_in_box = [s](size_t i, F heading) {
        const gt3r::sim::exact::Car& c = s->record(i);
        s->place_standing(i, c.pos.x, c.pos.y, c.ground_pos.z, heading);
    };
    h.hold = [s](size_t i, u16 steps) { s->set_pit_hold(i, steps); };
    return h;
}

PitStops::PitStops(const Field& field, const Program& program, size_t cars, PitHooks hooks, PitRace race)
    : flow_(program), hooks_(std::move(hooks)), race_(race), cars_(cars), last_d_(cars), last_lap_(cars, 0), stops_(cars, 0), records_(cars), default_choice_(cars, false), pending_(cars) {
    if (program.known()) race_.refuel_rate = program.b(0x620E25);
    for (const LinePoint& p : field.lines().line(CourseLines::kPitStop)) line_.dist.push_back(F(p.dist)), line_.heading.push_back(F(p.heading));
    // A car's box (0x363B10 by 0x46BDA0): the stop line's point 8 less its place on the grid (none without the line).
    for (size_t i = 0; i < cars; ++i) cars_[i].box = line_.dist.empty() ? 0 : gt3r::s8(8 - int(i));
}

void PitStops::before(const Field& field, std::span<race::CarInput> inputs) {
    (void)field;
    for (size_t i = 0; i < cars_.size() && i < inputs.size(); ++i) {
        PitCar& c = cars_[i];
        HoldEvents e;
        const u16 svc = c.service;
        const u8 chose_f = c.tire_front, chose_r = c.tire_rear;
        const bool held = c.hold != 0;
        // The stop's own timing (0x35C0B8, phase 4, while the hold counter runs): the counter, the tires.
        if (held) e = pit_hold_step(c, std::vector<gt3r::s8>(axle_));
        if (hooks_.hold) hooks_.hold(i, c.hold); // (the session holds the car while the counter runs)
        // The input phase after it (0x349FD0 → 0x35BFE8): the choice made for a B-spec car whose wait ran out (0x35BF08), or
        // the player's from the pit menu (choose()).
        if (e.default_choice) default_choice_[i] = true;
        if (default_choice_[i]) {
            default_choice_[i] = false;
            const Choice ch = pending_[i].value_or(Choice{1, 0xFE, 0xFE, 0xFE});
            pit_choice(c, ch.command, ch.front, ch.rear, ch.fuel);
            pending_[i].reset();
        }
        if (!held) continue;
        // At 645 with tires served: the stop's record takes the tires fitted (0xFE: the car's own kind again; a choice the
        // kind chosen).
        if (c.hold == 0x285 && (svc & 0xC0)) {
            const std::pair<int, int> own = hooks_.tires ? hooks_.tires(i) : std::pair<int, int>{-1, -1};
            StopRecord& r = records_[i][0];
            if (chose_f == 0xFE) r.front = own.first;
            if (chose_r == 0xFE) r.rear = own.second;
            if (e.chosen_tires && hooks_.change_tires) { // (the chosen kinds the pit has a part for: fitted and recorded)
                hooks_.change_tires(i, chose_f, chose_r, u8((svc & 0x40 ? 1 : 0) | (svc & 0x80 ? 2 : 0)));
                const std::pair<int, int> now = hooks_.tires ? hooks_.tires(i) : std::pair<int, int>{chose_f, chose_r};
                if (chose_f < 0xFE && now.first == chose_f) r.front = chose_f;
                if (chose_r < 0xFE && now.second == chose_r) r.rear = chose_r;
            } else {
                if (chose_f < 0xFE && e.chosen_tires) r.front = chose_f;
                if (chose_r < 0xFE && e.chosen_tires) r.rear = chose_r;
            }
        }
        if (e.chosen_params && hooks_.retune_tires) hooks_.retune_tires(i, u8((svc & 0x40 ? 1 : 0) | (svc & 0x80 ? 2 : 0)));
        for (int w : e.new_tires)
            if (hooks_.new_tire) hooks_.new_tire(i, w);
        // Standing in the box: the hold counter keeps the car (the preparation's hold, 0x35C0B8 / 0x357700).
        if (hooks_.hold) continue;
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
            need.wear = hooks_.wear_grip ? hooks_.wear_grip(i) : std::vector<F>{};
            need.burst = hooks_.tread ? hooks_.tread(i) : std::vector<F>{};
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
        // (The road's record alone: a car held in its box has its wheels flagged off the ground and stays on the lane.)
        for (const auto& g : session.record(i).g) lane = lane || (!g.miss && (g.flags & 1));
        c.flags = u8((c.flags & ~0x10) | (lane && race_.pits ? 0x10 : 0));
        field.set_marks(i, (c.flags & 0x10) != 0, c.tire_out != 0);
        std::vector<PitAction> acts;
        ServiceInputs sv;
        sv.pits_on = race_.pits, sv.lap_state = lap_state, sv.fuel_on = need.fuel_on, sv.control = gt3r::s8(control);
        sv.player_choice = race_.pit_menu;
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
