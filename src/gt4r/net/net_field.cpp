#include "gt4r/net/net_field.h"

#include <algorithm>
#include <cmath>

namespace gt4r::net {

namespace {

constexpr s64 kHitWait = 150;           // 0.05 s
constexpr s64 kContactWindow = 600;     // 0.2 s: our own contact this recent makes theirs a duplicate
constexpr s64 kStartGhost = 3 * 3000;   // the cars do not touch in the race's first three seconds
constexpr s64 kShove = 3000;            // a contact this recent explains a jump in speed
constexpr double kLapped = 0.9;         // of a lap apart: one is lapping the other
constexpr float kMostImpulse = 60000;   // N s: more than any hit between two cars gives

} // namespace

NetField::NetField(sim::Session& race, std::vector<CarInfo> cars, size_t ours) : race_(&race), ours_(ours) {
    cars_.resize(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) {
        cars_[i].info = std::move(cars[i]);
        if (i == ours_) continue;
        race_->set_posed(i, true);
        cars_[i].remote.attach(race_->solo(i));
        cars_[i].check.set_car(cars_[i].info.top_speed > 0 ? cars_[i].info.top_speed : 150.0f);
    }
    update_ghosts(-1);
}

int NetField::index_of(u8 id) const {
    for (size_t i = 0; i < cars_.size(); ++i)
        if (cars_[i].info.id == id) return int(i);
    return -1;
}

void NetField::before_step(s64 clock) {
    update_ghosts(clock);
    apply_hits(clock);
    for (size_t i = 0; i < cars_.size(); ++i) {
        Car& c = cars_[i];
        if (i == ours_ || c.gone) continue;
        c.remote.set_surroundings(race_->slipstream(i), race_->line_heading(i), race_->line_mode(i), race_->record(i).time_scale);
        if (const sim::Session* s = c.remote.predict(clock + kClockPerStep)) {
            race_->pose(i, *s);
            race_->set_shown(i, c.remote.shown(clock + kClockPerStep));
        }
        race_->set_ghost(i, c.ghost);
    }
}

void NetField::after_step(s64 clock) {
    hits_.clear();
    for (const sim::Session::CarContact& k : race_->car_contacts()) {
        const bool struck = size_t(k.struck) == ours_, striker = size_t(k.striker) == ours_;
        if (struck == striker) continue;
        const size_t other = size_t(struck ? k.striker : k.struck);
        if (other >= cars_.size()) continue;
        cars_[other].contact = clock;
        const float sign = struck ? -1.0f : 1.0f; // (the impulse the other car took)
        hits_.push_back({other, {tick_ + 1, k.x, k.z, sign * k.jx, sign * k.jz}});
        ++hits_sent_;
    }
}

CarSnapshot NetField::state(const race::CarInput& input, s64 clock, int laps) {
    std::copy_backward(inputs_, inputs_ + gt3r::net::kInputHistory - 1, inputs_ + gt3r::net::kInputHistory);
    const gt3r::sim::exact::Car& rec = race_->record(ours_);
    if (rec.control != 0) { // (the computer drives it: what he did with the pedals)
        race::CarInput c;
        c.computer = true, c.throttle = rec.throttle_pedal.host(), c.brake = rec.brake_pedal.host(), c.reverse = rec.reverse_req != 0;
        inputs_[0] = pad_input(c);
    } else inputs_[0] = pad_input(input);
    input_count_ = std::min(input_count_ + 1, gt3r::net::kInputHistory);
    const CarInfo& me = cars_[ours_].info;
    CarSnapshot s = make_snapshot(*race_, ours_, car_entry(*race_, ours_, me.asm_on, me.tcs_on), ++tick_, s32(clock), laps, inputs_, input_count_);
    s.player = me.id;
    // Put somewhere else than its own motion took it (a pit stop's box, 0x363560): no teleport to the others' checks.
    const Vec3 at{s.state.position.x, s.state.position.y, s.state.position.z}, v{s.state.velocity.x, s.state.velocity.y, s.state.velocity.z};
    if (placed_ && std::hypot(at.x - place_.x - velocity_.x / 60, at.z - place_.z - velocity_.z / 60) > 3.0f) respawn_ = 30;
    placed_ = true, place_ = at, velocity_ = v;
    s.respawned = respawn_ > 0;
    if (respawn_ > 0) --respawn_;
    return s;
}

void NetField::receive(size_t car, CarSnapshot s, s64 clock) {
    if (car >= cars_.size() || car == ours_ || cars_[car].gone) return;
    Car& c = cars_[car];
    if (c.remote.has() && s.tick <= c.remote.latest().tick) return;
    if (!sane(s, race_->spec(car).gears())) return;
    complete_snapshot(s);
    c.remote.receive(s, clock);
    const bool contact = c.contact > clock - kShove || c.reported > clock - kShove;
    auto report = [&](std::optional<gt3r::race::Plausibility::Flag> f) {
        if (f) flags_.push_back({car, f->kind, f->value});
    };
    report(c.check.on_state(s, contact));
    report(c.check.on_prediction_error(c.remote.last_error(), c.remote.last_horizon(), contact));
}

void NetField::take_hit(size_t car, const Hit& hit, s64 clock) {
    if (car >= cars_.size() || car == ours_) return;
    cars_[car].reported = clock;
    if (waiting_.size() < 64) waiting_.push_back({car, hit, clock + kHitWait});
}

void NetField::apply_hits(s64 clock) {
    for (size_t k = 0; k < waiting_.size();) {
        const Waiting w = waiting_[k];
        if (w.due > clock) {
            ++k;
            continue;
        }
        waiting_.erase(waiting_.begin() + std::ptrdiff_t(k));
        const Car& c = cars_[w.car];
        const Hit& h = w.hit;
        if (c.ghost || c.gone || c.contact >= clock - kContactWindow) continue;
        const float j = std::hypot(h.jx, h.jz);
        if (!(j > 0) || !std::isfinite(j) || !std::isfinite(h.point_x) || !std::isfinite(h.point_z)) continue;
        const Vec3 at = race_->cars()[ours_].position;
        if (std::hypot(h.point_x - at.x, h.point_z - at.z) > 15.0f) continue; // (nowhere near our car)
        const float f = j > kMostImpulse ? kMostImpulse / j : 1.0f;
        // (the arm the point had to our car when it was met: the car has moved on since)
        race_->apply_impulse(ours_, h.point_x + velocity_.x * float(kHitWait) / 3000.0f, h.point_z + velocity_.z * float(kHitWait) / 3000.0f, h.jx * f, h.jz * f);
        ++hits_applied_;
    }
}

void NetField::set_progress(std::span<const double> distance, double lap) {
    for (size_t i = 0; i < cars_.size() && i < distance.size(); ++i)
        cars_[i].lapped = lap > 0 && i != ours_ && std::fabs(distance[i] - distance[ours_]) > lap * kLapped;
}

void NetField::set_link(size_t car, double rtt_ms, int limit_ms) {
    if (car < cars_.size()) cars_[car].late = limit_ms > 0 && rtt_ms > double(limit_ms);
}

void NetField::set_gone(size_t car) {
    if (car >= cars_.size() || car == ours_) return;
    cars_[car].gone = true;
    race_->set_ghost(car, true);
    update_ghosts(0);
}

void NetField::set_finished(size_t car) {
    if (car < cars_.size()) cars_[car].finished = true;
}

void NetField::update_ghosts(s64 clock) {
    for (size_t i = 0; i < cars_.size(); ++i) {
        Car& c = cars_[i];
        if (i == ours_) continue;
        const bool no_state = !c.remote.has();
        c.why = c.gone ? "left the race" : c.finished || cars_[ours_].finished ? "finished" : no_state ? "no state yet" : c.late ? "round trip above the lobby's limit" :
                clock < kStartGhost ? "the start" : c.lapped ? "a lap apart" : "";
        c.ghost = c.why[0] != 0;
        race_->set_ghost(i, c.ghost);
    }
}

std::vector<NetField::Flag> NetField::take_flags() { return std::exchange(flags_, {}); }

void NetField::claim(size_t car, double claimed_s, double observed_s) {
    if (car >= cars_.size() || car == ours_) return;
    if (auto f = cars_[car].check.on_finish(claimed_s, observed_s)) flags_.push_back({car, f->kind, f->value});
}

} // namespace gt4r::net
