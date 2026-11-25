# Online races

Two or more copies of the game race each other, each player with his own car: on one network (LAN) or
through a relay server. The transport is the shared engine's, unchanged (`engine/gt3r/src/net/`:
the lobby, the reliable channel, hole punching, the relay, the clocks' agreement; its
`docs/dev/ONLINE.md`). What is Gran Turismo 4's is here: the car's state, the posed cars, the
prediction, the race's rules, and the lobby's pages and cars.

## What the original does (program facts)

`CORE.GT4`, SCUS-97328 v1.01. ✔ read in the program, ❓ not read to the end.

### The LAN battle the player can reach: every machine runs every car

`RaceLanBattle` (vtable 0x677C28; display mode 17; a `RaceInputLan` per member; its
`RaceLanControlManager`, vtable 0x6785D8, over `PDISTD::ControlManagerLanBase` 0x689B28), started
by 0x10D080. Its conductor is `DynamicsConductorBattleMP` (vtable 0x679B38). It sends **the
drivers' frames, not the cars**:

- A member's message is 0x40 bytes a control frame (0x338088, through 0x501B20): the flags at
  +0x00 (bits 0 - 1 the pad's kind, 4 in the race, 8 a pause asked, 0x10 the race on again, 0x20
  leaving, its end code in bits 6 - 9), the race's 32-byte pad frame from +0x04 (the race input's
  virtual 5: the frame the input phase reads), the menu's and views' button words, a word with the
  picture's side and a time.
- 0x337A70 takes one complete set (a frame of every member) a step and gives each member's
  `RaceInputLan` (virtual 5, 0x335D18) his. A step waits until the set is there. With none for
  5 s (0x337340's 5,000,000 µs; 0x3372E8) the race shows `DISCONNECTED` for 5 s (0x337260) and
  ends (code 7).
- The race's random numbers start from the session's (0x6464DC; 0x12345678 without it; virtual
  103, 0x336DE8), so every machine draws alike.
- A pause by one stops all; only he ends it (0x337528 with the flags above). `LAN_heavy` halves
  the frames sent (one for two steps; virtual 99).
- The grid is the members' order (0x335B48 writes entry i's place i); nothing is drawn.
- The race row is `sample_entry_1` (the arcade project's `doGame`; `RaceInformation` begins with
  the RACE row's bytes, SPEC_DATABASE::RaceSpec 0x446F90): its start type, **the row's +0x40, is 2:
  a hold of 10 s with the opening shots** (so the race information's start type byte +0x40 and its
  seconds +0x41 are the row's own bytes), a standing start from the grid (the conductor's virtuals
  10 and 30 are the base's: no rolling start, no handicap; only `DynamicsConductorBattle2P`'s
  virtual 30, 0x347108, reads a handicap).
- The penalty's kind is for the arcade's single race alone: `DynamicsConductor`'s virtual 26 (0x34CA20) sets the rule
  block's +0x48 (40), +0x49 (8) and +0x4A (10 s of speed cap for kind 1, 0xFF the pit lane's mark for kind 2; 0 and
  0xFF, 0xFF for none) from the race information's +0x04 only when the race is a `RaceArcadeSingle` (0x3BFD00).
- The race information's settings (0x10D080, from the host's six words): +0x00 the laps, +0x04 the
  penalty's kind, +0x08 the tire damage, the same three again at +0x0C, +0x10 and **+0x14**,
  +0x18 the handicap, **+0x1C the boost**.
