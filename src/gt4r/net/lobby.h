#pragma once

// The lobby of an online race (docs/specs/net.md "Lobby"): what the program's LAN battle keeps of each machine while its
// menus run (the member records behind the natives raceMenuNotify... / raceMenuPeek..., 0x1E5750 on; RTIME's client table),
// on the shared engine's session. Each player's record travels as one opaque block in the session's car info, sent whole
// when it changes, so nothing is added to the wire; the host's record also carries the race's settings.
//
//   net::Lobby lobby;
//   lobby.session().set_disc(...) (net::online_disc); lobby.session().host(...) / join(...);
//   lobby.begin("Player1"); every frame: lobby.poll();
//   the menus' natives (menu/game_network.*) notify and peek; lobby.take_race() gives the race once the host loads it.

#include "gt4r/net/car_choice.h"

#include "net/session.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::net {

constexpr int kMaxLobbyPlayers = 6; // (the pregrid page's slots; the grid's places)

// What the host lets race, ours (the original's LAN battle has no such rules): 0 and true allow everything.
struct LobbyLimits {
    u16 max_power = 0;        // PS as the car races (0: no limit)
    bool garage_cars = true;  // Gran Turismo mode's cars, with their parts and settings
    bool any_car = true;      // every car of the car pages (false: the favorites alone, the original's)
    bool operator==(const LobbyLimits&) const = default;
};

// Why a car may not race by the host's limits, or cannot be built here ("" : it races).
// `unit`, `unit_name`: the player's unit of power (of PS).
std::string limits_problem(const CarBuilder& builder, const CarChoice& c, const LobbyLimits& limits, float unit = 1, const std::string& unit_name = "PS");

// A machine's record. Names as the program's natives have them.
struct Member {
    std::string account, style = "play", target, course, car; // NotifyAccountName, Style, Target, Course, Car
    u8 paint = 0;
    int sequence = 0;     // NotifySequence: 1 the host waits, 2 connected, 3 the course page, 4 a course proposed, 5 the course
                          // taken, 6 the car page, 7 ready (the pregrid page), 8 off to the race
    int client_count = 0; // NotifyClientCount: the players the race waits for
    u32 time = 0;         // NotifyTime: the game time (ms) from which all are connected
    int ready_to_race = 0;
    std::array<u32, 8> entry{}; // the entry's setting (getBattleSettingEntry 0x162018)
    u16 sync = 0;         // the round whose barrier (enterNetSync) it entered
    u16 ended = 0;        // the round whose menus it ended (raceMenuEnd)
    std::optional<CarChoice> choice; // the car it races
    // The host's own:
    u16 round = 1;        // the races of this session so far, + 1
    bool closed = false;  // setGameClosed
    RaceRules rules;
    LobbyLimits limits;
    bool operator==(const Member& o) const;
};
// A block of at most 1 KB; nullopt for another version's or a malformed one.
Bytes encode_member(const Member& m, bool host);
std::optional<Member> decode_member(ByteSpan data);

class Lobby {
public:
    Lobby();
    explicit Lobby(gt3r::net::Session& session);

    gt3r::net::Session& session() { return session_; }
    const gt3r::net::Session& session() const { return session_; }
    // A session open and, for a guest, accepted.
    bool active() const { return session_.active() && session_.joined(); }
    bool host() const { return active() && session_.is_host(); }

    // After host(...) / join(...) of the session: our record from its start.
    void begin(const std::string& account);
    // Leaves: the session closed, the records gone.
    void end();
    // Receives and sends; the session's events, as far as the lobby deals with them. Not while a race owns the session.
    void poll();
    // Why the lobby ended or the last try failed ("" : it did not), once.
    std::string take_error();
    bool closed() const { return closed_; } // the session ended by itself (its reason in take_error)

    // The members by the program's index (the host first), and ours.
    int count() const { return int(session_.players().size()); }
    int my_index() const;
    u8 id_of(int index) const;
    const Member& member(int index) const; // an empty record for a member not heard from
    const Member& host_member() const { return member(0); }
    std::string name(int index) const; // its account name, else the session's
    bool heard(int index) const;       // its record arrived, of this version
    const Member& mine() const { return mine_; }
    // Changes our record (sent when it differs).
    void change(const std::function<void(Member&)>& f);
    // The records changed since the last call (raceMenuChanged 0x1F7520).
    bool changed();

    // The program's conditions.
    u32 game_time() const;         // the host's clock, ms (0x4FB1E8)
    bool all_connected() const;    // 0x1F7460
    int style_count(const std::string& style) const; // 0x1F6D60
    bool menus_ended() const;      // raceMenuForceEnd 0x1F6AC0: somebody ended this round's menus
    void enter_sync();             // enterNetSync: our barrier of this round, our part of the last race's menus forgotten
    bool synced() const;           // netSync 0x1F75C8: every member entered it
    u16 round() const { return host_member().round; }
    bool lost() const { return closed_ || (alone_ && host()); } // isDisconnected, isClientDisconnected

    // The host, its menus done: loads the race once every player's are (sequence 8 and a car), and not while one of their
    // cars cannot race. `waiting_for` says what it waits for.
    void want_race(const std::string& course) { wanted_ = course; }
    std::string waiting_for();
    // The disc's side (net/car_choice.h), set by whoever has the disc: a car's digest, why a car cannot race here ("" : it
    // can), and its equipment as raced on a course.
    std::function<u64(const CarChoice&)> digest_car;
    std::function<std::string(const CarChoice&, const LobbyLimits&)> check_car;
    std::function<std::optional<sim::Equipment>(const CarChoice&, const std::string& course)> build_car;
    std::string car_problem(int index); // ("" : none, or it has no car yet)
    // Mod cars (formats/db/mod_cars.h, net/mod_car_package.h), set by whoever has the disc: `describe_car` fills our car's
    // package and donor (and offers the package to the session) as our record changes; `wants_package` says whether a car
    // received is one to fetch (this disc lacks it and has its donor); `on_package` takes a package fetched (its hash, the
    // car, who sent it) and returns "" when it is installed, else why not.
    std::function<void(CarChoice&)> describe_car;
    std::function<bool(const CarChoice&)> wants_package;
    std::function<std::string(const std::string& hash, Bytes blob, const CarChoice&, const std::string& from)> on_package;
    // The race the host loaded, once: every machine's car by its record. The race owns the session from here; end_race
    // after it.
    std::optional<OnlineRace> take_race();
    // The race is over: the host takes everyone back (Session::lobby), a new round.
    void end_race();
    bool racing() const { return racing_; }

private:
    void publish();
    void refresh(); // the others' records out of the session's car info
    void on_event(const gt3r::net::Event& e);

    gt3r::net::Session& session_;
    Member mine_;
    std::map<u8, Member> members_;
    std::map<u8, Bytes> seen_;
    std::optional<std::string> wanted_;
    std::optional<OnlineRace> race_;
    std::string error_;
    std::map<Bytes, std::string> problems_; // car_problem's, by the record (kept: the check builds the car)
    std::map<std::string, std::string> packages_; // the packages asked for, by hash: "" on the way or installed, else why not
    void fetch_packages();
    LobbyLimits problems_for_;
    u64 version_ = 0, version_seen_ = 0;
    u16 next_round_ = 0; // after a race: the round it leads to (the host's word of it may still be on its way)
    bool closed_ = false, alone_ = false, racing_ = false, begun_ = false, loading_ = false;
};

// The lobby the menus, the pages and the race share (its session: gt3r::net::online()).
Lobby& lobby();

} // namespace gt4r::net
