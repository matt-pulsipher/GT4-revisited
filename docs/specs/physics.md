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
  (0x3F9870, 0x3F94C0). What the ported ones do differently: §3.3.
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
| 4 | 0x357700 | 0x1EA428 (changed) | preparation: 0x35C0B8 while the hold counter F +0x4BA runs, 0x35ACB0, step length (0x3658A0), 0x354AC0, aerodynamics (0x3F3090), the engine's full-throttle and friction torques at its speed (0x34E9A0 / 0x34EA08 into F +0x4E8 / +0x4EC), F +0x5BC = -(F +0x5B0 + F +0x5A4 - 9.8 x mass x F +0x44), peak force (0x357418, with the wear's grip 0x356EF0), slip angles (0x357690) (§3.3) |
| 5 | 0x349FD0 | 0x1E4D38 (changed, four times the size) | the driver's input |
| 6 | 0x35D1D0 | none | ❓ |
| 7 | 0x3F7EA8 | 0x1E8978 (rewritten) | each wheel's steer angle (0x3F7CA8 is GT3's 0x1E8910) |
| 8 | 0x351778 | 0x1E7328 (same calls) | slip ratios (0x3513F8, changed; §3.3) |
| 9 | 0x3F79A0 | 0x1E3B18 (rewritten) | the driving aids: a driven car's own (0x3F77B8) or its computer driver's (0x3F7828); TCS, brakes (0x3F9870), ASCC (0x3F94C0) |
| 10 | 0x3535B0 | 0x1E8598 (changed) | in order: brakes and clutch (0x3511A0), rolling resistance (0x3534A0), tire curves (0x351850); for a car a computer drives (control source 1 / 2 or a driver kind), the class grip (below); the longitudinal force (0x3518E8), the lateral forces (0x352408), the speed along the body (0x351FA8), wheel torques (0x36D230), torque vectoring (0x36D2F8), the axles (0x36D818), the wheel speeds (0x36E4B8); then by the driveline's kind 0x3694D0, or engine speed and gear (0x3519B8, 0x351DC8 or 0x351EB0); 0x350ED0; the forces across the car and the yaw (0x352848) and the standstill hold (0x352C90), both with the airborne flag (the wheels' loads sum to 0) (§3.3) |
| 11 | 0x3F38D8 | 0x1E6928 (small change) | brake heat |
| 12 | 0x350308 | 0x1DFD90 | clears the hit record |
| 13 | conductor virtual (+0x68), or 0x34DAA8 | virtual 12 / 0x1F3950 | the collision step: bodies from the cars (0x3F4B20), the step (0x45BB60 is GT3's 0x1FA8F8, the same code), results back (0x3F4C18) |
| 14 | 0x3F4638 | 0x1EC098 (small change) | which cars are near which, slipstream |
| 15 | 0x3F28B8 | none | the part that changes with speed (parameter block +0x1077 its kind 1 .. 6, +0x1078 / +0x107C the speeds it changes at; state car + 0x104 + 0x592) |
| 16 | 0x344420 | part of 0x1EB218 | hub offsets (0x3458D0) and two more routines |
| 17 | 0x344458 | part of 0x1E8B68 | the road under each wheel (§7) |
| 18 | 0x369888 | 0x1E8B68 (changed) | the ground contact: height, surface, pit lane flag, soft ground, bumps |
| 19 | 0x344530 | 0x1EB218 (same calls in a new order) | in order: 0x36A490 (§3.2), suspension (0x36A758), attitude (0x36B0C8), bump-stop nudge (0x36BA88), rumble (0x36CEE8), steering force (0x353EE8), 0x367148, engine sound (0x350080), 0x366718, the view's follower (0x345658), hub offsets (0x345A20), body axes (0x36C9F8, §3.4: not GT3's order of rotations), 0x35D350, speeds (0x35DE20), wheel spin (0x344040), tire effects (0x3668F0), 0x34A668, 0x34A788 |
| 20 | 0x352EA0 | 0x1E8190 (rewritten) | tire wear |
| 21 | 0x353218 | 0x1E8340 (rewritten) | wear and pit flags |
| 22 | 0x35A678 | 0x1E6A38 (changed) | the flaps |