- `BattleMP`: control source 0 for every member (virtual 29, 0x34CE48), the collision step and the
  order as a single race's (virtuals 13 and 4: 0x34DA70, 0x365790), **tire wear** from +0x14
  (virtual 11, 0x354F30: the rule block's +0x2C .. +0x34 from the setting), **the boost** from
  +0x1C (virtual 12, 0x354EB8): setting 1 the cars behind hurried by up to 7 % past 20 m, fully
  past 200 m, 2 by up to 10 % past 10 m; chained along the order from the leader (the rubber band's
  mode 0), no car slowed.

### The battle that sends cars: `RaceNetBattle`

`RaceNetBattle` (vtable 0x6787C0; display mode 18) is in the program and no menu of the disc leads
to it ❓. Every machine runs its own car and sends **its state every step** (0x3F12B0, between
phases 12 and 13: the cars of control source 0 filtered by 0x3EFF98, packed by 0x3F0230, sent
through the race's virtual 84), 0x30 bytes:

| Offset | Field |
|---|---|
| +0x00 | s32 the race's time (1/3000 s, 0x3F12A0), -1 none |
| +0x04 | the gear (bits 0 - 2), 8 the brake on, 0x10 the clutch locked |
| +0x05 / +0x06 | the throttle F +0x494 x 64; s8 the steering F +0x468 in half degrees |
| +0x08 | x and y (F +0x00, +0x04); +0x10 the heading F +0x570 |
| +0x14 | the velocity's x and y (F +0x544, +0x548); +0x1C the yaw rate F +0x57C |
| +0x20 .. +0x28 | the accelerations along and across the body and of the yaw (F +0x550, +0x554, +0x588), filtered (0x3EFF98) |
| +0x2C | 2 x s16 the axles' longitudinal forces |

The others are entries of **control source 4** (virtual 29 with the race's virtual 83). Their own
phases run; 0x3F1368 then takes the two newest states (0x3F0498), runs each on to now along its
accelerations (0x3F0A48), blends the newer over the older within 3 steps and pulls the car to that
place and heading with a spring and a damper (0x3F0E18, 0x3F05E0; the settings' bytes at 0x621FF8,
the gains at 0x622010), gear, clutch, pedals and steering as sent. The collision step and
everything after it run on them as on any car. A member that has gone is a ghost (F +0x441 = 1).
The height, the attitude and the wheels are the receiving machine's own.

## The race

Every player's machine runs **his car alone**, on Gran Turismo 4's own step, and sends its state
after every step; the others' cars are posed there. (The original's LAN battle runs every car on
every machine instead and waits for everybody's frames; this is the shape of its `RaceNetBattle`,
with more of the car in a state and the car's own step as the prediction.)

Code: `src/gt4r/net/` (`car_state.*` the state, `remote_car.*` the prediction, `net_field.*` the
other cars in our race, without a socket, `net_race.*` the race on a session, `lan_rules.*`,
`online_race.h` what the lobby hands over), `sim/session.*` ("online races": posed cars, `solo`,
`save` / `restore`), `app/race_world.*` (`WorldEntry::remote`, `WorldSetup::lan_wear`),
`app/screen_race.cpp` (`online_`, `Race::step_online`), `race/race_flow.*` (`claim_finish`).

### The car's state

`net::CarSnapshot` of the shared engine, as it is (116 bytes on the wire while steering, 60 a
second; a race datagram about 132 bytes):

| In the state | Gran Turismo 4's record |
|---|---|
| place, velocity | F +0x00, F +0x544, bit for bit |
| heading, pitch, roll, yaw rate | F +0x570, +0x568, +0x56C, +0x57C, bit for bit |
| engine speed, steering command, axle speeds | F +0x4FC, F +0x468, F +0x52C, bit for bit |
| pitch and roll rates; rim speeds; suspension | in 1/1024 rad/s, 1/128 m/s, 1/65536 m |
| gear, clutch | F +0x520, +0x522 |
| pad frames, the last three | the first 16 of the frame's 32 bytes: +0 the axes' flags, +1 the controller's kind, +2 the shift, +4 the steering, +8 .. +0x0E the pedals and axes. **The nitrous button (+7) travels in bit 3 of the kind** (the kinds are 0 .. 6) |
| entry | the gearbox's style, the wheel's kind F +0x443, ASM and TCS, the control source F +0x466, the driver's kind F +0x467 |
| race clock | 1/3000 s, 50 a step; the countdown negative |

A car the computer drives (after its finish, down the pit lane, B-spec) sends its pedals as its
frame and the steering command with its motion (control source 1 or 2).

Every quantity a car's motion needs fits, for every car of the disc (three-wheelers, stepless and
hybrid boxes, layout 7's motors: `net_state_round_trip`, 97 cars, back within the units above, the
rest bit for bit). **What does not fit**: the frame's button bytes +0x10 .. +0x13 (a pit stop's
answer: which tires, fuel) and the pit's tire change itself, so a remote car's prediction keeps
the tires it started on. An engine field for it would be a reliable message of the game's own
(bytes the session passes on, like `CarInfo`, in a race).

### Posed cars

`Session::set_posed`: the car's phases 1 - 12 and 15 - 22 do not run; its record is put in before
each step as it will be at the step's end (`Session::pose`, the prediction's record whole, but for
the index, laps, place, finish and ghost flag, and who is near it). It takes part in

