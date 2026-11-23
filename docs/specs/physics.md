# Gran Turismo 4 vehicle physics

What the program does to move a car, as far as it is known, and what our simulation
(`src/gt4r/sim`) runs in its place. Addresses are Gran Turismo 4 SCUS-97328 v1.01's
(`CORE.GT4`); "GT3" addresses and section numbers (physics §n) are the shared engine's
specification, [engine/gt3r/docs/specs/physics.md](../../engine/gt3r/docs/specs/physics.md),
which describes the simulation this one grew from. A fact is from reading the program's
listing unless marked: ✔ checked by running the original routine on the user's disc (§9),
🔎 seen but not yet checked that way, ❓ not known.

## 1. How it relates to Gran Turismo 3's

The simulation is Gran Turismo 3's grown, not a new one: the same race step over the same
phases, the same pipeline from the database to a car's parameters (parts → equipment → spec →
converter → parameter block), the same collision manager. Of the 593 routines the engine's
specification cites, 414 have a counterpart here. What changed:

- **Layouts.** The car record is 0x10A0 bytes (GT3 0x16DC) because the parameter block is no
  longer inside it; a wheel's record is 0xEC bytes (0xA4); the spec is 0x600 bytes (0x3E2);
  the parameter block is over 0x10DC bytes (0xEB0).
- **More of everything in the data**: 24 surfaces (16), engine curves of 24 points (16), 11
  forward gears in the spec and 9 in the driveline (7), 13 tire kinds per car, cars with
  other than four wheels (the wheel count is a byte of the parameter block's layout record,
  0x359510), ballast, a weight and a power percentage per entry, nitrous, a part that changes
  with speed, gearboxes without steps.
