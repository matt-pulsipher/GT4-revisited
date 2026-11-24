#include "gt4r/sim/ai/pit_flow.h"

namespace gt4r::sim::ai {

namespace {

// The constants as the program's code builds them.
const F kHalf = F::raw(0x3F000000);       // 0.5
const F k25 = F::raw(0x41C80000);         // 25 m before the box: onto the stop line
const F k100kmh = F::raw(0x41DE38E2);     // 27.78 m/s: too fast to stop
const F k40kmh = F::raw(0x4131C71B);      // 11.11 m/s
const F k10kmh = F::raw(0x4031C71B);      // 2.78 m/s
const F k5kmh = F::raw(0x3FB1C71B);       // 1.39 m/s
const F k3kmh = F::raw(0x3F555554);       // 0.83 m/s
const F kZero = F::raw(0);
const F k500 = F::raw(0x43FA0000);        // 500 m driven before the fuel is reckoned
const F kFuelMargin = F::raw(0x3F866666); // 1.05

bool between(F before, F at, F now) { return before < at && at <= now; } // crossed going on: before < at <= now

} // namespace

F PitLine::marker(int k) const {
    if (dist.empty()) return kZero;
    const int n = int(dist.size());
    const int i = k < 0 ? n + k : k;
    return i >= 0 && i < n ? dist[size_t(i)] : kZero;
}

F PitLine::box_dist(int box) const { return box >= 0 && size_t(box) < dist.size() ? dist[size_t(box)] : kZero; }
F PitLine::box_heading(int box) const { return box >= 0 && size_t(box) < heading.size() ? heading[size_t(box)] : kZero; }

PitFlow::PitFlow(const Program& program) {
    if (!program.known()) return;
    for (u32 k = 0; k < message_time.size(); ++k) message_time[k] = u8(program.w(0x6207E4 + 8 * k));
}

void PitFlow::message(PitCar& c, int code) const {
    const u8 t = code >= 0 && size_t(code) < message_time.size() ? message_time[size_t(code)] : 0;
    if (t != 0 || c.status_time == 0) c.status_time = t, c.status = u8(code);
}

void PitFlow::step(PitCar& c, const PitInputs& in, const PitLine& line, std::vector<PitAction>& out, const std::function<u16()>& service) const {
    if (in.race_over || in.counting) return;
    if (in.crossed) {
        c.flags |= 1;
        const F v = gt3r::ee::sqrt(in.speed * in.speed + in.side_speed * in.side_speed);
        if (k5kmh < v) {
            if (!in.contact && !(u8(in.control - 1) < 2) && in.kind == 0) message(c, 1);
        } else if (v < k3kmh) {
            message(c, 0);
        }
    } else {
        c.flags &= u8(~1);
        message(c, 0);
    }
    if (in.kind == 1 && (in.control == 0 || in.control == 3) && c.x4B2 == 0) message(c, in.full_throttle ? 5 : 4);
    pass(c, in, line, out, service);
}

void PitFlow::pass(PitCar& c, const PitInputs& in, const PitLine& line, std::vector<PitAction>& out, const std::function<u16()>& service) const {
    auto act = [&](PitAction::Kind k, F v = F()) { out.push_back({k, v}); };
    c.speeding = 0;
    if (!in.lines) return;
    const F L = in.lap, half = L * kHalf;
    F d = in.d, dp = in.d_prev;
    if (d < half) d = d + L;
    if (dp < half) dp = dp + L;
    if (c.hold != 0) message(c, 0xB);
    const u8 control = u8(in.control);
    if (c.hold == 0 && (c.flags & 1)) {
        // Just over the line, in the pit lane's first stretch: the pit lane's speed, and a car out of the lane backwards.
        if (u8(control - 1) < 2 || in.kind != 0 || !(c.flags & 0x10)) return;
        if (!(line.marker(3) < d) || !(d < line.marker(-2))) return;
        if (k40kmh < in.speed) c.speeding = 2;
        else if (k10kmh < in.speed) c.speeding = 1;
        const F m = line.marker(-3);
        if (m < dp && d <= m) {
            act(PitAction::Leave);
            return message(c, 0xC);
        }
        const F v = in.speed, s = in.side_speed, lo = -k5kmh;
        if (!(k5kmh < v) && !(v < lo) && !(k5kmh < s) && !(s < lo)) return;
        if (in.contact) return;
        return message(c, 2);
    }
    const int s3 = in.lap_state == 2 ? 0 : in.lap_state; // the car's race over
    const u16 s5 = c.service;                             // the service as the step found it
    switch (in.kind) {
    case 2: { // down the pit lane to the box
        const F m1 = line.marker(1);
        if (m1 <= d && m1 < L && !(c.flags & 0x10)) { // past the lane's start off it: back to the race
            act(PitAction::Return);
            message(c, 0);
            c.service = 0;
            c.flags &= u8(~8);
            if (c.pad_state == 2) c.pad_state = 1;
            return;
        }
        if (between(dp, m1, d)) {
            act(PitAction::CountStop);
            c.service = service();
        }
        const F bd = line.box_dist(c.box) - k25;
        if (between(dp, bd, d)) {
            if (s3) {
                act(PitAction::Leave);
                return message(c, 8);
            }
            if (s5 == 0) {
                act(PitAction::Leave);
                return message(c, 9);
            }
            if (k100kmh <= in.speed) {
                act(PitAction::Leave);
                return message(c, 7);
            }
            return act(PitAction::Stop);
        }
        if (line.marker(2) <= d) message(c, 0xA);
        return;
    }
    case 3: { // on the stop line: the stop in the box
        if (c.hold == 0) {
            const F bd = line.box_dist(c.box);
            if (s5 != 0 && between(dp, bd, d)) {
                act(PitAction::PlaceInBox);
                c.hold = 0x2D1;
                c.x64C = in.control == 1 && c.x64C != -1 ? 300 : 0;
            } else if (bd + kHalf <= d) {
                act(PitAction::Leave);
            }
        } else if (c.hold == 0x2D0) {
            c.x64C = 0;
            act(PitAction::Heading, line.box_heading(c.box));
            message(c, 0xB);
        } else if (c.hold == 1) {
            c.service = 0;
            c.flags &= u8(~4);
        }
        if (!(c.service & 0x100)) return;
        if (c.hold == 0x285) {
            c.service |= 0x20;
            return;
        }
        if (u16(c.hold - 1) < 0x22A) act(PitAction::Refuel);
        return;
    }
    case 4: { // leaving down the pit lane
        const F m1 = line.marker(1);
        if (m1 <= d && d < L && !(c.flags & 0x10)) {
            act(PitAction::Return);
            message(c, 0);
            if (c.pad_state == 2) c.pad_state = 1;
        }
        F a, b;
        if (in.control != 0 || c.x4B2 != 0) a = line.marker(-1), b = line.marker(-2);
        else a = line.marker(-2), b = line.marker(-3);
        if (L + half <= a) a = a - L;
        if (between(dp, a, d)) { // out of the lane's end
            if (c.x4B2 == 0) {
                act(PitAction::Return);
                if (!s3) act(PitAction::Recount);
            } else {
                act(PitAction::EndRun);
            }
            c.flags &= u8(~8);
            if (c.pad_state == 2) c.pad_state = 0;
            return;
        }
        if (b <= d) {
            if (s3) return;
            if (in.control != 0 && in.control != 3) return;
            return message(c, in.full_throttle ? 5 : 4);
        }
        if (c.x4B2 != 0) return;
        return message(c, 0xA);
    }
    default:
        break;
    }
    if (in.control == 0 && in.kind != 1) { // a pad car's own visit
        if (c.pad_state != 0) {
            const F m0 = line.marker(0);
            if (between(dp, m0, d)) {
                act(PitAction::Enter);
                if (c.pad_state == 0 && kZero < in.speed) act(PitAction::CountStop);
                c.service = service();
                if (c.pad_state != 0) c.pad_state = 2;
                return;
            }
        }
        if (!(c.flags & 0x10)) return;
        const F m1 = line.marker(1);
        if (between(dp, m1, d)) {
            if (in.speed < kZero) {
                act(PitAction::Leave);
            } else {
                act(PitAction::Enter);
                act(PitAction::CountStop);
                c.service = service();
            }
            if (c.pad_state != 0) c.pad_state = 2;
            return;
        }
        const F m2 = line.marker(2);
        if (between(dp, m2, d)) {
            act(PitAction::Leave);
            c.service = 0;
            message(c, 9);
        }
        return;
    }
    // A computer's car (or the end of a run): onto the pit lane at its entry when it asked for the pits.
    if (c.flags & 0x10) {
        if (in.contact_43C && d < L) {
            act(PitAction::Enter);
            return act(PitAction::Driver);
        }
    }
    if (!(c.flags & 8)) return;
    if (s3) return;
    if (between(dp, line.marker(0), d)) act(PitAction::Enter);
}

int lap_state(const LapStateInputs& in) {
    if (in.finished) return 0;
    if (in.limit_minutes != 0 && in.clock_ms >= in.limit_minutes * 60000u) return 1;
    if (in.laps == 0 || in.lap < in.laps) return 0;
    const int n = in.laps + (in.d < in.lap_length * kHalf ? 1 : 0);
    return in.lap >= n ? 1 : 2;
}

void pit_need(PitCar& c, const PitNeed& n) {
    c.tire_out = 0;
    if (!n.allowed) return;
    if ((c.flags & 8) || c.x4B2 != 0 || n.lap_state != 0) return;
    u32 need = 0;
    if (n.fuel_on && k500 < n.fuel_distance && n.d < n.pit_marker) {
        const F rate = n.fuel_distance / n.fuel_used;
        F want = n.pit_marker - n.d;
        want = want + n.lap;
        want = want / rate;
        want = want * kFuelMargin;
        if (n.fuel < want) need = 1;
    }
    if (n.wear_on) {
        for (size_t w = 0; w < n.wear.size(); ++w) {
            if (w < n.burst.size() && n.burst[w] < kZero) {
                c.tire_out = 1;
                continue;
            }
            if (n.wear[w] < n.wear_limit) {
                need |= 1;
                c.tire_out = 1;
                break;
            }
        }
    }
    if (n.tire_state_4)
        for (u8 x : n.damaged)
            if (x) {
                need |= 2;
                break;
            }
    if (!need) return;
    c.flags |= 4;
    if (n.control == 2 || (n.control == 1 && c.x5C1 == 0)) c.flags |= 8;
}

void pit_refuel(PitCar& c, F& fuel, F tank, F dt, u8 rate_byte) {
    if (!(c.service & 0x100)) return;
    const F rate = gt3r::ee::int_to_f(s32(rate_byte)) * F::raw(0x3DCCCCCC);
    fuel = fuel + rate * dt;
    if (tank <= fuel) {
        fuel = tank;
        c.service ^= 0x100;
        if (!(c.service & 0xC0)) c.hold = 90;
    }
}

HoldEvents pit_hold_step(PitCar& c, const std::vector<s8>& axle) {
    HoldEvents e;
    if (c.hold == 0x2D1 && (c.service & 0x10)) {
        if (c.x64C > 0 && --c.x64C == 0) e.default_choice = true;
        return e;
    }
    if (c.hold == 90 && (c.service & 0x100)) return e;
    --c.hold;
    const bool front = c.service & 0x40, rear = c.service & 0x80;
    if (front || rear) {
        const bool chosen = c.tire_front < 0xFE || c.tire_rear < 0xFE;
        if (c.hold == 0x285 && chosen) e.chosen_tires = true;
        if (c.hold == 0x284 && chosen) {
            e.chosen_params = true;
            c.tire_front = c.tire_rear = 0xFF;
        }
        if (c.hold == 60) {
            for (size_t w = 0; w < axle.size(); ++w) {
                const bool f = axle[w] > 0, r = axle[w] < 0;
                if ((f && front) || (r && rear) || (!f && !r && (front || rear))) e.new_tires.push_back(int(w));
            }
            c.service &= u16(~0xC0);
        }
    }
    if (c.hold == 59) c.service |= 0x200;
    return e;
}

u16 service_of(const PitCar& c, const ServiceInputs& s) {
    if (!s.pits_on || c.pad_state != 0 || s.lap_state == 1 || s.layout_kind == 4) return 0;
    u16 v = s.fuel_on ? 0xC : 0x4;
    if (s.tires_allowed) v |= 2;
    if (s.control != 2 && s.player_choice) return (v & 0xC) ? u16(v | 1) : v;
    if (v & 4) v = u16((v | 0xC0) & ~4);
    if (v & 8) v = u16((v | 0x100) & ~8);
    return v;
}

} // namespace gt4r::sim::ai
