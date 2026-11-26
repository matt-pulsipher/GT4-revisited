#pragma once

// What changes a car outside its state (docs/specs/net.md "Service"): a pit stop's hold counter, the car stood in its box and
// turned to it, the tires fitted and their base grips, new tread, the fuel put in, the pit pass's flags, a B-spec driver's pace
// and shift factor. None of it is in net::CarSnapshot, so its
// owner's machine logs each as it happens, by the race's clock, and sends the last seconds of them reliably with its car-info
// block (after the lobby's record, which stays as it is: net/lobby.h); every other machine applies them to its prediction of
// that car at the same moment of the car's run (net/remote_car.h), so the prediction runs on the new tires and fuel.
//
//   owner:   ServiceLog log; each step log.hold(clock, count) and log.add(event); log.take_changed() -> publish
//            with_service(record, {round, log.recent(clock)}) through Session::set_car_info.
//   others:  service_of(block) -> RemoteCar::add_service(events).

#include "gt4r/base/types.h"

#include "net/protocol.h"

#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace gt4r::net {

using gt3r::s32;
using gt3r::s64;

struct ServiceEvent {
    enum Kind : u8 {
        // Before the car's step from `clock` (the stop's timing, 0x35C0B8, in the step's preparation):
        Hold = 1, // the hold counter (F +0x4BA) from this step: value bits 0 - 15 the count, bit 16 standing (else counted down
                  // one a step, to 0)
        Tires,    // 645: the kinds chosen fitted (0x3F3B68, 0x361570): value front | rear << 8 | axles << 16 (0xFF none,
                  // 0xFE the kind fitted)
        Grips,    // 644: the tire records' base grips (0x361798) for the axles `value`
        NewTires, // 60: new tires (0x362E58) on the wheels of the mask `value`
        Driver,   // a B-spec driver's pace (car +0x81, the TCS target's class) | its shift factor (car +0x83) << 8, as the
                  // drivers set them for the step (0x3F19E0)
        // After the car's step that ends at `clock`, before its state (the pit pass, 0x35B060, and the refuelling, 0x354D60):
        Box,      // stood in its pit box (0x363560), its hold turning it to the heading `value` (float bits)
        Face,     // turned to the heading `value` (F +0x570)
        Fuel,     // the fuel (car +0x5D4) set to `value` liters (float bits): the last of a refuelling
        Flags,    // the pit pass's flags (F +0x4B1, Session::set_pit_flags) changed to `value`: bit 0 the line just crossed (the
                  // active steering's pause), 3 the pits asked for
    };
    s32 clock = 0;
    Kind kind = Hold;
    u32 value = 0;

    bool after() const { return kind >= Box; }
    // Its place on the car's run: 4 x clock, 2 less after a step; a state of the clock comes at 4 x clock - 1.
    s64 moment() const { return 4 * s64(clock) - (after() ? 2 : 0); }
    static s64 state_moment(s64 clock) { return 4 * clock - 1; }
    bool operator==(const ServiceEvent&) const = default;
};

// The hold counter `events` give the car's step from `clock` (0 with none).
u16 hold_at(const std::vector<ServiceEvent>& events, s64 clock);

// The section of a car-info block: the race's round (the lobby's, so a block left from an earlier race is not taken) and the
// events, oldest first.
struct CarService {
    u16 round = 0;
    std::vector<ServiceEvent> events;
    bool operator==(const CarService&) const = default;
};
// `record` (the lobby's block, a section it may end with left off) with the section after it: at most `limit` bytes, the
// oldest events left out to fit.
Bytes with_service(ByteSpan record, const CarService& service, size_t limit = 1024);
// The block without its section.
ByteSpan record_of(ByteSpan block);
// The section of a block (nullopt: none, another version's or malformed).
std::optional<CarService> service_of(ByteSpan block);

// The owner's log of its car's service.
class ServiceLog {
public:
    static constexpr s64 kKeep = 5 * 3000; // the events of the last 5 s are sent

    // Every step of the car (`clock` the step's start): the hold counter its pit stop gives it.
    void hold(s32 clock, u16 count);
    // An event as it happens. A Fuel event is kept back until a step passes without one (a refuelling sends its last); Flags
    // and Driver events that say what the last said are left out.
    void add(const ServiceEvent& e);
    // After every step (`clock` its end): a refuelling ended.
    void end_step(s32 clock);
    // New events since the last call.
    bool take_changed() { return std::exchange(changed_, false); }
    std::vector<ServiceEvent> recent(s64 clock) const;
    const std::deque<ServiceEvent>& events() const { return events_; }
    void clear() { *this = {}; }

private:
    void push(const ServiceEvent& e);
    std::deque<ServiceEvent> events_;
    std::optional<ServiceEvent> fuel_;
    std::vector<ServiceEvent> holds_; // (the last hold events: what hold_at gives)
    u16 last_count_ = 0;
    u8 last_flags_ = 0;
    std::optional<u32> last_driver_;
    bool fuel_this_step_ = false;
    bool changed_ = false;
};

} // namespace gt4r::net