- **phase 13**, the collision step: its body moves along the posed motion (it starts back by the
  velocity and the yaw rate) from its own outline with no contact held; what the step makes of it
  is thrown away. Both machines solve a contact, each for its own car;
- **phase 14**: our slipstream and the drivers' view of it;
- the race's flow: crossings, laps, places.

Its driver plans along (its line's heading and mode go to its prediction's active steering).

### The prediction

A remote car is run on its own (`Session::solo`: the car's record, body and parameters alone with
the race's road and tables). It has two parts:

1. the car **as its owner last sent it**: from the state before, the owner's own steps are run
   again here on his own frames (every state carries the last three, so a lost datagram loses
   none), then the state is put in: a field that already is what the state says stays as it is. So
   what no state carries (the tires' and brakes' heat, wear, fuel, nitrous, the turbo, a hybrid's
   battery, the pedals' and the steering's filters) follows the owner's car step for step;
2. from there, **up to our race's clock** with his newest frame (no shift of its own), at most 24
   steps (0.4 s); an older car is held.

It drafts and is boosted as our race has it (F +0x59C and the time scale from the posed car). A
state from ahead of our race's clock waits until our race reaches it. The jump between what was
shown and a new prediction is blended out over 0.1 s (place and heading; jumps over 8 m are not);
collisions use the prediction itself.

Measured: two machines in one process, states through the wire format, each car driven with the
pad at Tsukuba (an NSX-R and an R34; `GT4R_NET_TABLE=1 gt4r_tests net_race_remote_car_error`).

60 s of Tsukuba, rms / worst in cm of the posed car against its owner's at the same race clock. One way: the delay, up
to a fifth more at random. *Smooth*: along the road with a steering weave; *jerky*: steering jabs, pedals let go and
stabbed at random, one car on the manual gearbox. The cars touch now and then (3 to 39 steps of a run); *apart* leaves
out the two seconds after a contact. No plausibility warning in any run.

| One way | Driving | no loss: apart, all | 5 % lost: apart, all | 20 % lost: apart, all | carried along its velocity, no loss: all |
|---|---|---|---|---|---|
| 0 ms | smooth | 0.00 / 0.0, 0.10 / 5.5 | 0.00 / 0.1, 0.09 / 5.4 | 0.01 / 0.6, 0.18 / 6.4 | 0.25 / 2.5 |
| 0 ms | jerky | 0.00 / 0.0, 0.15 / 8.1 | 0.00 / 0.1, 0.15 / 8.1 | 0.02 / 1.1, 0.28 / 14.4 | 0.28 / 8.2 |
| 50 ms | smooth | 0.06 / 0.8, 0.67 / 23.2 | 0.06 / 1.2, 0.64 / 22.5 | 0.08 / 1.2, 0.57 / 19.1 | 2.63 / 27.7 |
| 50 ms | jerky | 0.13 / 1.8, 1.18 / 53.7 | 0.13 / 1.8, 1.15 / 53.7 | 0.22 / 5.6, 1.24 / 53.7 | 2.71 / 57.7 |
| 100 ms | smooth | 0.33 / 3.7, 1.57 / 41.2 | 0.35 / 5.3, 1.85 / 50.3 | 0.39 / 5.3, 2.03 / 60.8 | 7.63 / 51.5 |
| 100 ms | jerky | 0.88 / 11.2, 2.89 / 99.3 | 0.84 / 8.8, 2.88 / 99.3 | 1.04 / 16.9, 2.90 / 93.0 | 7.68 / 105.8 |
| 250 ms | smooth | 4.05 / 34.5, 9.85 / 95.9 | 3.86 / 31.4, 10.48 / 153.4 | 4.17 / 40.2, 14.93 / 186.0 | 38.60 / 132.5 |
| 250 ms | jerky | 10.71 / 72.5, 16.28 / 253.4 | 10.62 / 66.0, 17.08 / 278.3 | 11.01 / 83.2, 18.11 / 298.9 | 37.75 / 265.2 |

Two copies of the app on one machine (the B-spec
driver driving both cars, one lap of Tsukuba, contacts between them): 0.05 cm rms, 0.3 cm at worst, directly and
0.1 / 0.8 cm through a relay server here; the same finish times on both (72.718 s and 74.319 s). A step of a
prediction costs what a step of the car costs (about 50 µs on a loaded M-series Mac, `net_race_prediction_cost`).

### Contacts

The hit our machine's step meets goes to the other car's owner (the engine's Contact message: the
point and the impulse his car took). He applies it to his record (the velocity by j / m, the yaw
by its moment over 10 I, as 0x45F340 does) when his own machine met none with that car within
0.2 s; it waits 0.05 s for that, since between near machines it comes before his own step meets
it. At most 60,000 N s.

A pair's cars are **ghosts** to each other (no contact; the collision step's flag F +0x441): in the
race's first 3 s, when their round trip is above the lobby's limit, when they are more than 0.9
laps apart, once one has finished, before the first state, after its player left.