Then the conductor's own hooks (the race clock, the ranking, the rubber band 0x3F3808, the
end countdown 0x35F658, the pass-by states 0x4146E8) as in Gran Turismo 3.

## 3. The car and its step

### 3.1 The conductor

The single-player conductor (DynamicsConductorSinglePlayer, constructor 0x3474C0; its
initialisation is its virtual +0xE0, 0x34D490) holds, from its start (`cond`):

- +0x00 the race information; +0x04 .. +0x6F the race's wear rules (§3.3, 0x6C bytes: the
  floats +0x00 .. +0x1C and +0x50 .. +0x64, the switches +0x68 / +0x69; all 0 in a race the
  harness sets up, ❓ which races set them);
- car i's parameter block at +0x98 + 0x1100 i, car i's record at +0x6698 + 0x10A0 i (at most 6);
  0x343AB8(car, cond, info, block) clears both and sets car +0x00 = info, +0x04 = cond,
  +0x10 = the block;
- +0xCBFC + 4 i: car i's start hold counter; +0xDA50 the computer drivers' line object
  (EnemyLineProcessorOld); +0xF818 the rule block (0x80 bytes, copied from 0x6203C8 by the
  single-player constructor);
- +0x10000 - 0x3428 the course object; -0x3424 a configuration byte; -0x3423 the course's
  kind (0x34C968 reads it, 0xFF as 0; 0x34C998 maps 2 → 1, 3 → 2); -0x3422 .. -0x341F bytes
  of the course's row (0x34C7E0, through 0x447F48 on info +0x78); -0x3408 the time scale;
  -0x7D0 + the driveline class (P +0x31): a signed grip percentage per class for the cars a
  computer drives (phase 10 multiplies their wheels' +0x60 and +0x58 by 1 + percent x 0.01);
  -0x760 the car count;
- +0x10140 the vtable (8-byte slots {s16 this-offset, routine}): +0x68 the collision step
  0x3478C8, +0xD0 the configuration 0x34CA20 (the table builders 0x35F808, 0x3F7BE0,
  0x366100, 0x3F2390, 0x350A40, 0x366D68, 0x3F3AB8, 0x3F3808, 0x34A4B8), +0xD8 0x3477C8,
  +0xE0 the initialisation 0x34D490, +0xE8 0x347838 → 0x34CE48 (the control source: 0 for an
  entry whose controller +0x1CC is set, else 2), +0xF0 0x347618.

A race entry (0x4000 bytes): +0x20 the equipment (set-car 0x444480), +0x343C the controller
(+0x1CC set: a player's; its frame read is virtual +0x28 of the table at +0xD0, a 0x20-byte
frame in GT3's layout: +0 flags (1 | 2 | 4 analog steering / throttle / brake), +1 the kind
(3), +2 the shift, +4 s16 steering ±4096 (+ = left), +8 throttle, +0xA brake), +0x3444 the
grid place, +0x3448 the pedal style, +0x344C the driver kind, +0x3450 .. +0x3458 the aids'
switches, +0x3468 the 0x20-byte condition block.

**Car set-up** (0x363B10: car, spec, -1, control source, pedal style, ?, index, start kind;
f12 .. f15 = x, y, z, heading; on the stack the entry's condition block): the converter
0x361908, the pose 0x3630C0, 0x3455F0, then for a standing start 0x363560, 0x362FA8, 0x363528,
0x35FC48, 0x354AF8, 0x3F3360, 0x355840, 0x355DC8, 0x35D108 and the step length 0x3658A0 (a
moving start: 0x363868). 0x3487F0 sets the byte car +0x547; 0x362E00 sets F +0x466 (the control
source) and clears F +0x467. The grid place is the road's start record (road +0x5C + 16 i):
0x3952C0 its position, 0x395228 its heading + pi.

After the per-car phases: the conductor's virtuals +0x70, +0x108, 0x359008, +0x08, +0x78,
0x365E80, +0x18, +0xC0, +0x140, then the end countdown 0x35F658.

### 3.2 The car's record

Gran Turismo 3's record reshaped: the parameter block is its own allocation, a wheel's record
is GT3's wheel and its driven-wheel block (physics §1.3) merged and reordered, and the state
block F (car + 0x104; GT3's car + 0xF84) moved most fields. ✔ marks a field the harness (§9)
compares at every call of a routine that writes it.

- car +0x00 the race information, +0x04 the conductor, +0x08 the course, +0x10 the
  parameter block, +0x14 the computer driver's object (+0xE8 its car, +0xEC the line object),
  +0x110 / +0x11C / +0x128 the body's forward, lateral and up axes (F +0x0C ..) ✔.
- F +0x000 position, +0x030 .. +0x05F the ground under the car and its axes (GT3's F +0x30 ..).
- F +0x430 + 4 w (car +0x534) each wheel's camber force ✔; +0x440 index, +0x441 not in the
  race, +0x442 pedal style, +0x444 time scale, +0x448 step length ✔, +0x44C its inverse ✔,
  +0x45C the steering's display value, +0x460 steering percent, +0x461 the full-throttle flag,
  +0x466 control source (0 pad, 1 / 2 a computer drives), +0x467 driver kind (8 a stopped car),
  +0x468 the steering command, +0x46C the mean slip angle of the wheels ahead of the center of
  gravity ✔ (written when the switch byte 0x623E52 is set: the sum when fewer than two, else
  the mean), +0x488 throttle pedal, +0x48C brake pedal, +0x490 handbrake, +0x494 throttle,
  +0x498 / +0x49C steering bounds, +0x4A4 distance along the course, +0x4A8 road cell, +0x4AC
  s16 laps, +0x4AE lap base, +0x4AF rank, +0x4B1 flags (bit 4 on the pit lane), +0x4B5 a mask
  of the wheels whose suspension is beyond its bump stop (0x36A1D8), +0x4BA u16 the hold counter
  ✔, +0x4BC the start's hold ✔, +0x4D4 the mass as the step reads it ✔, +0x4E0 .. +0x4E8 the
  nitrous state, +0x4E8 / +0x4EC the engine's full-throttle and friction torques, +0x4FC
  engine speed (rad/s), +0x520 gear, +0x53C speed along the body ✔, +0x540 sideways speed ✔,
  +0x544 velocity (3) ✔, +0x550 / +0x554 longitudinal and lateral acceleration ✔, +0x554 time
  accumulator (the step's own use: §2), +0x564 the forces along the body ✔, +0x568 / +0x56C
  / +0x570 pitch, roll, heading ✔, +0x574 / +0x578 the ground's pitch and roll ✔, +0x57C yaw
  rate ✔, +0x580 / +0x584 pitch and roll rates, +0x588 yaw acceleration ✔, +0x58C lateral
  acceleration over the tires' grip ✔, +0x5A4 drag ✔, +0x5B0 rolling resistance ✔, +0x5B4 the
  sum of the body's up axis times three words of car +0x158 (0x36A490), +0x5BC (phase 4), +0x5C4
  speed / highest speed, +0x5D0 distance driven and its carry, +0x5D8 the engine condition's
  words (car +0x6DC), +0x5FC / +0x600 the view's heading and pitch lags ✔, +0x618 best lap,
  +0x63C / +0x640 a follower of the yaw acceleration ✔ (0x350D40: v += (500 (a - x) - 20 v) / 60,
  x += v / 60).
- **Wheel w** (car + 0x164 + 0xEC w; the step runs the wheel layout's count, §3.5): +0x00
  steer angle ✔, +0x04 camber ✔, +0x08 half track ✔, +0x0C hub offset (3), +0x1C an offset to
  the steer angle (the program's steering mode 0x845BF0: 1 added, 2 in its place, for the slip
  angle and the forces) ✔, +0x20 rim speed ✔, +0x24 spin angle ✔, +0x28 / +0x2C hub velocity
  along / across the car ✔, +0x30 slip direction ✔, +0x34 slip angle ✔, +0x38 slip ratio ✔,
  +0x3C slip gain ✔, +0x44 slip state ✔, +0x45 the longitudinal force's sign ✔, +0x46 flags
  (0: loaded and not braked) ✔, +0x48 load ✔, +0x4C peak force ✔, +0x50 the wear's grip ✔,
  +0x54 curve C's fraction ✔, +0x58 friction circle ✔, +0x5C curve A at the slip angle ✔,
  +0x60 the slip direction's low-speed fade ✔, +0x64 brake pressure, +0x68 / +0x6C the
  lateral forces (curve A, curve B) ✔, +0x70 / +0x74 the longitudinal force driving / braking
  ✔, +0x78 brake torque, +0x7C / +0x80 its reaction driving / braking ✔, +0x89 surface index,
  +0x8A / +0x8B the ground record's bytes, +0x8C ground height ✔, +0x90 the road bump, +0x94
  suspension travel, +0xA4 brake heat ✔, +0xA8 the second wear ✔, +0xAC the tread wear ✔,
  +0xB1 the HUD's wear level ✔, +0xC0 flap ✔, +0xC8 a per-wheel suspension term (0x36A080),
  +0xD4 the wheel's force across the car ✔, +0xDD the tire record's byte +0x1DA ✔. (The
  spec's earlier "+0x00 / +0x04 ground height, +0x08 load" was wrong.)
- car + 0x7B8 + 0x34 w: wheel w's ground record (§6; GT3's layout) ✔.
- car + 0x8BC: the last step's values (0x343ED0) ✔: +0x00 pitch, roll, heading, the ground's
  pitch and roll; +0x14 + 0x10 k the four wheels' suspension travel, steer, camber, half track,
  spin, brake heat and the ground record's two values; +0x94 + 0xC w the ground record's x,
  the wheel's ground height and the record's z; +0xC4 the flaps; +0xD8 the view's two lags and
  the distance along the course. Only the layout's wheels are saved.

### 3.3 The routines ported

Each is Gran Turismo 4's own in `src/gt4r/sim/rules_*.cpp` and equal to the program's at every
call the harness makes (§9), or the engine's routine where the harness shows the program's is
the same. "As the engine's" names the engine's routine (engine/gt3r/src/sim/exact).

- **Body axes** (0x36C9F8): the pitch is applied before the roll: forward = (-sh cp, ch cp,
  sp), lateral = (ch cr - sh sp sr, sh cr + ch sp sr, -cp sr), up = (ch sr + sh sp cr,
  sh sr - ch sp cr, cp cr) (s, c: sine and cosine of pitch p, roll r, heading h).
- **The last step's values** (0x343ED0): as the engine's `save_previous`, at the places of
  §3.2, for the layout's wheels.
- **Step length** (0x3658A0): as the engine's: the time scale x 1/60 and 60 / the time scale.
- **The wear's grip** (0x356EF0; returns whether it ran): nothing with both wear switches
  off or on the pit lane. The tread wear e (wheel +0xAC) by the rules (§3.1): from the greatest
  value (held there) grip 1 - loss_max; between the second knee and it, the grip falls linearly
  from 1 - loss2 to 1 - loss_max (u = the share of the way); between the knees from 1 - loss1
  to 1 - loss2; from 0 to the first knee 1 - loss1 u; below 0 down to the negative bound
  1 - loss_neg (e / bound); below it 1. The HUD level (wheel +0xB1, set here only with the
  second wear off): 0x40 + u x 63.9, 0x20 + u x 31.9, u x 31.9, -(u x 63.9), each saturating
  (0x7F, 0x40, 0x20, -0x3F), -0x80 below the bound (truncated). The second wear (wheel +0xA8)
  by one knee in the same way, its level 0x40 + u x 63.9 above the knee, u x 63.9 below; its
  grip multiplies the tread's when both are on. Then no wheel's grip below its tire record's
  floor (+0x68).
- **Peak force** (0x357418): load x the load curve at (load / the axle's width P +0x2D4),
  x the wear's grip when the wear ran, x the axle's grip (P +0xFD0), x the surface's grip
  (the record's +0x08 + 4 s), x the camber curve (P +0x3B8 + 0xC a) at (roll - the ground's
  roll, negated on a left wheel) + the wheel's own camber (+0x04; 0 for a layout of kind 1),
  then x the computer driver's grip for the tire (0x467470: 1 for a player's car; for a car a
  computer drives, a word of its driver's table by the tire record's index ❓). The wheel's
  +0xDD is the record's +0x1DA.
- **Slip angles** (0x357690 / 0x3575E0): as the engine's 2002 Tokyo-Geneva rule: the fade 0
  below 1 km/h, 1 above 10 km/h, (|v| - 1 km/h) x 0.4 between, the direction
  atan2(-vy, vx) where it is not 0.
- **Slip ratios** (0x351778 / 0x3513F8): the hub's speed along the wheel's heading
  (cos steer vx - sin steer vy) against the rim's. A tire record whose byte +0x1D8 is set
  takes the ratio as (along - rim) x P +0xFC8 (its sign turned for a rim turning backwards),
  gain 1, state and sign from which is faster, clamped to ±1, no fade. Otherwise GT3's rule
  (physics §3.4) with 2002 Tokyo-Geneva's 1/57.6 m/s and 1/3.6, on the static block's own slip
  bytes (+0xB4 the fade speeds braking / driving, +0xB6 the gains' switches, +0xB8 their values
  at rest in percent, +0xBA their full speeds: 10, 10, 0 ... in the disc's block, so no gain).
- **Tire curves** (0x351850 / 0x3598F8): curve C's fraction (wheel +0x54) and the friction
  circle (+0x58) at the slip ratio, both on curve C's axis, as the engine's.
- **The longitudinal force** (0x3518E8): as the engine's, on the static block's own gain
  switches.
- **Rolling resistance** (0x3534A0): clears F +0x564, +0x550, +0x5B0; the wheels' loads
  times the race's per-surface factors (0x620984, 24 words) times the speed's curve
  (0x620938; negative for a car going forward).
- **The lateral forces** (0x352408): the slip angle is the wrapped difference of the wheel's
  angle (+pi rolling backwards) and its slip direction, as the engine's 2002 Tokyo-Geneva rule.
  A tire record whose byte +0x1D9 is set (the cars the harness drove: all of them) reads curves
  A and B not at the folded slip angle but at |cos steer vy + sin steer vx| x P +0xFCC (the
  hub's speed across the wheel, scaled), the force's sign the sign of that speed, and is not
  faded at low speed; otherwise GT3's fold (physics §3.2) and the fade. A record whose byte
  +0x1DA is 1 has curve A alone, held within ± the friction circle (no curve B), and its sign
  turned while the rim turns backwards (both kinds); otherwise curves A and B scaled into the
  circle together. For the layout's wheels marked 2 at +0x44 a camber force: (roll - the
  ground's roll ± the wheel's camber) over the reference angle (0x620764, degrees), clamped to
  ±1, x -(0x620768), faded unless +0x1D9, x the peak force (car +0x534 + 4 w). The mean slip
  angle of the wheels ahead of the center of gravity into F +0x46C (above).
- **The speed along the body** (0x351FA8): the engine's `forward_step` but for: cos(steer)
  computed afresh, the mass from F +0x4D4 (1 / (mass + the free wheels' masses P +0x300 + 4 a),
  always), no external force term, and for a tire with +0x1DA = 1 (the wheel's +0xDD) its curve
  A force taken along the wheel's steer angle (sin steer) instead of its slip direction.
- **The forces across the car and the yaw** (0x352848): per wheel, in the car's frame, the
  lateral forces (as the speed's: along the steer angle for +0xDD = 1), the camber force
  (-cos / -sin steer), the low-speed hold (0x3522B0: the engine's with 2002 Tokyo-Geneva's
  constants), the longitudinal force; the force across the car kept per wheel (+0xD4); the
  torque about levers from the layout: across, ± the half track of the layout's axle byte
  (+0x2C + w) by side (0 left -, 1 right +, other 0); along, ± that axle's distance from the
  center of gravity by the signed byte +0x3C + w. Yaw acceleration = torque x P +0x118, its
  follower (0x350D40), the yaw rate clamped to ±2 pi; the lateral acceleration = the sum /
  the mass (F +0x4D4); over the grip: a_y x (the wheel count) / Σ (surface grip x the driver's
  grip x the record's +0x00).
- **The standstill hold** (0x352C90): with the hold counter running: the velocity, the forces
  along the body and the yaw rate cleared. Airborne: nothing. Stopped outright (every speed,
  rate and acceleration cleared) under the start's hold, for driver kind 8, or (not a computer's
  car: control source 1 / 2, driver kinds 1, 2, 4) below 1 km/h with every wheel's fade 0 and the
  brake pedal or handbrake pressed (GT3: a braked wheel). Otherwise the accelerations into the
  velocity, as the engine's.

### 3.4 Others read so far

- 0x36A490 (first of phase 19): F +0x5B4 = Σ the up axis x car +0x158 .. (3 words); each
  wheel's +0xC8 term from its ground record's normal and the body's up axis; the suspension
  0x36A080 (each wheel's place along its travel, car +0x514 + 4 w); F +0x4B5 = the mask of
  wheels past their bump stop (0x36A1D8). Not ported.

### 3.5 The wheel layout

A car's wheels are a row of the program's table at 0x620478 (0x54 bytes a row; the row is the
parameter block's byte +0x30; 0x359510 / 0x359538 return it): +0x00 a kind (1: the camber's
grip is read at 0), +0x01 the wheel count the step runs, +0x14 + w the wheel's axle (the tire
records, the widths, the grips), +0x1C + w its side (0 left, 1 right, other: on the middle),
+0x2C + w the axle whose half track and distance the yaw's levers take, +0x3C + w (signed)
ahead of (above 0) or behind the center of gravity, +0x44 + w 2 for a wheel with a camber
force.

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
- 0x620478 the wheel layouts (§3.5). 0x620938 the rolling resistance's speed curve and 0x620984
  its per-surface factors (24 words), built at a race's start by 0x35F808. 0x620764 / 0x620768
  the camber force's reference angle and gain; 0x623E52 the mean slip angle's switch; 0x845BF0
  the steering mode (a word of the program's, 0 in every race seen).
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
| Gran Turismo 4's own part of the record | `rules_build.*` `gt4_extras` | the program's converter (§3.3, §9): the tire records with 24 surfaces and their kind bytes, the tire index, widths, axle grips, camber curves, the direct scales, the wheel layout's row; bit for bit on every car but the base grip of 168 of 4,332 records (a unit in the last place: it takes the static loads of the engine's converter) |
| The wear's grip, peak forces, slip angles (phase 4's tire part), slip ratios (phase 8), rolling resistance, tire curves, the longitudinal and lateral forces, the speed along the body, the forces across the car and the yaw, the standstill hold (phase 10) | `rules_*.cpp` through `rules_step.*` | Gran Turismo 4 (§3.3); the race's tables (`rules::tables`: the rolling resistance by surface and speed, the slip bytes, the globals) as the program's race set-up makes them (checked, §9); the wear rules off, as in every race seen |
| The rest of preparation, the input, steering, aids, brakes and clutch, driveline, engine and gearbox, suspension, attitude, wear | the engine's routines | GT3 |
| Collisions | the engine's collision step on the road's walls | the same routines as Gran Turismo 4's; the walls near a body are found by a grid of ours, levels by height |
| Start | `session.cpp` `place` | the engine's race start (GT3's pose and standing start) on the first start places; a hold of 2 s before time 0 |
| Distance along the course, laps | `ground.*` `distance_along` | ours (between the checkpoint gates) |
| The driver of `gt4tool drive` | `line_driver.*` | ours; not a computer driver of the game's |

So a car's weight, power, gearing, tires, springs and aids are its own, and so is how its tires
answer the road; the suspension, the body's attitude, the driveline and the aids answer as Gran
Turismo 3's until their rewritten routines are ported. `GT4R_SIM_RULES=engine` runs the
engine's routines throughout, for comparison. The figures a run gives (the engine's
throughout → with Gran Turismo 4's routines): standing 400 m Mazda 787B 9.73 → 9.50 s, Dodge
Viper GTS 12.98 → 12.67 s, Honda NSX Type R 13.70 → 13.55 s, Honda Civic Type R 16.23 →
15.93 s; top speed on the test course Viper 294.2 → 305.9 km/h, NSX 279.1 → 288.9 km/h (the
tires these cars wear give no force against the velocity while running straight, §3.3).

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
- **leaves**: the C library's sinf (0x57D2B8), cosf (0x57D1F0) and tanf (0x57D380) are
  bit-equal to the engine's on 20,000 samples each; atan2f (0x57D118) to the engine's 2002
  Tokyo-Geneva variant (GT3's and the later builds' differ on most samples). So
  `rules::behaviour()` runs the engine's shared routines with 2002 Tokyo-Geneva's rules.
- **step** (`tools/gt4diff/step.cpp`, `orig.cpp`, `taps.cpp`): the program's own race step,
  set up and run in the interpreter (below), with each of our routines run in place at every
  call of the program's ("taps"): at a tapped routine's entry the car's record (0x10A0 bytes and
  its parameter block) is read into ours (`sim::Record`: the engine's mirror through
  `sim::layout`, each area's extra fields), ours runs on it; at the routine's return the
  program's record is read again and every field with a place is compared bit for bit, with
  the routine's own results (f0, v0, memory behind its pointers) where a tap names them. The
  report gives each tap's calls, the calls with every field equal, per differing field the
  count and the worst difference with an example, the fields the program wrote, and the record
  offsets it wrote that no field of ours has a place for. The drive: a standing start at full
  throttle through the gears, a lift, a turn each way, the brakes to a stop, 1200 steps
  (`GT4R_STEP_COURSE`, `GT4R_STEP_CAR`, `GT4R_STEPS`; `GT4R_STEP_WEAR` turns the wear
  switches on with a made-up curve and spread wears to run every branch; `GT4R_DUMP`,
  `GT4R_DUMP_POINTERS`, `GT4R_DUMP_WEAR` print the record, the parameter block's pointers into
  itself and the wear rules). Every routine of §3.3 is equal at every call for the Honda NSX
  Type R, Civic Type R, Dodge Viper GTS, Mazda 787B and Subaru Impreza WRC on Tsukuba, and
  with each wear switch.
  The set-up: the program and the database's tables (as above); the course's road (slot 55)
  relocated by the program's 0x3E5818; a course object (+0x04 → a container whose +0xDC is the
  road, +0xB0 = -1); a race information stand-in (+0x60 → the entries' list {count, +8 → the
  entries}, +0x78 the course's id (table 0x22 in the high half), +0x80 the course object, +0x84
  a race object whose virtuals (table at +0x64) all answer 0); entries as §3.1 with the
  equipment by set-car; the controller's frame read answered from the harness's pad. Then the
  single-player conductor's constructor, its race information and car count, its
  initialisation (virtual +0xE0) with 0, and the step 0x34DB30(cond, 0) per step. Stand-ins (each
  a gap of the set-up, not of the step's rules): the computer driver object's line start and
  update (0x4639E0, 0x469E58, 0x465078: no AI lines are loaded), the driving line's heading
  (0x4649F0: 0), the pit tire choices' parts file (0x449E10, 0x44A048), the race's end hook
  (0x35F658), the race object's type test (0x3BFD00: 0).
- Not compared yet: the aids' block, the steering curves, the driveline's shift speeds and
  differentials, the turbo; the step's routines not in §3.3.

## 10. Open

- Everything marked ❓ above; the rewritten routines of §1; the three new phases.
- The step's routines not yet in §3.3 (the brakes and clutch, the driveline and gears, the
  ground contact, suspension and attitude, the steering, the aids, the driver input, wear,
  the new phases 0x355E10, 0x35CA60, 0x35D1D0, 0x3F28B8); our session still runs the engine's
  (§8).
- The computer drivers (the courses' `.ad` files and `crs/bestline`), the race flow (laps as
  the game counts them, sectors, the finish), pit stops, tire wear's curve per race, the
  rubber band.
- The start: Gran Turismo 4's own set-up routine (0x363B10) and starts (0x363560 standing,
  0x363868 moving).
- Other builds: a program that is not the mapped build serves no tables, and a session then
  runs on the engine's own values and says so (`Session::notes`). Prologue's disc runs a
  session on its database in Gran Turismo 4's layout, its `GTRW` roads and its own physics
  tables in the engine's core ([formats/prologue.md](../formats/prologue.md)).
