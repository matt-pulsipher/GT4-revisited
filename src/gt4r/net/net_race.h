#pragma once

// An online race on a session (docs/specs/net.md "Race"): from the race being loaded to the results, it owns the shared engine's
// net::Session (polling, the start handshake, the cars' states, finishes, hits). The race's own step stays the caller's: every
// frame, frame(); then as many steps as steps() says, each before_step() - the step - after_step().

#include "gt4r/net/net_field.h"
#include "gt4r/net/online_race.h"
#include "race/interp.h"

#include <deque>
#include <string>
#include <vector>

namespace gt4r::net {

// The order a machine builds the race's cars in: its own first, then the others in grid order (indices into OnlineRace::cars;
// the place on the grid of each is that index).
std::vector<size_t> seat_order(const OnlineRace& race);

class NetRace {
public:
    NetRace(gt3r::net::Session& net, OnlineRace race);

    const OnlineRace& race() const { return race_; }
    const std::vector<size_t>& seats() const { return seats_; } // world car -> OnlineRace::cars
    // The race's session once its cars are built in the order of seats(); again when the race is set up anew. Before it, frame()
    // only polls.
    void attach(sim::Session& race);
    NetField* field() { return field_.get(); }
    const NetField* field() const { return field_.get(); }

    enum class Phase {
        Loading, // the players' machines set the race up
        Ready,   // the host has given the start; its moment is still to come
        Racing,  // steps run
        Over,    // the host is back in its lobby, or we left
    };
    Phase phase() const { return phase_; }
    // Everything up to the next step: the network's events, the start, the round trips. `dt`: the frame's time (s).
    void frame(double dt);
    // Steps to run now (none before the start; after a stall at most `most` a frame), by the clock all the machines share; and the
    // way into the step after them (0 .. 1), for the blend between steps, by the same clock.
    int steps(double dt, int most = 30);
    float alpha() const { return pace_.alpha(); }
    // Seconds until the start's moment (the countdown's first step); 0 from it on.
    double start_in() const;

    // A step of the race: `clock` the race's, in units of 1/3000 s, 50 a step, negative while the start's hold runs
    // (race_clock(hold_left, clock)): at its start, and at its end with the input our car took and its laps done.
    void before_step(s64 clock);
    void after_step(s64 clock, const race::CarInput& ours, int laps);
    static s64 race_clock(int hold_left, s64 clock) { return hold_left > 1 ? -s64(hold_left - 1) * kClockPerStep : clock; }
    // Each car's distance from the start (crossings x the lap + its place in it), after a step.
    void set_progress(std::span<const double> distance, double lap);
    // What changed our car outside its state in the step that ends at `clock` (a pit stop: net/car_service.h; each step's hold
    // counter too), given before after_step: sent with our car-info block when there is something new.
    void service(const ServiceEvent& e) { service_.add(e); }
    const ServiceLog& service_log() const { return service_; }
    // The pit stop's tire change on the other cars' predictions (NetField::set_tire_change), kept for a race set up anew.
    void set_tire_change(std::function<void(sim::Session& solo, RemoteCar& car)> make);
    // Our car finished (the race's time and the best lap in units of 1/3000 s; -1 no lap); a car our race saw finish.
    void finished(s64 units, s64 best_units);
    void observed_finish(size_t car, s64 units);
    // We leave the race (the lobby's session stays).
    void retire();

    // A car as the race's display and the results need it, in the world's order.
    struct Standing {
        size_t car = 0; // the world's index
        u8 id = 0;
        std::string player, label;
        bool ours = false;
        bool finished = false; // by its owner
        s64 time_units = -1, best_units = -1;
        bool retired = false;  // left the race
        bool ghost = false;
        const char* ghost_reason = "";
        bool has_state = false;
        bool linked = false, direct = false;
        double rtt_ms = 0;
    };
    std::vector<Standing> standings() const;
    bool car_gone(size_t car) const { return car < standing_.size() && standing_[car].retired; }
    // The other cars' finishes as their owners sent them, not yet taken: for the race's own flow.
    struct Finish {
        size_t car = 0;
        s64 units = 0, best = -1;
    };
    std::vector<Finish> take_finishes() { return std::exchange(finishes_, {}); }
    bool all_done() const; // every car still in the race has finished

    bool over() const { return phase_ == Phase::Over; }
    bool lost() const { return lost_; }                  // the connection is gone: our car races on alone
    const std::string& ended_why() const { return why_; } // "" when the race ended as races do
    // What to tell the player ("Anna left the race"), oldest first.
    std::vector<std::string> take_messages() { return std::exchange(messages_, {}); }
    // Events the race has no use for (the lobby's).
    std::optional<gt3r::net::Event> next_event();

    u32 states_sent() const { return states_sent_; }

private:
    void handle(const gt3r::net::Event& e);
    int world_of(u8 id) const;
    void take_service(size_t car);    // a car's section of its car-info block
    void publish_service(s64 clock);  // ours

    gt3r::net::Session* net_;
    OnlineRace race_;
    std::vector<size_t> seats_;
    std::unique_ptr<NetField> field_;
    sim::Session* sim_ = nullptr;
    std::vector<Standing> standing_;
    Phase phase_ = Phase::Loading;
    bool loaded_sent_ = false, go_sent_ = false, lost_ = false, finish_sent_ = false, retired_ = false;
    double start_at_ = 0; // our clock (net::Session::now) at the countdown's first step
    long long steps_done_ = 0;
    gt3r::race::FixedStep pace_{60.0};
    s64 clock_ = -1 << 30;
    std::vector<Finish> finishes_;
    std::vector<s64> hit_logged_; // (the log's: when a hit with each car was last)
    std::vector<s64> seen_;       // our race's own sight of each car's finish (-1 none)
    std::vector<bool> claimed_;
    std::vector<std::string> messages_;
    std::deque<gt3r::net::Event> rest_;
    std::string why_;
    u32 states_sent_ = 0;
    ServiceLog service_;
    std::function<void(sim::Session& solo, RemoteCar& car)> tire_change_;
};

} // namespace gt4r::net
