#include "gt4r/net/remote_car.h"

#include <algorithm>
#include <cmath>

namespace gt4r::net {

namespace {


constexpr double kUnits = 3000.0;

float wrap(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

} // namespace

void RemoteCar::attach(std::unique_ptr<sim::Session> solo) {
    car_ = std::move(solo);
    if (!car_) return;
    base_ = std::make_unique<sim::Session::Memory>();
    car_->save(0, *base_);
    have_ = false;
    ntrail_ = 0;
    history_.clear();
    cursor_ = std::numeric_limits<s64>::min();
}

void RemoteCar::set_surroundings(gt3r::ee::F slipstream, std::optional<gt3r::ee::F> line_heading, u8 line_mode, gt3r::ee::F time_scale) {
    slip_ = slipstream, line_ = line_heading, line_mode_ = line_mode, scale_ = time_scale;
}

RemoteCar::Place RemoteCar::place(s64 clock) const {
    const gt3r::sim::exact::Car& c = car_->record(0);
    return {clock, {c.pos.x.host(), c.pos.z.host(), -c.pos.y.host()}, c.heading.host()};
}

// One step of the car alone, from the race's clock `from`: the race's countdown holds it below 0.
void RemoteCar::step(const PadInput& p, const CarEntry& e, const CarMotion& m, bool owners, s64 from) {
    if (!physics_) { // dead reckoning: along the velocity, turning on
        CarMotion d = car_motion(*car_, 0);
        auto f = [](u32 bits) { return gt3r::ee::F::raw(bits); };
        const gt3r::ee::F dt(1.0f / 60);
        for (size_t k = 0; k < 3; ++k) d.pos[k] = (f(d.pos[k]) + f(d.vel[k]) * dt).bits;
        d.heading = (f(d.heading) + f(d.yaw_rate) * dt).bits;
        put_motion(*car_, 0, d);
        return;
    }
    race::PadRecord pad;
    const race::CarInput in = driver_input(p, e, m, pad, owners);
    if (line_) car_->set_line_heading(0, *line_, line_mode_);
    car_->set_slipstream(0, slip_);
    car_->set_time_scale_raw(0, scale_.bits);
    // Its service: what came before the step (the stop's timing in the preparation), the hold counter its owner's pit stop gives
    // it (out of the countdown, as the owner's race does), and what came after it.
    apply_events(ServiceEvent::state_moment(from) + 1);
    if (from >= 0) car_->set_pit_hold(0, hold_at(service_, from));
    if (from < 0) car_->hold_once({&in, 1});
    else car_->step_once({&in, 1});
    apply_events(ServiceEvent::state_moment(from + kClockPerStep) - 1);
}

void RemoteCar::apply_one(const ServiceEvent& e) {
    using K = ServiceEvent::Kind;
    switch (e.kind) {
    case K::Hold: break; // (each step's, by hold_at)
    case K::Tires:
        if (change_tires_) change_tires_(u8(e.value), u8(e.value >> 8), u8(e.value >> 16));
        break;
    case K::Grips:
        if (retune_tires_) retune_tires_(u8(e.value));
        break;
    case K::NewTires:
        for (int w = 0; w < 4; ++w)
            if ((e.value >> w) & 1) car_->new_tire(0, w);
        break;
    case K::Box: car_->stop_in_box(0, gt3r::ee::F::raw(e.value)); break;
    case K::Face: car_->face(0, gt3r::ee::F::raw(e.value)); break;
    case K::Fuel: car_->set_fuel(0, gt3r::ee::F::raw(e.value)); break;
    case K::Flags: car_->set_pit_flags(0, u8(e.value)); break;
    }
}

void RemoteCar::apply_events(s64 upto) {
    if (upto <= cursor_) return;
    for (const ServiceEvent& e : service_) {
        const s64 m = e.moment();
        if (m > upto) break;
        if (m > cursor_) apply_one(e);
    }
    cursor_ = upto;
}

void RemoteCar::add_service(const std::vector<ServiceEvent>& events, s64 clock) {
    std::vector<ServiceEvent> fresh;
    for (const ServiceEvent& e : events) {
        if (std::find(service_.begin(), service_.end(), e) != service_.end() || std::find(fresh.begin(), fresh.end(), e) != fresh.end()) continue;
        fresh.push_back(e);
        auto at = service_.end();
        while (at != service_.begin() && std::prev(at)->moment() > e.moment()) --at;
        service_.insert(at, e);
    }
    if (fresh.empty() || !car_ || !have_ || !physics_) return;
    s64 earliest = std::numeric_limits<s64>::max();
    for (const ServiceEvent& e : fresh) earliest = std::min(earliest, e.moment());
    if (earliest > ServiceEvent::state_moment(base_clock_)) { // (the base has not met them)
        if (earliest <= cursor_) rerun(); // (the run on from it has)
        return;
    }
    // The base has run past them: again from the newest state before them, through the states since.
    size_t j = history_.size();
    while (j > 0 && ServiceEvent::state_moment(history_[j - 1].state.clock) >= earliest) --j;
    if (j > 0) {
        std::vector<CarSnapshot> later;
        for (size_t k = j; k < history_.size(); ++k) later.push_back(history_[k].state);
        history_.resize(j);
        const Past& from = history_.back();
        *base_ = *from.car;
        base_clock_ = from.state.clock, snap_ = from.state;
        held_ = from.state.input_count ? from.state.inputs[0] : PadInput{};
        for (const CarSnapshot& s : later) advance(s);
    } else { // (older than the states kept: put in the base as it is)
        car_->restore(0, *base_);
        for (const ServiceEvent& e : fresh) apply_one(e);
        car_->save(0, *base_);
    }
    ++reruns_;
    rerun();
    (void)clock;
}

void RemoteCar::rerun() {
    const s64 at = clock_;
    const Place was = place(at);
    const sim::Session::Shown off = shown(at);
    car_->restore(0, *base_);
    clock_ = base_clock_, cursor_ = ServiceEvent::state_moment(base_clock_);
    run_to(at);
    const Place now = place(at);
    off_ = {was.pos.x + off.shift.x - now.pos.x, was.pos.y + off.shift.y - now.pos.y, was.pos.z + off.shift.z - now.pos.z};
    off_yaw_ = wrap(was.heading - now.heading + off.yaw);
    if (std::sqrt(off_.x * off_.x + off_.y * off_.y + off_.z * off_.z) > kSnapDistance) off_ = {}, off_yaw_ = 0;
    off_clock_ = at;
}

void RemoteCar::receive(const CarSnapshot& s, s64 clock) {
    if (!car_ || (have_ && s.tick <= newest_.tick)) return; // (late or twice)
    ++states_;
    newest_ = s;
    // A state from ahead of our race (our machine stalled, or the other runs a step early) waits until our race reaches its
    // time: the car is where it was at our race's time, not where it is at the wall's.
    if (have_ && s64(s.clock) > clock) {
        pending_.push_back(s);
        if (pending_.size() > kPending) pending_.pop_front();
        return;
    }
    pending_.clear(); // (older than this one)
    apply(s, clock);
}

void RemoteCar::apply(const CarSnapshot& s, s64 clock) {
    const bool first = !have_;
    Place was;
    sim::Session::Shown off;
    if (!first) {
        run_to(clock);
        was = place(clock), off = shown(clock);
        // How good the prediction was: where it had the car at this state's time.
        const Vec3 at{s.state.position.x, s.state.position.y, s.state.position.z};
        for (size_t k = 0; k < std::min(ntrail_, trail_.size()); ++k) {
            const Place& t = trail_[k];
            if (t.clock != s64(s.clock)) continue;
            const double dx = t.pos.x - at.x, dy = t.pos.y - at.y, dz = t.pos.z - at.z;
            last_error_ = float(std::sqrt(dx * dx + dy * dy + dz * dz));
            last_horizon_ = double(s.clock - snap_.clock) / kUnits;
            break;
        }
    }
    advance(s);
    run_to(clock);
    if (first) {
        off_ = {}, off_yaw_ = 0, off_clock_ = clock;
        return;
    }
    const Place now = place(clock);
    off_ = {was.pos.x + off.shift.x - now.pos.x, was.pos.y + off.shift.y - now.pos.y, was.pos.z + off.shift.z - now.pos.z};
    off_yaw_ = wrap(was.heading - now.heading + off.yaw);
    if (std::sqrt(off_.x * off_.x + off_.y * off_.y + off_.z * off_.z) > kSnapDistance) off_ = {}, off_yaw_ = 0;
    off_clock_ = clock;
}

// The base from the state it is at to `s`: the owner's own run from the state before, on his frames, with the service where it
// came; then the state put in.
void RemoteCar::advance(const CarSnapshot& s) {
    if (have_) {
        car_->restore(0, *base_);
        cursor_ = ServiceEvent::state_moment(base_clock_);
        const s64 steps = (s64(s.clock) - base_clock_) / kClockPerStep;
        s64 at_clock = s64(s.clock) - std::min<s64>(steps, kMaxReplay) * kClockPerStep;
        for (s64 k = std::min<s64>(steps, kMaxReplay) - 1; k >= 0; --k, at_clock += kClockPerStep) {
            const bool sent = k < s64(s.input_count);
            if (physics_) step(sent ? s.inputs[k] : held_, s.entry, s.motion, sent, at_clock);
        }
    }
    if (physics_) apply_events(ServiceEvent::state_moment(s.clock) - 1);
    put_motion(*car_, 0, s.motion);
    car_->set_driver_kind(0, s.entry.driver_kind);
    snap_ = s;
    have_ = true;
    held_ = s.input_count ? s.inputs[0] : PadInput{};
    base_clock_ = clock_ = s.clock;
    car_->save(0, *base_);
    cursor_ = ServiceEvent::state_moment(s.clock);
    if (physics_) {
        history_.push_back({s, std::make_shared<const sim::Session::Memory>(*base_)});
        if (history_.size() > kHistory) history_.pop_front();
    }
}

void RemoteCar::run_to(s64 clock) {
    while (clock_ < clock && clock_ - base_clock_ < kMaxAhead * kClockPerStep) {
        step(held_, snap_.entry, snap_.motion, false, clock_);
        clock_ += kClockPerStep;
        trail_[ntrail_++ % trail_.size()] = place(clock_);
    }
}

const sim::Session* RemoteCar::predict(s64 clock) {
    if (!car_ || !have_) return nullptr;
    // The newest waiting state our race has reached (its time at most the coming step's start: the pose is then a step of the
    // car's own from it).
    const s64 reach = clock - kClockPerStep;
    if (!pending_.empty() && s64(pending_.front().clock) <= reach) {
        while (pending_.size() > 1 && s64(pending_[1].clock) <= reach) pending_.pop_front();
        const CarSnapshot s = pending_.front();
        pending_.pop_front();
        apply(s, reach);
    }
    run_to(clock);
    return car_.get();
}

sim::Session::Shown RemoteCar::shown(s64 clock) const {
    const float k = std::exp(-float(double(std::max<s64>(0, clock - off_clock_)) / kUnits) / kBlend);
    if (k < 1e-3f) return {};
    return {{off_.x * k, off_.y * k, off_.z * k}, off_yaw_ * k};
}

} // namespace gt4r::net
