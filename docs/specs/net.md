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
TABLE_HERE

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
`net_race_stalled_machine`, `net_race_six_cars`, `net_lan_rules`; over loopback sockets
(`tests/test_net_session.cpp`): `net_session_race` (direct, with datagrams lost, through a relay
server in the process; the clocks 5 s apart) and `net_session_finish_and_loss`.

## What runs where

| Piece | Code | Whose |
|---|---|---|
| Transport, lobby channel, hole punching, relay, clocks, plausibility checks | `engine/gt3r/src/net`, `race/net_race.h` `Plausibility` | the shared engine's, unchanged |
| The car's state, posing, prediction, hits | `net/`, `sim/session.*` | ours, on Gran Turismo 4's step (nothing of Gran Turismo 3's runs in a car) |
| Wear, boost, start, grid | `net/lan_rules.*`, `race_world.*` | Gran Turismo 4's |