### The race's rules

| | Here | The original's LAN battle |
|---|---|---|
| Who runs a car | its owner | every machine, all of them |
| Start | the grid in the lobby's order, 10 s with the opening shots; every machine's countdown begins at the same moment (the host's Go, 1.5 s ahead; the clocks agreed by the engine's pings) | the same hold, the steps together |
| Laps, order, finish | as any race; the times are the owners' | as any race |
| Tire wear | the host's setting by `lan_wear` (0x354F30's numbers from the program) | the same |
| Boost | by `lan_boost` (0x354EB8's numbers), the rubber band from the leader; off by default | the same |
| Fuel | as the race row's (`sample_entry_1`) on the course | the same |
| Penalty | none | none: the host's kind is sent, but the conductor's virtual 26 (0x34CA20) takes it only in a `RaceArcadeSingle` (0x3BFD00); a LAN race keeps its row's, which has none |
| Handicap | none | none |
| Pause | opens the race's window; the race goes on | stops everybody |
| A player gone | his car leaves the race; the others race on | the race ends (`DISCONNECTED`) |
| Connection lost | our car races on alone | the race ends |
| Plausibility | speed, jumps, acceleration and motion of the others' cars by the engine's checks, reported to the host | none |

### The steps and the picture

Steps run by the machines' shared clock (`NetRace::steps`, the engine's `FixedStep::follow`): a
stalled frame catches up, and the blend between the last two steps comes from the same clock (by
frame times the picture steps back every other frame above 60 fps). A correction of the start's
moment during the race is eased in (the race at most 5 % fast or slow).

`GT4R_NET_DEBUG=1` logs where every car is by the race's clock, twice a second: the logs of two
machines compare.

### Tests

`tests/net_machines.h` (machines in one process, states with delay, jitter and loss):
`net_state_round_trip`, `net_race_remote_car_error`, `net_race_contact`, `net_race_posed_car`,
`net_race_stalled_machine`, `net_race_six_cars`, `net_race_car_put`, `net_race_hostile_states`,
`net_lan_rules`; over loopback sockets
(`tests/test_net_session.cpp`): `net_session_race` (direct, with datagrams lost, through a relay
server in the process; the clocks 5 s apart), `net_session_finish_and_loss` and `net_session_120hz`.

## What runs where

| Piece | Code | Whose |
|---|---|---|
| Transport, lobby channel, hole punching, relay, clocks, plausibility checks | `engine/gt3r/src/net`, `race/net_race.h` `Plausibility` | the shared engine's, unchanged |
| The car's state, posing, prediction, hits | `net/`, `sim/session.*` | ours, on Gran Turismo 4's step (nothing of Gran Turismo 3's runs in a car) |
| Wear, boost, start, grid | `net/lan_rules.*`, `race_world.*` | Gran Turismo 4's |

## The lobby

### What the original's menus share (program facts)

Arcade's *Multi-LAN Race* (the script's `LANBattle`; `GameRoot::LANBattle`, `MatchingRoot`, `MonitorRoot`, `CourseRoot`,
`CarRoot`, `SettingPopup`, `PregridRoot` of the project `arcade`) runs the same pages on every machine at once. Each machine
writes its own record and reads the others' through `main::network` (MNetwork, registered at 0x1E9A40; the natives
0x1E4B20 .. 0x1E7210, on RTIME, the session library, at 0x645570).

