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
}

void RemoteCar::set_surroundings(gt3r::ee::F slipstream, std::optional<gt3r::ee::F> line_heading, u8 line_mode) {
    slip_ = slipstream, line_ = line_heading, line_mode_ = line_mode;
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
    if (from < 0) car_->hold_once({&in, 1});
    else car_->step_once({&in, 1});
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
        // The owner's own run from the state before to this one, on his frames.
        car_->restore(0, *base_);
        const s64 steps = (s64(s.clock) - base_clock_) / kClockPerStep;
        s64 at_clock = s64(s.clock) - std::min<s64>(steps, kMaxReplay) * kClockPerStep;
        for (s64 k = std::min<s64>(steps, kMaxReplay) - 1; k >= 0; --k, at_clock += kClockPerStep) {
            const bool sent = k < s64(s.input_count);
            if (physics_) step(sent ? s.inputs[k] : held_, s.entry, s.motion, sent, at_clock);
        }
    }
    put_motion(*car_, 0, s.motion);
    snap_ = s;
    have_ = true;
    held_ = s.input_count ? s.inputs[0] : PadInput{};
    base_clock_ = clock_ = s.clock;
    car_->save(0, *base_);
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
