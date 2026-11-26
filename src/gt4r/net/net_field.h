#pragma once

// The other players' cars in our race (docs/specs/net.md "Race"): every machine runs only its own car and poses the others
// from their owners' states. This is the race's side of it, without a socket: states come in and go out as the shared engine's
// net::CarSnapshot, hits as its net::Event::Hit; net_race.h ties it to a session.

#include "gt4r/net/remote_car.h"
#include "gt4r/sim/pit_tires.h"
#include "net/session.h"
#include "race/net_race.h"

#include <functional>
#include <string>
#include <vector>

namespace gt4r::net {

using gt3r::s64;
using Hit = gt3r::net::Event::Hit;

class NetField {
public:
    struct CarInfo {
        u8 id = 0;               // its player's
        std::string player;
        bool asm_on = true, tcs_on = true; // as its owner races it
        float top_speed = 0;     // m/s by its gearing (0: not known), for the plausibility checks
    };
    // `cars`: the race's cars in the session's order, ours (`ours`) among them; the others become posed cars of the session, which
    // lives longer than this.
    NetField(sim::Session& race, std::vector<CarInfo> cars, size_t ours = 0);

    // Before a step: every other car posed for its end (`clock`: the race's clock at the step's start). After it, at its end.
    void before_step(s64 clock);
    void after_step(s64 clock);

    // Our car's state after a step: `input` the driver's of this step, `laps` done. Its frames go with the next two states too.
    CarSnapshot state(const race::CarInput& input, s64 clock, int laps);
    void receive(size_t car, CarSnapshot s, s64 clock);

    // A car's service events from its owner (net/car_service.h): applied to its prediction where they came.
    void add_service(size_t car, const std::vector<ServiceEvent>& events, s64 clock);
    // The pit stop's tire change on the other cars' predictions: `make` gives each its hooks (RemoteCar::set_tire_change) on
    // its one-car session (tire_change_on: the program's, sim/ai/pit_tire_change.h).
    void set_tire_change(const std::function<void(sim::Session& solo, RemoteCar& car)>& make);

    // The hits of the last step between our car and another's: for that car's owner (the impulse his car took).
    struct Sent {
        size_t car = 0;
        Hit hit;
    };
    const std::vector<Sent>& hits() const { return hits_; }
    // A hit on our car that car's machine saw. It waits 0.05 s (between near machines it comes before our own step meets it) and
    // is applied unless our machine saw one with that car within 0.2 s, or it is a ghost here.
    void take_hit(size_t car, const Hit& hit, s64 clock);

    // The race's length along the course of each car (crossings x the lap + the place in it), and the lap, for the lapped rule.
    void set_progress(std::span<const double> distance, double lap);
    // The round trip to a car's owner (ms; negative: not known) and the lobby's limit (0: none).
    void set_link(size_t car, double rtt_ms, int limit_ms);
    void set_gone(size_t car);      // its player left: not in the race any more
    void set_finished(size_t car);  // it has finished: no contact any more
    // What a car is here: a ghost drives through and is drawn through.
    bool ghost(size_t car) const { return cars_[car].ghost; }
    const char* ghost_reason(size_t car) const { return cars_[car].why; }
    bool gone(size_t car) const { return cars_[car].gone; }
    bool has_state(size_t car) const { return cars_[car].remote.has(); }

    // What another player's car did that a car of its kind does not (the host is told): since the last call.
    struct Flag {
        size_t car = 0;
        gt3r::net::FlagKind kind;
        float value = 0;
    };
    std::vector<Flag> take_flags();
    // His claimed finish against when we saw it.
    void claim(size_t car, double claimed_s, double observed_s);

    // The number of our last state (a race set up anew goes on from it: the others take newer ones only).
    u32 tick() const { return tick_; }
    void set_tick(u32 t) { tick_ = t; }

    size_t size() const { return cars_.size(); }
    size_t ours() const { return ours_; }
    const CarInfo& info(size_t car) const { return cars_[car].info; }
    int index_of(u8 id) const;
    const RemoteCar& remote(size_t car) const { return cars_[car].remote; }
    RemoteCar& remote(size_t car) { return cars_[car].remote; }
    u32 hits_applied() const { return hits_applied_; }
    u32 hits_sent() const { return hits_sent_; }

private:
    struct Car {
        CarInfo info;
        RemoteCar remote;
        gt3r::race::Plausibility check;
        bool ghost = false, gone = false, finished = false, lapped = false, late = false;
        const char* why = "";
        s64 contact = -1 << 30; // the race's clock at our last contact with it
        s64 reported = -1 << 30; // and at the last one another machine reported of it
    };
    void update_ghosts(s64 clock);
    void apply_hits(s64 clock);
    struct Waiting {
        size_t car = 0;
        Hit hit;
        s64 due = 0;
    };
    std::vector<Waiting> waiting_;
    bool placed_ = false;       // our car's place and velocity after its last state
    Vec3 place_{}, velocity_{};
    int respawn_ = 0;           // states left that say our car was put somewhere (out of its pit box)

    sim::Session* race_;
    std::vector<Car> cars_;
    size_t ours_ = 0;
    std::vector<Sent> hits_;
    std::vector<Flag> flags_;
    PadInput inputs_[gt3r::net::kInputHistory];
    int input_count_ = 0;
    u32 tick_ = 0;
    u32 hits_applied_ = 0, hits_sent_ = 0;
};

// The program's pit tire change (sim/ai/pit_tire_change.h) for NetField::set_tire_change, on the race's database and program,
// which must outlive the race; `read` reads the database's files.
std::function<void(sim::Session& solo, RemoteCar& car)> tire_change_on(db::SpecDb& db, const sim::Program& program, sim::FileReader read);

} // namespace gt4r::net