- A member's record in RTIME's client table (`raceMenuNotify...` writes ours, `raceMenuPeek...(i)` reads member i's; ours
  0x4FA008, the others 0x4F9F70 / 0x4F9F18, written back 0x4FA048): +0x00 u32 the time asked for (`NotifyTime`), +0x35 s8 the
  style (0 .. 2 play, live, monitor; 0x1F0DC0), +0x36 s8 the sequence, +0x37 s8 the players the host waits for
  (`NotifyClientCount`), +0x39 char[0x40] the car's label and +0x79 its paint (0x1F6EF0), +0xBA 0x20 bytes the entry's
  setting (0x1F72A0). The session's: +0xF78 char[0x20] the course (0x1F73B0, the host's alone), +0xF98 0x20 bytes the race's
  setting (0x1F7330, the host's alone), +0xEE0 the members, +0xFCC the menus' state (1: ended, `raceMenuEnd` 0x1F6A98, read
  by `raceMenuForceEnd` 0x1F6AC0), +0xFD4 a counter of changes (`raceMenuChanged` 0x1F7520).
- **All connected** (`raceMenuAllConnected`, 0x1F7460): every member has a record, the greatest count asked for is above 0
  and no more than the members, and the game's time is past the greatest time asked for.
- **The barrier** (`enterNetSync`, `netSync` 0x1F75C8, `leaveNetSync`): yes once every member entered it, or on an error.
- **The race's setting** (`getBattleSettingRace`, 0x161F28): six words, the 2-Player Battle's options and two more:
  split_race_laps, split_handicap, split_boost_level, split_tire_damage not 0, LAN_heavy (the network's load), penalty_type.
  **An entry's** (`getBattleSettingEntry`, 0x162018): automatic gears, ASM, TCS, vibration, active steering, the tires'
  kind, the first view, a bit of the race display's word, and one more.
- The pages: the host waits in *Matching* for `LAN_player_num` + `LAN_monitor_num` members, then names the count and a
  second from now; all go to the course page together (sequence 3). Any player's course is a proposal (sequence 4); the
  host takes it, or chooses its own, and is at 5; the others follow a member at 5 or more with its course. The cars are the
  favorites alone, else the script's `lan_carlist` / `lan_dirt_carlist`; convertibles of the open kind 2 are refused. The
  driving options' OK sends the settings and goes to the pregrid page (7), which shows six names; when every player is at 7
  or past it a count runs out and each machine leaves for the sequence `lan_race` (8) with its car. Back from the race the
  arcade project opens the course page again. There is no chat in the LAN Battle, no removing of a player and no way back
  from the course and car pages.
- Options: *LAN* (machine name, host or not, style, players, monitors, the network's load; Option +0x1344 .. +0x13F0: the
  style, two names of 0x44 bytes, then u32s: monitors, the monitor's place x and y, a delay, host, load, players, automatic
  connection, its count and index). The project `network` sets a console's network adaptor up and opens RTIME; the LAN
  Battle goes through it first (`next_menu_project` "network", argument "LAN").
- **The tires** a race puts on (the race object's virtual 92: `RaceLanBattle` 0x338348, `RaceArcade`, `RaceSplitBattle`,
  `RaceFreeRun`): on a dirt course kind 11 for every entry, on snow 12, else the entry's chosen kinds (+0x160, +0x161 with
  +0x165 set), each the car's fitting part of that kind (its CARS file); a car without one keeps its own.

### The game's own pages here

`menu/game_network.*` answers those natives from a `net::Lobby` (`net/lobby.*`): each player's record is one block (at most
1 KB) in the session's car info (`Session::set_car_info`, which the host passes on, to players who join later too). Nothing
is added to the shared engine's protocol. The pages are the disc's, unchanged; the record holds the natives' fields, the
player's car, the barrier's round, and the host's settings.

- **The barrier by rounds.** The host's round goes up with each race. Entering the barrier names the round it leads to and
  takes the sequence back to 2, so a player still in the last race's menus is not taken for one who chose a course. A player
  whose menus have not come to a barrier yet (joining late) holds nobody; he takes the course the others are at.
- **What is added on the pages**, by functions of the scripts given another body when the arcade project is read
  (`adapt_lan_pages`): after the course the makers' page as the 2-Player Battle has it (every car; the garages of the two
  card slots), when the host lets every car race (else the favorites alone, as the original); Triangle on a maker's cars
  goes back to the makers; the driving options' OK says at once why a car may not race and stays. `isClientDisconnected` and
  `isDisconnected` answer no: a player who leaves ends nothing for the others, and the lobby's own window says what ended.
