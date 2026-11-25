#include "gt4r/net/net_race.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>

namespace gt4r::net {

namespace {

constexpr u32 kGoDelayMs = 1500;     // the host's start signal to the countdown's first step: every machine hears it before
constexpr double kClockEase = 0.05;  // racing, a correction of the start's moment runs our race at most 5 % fast or slow
constexpr double kUnits = 3000.0;

} // namespace

std::vector<size_t> seat_order(const OnlineRace& race) {
    std::vector<size_t> s;
    if (race.ours >= 0 && size_t(race.ours) < race.cars.size()) s.push_back(size_t(race.ours));
    for (size_t i = 0; i < race.cars.size(); ++i)
        if (int(i) != race.ours) s.push_back(i);
    return s;
}

NetRace::NetRace(gt3r::net::Session& net, OnlineRace race) : net_(&net), race_(std::move(race)), seats_(seat_order(race_)) {
    for (size_t w = 0; w < seats_.size(); ++w) {
        const RaceCar& c = race_.cars[seats_[w]];
        Standing s;
        s.car = w, s.id = c.id, s.player = c.player, s.label = c.label, s.ours = w == 0;
        standing_.push_back(s);
    }
    seen_.assign(seats_.size(), -1), claimed_.assign(seats_.size(), false);
    lost_ = !net.joined();
}

void NetRace::attach(sim::Session& race) {
    sim_ = &race;
    std::vector<NetField::CarInfo> info;
    for (size_t w = 0; w < seats_.size(); ++w) {
        const RaceCar& c = race_.cars[seats_[w]];
        info.push_back({c.id, c.player, c.asm_on, c.tcs_on, w < race.cars().size() ? race.record(w).p.top_speed.host() : 0.0f});
    }
    field_ = std::make_unique<NetField>(race, std::move(info), 0);
    for (size_t w = 0; w < standing_.size(); ++w)
        if (standing_[w].retired && !standing_[w].ours) field_->set_gone(w);
}

int NetRace::world_of(u8 id) const {
    for (size_t w = 0; w < standing_.size(); ++w)
        if (standing_[w].id == id) return int(w);
    return -1;
}

void NetRace::handle(const gt3r::net::Event& e) {
    using K = gt3r::net::Event::Kind;
    const int w = world_of(e.player);
    switch (e.kind) {
    case K::Go:
        start_at_ = e.start_at_ms;
        if (phase_ == Phase::Loading) phase_ = Phase::Ready;
        break;
    case K::Finish:
        if (w > 0 && !standing_[size_t(w)].finished) {
            Standing& s = standing_[size_t(w)];
            s.finished = true;
            s.time_units = s64(std::llround(e.time * kUnits)), s.best_units = e.best >= 0 ? s64(std::llround(e.best * kUnits)) : -1;
            finishes_.push_back({size_t(w), s.time_units, s.best_units});
            if (field_) field_->set_finished(size_t(w));
            LOG_INFO("Online: %s finished in %.3f s", s.player.c_str(), e.time);
        }
        break;
    case K::Contact:
        if (w > 0 && field_ && int(e.target) == int(standing_[0].id) && field_->apply_hit(size_t(w), e.contact, clock_))
            LOG_INFO("Online: %s's machine saw a hit ours missed (%.0f N s), applied", standing_[size_t(w)].player.c_str(), std::hypot(e.contact.jx, e.contact.jz));
        break;
    case K::Flag: { // (the host: a warning about a player)
        const int by = world_of(e.flag.reporter);
        messages_.push_back("Warning: " + (w >= 0 ? standing_[size_t(w)].player : std::string("a player")) + " " + gt3r::net::flag_text(e.flag.kind) +
                            (by >= 0 ? " (seen by " + standing_[size_t(by)].player + ")" : std::string()));
        break;
    }
    case K::Retire:
        if (w > 0 && !standing_[size_t(w)].retired) {
            standing_[size_t(w)].retired = true;
            if (field_) field_->set_gone(size_t(w));
            messages_.push_back(standing_[size_t(w)].player + " left the race");
        }
        break;
    case K::Disconnected:
    case K::Rejected:
        lost_ = true;
        why_ = e.text;
        for (size_t k = 1; k < standing_.size(); ++k) {
            if (!standing_[k].finished) standing_[k].retired = true;
            if (field_) field_->set_gone(k);
        }
        messages_.push_back("Connection lost: " + e.text);
        if (phase_ != Phase::Over) phase_ = Phase::Racing; // (drive on alone)
        break;
    case K::Lobby: phase_ = Phase::Over; break;
    default: rest_.push_back(e); break;
    }
}

std::optional<gt3r::net::Event> NetRace::next_event() {
    if (rest_.empty()) return std::nullopt;
    gt3r::net::Event e = std::move(rest_.front());
    rest_.pop_front();
    return e;
}

void NetRace::frame(double dt) {
    if (phase_ == Phase::Over) return;
    if (lost_ && phase_ == Phase::Loading) phase_ = Phase::Racing;
    net_->poll();
    while (std::optional<gt3r::net::Event> e = net_->next_event()) {
        handle(*e);
        if (phase_ == Phase::Over) return;
    }
    if (lost_) return;
    if (phase_ == Phase::Loading && field_) {
        if (!loaded_sent_) net_->loaded(), loaded_sent_ = true;
        if (net_->is_host() && !go_sent_ && net_->all_loaded()) {
            net_->go(kGoDelayMs), go_sent_ = true;
            while (std::optional<gt3r::net::Event> e = net_->next_event()) handle(*e);
        }
    }
    // The start's moment as the clocks' agreement refines it; racing, a correction is eased in (over a long link the estimate
    // moves by some ms whenever its best sample goes, and the whole picture with it).
    if (const double refined = net_->race_start_ms(); refined > 0 && phase_ != Phase::Loading) {
        const double ease = phase_ == Phase::Racing && start_at_ > 0 ? kClockEase * dt * 1000.0 : 1e300;
        start_at_ = std::clamp(refined, start_at_ - ease, start_at_ + ease);
    }
    if (phase_ == Phase::Ready && net_->now() >= start_at_) phase_ = Phase::Racing, pace_.reset(), steps_done_ = 0;
    if (!field_) return;
    for (size_t w = 1; w < standing_.size(); ++w) {
        const gt3r::net::Link l = net_->link(standing_[w].id);
        Standing& s = standing_[w];
        s.linked = l.known, s.direct = l.direct, s.rtt_ms = l.known ? l.rtt_ms : 0;
        field_->set_link(w, l.known ? l.rtt_ms : -1, race_.rules.ghost_ping_ms);
    }
}

double NetRace::start_in() const { return phase_ == Phase::Ready ? std::max(0.0, (start_at_ - net_->now()) / 1000.0) : 0.0; }

int NetRace::steps(double dt, int most) {
    if (phase_ != Phase::Racing) {
        pace_.reset();
        return 0;
    }
    if (lost_ || start_at_ <= 0) return pace_.advance(dt);
    return pace_.follow((net_->now() - start_at_) * 60.0 / 1000.0, steps_done_, most);
}

void NetRace::before_step(s64 clock) {
    clock_ = clock;
    if (!field_) return;
    if (!lost_)
        for (size_t w = 1; w < standing_.size(); ++w)
            if (const gt3r::net::Session::Remote* r = net_->remote(standing_[w].id)) field_->receive(w, r->snap, clock);
    field_->before_step(clock);
}

void NetRace::after_step(s64 clock, const race::CarInput& ours, int laps) {
    clock_ = clock;
    if (!field_) return;
    field_->after_step(clock);
    const CarSnapshot s = field_->state(ours, clock, laps);
    if (!lost_ && net_->joined() && !retired_) {
        net_->send_state(s);
        ++states_sent_;
        for (const NetField::Sent& h : field_->hits()) {
            net_->send_contact(standing_[h.car].id, h.hit);
            LOG_INFO("Online: our car hit %s's (%.0f N s) at %.2f s", standing_[h.car].player.c_str(), std::hypot(h.hit.jx, h.hit.jz), double(clock) / kUnits);
        }
        for (const NetField::Flag& f : field_->take_flags()) net_->report_flag(standing_[f.car].id, f.kind, f.value);
        net_->poll(); // (out at once)
    }
    // A finish claimed against our own sight of it.
    for (size_t w = 1; w < standing_.size(); ++w)
        if (!claimed_[w] && standing_[w].finished && seen_[w] >= 0) claimed_[w] = true, field_->claim(w, double(standing_[w].time_units) / kUnits, double(seen_[w]) / kUnits);
}

void NetRace::set_progress(std::span<const double> distance, double lap) {
    if (field_) field_->set_progress(distance, lap);
}

void NetRace::finished(s64 units, s64 best_units) {
    if (finish_sent_ || standing_.empty()) return;
    finish_sent_ = true;
    Standing& s = standing_[0];
    s.finished = true, s.time_units = units, s.best_units = best_units;
    if (field_) field_->set_finished(0);
    if (!lost_ && net_->joined()) net_->send_finish(double(units) / kUnits, best_units >= 0 ? double(best_units) / kUnits : -1.0);
    LOG_INFO("Online: finished in %.3f s; %u states sent, hits sent %u, applied %u", double(units) / kUnits, states_sent_, field_ ? field_->hits_sent() : 0, field_ ? field_->hits_applied() : 0);
    for (size_t w = 1; field_ && w < standing_.size(); ++w)
        LOG_INFO("Online: %s: %u states, last error %.3f m, round trip %.1f ms", standing_[w].player.c_str(), field_->remote(w).states(), double(field_->remote(w).last_error()), standing_[w].rtt_ms);
}

void NetRace::observed_finish(size_t car, s64 units) {
    if (car < seen_.size() && seen_[car] < 0) seen_[car] = units;
}

void NetRace::retire() {
    if (retired_) return;
    retired_ = true;
    if (!standing_.empty()) standing_[0].retired = !standing_[0].finished;
    if (!lost_ && net_->joined()) net_->retire();
}

std::vector<NetRace::Standing> NetRace::standings() const {
    std::vector<Standing> out = standing_;
    if (field_)
        for (size_t w = 1; w < out.size() && w < field_->size(); ++w) out[w].ghost = field_->ghost(w), out[w].ghost_reason = field_->ghost_reason(w), out[w].has_state = field_->has_state(w);
    return out;
}

bool NetRace::all_done() const {
    return std::all_of(standing_.begin(), standing_.end(), [](const Standing& s) { return s.finished || s.retired; });
}

} // namespace gt4r::net
