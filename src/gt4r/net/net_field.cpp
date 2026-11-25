#include "gt4r/net/net_field.h"

#include <algorithm>
#include <cmath>

namespace gt4r::net {

namespace {

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
    for (size_t i = 0; i < cars_.size(); ++i) {
        Car& c = cars_[i];
        if (i == ours_ || c.gone) continue;
        c.remote.set_surroundings(race_->slipstream(i), race_->line_heading(i), race_->line_mode(i));
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
    inputs_[0] = pad_input(input);
    input_count_ = std::min(input_count_ + 1, gt3r::net::kInputHistory);
    const CarInfo& me = cars_[ours_].info;
    CarSnapshot s = make_snapshot(*race_, ours_, car_entry(*race_, ours_, me.asm_on, me.tcs_on), ++tick_, s32(clock), laps, inputs_, input_count_);
    s.player = me.id;
    return s;
}

void NetField::receive(size_t car, CarSnapshot s, s64 clock) {
    if (car >= cars_.size() || car == ours_ || cars_[car].gone) return;
    Car& c = cars_[car];
    if (c.remote.has() && s.tick <= c.remote.latest().tick) return;
    complete_snapshot(s);
    c.remote.receive(s, clock);
    const bool contact = c.contact > clock - kShove || c.reported > clock - kShove;
    auto report = [&](std::optional<gt3r::race::Plausibility::Flag> f) {
        if (f) flags_.push_back({car, f->kind, f->value});
    };
    report(c.check.on_state(s, contact));
    report(c.check.on_prediction_error(c.remote.last_error(), c.remote.last_horizon(), contact));
}

bool NetField::apply_hit(size_t car, const Hit& hit, s64 clock) {
    if (car >= cars_.size() || car == ours_) return false;
    Car& c = cars_[car];
    c.reported = clock;
    if (c.ghost || c.gone || c.contact >= clock - kContactWindow) return false;
    const float j = std::hypot(hit.jx, hit.jz);
    if (!(j > 0) || !std::isfinite(j) || !std::isfinite(hit.point_x) || !std::isfinite(hit.point_z)) return false;
    const course::Vec3 at = race_->cars()[ours_].position;
    if (std::hypot(hit.point_x - at.x, hit.point_z - at.z) > 10.0f) return false; // (nowhere near our car)
    const float k = j > kMostImpulse ? kMostImpulse / j : 1.0f;
    race_->apply_impulse(ours_, hit.point_x, hit.point_z, hit.jx * k, hit.jz * k);
    ++hits_applied_;
    return true;
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