- **The host's rules**: the 2-Player Battle's options (laps, tire wear, boost) as the original's, and ours: a power limit (the
  car's as it races), garage cars or cars as sold, every car or the favorites, contact by round trip. The course is the
  pages'. Each player's gearbox, aids and tires are his own (the driving options; a garage car's own parts).
- **The connection's page** (`app/screen_online.cpp`) stands in for the `network` project: the lobbies found on the local
  network, open one, join by address; open one with a code, join with a code; the player's name (the option's LAN machine
  name), the server. Text is typed on a keyboard, or with a pad on a keyboard shown.
- **The players' panel**: on the matching page always (the code or the address to join, who is there, the link and round
  trip of each, the rules; the host's Start begins with the players there), on the other pages with Select: the host
  removes a player (Square), anyone leaves the lobby (Start). It says why a car cannot race here.
- Six players at most (the pregrid page's slots). ❓ The styles live and monitor (watching) are not offered.
- **After a race** everyone is taken back to the course page by the host (*Again* is the same pages). A player's record goes
  with him when he leaves; the host closing the lobby or lost ends it for all, with a window that says so.

### The session's identity

The disc's id is `gt4` for the release as it shipped, `gt4:<the files' digest>` for any other Gran Turismo 4 disc
(`net/identity.*`); Prologue has none and no Multi-LAN Race. Two ids that differ never meet: the host refuses, and the refusal
calls our disc "another game", whichever side hosts. The version sent is the shared engine's. A relay server that asks for a
newer version is answered on the page by a text of ours with no other name and no download; the relay's version notices are
not used.

### The car

`net/car_choice.*`. An arcade car travels as its label, paint and the kind of tires chosen; a garage car with its equipment
record (0x178 bytes: parts and settings) and condition block (0x20) as well; each with the owner's gearbox, aids and wheel.
Every machine builds it from its own disc: the car as sold, or the record sent, then the race's tires. The **digest** is
FNV-1a over the parameter block (0x1100 bytes) the converter makes of the car on a paved course, with a garage car's
condition; a car whose digest differs here, whose parts are another car's or do not fit it (its CARS file; wheels and wings
fit any car), or which the disc lacks, is named on the panel with the reason, and the host's race does not start with it. ❓
A garage car's settings are not checked against the ranges the settings page gives: a setting no page gives would race
as sent (on every machine alike).

### Finding a lobby

`net/discovery.*`: the host of a lobby on the local network answers on the first free UDP port of 47640 .. 47643; a guest's
page asks them once a second on each network's broadcast address and on this machine. Question and answer start "GT4L", u8 1
(the version), u8 1 (a question) or 2 (an answer), u32 the question's number; the answer adds u16 the session's port, the
host's name and disc id (u8 length and bytes each), u8 players, u8 the most, u8 open. The session's own port is 47634 and the
three above it. Nothing is sent but to an open page's question.

### The relay server

The shared engine's (`Session::host_via`, `join_code`, its `docs/dev/ONLINE.md` "Server"), named in `config.ini` `[online]
server`. No server is set until the player chooses one on the page: the first thing that needs one asks, offering
`gt3r.gameversions.com` or a name typed. A player's own (`gt3r-relay`) is typed there too. Tests use a server of their own on
the loopback.

### Scripted races

`--screen online` with `GT4R_ONLINE` (the top of `app/screen_online.cpp`): two copies race without the menus, direct or
through a local relay server; `GT4R_NET_LOOPBACK=1` keeps them on this machine (no addresses told, no broadcast).

### What runs where (the lobby)

| Part | Where | Whose |
|---|---|---|
| The pages from *Matching* to the pregrid, their rules and texts | the disc's scripts | Gran Turismo 4 |
| What the pages share (the natives) | `menu/game_network.*`, `net/lobby.*` | Gran Turismo 4's rules on the shared engine's session |
| The connection's page, the panel, the rules that are ours | `app/screen_online.cpp`, `app/race_online_ui.*` | ours |
| The transport, the codes, the relay | `engine/gt3r/src/net` | the shared engine's |
| The car's parameter block, the race's tires | `sim/convert.*`, `sim/race_tires.*` | Gran Turismo 4 |

Tests: `net_lobby_*`, `disc_net_cars` (every car of the disc through the wire and the digest), `disc_net_lan_pages` and
`disc_net_lan_garage_car` (two games' menus in one process from the Multi-LAN Race to the race and back).