- **Rules rewritten**, by the size of the routines: the suspension step (0x36A758, 1,732 bytes
  for GT3's 476), the body's attitude (0x36B0C8: 2,492 for 676), the tire's slip force
  (0x352408: 1,084 for 388), the slip ratio (0x3513F8), the axle step (0x36D818), the driver
  input (0x349FD0), the wear's grip (0x356EF0), the steering force (0x353EE8), brakes and ASCC
  (0x3F9870, 0x3F94C0). ❓ What they do differently.
- **Phases added** to the step: 0x355E10, 0x35CA60, 0x35D1D0, 0x3F28B8 (below).

## 2. The race step (0x34DB30)

The conductor's step (GT3 0x1F39D8, physics §1.2). The cars are records inside the
conductor, car i at conductor + 0x6698 + 0x10A0 i (0x34C190); the count is at
conductor + 0x10000 - 0x760. As in Gran Turismo 3 the step first lowers each car's time
accumulator (car + 0x104 + 0x554) by the conductor's time scale and runs only when one goes
below zero, then runs each phase for every car before the next:

| # | Routine | GT3's | What |
|---|---|---|---|
| 1 | 0x343ED0 | 0x1EADB8 (same but for layout) | this step's values into the "last step" slots |
| 2 | 0x355E10 | none | ❓ (reads the driven axle's speed, 0x355D48) |
| 3 | 0x35CA60 | none | ❓ |
| | 0x34C210, then per car | 0x1F2B98 | the start's countdown: the cars are held while it runs |
| 4 | 0x357700 | 0x1EA428 (changed) | preparation: step length (0x3658A0), aerodynamics (0x3F3090), the engine's torque at its speed (0x34E9A0), each wheel's grip from its wear (0x356EF0) and peak force (0x357418), slip angles (0x357690) |
| 5 | 0x349FD0 | 0x1E4D38 (changed, four times the size) | the driver's input |
| 6 | 0x35D1D0 | none | ❓ |
| 7 | 0x3F7EA8 | 0x1E8978 (rewritten) | each wheel's steer angle (0x3F7CA8 is GT3's 0x1E8910) |
| 8 | 0x351778 | 0x1E7328 (same calls) | slip ratios (0x3513F8, changed) |
| 9 | 0x3F79A0 | 0x1E3B18 (rewritten) | the driving aids: a driven car's own (0x3F77B8) or its computer driver's (0x3F7828); TCS, brakes (0x3F9870), ASCC (0x3F94C0) |
| 10 | 0x3535B0 | 0x1E8598 (changed) | tire forces, engine, clutch, gearbox, driveline, the motion in the plane |
| 11 | 0x3F38D8 | 0x1E6928 (small change) | brake heat |
| 12 | 0x350308 | 0x1DFD90 | clears the hit record |
| 13 | conductor virtual (+0x68), or 0x34DAA8 | virtual 12 / 0x1F3950 | the collision step: bodies from the cars (0x3F4B20), the step (0x45BB60 is GT3's 0x1FA8F8, the same code), results back (0x3F4C18) |
| 14 | 0x3F4638 | 0x1EC098 (small change) | which cars are near which, slipstream |
| 15 | 0x3F28B8 | none | the part that changes with speed (parameter block +0x1077 its kind 1 .. 6, +0x1078 / +0x107C the speeds it changes at; state car + 0x104 + 0x592) |
| 16 | 0x344420 | part of 0x1EB218 | hub offsets (0x3458D0) and two more routines |
| 17 | 0x344458 | part of 0x1E8B68 | the road under each wheel (§7) |
| 18 | 0x369888 | 0x1E8B68 (changed) | the ground contact: height, surface, pit lane flag, soft ground, bumps |
| 19 | 0x344530 | 0x1EB218 (same calls in a new order) | suspension (0x36A758), attitude (0x36B0C8), bump-stop nudge (0x36BA88), rumble (0x36CEE8), steering force (0x353EE8), engine sound (0x350080), the view's follower (0x345658), body axes (0x36C9F8, the same code), speeds (0x35DE20), wheel spin (0x344040), tire effects (0x3667A8) |
| 20 | 0x352EA0 | 0x1E8190 (rewritten) | tire wear |
| 21 | 0x353218 | 0x1E8340 (rewritten) | wear and pit flags |
| 22 | 0x35A678 | 0x1E6A38 (changed) | the flaps |

Then the conductor's own hooks (the race clock, the ranking, the rubber band 0x3F3808, the
end countdown 0x35F658, the pass-by states 0x4146E8) as in Gran Turismo 3.

## 3. The car's record

- car + 0x00 the race information, + 0x04 the conductor, + 0x08 the course, **+ 0x10 the
  parameter block** (its own allocation).
- car + 0x104: the state block (GT3's F = car + 0xF84): the controls, the attitude, speeds.
  Seen fields: +0x440 the car's index, +0x467, +0x4B1 flags (bit 4: on the pit lane), +0x4BC
  the start hold, +0x554 the time accumulator, +0x574 .. the attitude, +0x62C .. +0x631 ground
  contact bytes.
- car + 0x164 + 0xEC w: wheel w. +0x00 / +0x04 ground height, +0x08 load, +0x40 the speed
  ramp, +0x89 the surface index, +0x8A / +0x8B the ground record's bytes, +0x8C the ground
  height, +0x90 the road bump.
- car + 0x7B8 + 0x34 w: wheel w's ground record (§7).
- car + 0x8BC ..: the last step's values.

## 4. From the database to the spec

### 4.1 The car's equipment (0x178 bytes) ✔

A race entry's car: the car, a part per table (each a 64-bit word: the row id, the table id
in the high half; -1 none) and the settings. `sim::Equipment` keeps the record's bytes.

| Offset | Part | | Offset | Part |
|---|---|---|---|---|
| 0x00 | the car (`GENERIC_CAR`) | | 0x88 | `PORTPOLISH` |
| 0x10 | its paints (`VARIATION` id), 0x18 the paint | | 0x90 | `ENGINEBALANCE` |
| 0x20 | `BRAKE` | | 0x98 | `DISPLACEMENT` |
| 0x28 | `BRAKECONTROLLER` | | 0xA0 | `COMPUTER` |
| 0x30 | `CHASSIS` | | 0xA8 | `NATUNE` |
| 0x38 | `ENGINE` | | 0xB0 | `TURBINEKIT` |
| 0x40 | `DRIVETRAIN` | | 0xB8 | `FLYWHEEL` |
| 0x48 | `GEAR` | | 0xC0 | `CLUTCH` |
| 0x50 | `SUSPENSION` | | 0xC8 | `PROPELLERSHAFT` |
| 0x58 | `LSD` | | 0xD0 | `MUFFLER` |
| 0x60 | `FRONTTIRE` | | 0xD8 | `INTERCOOLER` |
| 0x68 | `REARTIRE` | | 0xE0 | `ASCC` |
| 0x70 | `STEER` | | 0xE8 | `TCSC` |
| 0x78 | `LIGHTWEIGHT` | | 0xF8 | 🔎 `NOS` (a u16 capacity and a setting) |
| 0x80 | `RACINGMODIFY` | | 0x100 / 0x108 | 🔎 `WHEEL` (nothing read) / `SUPERCHARGER` (two engine factors) |

Settings: 0x110 u16 x 12 the gear ratios (reverse, then 1 .. 11; x 1000), 0x128 u16 final
drive, 0x12A the gearbox slider, 0x12E the torque split, 0x12F / 0x130 brake controller
indices, 0x131 / 0x132 downforce, 0x133 .. 0x138 boost, 0x139 / 0x13A camber, 0x13C / 0x13E
u16 ride height, 0x140 / 0x141 toe, 0x142 / 0x143 spring rate, 0x144 / 0x145 motion ratio,
0x146 .. 0x14D damper indices, 0x14E / 0x14F anti-roll indices, 0x150 .. 0x155 the
differentials (front initial, rear initial, front acceleration, rear, front deceleration,
rear), 0x156 the TCS index, 0x157 .. 0x15A ASCC indices, 0x15B / 0x15C ballast and its place,
0x15E u16 an output factor that replaces the engine's when not 0, 0x162 / 0x163 the weight
and power percentages (100), 0x167 the nitrous setting, 0x168 a byte added to two chassis
bytes.

**Set-car** (0x444480, GT3 0x231DB0): clears the record (0x4442B8), takes the car's row, its
set of parts (`GENERIC_CAR` +0x00, the `df_pt_sim_` row of `DEFAULT_PARTS`) and copies the 26
parts to their places; the paints' id (0x4457B0) and paint 0; then each part's default
settings (0x4449C0: eleven setters 0x444A88 .. 0x445230), every one clamped into its range
(0x4452A8; index settings have the least value 1); then, for a gearbox whose ratios are made
(`GEAR` +0x25), the ratios from the slider (0x443240 → 0x354FF8, GT3's 0x1E28A8: the same
ratios on every car whose gearbox is made, physics §6.7). The generator's caller copies twelve
words into the record, so the gear words past the gearbox's count hold whatever the last
call left; the spec builder overwrites them. The columns:
[formats/database.md](../formats/database.md).

### 4.2 The spec (0x600 bytes) ✔

0x3F3B68 clears the spec, runs the builder 0x4413C8 and applies the fix-ups. The builder
starts the two engine factors at 1000 (+0x10A, per mille) and 100 (+0x10C, percent) and runs
a helper per part, in this order: brake, brake controller, steer, chassis, engine,
drivetrain, gear, suspension, differential, front tire, rear tire, lightweight, racing
modify, port polish, engine balance, displacement, computer, NA tune, turbine kit, flywheel,
clutch, propeller shaft, muffler, intercooler, the part at equipment +0x108, ASCC, TCSC,
ballast, the parts at +0x100 and +0x0F8, the body's box (from the car's `VARIATION` row).
Percentages are whole-number: v = v x b / 100, truncated, in that order. An index setting
becomes a value by 0x4452C0 (GT3's 0x233F60).

The layout, with the Gran Turismo 3 spec's offset for the same thing (physics §6.2):

| Spec | GT3's | Contents |
|---|---|---|
| 0x00 | | 2 |
| 0x01 / 0x04 | | the car's category and a byte (`GENERIC_CAR` +0x13 / +0x14) |
| 0x05 / 0x06 | | ballast (kg) and its place (the weight's share moves by (50 - place) percent of it) |
| 0x07 / 0x08 | | the entry's power and weight percentages |
| 0x10 | 0x1A | length, mm |
| 0x12, 0x14, 0x16, 0x18 | 0x1C .. 0x22 | the body's box: ahead of and behind its middle (half the length when 0), width, height, mm |
| 0x1A | 0x24 | wheelbase |
| 0x1C / 0x1E | 0x26 / 0x28 | front / rear tread |
| 0x20 | 0x2A | weight, with the lightweight's and the body's per mille |
| 0x22 | 0x2C | front weight, percent |
| 0x24, 0x25, 0x26 | 0x2E .. 0x30 | the yaw inertia byte (with two percentages), and the pitch's and roll's: 73 and 13 percent of it (program bytes 0x620E15 / 0x620E16) |
| 0x27 | 0x31 | the center of gravity's height term |
| 0x28 | 0x35 | steering lock, degrees |
| 0x2A / 0x2B | 0x36 / 0x37 | the axles' grip (the low byte of the per mille value + 124) |
| 0x30 .. 0x36 | | the widths across the tires and the wheel-top limits (`CHASSIS` +0x12 ..) |
| 0x38 / 0x39 | | two chassis bytes plus the entry's byte |
| 0x3A | | the chassis weight again |
| 0x40 | 0x16 | frontal area x 10000 |
| 0x42, 0x43, 0x44 | 0x32 .. 0x34 | drag coefficient x 100, front and rear downforce |
| 0x4C .. 0x50 | | the part that changes with speed (`RACINGMODIFY` +0x18 ..) |
| 0x58 .. 0x67 / 0x70 .. 0x7F | 0x3A .. 0x47 / 0x4A .. 0x57 | the front / rear spring and damper block: spring rate, anti-roll, bump-stop rate, unsprung mass, four damper speeds and forces in turn, (unused), bump damping, then the gap to the progressive rate as a u16 (GT3: a byte before the bump damping) |
| 0x88 / 0x89 | 0x5A / 0x5B | camber |
| 0x8C / 0x8D | 0x5E / 0x5F | motion ratio |
| 0x8E / 0x8F | 0x60 / 0x61 | toe |
| 0x90 / 0x91 | 0x62 / 0x63 | two suspension bytes |
| 0x92 .. 0x95 | 0x64 .. 0x67 | bump travel front / rear, droop travel front / rear |
| 0x96 / 0x97 | 0x68 / 0x69 | wheel offsets |
| 0x98 / 0x99 | 0x38 / 0x39 | two body bytes |
| 0x9A / 0x9C | 0x5C / 0x5D | ride height front / rear, u16 mm (GT3: bytes) |
| 0xA8 | | a suspension byte (3 for a rally car on a loose course) |
| 0xB8 / 0xBA | 0x6A / 0x6C | engine inertia / engine friction |
| 0xBC, 0xBD, 0xBE | 0x6E .. 0x70 | idle, rev limit, shift limit |
| 0xBF | 0x71 | the torque curve's points (those past it: speed 0xFF, torque 0, up to 16) |
| 0xC0 .. 0xD7 | 0x72 .. 0x81 | the curve's engine speeds, 24 bytes |
| 0xD8 .. 0x107 | 0x82 .. 0xA1 | its torques, 24 u16 |
| 0x108 | 0xA2 | the output factor (the engine's percent x 10, or the entry's) |
| 0x10A / 0x10C | 0xA4 / 0xA6 | the two engine factors |
| 0x110 / 0x111 | | two engine bytes |
| 0x112 / 0x11C | | two bytes with program defaults (0x620E23: 200, 0x620E22: 80), 0 for car categories 2 .. 4 |
| 0x113 | 0xA7 | the boost cap |
| 0x116 .. 0x11B | 0xAA .. 0xAF | the boost settings |
| 0x11D / 0x11E | | the nitrous setting and capacity |
| 0x120 | | a kind from the car's kind byte and the muffler |
| 0x128, 0x129, 0x12A | 0xB2 .. 0xB4 | drive, four wheel drive system, front torque share |
| 0x138 .. 0x14F | 0xB6 .. 0xC5 | the gear ratios, 12 u16 (past the count: -1) |
| 0x150 | 0xC6 | final drive |
| 0x152 / 0x154 | | the gearbox's second reduction (x 1000) and its kind (`GEAR` +0x1E / +0x29; §4.3) |
| 0x155, 0x156, 0x157, 0x158 | 0xC8, 0xCA, 0xCB | forward gears, 0, the slider, the gearbox's kind |
| 0x160 / 0x161 | 0xC9 / 0xCC | the clutch's take-up speed / the clutch's byte |
| 0x164 / 0x166 | 0xCE / 0xD0 | shaft inertias |
| 0x168 .. 0x16F | 0xD6 .. 0xDD | the differentials' types and settings |
| 0x174 / 0x176 | 0xD2 / 0xD4 | wheel inertias |
| 0x178 .. 0x17A | 0xDE .. 0xE0 | brake torques |
| 0x180, 0x181, 0x182 | 0xE2, 0xEF, 0xFC | the three steering curves' counts (6) |
| 0x184, 0x190, 0x19C | 0xE3, 0xF0, 0xFD | their 12 bytes each (the defaults 0x6A4028 for the second and third when empty) |
| 0x1A8 / 0x1A9 | 0x10A / 0x10B | the brake controller's values |
| 0x1AA / 0x1AB | 0x112 / 0x113 | two brake bytes (defaults 140 and 100; GT3 100 and 100) |
| 0x1B8 | 0x109 | the ASCC's mode bits (cleared in halves when its indices are 0) |
| 0x1BA .. 0x1BF | 0x10C, 0x10D, 0x111, 0x114, 0x115, 0x116 | the ASCC's gains and thresholds |
| 0x1C0 .. 0x1C3 | 0x10E, 0x10F, 0x110, 0x117 | the TCS's terms, gain and level (level 100, or index 0: gain 0) |
| 0x1C8, 0x1CA, 0x1CC (+1 rear) | 0x11A, 0x11C, 0x11E | tire width / 5 mm, rim (tenths of an inch; GT3 inches), aspect / 5 |
| 0x1CE .. 0x1DF | 0x120 .. 0x131 | the camber curves: counts (4), front and rear angles, front and rear gains |
| 0x1E2 .. 0x1E5 | | the chassis' two tire bytes, per axle, for tire kinds 6 .. 10 |
| 0x1F0 + s | 0x132 + s | surface s's tire block class: 1 (loose) for 2 .. 5 and 13 .. 15, 2 (wet) for 6 and 20 .. 23 |
| 0x210 + 0xA8 k, 0x408 + 0xA8 k | 0x142 + 0x70 k, 0x292 + 0x70 k | the front and rear tire blocks, k = 0 paved, 1 loose, 2 wet |

A tire block (0xA8 bytes; GT3's 0x70, physics §6.5), from its compound row C and force-volume
row V: +0x00 grip, +0x01 the wear rate, +0x02 curve A's slip angle range (45 when 0), +0x03
three compound bytes, +0x08 .. +0x0C the curves' point counts (8, 6, 6, 4, 6), +0x10 curve A,
+0x20 / +0x28 the slip ratio axis below / above zero, +0x30 / +0x38 curve C, +0x40 / +0x48
curve D, +0x50 curve B (the default 0x622420 when empty), +0x5C the load curve, +0x64 + s
the grip percentage on surface s (0 .. 14 and 20 .. 22 from V; 10 .. 14 default to 100, 15 to
80, 20 / 21 / 22 to those of 0 / 1 / 9; every other block's empty entries are the first front
block's).

**A rally car on a loose course** (category 2, course kind 2 or 3) gets, from the program
bytes at 0x6224A0: the suspension byte 3, 80 added to both gaps to the progressive rate, 180
to both ride heights, and both spring rates set to 50. ✔

### 4.3 The converter (0x361908) and the parameter block

GT3's 0x1DBFD8 (physics §6.4) with the block's fields at new places and these differences:

- **Mass** (+0x48): the spec's weight (1200 when 0) times the weight percentage, plus the
  ballast, plus the static block's two bytes +3 and +4 (65 + 5 kg; GT3 65 + 55). The front
  weight share (+0x24) is the mass-weighted mean of the car's and the ballast's place.
- **Pitch and roll levers** (+0x11C .. +0x128) ✔: the arm over the inertia byte x 100. GT3's has
  2 pi in the numerator; with the pitch and roll bytes 73 and 13 percent of the yaw's, a car's
  pitch inertia is 4.6 times and its roll inertia 1.6 times what GT3's rule gives the same
  yaw byte. The pitch's tire term (+0x12C): the static block's byte +7 x 0.01 over (the
  pitch inertia + mass x arm squared).
- **Ride heights** (+0x40 / +0x44): the front's and the rear's own (GT3 gives both axles the
  front's); the center of gravity's height (+0x3C) is their mean weighted by the weight
  shares; the load transfers (+0x34 / +0x38) follow it. ✔
- **The suspension's places along its travel** (0x360DC8; block +0x308 + 0x58 x axle), with
  x the travel (static x_s, more negative: more compressed), ride the axle's ride height,
  t = min(ride, bump travel), g = min(ride, the gap word): the hub's offset +0x0C = tire
  radius - ride + x_s; the bump stop +0x04 = x_s - ride + t; the start of the progressive
  rate +0x00 = the bump stop when the gap word is 0 or g <= t, else x_s - ride + g; the
  droop +0x08 = min(x_s + droop travel, unsprung weight / spring rate). The static travel
  x_s = -(the axle's static load / its wheels) / spring rate. ✔
- **Drag** (+0x1068): area x air density / 2 x Cd, plus the two downforce coefficients' sum
  times the static block's byte +0x0A per mille (40: 4 percent; GT3 a tenth). ✔
- **The gearbox's second reduction** (0x34B1A8; spec +0x152 when at least 500, by the kind at
  +0x154): kind 1 multiplies every gear by it (the Honda S2000's 1.160); kind 2 uses it in
  place of the final drive for reverse and the gears from the fifth on (the Audi A3 3.2
  quattro: 4.800 and 3.600); ❓ kinds 3 and 4 (five rows).
- **Tires** (0x34BA30): radius = rim / 2 + width x aspect; the rim in tenths of an inch
  (values below 61: inches); for tire kinds 6 .. 10 the chassis' two bytes change the
  sidewall and the width. The half-track is narrowed where the tire would stand out past the
  chassis' width across the tires (0x361D70).
- **Engine** (0x34EF30): the curve at +0x134 with 24 points; the entry's power percentage in
  the output factor; a power limit from a rule byte, as GT3's. Driveline (0x34ACA8) at
  +0x234: at most 9 forward gears; the gearbox's kind byte (+0x12) from the spec's (kinds 5
  and 6 are turned into others ❓); the four wheel drive systems 0 .. 8 by a jump table (0x34BC98).
- **Aerodynamics**: drag +0x1068 with a share of the downforce coefficients added (static
  block +0xA, per mille); the part that changes with speed at +0x1077 .. +0x1084; nitrous at
  +0x10D4 / +0x10D8.
- Brake torques +0x1010 .. (byte x 98 N m), the aids' block +0x1098 (GT3's +0xE7C), the
  steering curves +0x58 .. +0xF4, the suspension blocks +0x308 / +0x360 (0x58 bytes an axle;
  GT3 0x44), the camber's grip curves +0x3B8.
- **The tire records** ✔: +0x410 + 0x1EC r (GT3: +0x398 + 0x1B4 r), r = 0 .. 2 the front's
  paved / loose / wet, 3 .. 5 the rear's; GT3's record (physics §6.5) with the grip of 24
  surfaces from +0x08 and everything after them 0x24 further on (curve A's axis +0x78,
  values +0x98, its peak's slip angle +0xB8, curve B +0xC8 / +0xE0, curve C +0x104 / +0x134,
  the optimum slips +0x164 / +0x168, the braking peak +0x16C, curve D +0x17C, the load curve
  +0x1B8 / +0x1C8).

## 5. The program's tables

Read from the user's `CORE.GT4` when a session is set up (`sim::Program`); the build is
recognized by a hash of the three routines above.

- **The static physics block, 0x620CD8**: GT3's block (physics §3.6, 0x2D28B0) with 24
  surfaces. +0x00 air density x 10, +0x01 / +0x02 slipstream shares, +0x03 / +0x04 added
  mass, +0x05 / +0x06 slipstream build-up and decay, +0x07 the pitch's tire term, +0x08, +0x0A
  (a downforce share of the drag, per mille), +0x0B the rolling-resistance curve's points (8),
  +0x0C its speeds, +0x14 its values (both as GT3's); then per surface: +0x1C bump amplitude
  (mm), +0x34 s16 rolling resistance (x 0.01), +0x64 bump wavelength (dm), +0x7C bump scaled
  by speed, +0x94 bump mode; then GT3's +0x7C onwards 0x30 bytes further on: +0xAC brake heat
  (7, 20, 100, 5: GT3's values), +0xB4 the slip ratio's bytes (10, 10, 0 ...; GT3 20, 20, 1,
  1, 50, 50, 100, 100), +0xBC / +0xBD the tire load knees (20, 40), +0xBF three grip bytes
  (80), +0xD0 the engine condition's nine bytes, +0xDD / +0xDF the oversteer indicator's
  angles and gains (7, 5 / 30, 60), +0xE5 .. +0xE7 the dirt steering aid.
  Rolling resistance by surface: 0.30, 0.10, 0.20, 40.0, 0.30, 0.20, 0.10 .., surface 12
  0.60; bump amplitudes 0, 5, 16, 14, 20, 5 mm for surfaces 0 .. 5, 3 and 2 for 10 and 11, 2
  for 15.
- 0x620938: the per-surface tables the block is made into at a race's start (0x35F808, GT3's
  0x1DAE70 with other constants). 0x622430: a material's soft flag, 24 words (2, 3, 4, 6).
- 0x6A4028 the default steering curve, 0x622420 the default tire curve B, 0x6224A0 the rally
  car's loose-course changes, 0x6224A8 an ASCC switch, 0x620E15 .. 0x620E23 the spec's
  defaults.
- ❓ The tire effect block, the controller settings and the computer drivers' tables (GT3's
  0x2D35B8, 0x2D4628, 0x2DA030 ..): their counterparts are not located.

## 6. The ground

- **The query** (0x395760 for one wheel, 0x3957F0 for four; the road's 0x3E5A30): a line from
  5 m above the wheel's point straight down to the road's lowest bound. The road's lookup
  tree is walked for the line (0x3E5BC8); in each cell reached, every triangle is tried
  (0x3E5FD8): it counts when the line passes through it from its upper side, edges
  included; the one met first from above wins, and one at the same height later in the list
  does not replace it. The answer (0x30 bytes): the point (clamped into the triangle's box),
  the material (triangle +6, bits 0-4), the flags (triangle +7), the corners' two bytes
  interpolated (/ 255 and / 127, clamped to 0 .. 1), the unit normal, the plane's constant,
  the triangle and its cell.
- The wheel's ground record (car + 0x7B8 + 0x34 w): +0x00 the query point, +0x10 height,
  +0x14 / +0x18 the two values, +0x1C normal, +0x28 plane, +0x30 1 when nothing was found,
  +0x31 material, +0x33 flags. A normal with y below 0.5 is replaced by straight up (0x344458).
- **The surface index** (0x3F4E40): the material; with flags bit 1, material 0 or 7 → 20, 1,
  10, 11 or 12 → 21, 9 → 22. The surface picks the tire block (by its class), the grip
  percentage, the rolling resistance and the road bump. Flags bit 0: the pit lane.
- The disc's courses use materials 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11 and 12.

## 7. Walls and other cars

The collision manager is Gran Turismo 3's routine for routine (0x45BB60 is 0x1FA8F8 with only
its calls moved; physics §5.3), and the road's wall vertices are its wall point records
(position, link, flags). ❓ How the walls near a car are found here (the road's wall
lookups), and how levels are told apart.

## 8. What runs where in our simulation

`sim::Session` steps the cars with the shared engine's vehicle step (the "exact core", GT3's
rules in the PlayStation 2's arithmetic), fed with Gran Turismo 4's data:

| Part | Runs | Follows |
|---|---|---|
| Parts, equipment, default settings, the spec, its fix-ups | `equipment.*`, `car_spec.*` | Gran Turismo 4 (§4.1, §4.2): equal to the program's on all 722 cars, byte for byte (§9); computed gearboxes' ratios by the engine's generator (7 gears at most) |
| Spec → parameter block | `exact_bridge.*`: `core_spec`, the engine's converter, `gt4_params` | GT3's converter on our spec translated field by field, then Gran Turismo 4's rules where they differ (the levers, drag, ride heights, the suspension's travel). Equal to the program's converter on every car for the fields of §9, but for what the engine's block cannot hold: engine curves beyond 16 points (thinned), gears beyond 7, half-inch rims and tire kinds' changes (one car), the three-wheelers' loads, four wheel drive system 5, the speed-switched part, nitrous, ballast's place, gearboxes without steps (run as stepped) |
| Program tables | `program.*`, `core_program` | the static block from `CORE.GT4`, folded to the core's 16 surface rows (0 .. 11 as they are, 20 / 21 / 22 → 12 / 13 / 14, 12 → 15), except the slip ratio's bytes; every other table the engine's own (GT3's values) |
| The road query, surface index | `ground.*` | Gran Turismo 4 (§6), in ordinary floats |
| Ground contact, phase order | `session.cpp` | the engine's phase 12 order with our query; phases 1 .. 13 as GT3's step |
| Preparation, input, steering, slip, aids, forces, driveline, suspension, attitude, wear | the engine's routines | GT3 |
| Collisions | the engine's collision step on the road's walls | the same routines as Gran Turismo 4's; the walls near a body are found by a grid of ours, levels by height |
| Start | `session.cpp` `place` | the engine's race start (GT3's pose and standing start) on the first start places; a hold of 2 s before time 0 |
| Distance along the course, laps | `ground.*` `distance_along` | ours (between the checkpoint gates) |
| The driver of `gt4tool drive` | `line_driver.*` | ours; not a computer driver of the game's |

So a car's weight, power, gearing, tires, springs and aids are its own; how the tires, the
suspension and the body answer is Gran Turismo 3's until the rewritten routines are ported.
The figures a run gives: standing 400 m in 9.8 s (Mazda 787B), 12.9 s (Dodge Viper GTS),
13.7 s (Honda NSX Type R), 16.2 s (Honda Civic Type R); 294 km/h for the Viper and 279 km/h
for the NSX on the test course.

## 9. Checks against the program

`gt4r_diff <disc>` (`tools/gt4diff`, also run by the tests `sim_program_*`) runs the program's
own routines from the user's `CORE.GT4` in the shared engine's R5900 interpreter
(engine/gt3r/tools/diffharness), on the database's tables opened from the disc's files as the
game's loader leaves them (a table object 0x448F18 / 0x4490F8 per table at the database
object 0x6235A8 + 0x134 + 4 x table id; the row cache left out), and compares:

- **spec**: set-car (0x444480) and the spec builder (0x3F3B68, course kinds 0, 2 and 3) for
  every car against `sim::stock_car` and `sim::build_spec`: the equipment (but for the stale
  gear words past the count) and all 0x600 spec bytes are equal on all 722 cars.
- **params**: the converter (0x361908, after the static tables' builder 0x35F808) on each
  car's spec against the engine's converter on our translated spec with `gt4_params`. Equal
  (within a part in 100,000) on every car: the body's box, wheelbase, track and half-tracks,
  the axles' distances, mass and weight, the origin's offset, steering lock, yaw inertia,
  the pitch and roll levers and the pitch's tire term, the center of gravity's height and
  both ride heights, drag and downforce, brake and handbrake torques, the axles' grip, the
  engine's curve (where it has 16 points or fewer), rev limit, idle, friction and inertia,
  tire widths, of each axle's suspension the spring rate, both damper fits, anti-roll,
  bump-stop rate, unsprung weight and bump damping, and of all six tire records the wear
  rate, the grip on every surface, curves A, B, C and D, the optimum slips, the braking
  peak and the load curve. Equal on all but a few cars: the load
  transfers, static loads and the suspension's places (not the two three-wheelers), tire
  radii (not one car), the gears' speed factors (all but 10 of 4,490, to a ratio's last
  digit), the driveline's layout (not the car with four wheel drive system 5).
- Different on every car: a tire record's first word (the program's 0x361570 makes it with
  the load curve; the core's is 9.8 x curve D at no slip; ❓ its reader) and the slip angle
  of curve A's peak (the program's 0x353800 finds it more finely than the core's whole
  degrees: 31.06 where the core has 32).
- Not compared yet: the aids' block, the steering curves, the driveline's shift speeds and
  differentials, the turbo; any routine of the step itself.

## 10. Open

- Everything marked ❓ above; the rewritten routines of §1; the three new phases.
- The step: no routine of it has been run against the original. The interpreter runs this
  program's routines (§9), so each can be, as the engine's diff harness does for Gran
  Turismo 3.
- The computer drivers (the courses' `.ad` files and `crs/bestline`), the race flow (laps as
  the game counts them, sectors, the finish), pit stops, tire wear's curve per race, the
  rubber band.
- The start: Gran Turismo 4's own set-up routine (0x363B10) and starts (0x363560 standing,
  0x363868 moving).
- Other builds: a program that is not the mapped build serves no tables, and a session then
  runs on the engine's own values and says so (`Session::notes`). Prologue cannot run a
  session at all yet: its database's tables are in an earlier form `specdb.*` does not read,
  and its courses have no `RNW4` road.
