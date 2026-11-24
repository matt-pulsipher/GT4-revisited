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
| 4 | 0x357700 | 0x1EA428 (changed) | preparation: 0x35C0B8 while the hold counter F +0x4BA runs, 0x35ACB0, step length (0x3658A0), the masses (0x354AC0: F +0x4D4 / +0x4D8 = P +0x48 / +0x4C + 0.74 x the fuel F +0x4D0), aerodynamics (0x3F3090, §5 "Aerodynamics"), the engine's full-throttle and friction torques at its speed (0x34E9A0 / 0x34EA08 into F +0x4E8 / +0x4EC), F +0x5BC = -(F +0x5B0 + F +0x5A4 - 9.8 x mass x F +0x44), peak force (0x357418, with the wear's grip 0x356EF0), slip angles (0x357690) (§3.3) |
| 5 | 0x349FD0 | 0x1E4D38 (changed, four times the size) | the driver's input |
| 6 | 0x35D1D0 | none | ❓ |
| 7 | 0x3F7EA8 | 0x1E8978 (rewritten) | each wheel's steer angle (0x3F7CA8), the command's follower (0x40CC58), the slip angle's offset and the compliance steer (§3.3) |
| 8 | 0x351778 | 0x1E7328 (same calls) | slip ratios (0x3513F8, changed; §3.3) |
| 9 | 0x3F79A0 | 0x1E3B18 (rewritten) | the driving aids (§3.3): the indicators (0x3F71E8), a driven car's own aids (0x3F77B8) or its computer driver's (0x3F7828): TCS (0x3F9D00), brakes and ABS (0x3F9870), ASCC (0x3F94C0); the race's speed caps (0x3F7880, 0x3F76E8) |
| 10 | 0x3535B0 | 0x1E8598 (changed) | in order: brakes and clutch (0x3511A0), rolling resistance (0x3534A0), tire curves (0x351850); for a car a computer drives (control source 1 / 2 or a driver kind), the class grip: the rule block's signed byte +0x18 + the drive class P +0x31 (conductor + 0xF830 ..) x 0.01 + 1 times each wheel's slip fade (+0x60) and friction circle (+0x58); a single race's conductor (and the free run's) sets each class's byte that the event leaves 0 to 20 (0x353570, its virtual +0xD8 at the race's start), so computer cars have 20 % more there; the longitudinal force (0x3518E8), the lateral forces (0x352408), the speed along the body (0x351FA8), wheel torques (0x36D230), torque vectoring (0x36D2F8), the axles (0x36D818), the wheel speeds (0x36E4B8); then by the driveline's kind 0x3694D0, or engine speed and gear (0x3519B8, 0x351DC8 or 0x351EB0); 0x350ED0; the forces across the car and the yaw (0x352848) and the standstill hold (0x352C90), both with the airborne flag (the wheels' loads sum to 0) (§3.3). The gearbox's step by its kind: a one-speed box (kind 3) 0x3694D0 (the rpm) then 0x351EB0; a stepless box of kind 1, or a special car of kind 2, 0x3519B8 then 0x351EB0; any other 0x3519B8 then 0x351DC8 |
| 11 | 0x3F38D8 | 0x1E6928 (small change) | brake heat (§3.3) |
| 12 | 0x350308 | 0x1DFD90 | clears the hit record |
| 13 | conductor virtual (+0x68), or 0x34DAA8 | virtual 12 / 0x1F3950 | the collision step (§7): bodies from the cars (0x3F4B20), the step (0x45BB60), results back (0x3F4C18) |
| 14 | 0x3F4638 | 0x1EC098 (small change) | which cars are near which, slipstream |
| 15 | 0x3F28B8 | none | the part that changes with speed (parameter block +0x1077 its kind 1 .. 6, +0x1078 / +0x107C the speeds it changes at; state car + 0x104 + 0x592) |
| 16 | 0x344420 | part of 0x1EB218 | the hubs' offsets across and along (0x3458D0, by the wheel layout), each wheel's speed ramp (0x345A50: \|hub velocity\| x 0.02, at most 1, wheel +0x40), the ground records' query points (0x3697A8, §6) |
| 17 | 0x344458 | part of 0x1E8B68 | the road under each wheel (§6) |
| 18 | 0x369888 | 0x1E8B68 (changed) | the ground contact: height, surface, pit lane flag, soft ground, bumps |
| 19 | 0x344530 | 0x1EB218 (same calls in a new order) | in order: 0x36A490 (§3.2), suspension (0x36A758), attitude (0x36B0C8), bump-stop nudge (0x36BA88), rumble (0x36CEE8), steering force (0x353EE8), 0x367148, engine sound (0x350080), 0x366718, the view's follower (0x345658), hub offsets (0x345A20), body axes (0x36C9F8, §3.4: not GT3's order of rotations), the ground's frame (0x35D350, §6), speeds (0x35DE20), wheel spin (0x344040), tire effects (0x3668F0), 0x34A668, 0x34A788 |
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

**The rolling start** (the single-player conductor's virtual +0xF0, 0x347618, asked per car by the
initialisation before the car's set-up): a car's own place offset (s16 at cond + 0x10000 - 0x794 + 2 i,
virtual +0x118 = 0x34D030) and speed (byte at -0x79A + i, else the rule block's +0x4E; virtual +0x120
= 0x34D048); when neither is set and the rule block's s16 +0x6C is negative with a speed at +0x6E, the
car at grid place p of n starts at +0x6C + (n - p - 1) x +0x6F metres from the line (negative: before
it), its start kind 1 (at or past the line) or 2, at +0x6E km/h. In the original's arcade race at
Tsukuba (reference `03-race`, read from its saved states): +0x6C = -400, +0x6E = 80, +0x6F = 20, the
rule block's +0x4E 0 and no car's own: six cars 20 m apart in single file, the last 400 m before the
line, all at 80 km/h. The set-up (0x363B10) puts a car of start kind 1 .. 5 on the course's driving
line at that distance (0x34C5E0, through the line object's 0x46ADE8; the distance at most the lap
less 20 m), otherwise on its grid place with its nose there, and with a speed calls the moving start
0x363868 (§3.3). ❓ Who fills +0x6C .. +0x6F from the event.

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
  the mean), +0x462 / +0x463 reverse and shift requests ✔, +0x488 throttle pedal, +0x48C brake pedal, +0x490 handbrake, +0x494 throttle,
  +0x498 / +0x49C steering bounds, +0x4A4 distance along the course, +0x4A8 road cell, +0x4AC
  s16 laps, +0x4AE lap base, +0x4AF rank, +0x4B1 flags (bit 4 on the pit lane), +0x4B5 a mask
  of the wheels whose suspension is beyond its bump stop (0x36A1D8), +0x4BA u16 the hold counter
  ✔, +0x4BC the start's hold ✔, +0x4D4 the mass as the step reads it ✔, +0x4E0 .. +0x4E8 the
  nitrous state, +0x4E8 / +0x4EC the engine's full-throttle and friction torques ✔, +0x4F0 the
  turbo's load ✔, +0x4F4 the two stages' lagged rpm ✔, +0x4F8 the turbo's pitch (rpm) ✔, +0x4FA
  rpm (s16) ✔, +0x4FC engine speed (rad/s) ✔, +0x500 the turbo's load at the last step ✔, +0x504
  blow-off ✔, +0x508 intake ✔, +0x50C / +0x510 the clutch's engine and wheel-side speeds ✔, +0x514
  the clutch request ✔, +0x518 a stepless box's ratio (1 locked in a gear) ✔, +0x51C the clutch's
  torque ✔, +0x520 gear ✔, +0x521 the rev limiter ✔, +0x522 the clutch (0 open, 1 locked, 2
  slipping, 3 shifting) ✔, +0x523 the center clutch locked ✔, +0x524 + k wheel group k's
  differential locked ✔, +0x526 the gearbox's lock-out ✔, +0x527 .. +0x529 the kick-down's level,
  timer and count ✔, +0x52A u16 the anti-hunt count ✔, +0x52C / +0x530 the axles' speeds ✔, +0x534
  / +0x538 the axles' drive torques ✔, +0x4BC .. +0x4E4 the fuel and nitrous (0x354B78, 0x354CE8:
  not ported), +0x53C speed along the body ✔, +0x540 sideways speed ✔,
  +0x544 velocity (3) ✔, +0x550 / +0x554 longitudinal and lateral acceleration ✔, +0x554 time
  accumulator (the step's own use: §2), +0x564 the forces along the body ✔, +0x568 / +0x56C
  / +0x570 pitch, roll, heading ✔, +0x574 / +0x578 the ground's pitch and roll ✔, +0x57C yaw
  rate ✔, +0x580 / +0x584 pitch and roll rates, +0x588 yaw acceleration ✔, +0x58C lateral
  acceleration over the tires' grip ✔, +0x5A4 drag ✔, +0x5B0 rolling resistance ✔, +0x5B4 the
  sum of the body's up axis times three words of car +0x158 (0x36A490), +0x5BC (phase 4), +0x5C4
  speed / highest speed, +0x5D0 distance driven and its carry, +0x5D8 the engine condition's
  words (car +0x6DC), +0x606 / +0x607 the turbo's spool level and blow-off state ✔, +0x5FC / +0x600 the view's heading and pitch lags ✔, +0x618 best lap,
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
  +0x60 the slip direction's low-speed fade ✔, +0x64 brake pressure ✔, +0x68 / +0x6C the
  lateral forces (curve A, curve B) ✔, +0x70 / +0x74 the longitudinal force driving / braking
  ✔, +0x78 brake torque ✔, +0x7C / +0x80 its reaction driving / braking ✔, +0x84 the net torque ✔,
  +0x89 surface index,
  +0x8A / +0x8B the ground record's bytes, +0x8C ground height ✔, +0x90 the road bump, +0x94
  suspension travel, +0xA4 brake heat ✔, +0xA8 the second wear ✔, +0xAC the tread wear ✔,
  +0xB1 the HUD's wear level ✔, +0xC0 flap ✔, +0xC8 a per-wheel suspension term (0x36A080),
  +0xD4 the wheel's force across the car ✔, +0xDD the tire record's byte +0x1DA ✔. (The
  spec's earlier "+0x00 / +0x04 ground height, +0x08 load" was wrong.)
- car + 0x7B8 + 0x34 w: wheel w's ground record (§6; GT3's layout) ✔.
- car + 0x774: four signed bytes (0x35A5F8); [0] the last gear change (+1 up, -1 down, 0 none) ✔.
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

- **Brakes and clutch** (0x3511A0): the clutch (0x350FB8 as the engine's `clutch_engage` under 2002
  Tokyo-Geneva's constants; a one-speed box, kind 3, 0x351178: locked while the clutch is requested,
  else open; a box of kind 4 always locked); each of the layout's wheels' brake torque its pressure times
  its axle's brake torque (P +0x1010 + 4 a), the handbrake (+0x490 x P +0x1018) added on the wheels
  behind the center of gravity (layout +0x3C + w negative); the wheels' flags (2 braked, 1 no load).
- **The engine's torque** (0x3554A0): as the engine's `engine_torque` (turbo lag, boost 0x34ECF0 /
  0x34EC30 / 0x34EB00, blow-off, spool) with the rev limiter (0x355410: on at the rev limit E +0xCC,
  off 500 rpm below) and the power cap (0x34EA68) of the engine block (§4.3); then 0x354B78 counts the
  fuel and the nitrous (not ported). Phase 4's full-throttle torque: the 24-point curve (bisected) times
  the condition's factor; the friction torque E +0xD0 x the speed (0x34EA08; its other rule, by the
  static byte 0x620E14, is off).
- **The wheels' torques** (0x36D230) and **vectoring** (0x36D2F8): the engine's, over the layout's wheels
  and wheel groups (§3.5; the group's first wheel and its partner), the tire radius D +0x90 + 4 a;
  layout 7's front motors (0x356418) not ported.
- **The axle step** (0x36D818): the engine's later builds' (`axle_step_later`) with the full transfer
  97999.99 and 2002 Tokyo-Geneva's constants, generalised: the driven wheels' flags by the layout and
  each wheel's side of the center of gravity; layouts 9 (four wheel drive biased to the front: its own
  coupling, the front axle carrying the engine's inertia, the center clutch locking as 4 .. 6) and 10 (as
  3); the wheels' torques summed per axle; the coupled wheels (flag 4) those not on the driven axle
  (layout +0x2C + w); the braked test any wheel's brake torque (0x36D7A8), per group its two wheels';
  four wheel drive system 5's gain 0x36D548 with the program's fade (§5); a stepless box's ratio and
  torque (0x3F2110) with its rates (§5). A one-speed box's hybrid drive (0x368628, the Prius's), a box
  of kind 4's motor (0x356E58) and layout 7's engine and motors (0x3560F0, 0x356BB8) are not ported.
- **The wheels' speeds** (0x36E4B8): the engine's per wheel group: open (each wheel alone), locked
  (both at the axle's speed), clutch-pack (3 / 4), viscous (5) or through the differential (any other);
  the lock and the drive torque by the group's index; with the axles' tires told apart (D +0x01) every
  group at the driven axle's radius.
- **The engine's speed** (0x3519B8) and rpm (0x350F48): the engine's; the pedal clutch pressed home
  sets each group's axle and wheels from the last group's wheel speed on (the program carries the
  product across groups); the rpm not below idle - 1 (a box of kind 4: 0); the gear change mark car
  +0x774 cleared but while shifting.
- **The gearbox** (0x351DC8, 0x36C8E8): by the pedal style: automatic 0x36C160 (style 0; 2 with the
  kick-down 0x36C098), manual 0x36C690 (1), sequential 0x36C780 (3, 4, 6), sequential into reverse
  0x36C850 (5); the engine's rules but for: the lock-out held at 1 only in the controller settings'
  mode 2 (automatic), the gear for a speed 0x36BF68 (first gear's up-shift less 2,000 rpm, the others'
  less 500), the down-shift speed D +0x30 + 2 (g - 2) (0x36BF28) and the up-shift D +0x2E (0x36BEE0; a
  computer's own by its driver word ❓), the traction veto's wheels by layout (1, 2, 9 the front; 3, 8,
  10 both; the rest the rear) and side, the change mark (0x35A5F8), a special car of kind 4 never in
  reverse (0x36BEB8). One-speed, stepless kind 1 and special kind 2: 0x351EB0 (forward or reverse,
  first gear while the start's hold runs; the clutch to slip for a stepless box).
- **Brake heat** (0x3F38D8): the engine's rule per layout wheel, the torque the axle's brake torque times
  its tire radius, no heating while the brake pedal is up; constants 0x845A00 made at a race's set-up
  by 0x3F3808: 1 / (b0 x 9800), 1 / (b1 x 0.1 x 100), b2 / 3.6, b3 x 0.01 of the static block's +0xAC ..
  +0xAF (b0, b1 at least 1).
- **The moving start** (0x363868): the speed at most the top speed P +0xFD8; the yaw rate cleared, the
  velocity along the forward axis; the speed records (0x363538); the gear 0x36BF68; F +0x518 = 1; the
  engine at the speed times the gear's factor (a stepless box of kind 1 held at its hold speed,
  0x3F1FB0), between idle and the rev limit; the turbo spooled (the lags at the rpm, intake the capped
  boost, load 1); a computer's car (its driver object's +0x1E 0) the throttle and its pedal at 1; the
  clutch locked and requested; the speed along the body; every wheel's rim and hub speed; each axle at
  the speed over its tire radius (the driven axle's for both with unequal tires).

- **The suspension's places** (0x36A490, 0x36A080, 0x36A1D8): F +0x5B4 = the body's up axis . the ground's
  up axis; per wheel, wheel +0xC8 = its ground normal (course frame turned into the record's: (x, -z, y))
  . the body's up axis; the hub's rest drop r_w = -(x_s) - (tire radius - P +0x2DC) + the hub offset
  (0x3696C8; P +0x2DC + 4 a = the tire's sidewall, width x aspect, less a larger rim's half, at least half
  an inch: 0x34BA30); the place along the travel F +0x410 + 4 w = ((pos z + hub z + up z x r_w) - (ground
  + bump)) x ground up z / wheel +0xC8 - tire radius + hub offset - r_w; F +0x4B5 the wheels below their
  bump stop. Not GT3's (the terrain factor ground up z / up z for all four).
- **The suspension** (0x36A758): per wheel, wheel +0xD8 = the hub's velocity (the body's, plus the up axis
  x (± half track x roll rate by side ± the axle's distance x pitch rate)) along its ground normal. Each
  wheel's travel (0x369DE8) moves on with its own mass (block +0x38: the spec's unsprung kilograms x 0.25;
  x + (v + (spring + damper) / m x dt / 2) x dt, held within the droop, block +0x44 while the hold counter
  runs, and the bump stop); a travel more compressed than the ground's place leaves the wheel off the ground
  (spring -x k, its speed (x - last) / dt within ±1, 0 at the droop, load 0); on the ground GT3's spring,
  progressive rate, damper (0x369B48: GT3's root fit; with the switch 0x620DD9 a linear fit, block +0x48 /
  +0x4C; with the word 0x620CB0 eased into a square law at 0x620CB8 ..: both off on the disc) and bump
  damping; a negative load clears the damper and sets the spring to -the wheel's weight. Wheel +0xDC: 0 off,
  1 on, 2 landed, 3 left. The half track (0x36A690) and camber (0x36A578) by the layout (GT3's rules; the
  steering's camber on wheels ahead of the centre of gravity; the in-line layout's camber the steering x
  P +0x1004 x cos(roll - ground roll)). Anti-roll per wheel group (0x36A048). Each wheel's body force F +0x420
  + 4 w: its spring (-weight when unloaded), ∓ the bar by side, - the axle's downforce F +0x5A8 / +0x5AC
  over its wheels, ± the longitudinal transfer P +0x38 x F +0x564 (x 2 / wheels when not 2); and, with the
  static block's byte +0x139 set (it is), the lateral transfer per wheel: (P +0x1060 + place - hub offset +
  tire radius) / half track x the wheel's lateral force (wheel +0xD4), ∓ by side (P +0x1060 = the spec's
  +0x27 x 0.001); without it GT3's P +0x34 x -a_y. A wheel without a partner in its group takes no bar and
  no lateral transfer.
- **The attitude** (0x36B0C8; the start's hold 0x36AE20 not ported): pitch and roll from the body forces and
  dampers by axle about P +0x11C .. +0x128 with the tires' moment (0x36AF30, GT3's), the pitch rate within
  ±pi / 2, the roll rate within **±4 pi** (GT3 pi / 2), both angles within ±pi / 4; the body axes
  (0x36C9F8). The heave by the static block's byte +0x114 (2 on the disc): each wheel's load less its own
  weight, x (its ground normal . up), along its ground normal (1: the ground's up axis; 0: GT3's sum along
  the body's up axis), plus (the tires' lift - the downforce) along up (the speed-switched part of kind 7
  adds a lift near the ground), over the sprung mass F +0x4D8 (P +0x4C + fuel x 0.74, 0x354AC0; P +0x4C =
  the mass less the wheels' own, 0x362B58); gravity; F +0x558 the acceleration along up. The in-line
  layout's own lean term (0x62076C ..) is ported but no car uses it.
- **The bump-stop nudge** (0x36BA88): GT3's, on the layout (kinds 1 .. 3 pick the wheels; kind 3 by the
  front share P +0x24), with GT4's places and the hubs' heights 0x3457B0.
- **The steering** (0x3F7EA8): each wheel's angle (0x3F7CA8: the command x P +0x10C / +0x110 by side ahead of
  the centre of gravity; behind it -command x P +0x10CC x the ratio when P +0x10D0 is 1; the axle's toe
  P +0x1008 + 4 a, atan2((spec +0x8E + a - 0x80) / 2 mm, tire radius), off on the left, on on the right);
  the command's follower F +0x664 ..; with the mode word 0x845BF0 (the static block's +0xED, made by
  0x3F7BE0 at the race's start) the slip angle's offset wheel +0x1C (rate +0x18) springing toward half the
  slip direction's difference from the wheel's angle (0 on the surfaces 0x620288 marks), within ±limit; with
  0x620AF0 the compliance steer wheel +0xCC (rate +0xD0) from curve A's force x P +0x1058, a dead band and a
  gain by the car's age F +0x5E8 (tables 0x366100), added to the angle.
- **The driving aids** (0x3F79A0): by the control source: a pad car its own block (P +0x1098, from spec
  +0x1B8 / +0x1BA ..), a computer's car its driver's (the driver object at car +0x14, its parameter block
  +0x24 .., from the course set's s9, s10, s20, s19, s21 .. s23, s11 .. s13, s24, 0x466C20); the indicators
  F +0x470 .. +0x47C (0x3F71E8); TCS (0x3F9D00) on the driven wheels by layout, the target the tire's driving
  slip (curve D's rising side with the static byte +0x117), a throttle follower above 100 km/h F +0x644;
  brakes and ABS (0x3F9870: the target the braking slip, P +0x1024 / +0x102C / +0x1034 per axle; a dirt
  course's brake cut by the steering, static +0xE5 ..); ASCC (0x3F94C0) through brake shares (0x3F9340);
  the race's speed caps (rule block +0x42 / +0x43 / +0x4B / +0x4C, the rolling start's pace 0x3F7880, the
  throttle for a speed 0x3F76E8); F +0x5F8 / +0x5F9 the ASCC's and TCS's states.

### 3.4 Others read so far

- A computer's input (0x3F3400, phase 5 for control sources 1 / 2): the handbrake and shift requests
  cleared; the clutch request (ordinary gearbox, racing): with the clutch open, set once the rpm passes
  three quarters of the up-shift speed (P +0x262), with it closed, set down to idle (P +0x202); 0 for a
  stopped driver (kind 8); a box of kind 4 always 1. The session runs this rule; the rest of the input
  phase (0x349FD0) is not ported.

### 3.5 The wheel layout

A car's wheels are a row of the program's table at 0x620478 (0x54 bytes a row; the row is the
parameter block's byte +0x30; 0x359510 / 0x359538 return it): +0x00 a kind (1: the camber's
grip is read at 0), +0x01 the wheel count the step runs, +0x14 + w the wheel's axle (the tire
records, the widths, the grips), +0x1C + w its side (0 left, 1 right, other: on the middle),
+0x2C + w the axle whose half track and distance the yaw's levers take, +0x3C + w (signed)
ahead of (above 0) or behind the center of gravity, +0x44 + w 2 for a wheel with a camber
force; and its wheel groups (the driveline's): +0x02 their count, +0x04 + a axle a's wheels, +0x08 + k
group k's wheels, +0x0C + k its first wheel, +0x10 + k its axle, +0x24 + w the other wheel of wheel
w's group (0x7F none). The table has four rows: 0 four wheels (two groups), 1 two (in line), 2 and 3
three (one wheel in the middle at the front or the rear).

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
- **The gearbox** (0x34ACA8) ✔: its kind spec +0x158 (5 read as 2, 6 as 0; a stepless kind 1 with
  fewer than two gears is 0), the gears +0x155 (at most 9; a one-speed box 1); each gear's ratio its word
  +0x138 + 2 g x 0.001 (reverse 3.5 and first 4.0 when below 100; a later gear below 100 ends the box
  one gear before it) times the final drive (+0x150 x 0.001; 4.0 below 500), reverse negative; the
  second reduction (+0x152 x 0.001, from 500) by its kind +0x154: 1 every gear times it (the Honda
  S2000's 1.160), 2 reverse and the gears from the fifth on times it over the final drive (the Audi A3
  3.2 quattro), 3 reverse and the third and fourth, 4 none (it marks the axles' tires as unequal, D
  +0x01, as layout 5 with unequal radii does); each gear's speed factor its ratio over the driven
  axle's tire radius; the engine's inertia +0xB8 x 0.001; the launch speed +0x160 x 10 (at least idle +
  100; a stepless box's idle), the up-shift speed +0xBE x 100, each down-shift speed (g = 1 .. gears - 1,
  into D +0x30 + 2 (g - 1)) the up-shift speed carried into the next gear (x ratio g + 1 / ratio g; the
  first averaged with the launch speed when the static byte +0x112 is set) less 10 km/h's rpm in the
  next gear, at least the launch speed + 500 (for every gear); a stepless box's ratios from the second on relative to
  the first; D +0x13 the up-shift speed's class (1 below 4,500, 2 below 5,000, else 3; 4 for the car
  category 5). A stepless box of kind 1's first down-shift speed is set again by 0x360780 ❓.
- **The drive** (0x34BC98) ✔: spec +0x128 0, 3, 4 the rear wheels (layout 0), 1 the front (1), 2 all four
  by the system +0x129: 0 layout 2 (the front axle driven), 1 layout 3, 2 layout 4 (the split 1 from a
  front share of 50 on; capacity +0x12B x 10 x 9.8, 8 when 0), 3 layout 5 (the split the share x 0.36 x
  the program's byte 0x620308 percent (200); the capacity its byte 0x620309 x 98; the fade's start and
  floor 0x62030C / 0x620314 from the bytes 0x620310 / 0x620318 percent), 4 layout 6 (the split 1 / (the
  share in degrees); capacity 15 when 0), 5 layout 7 (0x355978: the motors, not ported), 6 layout 9 (the
  front axle driven), 7 layout 8, 8 layout 10; the split otherwise the front share (+0x12A percent).
- **Tires** (0x34BA30): radius = rim / 2 + width x aspect; the rim in tenths of an inch
  (values below 61: inches); for tire kinds 6 .. 10 the chassis' two bytes change the
  sidewall and the width. The half-track is narrowed where the tire would stand out past the
  chassis' width across the tires (0x361D70).
- **Engine** (0x34EF30) ✔: the block at +0x134 (the engine's E with a curve of 24 points, what follows
  it 0x40 further on). Idle spec +0xBC x 10, rev limit +0xBD x 100 (the last point's speed when 0; at
  least the shift limit +0xBE x 100 + 200); speeds the points' bytes +0xC0 x 100 rpm in rad/s; torques
  the u16s at +0xD8 (the last, when 0, the one before; values below -1 read unsigned) x 9.8 x 0.01 x the
  output factor (+0x108 per mille) x the power percentage (+0x07), shaded linearly over the curve from
  +0x10C percent at its first speed to +0x10A per mille at its last; friction +0xBA x 0.1 x 9.8 over the
  last speed (x 0.9 instead with the static byte +0x13C); the turbo (0x34ED48): the cap +0x113 x 0.1 with
  a first stage (knee +0x116 x 100 rpm, peak +0x118 and rate +0x11A x 0.1, shape +0x114) and a second
  when its three bytes are set, each stage's square term its peak over its knee squared; each torque
  then times the capped boost at its speed over the uncapped; a power limit from a rule byte, as GT3's. Driveline (0x34ACA8) at
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
- 0x622110 the controller settings (GT3's 0x2D4628): +0x08 the automatic gearbox's mode (1), +0x09
  .. +0x0B the down-shift's offsets with the throttle, the lift and the brake (5, 0, 0; x 100 rpm); +0x70
  the driver input's steering prefilter.
- 0x62030A four wheel drive system 5's switch (a negative torque gives minus the full transfer),
  0x62030C / 0x620314 its fade's start and floor (0.25, 0.15). 0x6220F8 .. 0x622108 the stepless
  boxes' rates, made once by 0x3F2070 from the bytes 0x6220F4 / F5 / 0x622100 / 01 x 0.01 (0.40,
  0.15, 1.5, 1.5). 0x845A00 the brake heat's constants (§3.3).
- Words the race's start sets that the image holds otherwise (a session builds them as the start does): the
  aerodynamics' globals 0x622308 .. 0x622318 (0x3F2F78, below); the dampers' square-law table 0x620CB0 .. 0x620CD4
  (0x360A78, called by the converter: on with the static byte +0x102; each axle's bump and rebound knees +0x105 /
  +0x109 x 0.01 and shares +0x107 / +0x10B / 100); the rolling resistance and bump tables (0x35F808); the steering's
  compliance (0x366100); the TCS classes' factors 0x6220E0 (0x3F1790). `gt4r_diff ai` with `GT4R_AI_GLOBALS` lists
  every data word between 0x620000 and 0x624000 that a race's set-up changes.
- **Aerodynamics** (0x3F3090, phase 4): the globals as 0x3F2F78 sets them: 0x622308 the air's density (the static byte
  +0 x 0.1), 0x62230C / 0x622310 the drag's and the downforce's shares a slipstream does not take (bytes +1 / +2 over
  +0), 0x622314 / 0x622318 the slipstream filter's rates up and down (10 over the bytes +5 / +6; 1 and 0.25 without
  them). The slipstream's filter F +0x5A0 moves toward the target F +0x59C (phase 14 sums the factors behind every
  other car above 100 km/h, at most 1) by the rates x the step, within 0 .. 1. The drag F +0x5A4 = -(P +0x1068 x
  ((share + (1 - share) (1 - filter)) x v²)) (+ backing up; the share 20 in the mode F +0x5C3 = 2 ❓ its writer); the
  downforce F +0x5A8 / +0x5AC = P +0x106C / +0x1070 x (the same with the downforce's share) x v², nothing when the
  flow's angle to the body (atan2 of the velocity along the body's up axis over the speed) is past 10 degrees, faded
  linearly from 5 to 10; then the part that changes with speed of kinds 1, 2, 5 and 6 adds its lift P +0x1084 x the
  same x its state F +0x594 to the rear (0x3F2DB8). The converter's coefficients (0x3621B0): the frontal area (the
  spec's s16 +0x40 / 10,000, or the body's width x height) x the density x 0.5, times the spec's bytes +0x42 / +0x43 /
  +0x44 x 0.01; the drag then plus the two downforces x the static byte +0x0A per mille.
- ❓ The tire effect block and the computer drivers' tables (GT3's 0x2D35B8, 0x2DA030 ..): their
  counterparts are not located.

## 6. The ground

- **The query points** (0x3697A8, phase 16): each wheel's hub point (position + the hub's offset) moved along the
  body's up axis by the hub's rest drop under its travel (0x3696C8: -(x_s) - (tire radius - P +0x2DC + 4 a) + the
  suspension block's +0x0C), into the record's +0x00 as (x, height, -y).
- **The query** (0x395760 for one wheel, 0x3957F0 for four; the road's 0x3E5A30): a segment from
  5 m above the record's point (from the road's highest bound when above it) straight down to the road's
  lowest bound. The road's lookup tree is walked along the segment (0x3E5BC8): a node's split at
  box low (1 - f) + box high f, f = (place + 0.5) / 64; a segment that crosses it is cut there (the cut
  point (1 - t) a + t b, its split coordinate the split itself) and the part on its start's side walked
  first; the first cluster with a hit ends the walk. In a cluster (0x3E5FD8) each triangle is tried with
  D = b - a, P0..P2 = the corners - a: P1 . (D x P0) at most 0, P2 . (D x P0) at least 0, D . (P1 x P2)
  at most 0; with N = (V1 - V0) x (V2 - V0), t = N . P0 / N . D when N . D is not 0 (N . P0 at most 0
  and not below N . D), else t = 0; the earliest t wins (the first of equals in the list; the bound starts
  at the word 0x7FFFFFFF). The answer (0x30 bytes): the point a + t D (clamped into the triangle's box),
  the material (triangle +6, bits 0-4, a word), the flags (triangle +7, a word), the corners' bytes +0x0E
  / 255 and +0x0F (bits 0-6) / 127 weighted (V1 by -P2 . (D x P0), V2 by P1 . (D x P0), over their sum
  with D . (P1 x P2)) and clamped to 0 .. 1, the unit normal (N x rsqrt), the plane -(n . V0), +0x2C the
  triangle, +0x2E its cluster. Ours (`RoadGround::query`) is this in the PlayStation 2's arithmetic.
- The wheel's ground record (car + 0x7B8 + 0x34 w): +0x00 the query point, +0x0C the triangle and its
  cluster (cluster << 16 | triangle; wheel 0's is the car's F +0x4A8, the collision step's hint), +0x10
  height, +0x14 / +0x18 the two values, +0x1C normal, +0x28 plane, +0x30 1 when nothing was found (the
  rest then kept), +0x31 material, +0x33 flags. A normal with y below 0.5 is replaced by straight up, the
  plane kept (0x344458).
- **The ground's frame** (0x35D350, phase 19 after the body's axes): from the wheels' ground heights g0 .. g3 by
  the wheel layout's kind: four wheels: pitch atan2(((g0 + g1) - (g2 + g3)) x 0.5, wheelbase x cos pitch), u = g0 +
  (g2 - g0) P +0x20, v = g1 + (g3 - g1) P +0x20, roll atan2(u - v, track x cos roll), height (u + v) x 0.5; two in
  line: pitch atan2(g0 - g1, wheelbase), the roll from each wheel's ground normal across the body; one ahead: pitch
  against the rear pair's middle, roll atan2(g1 - g2, 2 half tracks); one behind likewise. Its axes as 0x36C9F8.
- **The surface index** (0x3F4E40): the material; with flags bit 1, material 0 or 7 → 20, 1,
  10, 11 or 12 → 21, 9 → 22. The surface picks the tire block (by its class), the grip
  percentage, the rolling resistance and the road bump. Flags bit 0: the pit lane.
- The disc's courses use materials 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11 and 12.

## 7. Walls and other cars

What a step publishes of the collision step (`race::CarState::contact`, `contact_kind`, `contact_car`):
the body's summed impulse strength (|j| / 15,000 between cars, / 100,000 against a wall, at most 1),
whether a wall or a car was met, and the car (the one whose edge this body's vertex met, or whose
vertex met this body's edge). `landed`: a wheel loaded at the step's end after a step without load.
The program's own results back to the cars (0x3F4C18) keep, per car at cond + 0xCAB8 + 0x30 i, a hit
flag (+0x24) and, with the switch byte 0x6202BA, a change of velocity in the car's frame past a threshold
(+0x1C / +0x20) ❓ its readers.

**The collision step** (0x45BB60 on the manager at conductor + 0xDA6C: +0 the conductor, +4 the count, +8 the
course object, +0xC the bodies, 0x4F0 bytes each) follows Gran Turismo 3's (physics §5.3) step for step, with
these changes:

- The body record from +0x450: +0x450 the car, +0x454 position, +0x460 velocity, +0x46C pitch, roll, heading,
  +0x478 their rates (0x3F4B20 copies F +0, +0x544, +0x568 .., +0x57C .., the hint F +0x4A8 to +0x484; only the
  position, velocity, heading and yaw rate go back), +0x488 the course object, +0x48C front, rear, half width,
  mass, 10 x the yaw inertia (0x846638 = 10), the two chamfers (0.2 x width, 0x3E4CCCCC), +0x4A8 the bodies'
  array, +0x4AC / +0x4C4 the pair tables (six bodies). Wall references are pointers to the road's wall points.
- **The walls near a body** (0x395A38 with 0x3E6A58): those listed at the road triangle of its hint (the
  triangle's +0x08 word: start in the low 24 bits, count in the high 8, into the s16 list at road +0x8C of wall
  points at road +0x84); with a negative hint, the road found straight down from 10,000 m at the box's middle
  (0x3E5A30); none when the hint's cluster is negative. Of those, each whose segment is not wholly beyond one side
  of the box is handed to the sweep (0x45D550) or the push-out (0x45DBB8) in the list's order.
- **Levels**: two bodies meet (separation 0x45FAD0, sweep 0x45ECB0) only when their sweeps' start heights are
  within 4 m.
- **The wall hold** (0x45D0E0): the velocity becomes its projection on the held point's own tangent.
- **The stuck nudge** (0x45CA60): only a player's car (control source and driver kind 0; 0x3451B8), touching a
  wall (a point or corner hit, a point held or crossing, an edge's +0x1C or crossing), turning by its steering's
  value F +0x45C x 20 x dt.
- A corner hit sets the body's front (edges 0 .. 2) or rear (4 .. 6) flag; the sensor check runs on every body.
- The coefficients from 0x45C0F8 (given 0): the cars' restitution 0.5, a wall's 0.05 (0x3D4CCCCC), friction
  0.5. The pad's rumble after a wall hit lasts 23 steps (F +0x610). The hit records (0x350530: the car's damage
  0x35CF40 / 0x35CF88 / 0x35D0C0, F +0x62F .. +0x631, the conductor's virtual +0x190, F +0x608 .. +0x60D) are not
  ours.
- A body is left out of car contact (0x343BF0) when its car is not in the race or its driver is on line 4.

## 8. What runs where in our simulation

`sim::Session` steps the cars with the shared engine's vehicle step (the "exact core", GT3's
rules in the PlayStation 2's arithmetic), fed with Gran Turismo 4's data:

| Part | Runs | Follows |
|---|---|---|
| Parts, equipment, default settings, the spec, its fix-ups | `equipment.*`, `car_spec.*` | Gran Turismo 4 (§4.1, §4.2): equal to the program's on all 722 cars, byte for byte (§9); computed gearboxes' ratios by the engine's generator (7 gears at most) |
| Spec → parameter block | `exact_bridge.*`: `core_spec`, the engine's converter, `gt4_params` | GT3's converter on our spec translated field by field, then Gran Turismo 4's rules where they differ (the levers, drag, ride heights, the suspension's travel). Equal to the program's converter on every car for the fields of §9, but for what the engine's block cannot hold: engine curves beyond 16 points (thinned), gears beyond 7, half-inch rims and tire kinds' changes (one car), the three-wheelers' loads, four wheel drive system 5, the speed-switched part, nitrous, ballast's place, gearboxes without steps (run as stepped) |
| Program tables | `program.*`, `core_program` | the static block from `CORE.GT4`, folded to the core's 16 surface rows (0 .. 11 as they are, 20 / 21 / 22 → 12 / 13 / 14, 12 → 15), except the slip ratio's bytes; every other table the engine's own (GT3's values) |
| The road query (0x3E5A30 with 0x3E5BC8 / 0x3E5FD8), surface index | `ground.*` | Gran Turismo 4 (§6), in the PlayStation 2's arithmetic: equal to the program's answer at every call (§9) |
| The collision step (phase 13: 0x3F4B20, 0x45BB60 and its routines, 0x3F4C18; the walls near a body, 0x395A38 / 0x3E6A58) | `collide.*` through `session.cpp` | Gran Turismo 4 (§7): every body field equal to the program's at every call (§9); the bodies built as 0x45B978 builds them |
| The query points (phase 16, 0x3697A8), the steep ground's normal (phase 17, 0x344458), the wheels' surfaces as 24-row indexes, wheel 0's triangle as the collision step's hint (phase 18), the ground's frame (phase 19, 0x35D350) | `rules_body.*`, `session.cpp` | Gran Turismo 4 (§6) |
| The aerodynamics (phase 4, 0x3F3090 with 0x3F2DB8; its globals as 0x3F2F78 and the converter's coefficients as 0x3621B0 make them) and F +0x5BC | `rules_chassis.*`, `exact_bridge.*`, `rules_step.*` | Gran Turismo 4 (§5) |
| The class grip of a car a computer drives (phase 10; a single race's 20 % for every drive class, 0x353570; `rules::Tables::class_grip`) | `rules_step.*` | Gran Turismo 4 (§2) |
| Phase order | `session.cpp` | phases 1 .. 13, then 16 .. 22, as the program's step; the engine's remaining routines with the program's C library and constants (`rules::behaviour()`: 2002 Tokyo-Geneva's) |
| Gran Turismo 4's own part of the record | `rules_build.*` `gt4_extras` | the program's converter (§3.3, §4.3, §9): the tire records with 24 surfaces and their kind bytes, the tire index, widths, axle grips, camber curves, the direct scales, the wheel layout's row; bit for bit on every car but the base grip of 168 of 4,332 records (a unit in the last place: it takes the static loads of the engine's converter). The engine block (24-point curves, friction, turbo) bit for bit on every car; the driveline block's layout, drive, gearbox kind, gears, ratios, splits, capacities, launch and up-shift speeds bit for bit; its tire radii, widths, differentials' settings and axle inertias the engine converter's (a unit or two in the last place; the tank's tires 1.5 % off), so the tank's speed factors and a down-shift speed differ; the down-shift speeds a stepless box's hold speed sets (0x360780: 3 cars) are not made |
| The engine's torques (phase 4), brakes and clutch, wheel torques, vectoring, the axle step, wheel speeds, engine speed and gearbox (phase 10), brake heat (phase 11) | `rules_drive.*`, `rules_wear.*` through `rules_step.*` | Gran Turismo 4 (§3.3) on the engine and driveline blocks; the four wheel drive's fade, the stepless rates, the controller settings and the brake heat's constants as the program has them (checked, §9). The Priuses' hybrid one-speed box, the box of kind 4 (the MTR GMV) and layout 7's motors (the Acura DN-X) run as an ordinary box; the fuel and nitrous are not counted. A car our computer drivers drive shifts by the engine's gearbox: the program shifts a computer's car at its driver's speeds (0x36BEE0 / 0x36BF28 through 0x3F1E30, the speeds x a driver byte / 128 ❓), and with the program's own speeds an automatic box can hunt (the Volkswagen Beetle 1100's down-shift into third and fourth at 3,500 and 4,000 rpm, its up-shift at 4,000). With the rest of the driveline Gran Turismo 4's, the original's arcade field at Tsukuba laps in 96.4 .. 101.7 s (the original's two-lap race: 92 .. 101 s a lap; the engine's throughout: 99.5 .. 105.4 s) |
| A rolling start | `rules_start.*`, `session.cpp` | the car set-up's moving start (0x363868) after the session's hold; its places (rules::rolling_start_distance, the race's rule) on the road's middle, not the driving line |
| The wear's grip, peak forces, slip angles (phase 4's tire part), slip ratios (phase 8), rolling resistance, tire curves, the longitudinal and lateral forces, the speed along the body, the forces across the car and the yaw, the standstill hold (phase 10) | `rules_*.cpp` through `rules_step.*` | Gran Turismo 4 (§3.3); the race's tables (`rules::tables`: the rolling resistance by surface and speed, the slip bytes, the globals) as the program's race set-up makes them (checked, §9); the wear rules off, as in every race seen |
| The suspension's places, suspension, attitude, bump-stop nudge, body axes (phase 19's first four and its axes), the steering (phase 7), the driving aids (phase 9: a computer's car by its driver's block, `Session::set_driver_aids`, as control source 2) | `rules_body.*`, `rules_steer.*`, `rules_aids.*` through `session.cpp` | Gran Turismo 4 (§3.3); while the hold counter runs, the engine's start hold |
| The road's bumps (0x369888's: Gran Turismo 4's 24 surfaces, the soft materials' halving) and a computer driver's grip by tire class in the peak forces and the lateral acceleration over the grip (0x467470: the driver's parameter block +0xA8, 1.2 on a loose course's set; `Session::set_driver_aids(car, aids, grip)`) | `rules_body.*`, `rules_tires.*`, `rules_chassis.*` | Gran Turismo 4 |

**What still runs Gran Turismo 3's code** (the engine's routines, through `session.cpp`; each to be replaced by the
program's own):

- Phase 1's last-step values (`ex::save_previous`: the engine's routine, checked equal to 0x343ED0).
- Phase 2 and 3, 0x355E10 and 0x35CA60: not run at all.
- Phase 4, the preparation (0x357700) but for its tire part, the aerodynamics and F +0x5BC: `ex::prep` (0x35C0B8,
  0x35ACB0, the step length; the masses with the fuel, 0x354AC0, not counted).
- Phase 5, the driver's input (0x349FD0): `ex::driver_input_pad` for a pad car (a computer's car takes its
  driver's outputs as 0x464F38 writes them, and the clutch request as 0x3F3400 makes it).
- Phase 6, 0x35D1D0: not run.
- Phase 10's gearbox for a stepless box of kind 1 and the hybrid one-speed box (0x368628), a box of kind 4's
  motor (0x356E58), layout 7's motors: run as an ordinary box; the fuel and nitrous (0x354B78): not counted.
- Phase 12, the hit record (0x350308): `ex::clear_feedback`.
- Phase 13's hit records (0x350530, 0x350328: the cars' damage, the conductor's virtual +0x190) and the results'
  velocity change records (cond + 0xCAB8): not made. A body's driver on line 4 (0x343BF0) is not known to the
  session: such a car still meets the others. The stuck nudge's steering value F +0x45C is taken as 0x348800 makes
  it of the steering command (❓ the input phase writes it so for the pads' kinds read).
- Phase 14, the proximity and slipstream (0x3F4638): not run (the slipstream's target stays 0: it counts only
  above 100 km/h, 0x3F44A0).
- Phase 15, the part that changes with speed (0x3F28B8): not run.
- Phase 16's hub offsets and speed ramp (`ex::hub_offsets_xy`, `ex::speed_ramp`: equal to 0x3458D0 / 0x345A50 at
  every call on four-wheeled layouts, §9).
- Phase 19 after the bump-stop nudge: the rumble, the steering force (0x353EE8), 0x367148, the engine's sound, the
  view's follower (0x345658: differs), the hub offsets (equal), the speeds (0x35DE20: the engine's wear, oil and
  odometer differ), the wheel spin (0x344040: equal), the tire effects,
  0x34A668, 0x34A788 (`ex::pad_rumble`, `ex::steer_force`, `ex::tyre_effects`, ..., `ex::speed_bookkeeping`,
  `ex::wheel_spin`); the attitude's start hold (0x36AE20: the engine's spawn hold); the speed-switched part's
  damping (0x3F2650) and a special car's spring rate (0x35D248).
- Phases 20 .. 22, the wear (0x352EA0, 0x353218) and the flaps (0x35A678): `ex::tire_wear`, `ex::wear_flags`,
  `ex::flaps`.
- The start: the engine's pose and standing start, and the session's two-second hold, not 0x363B10 / 0x363560.
| Start | `session.cpp` `place` | the engine's race start (GT3's pose and standing start) on the first start places; a hold of 2 s before time 0 |
| Distance along the course, laps | `ground.*` `distance_along` | ours (between the checkpoint gates) |
| The driver of `gt4tool drive` | `line_driver.*` | ours; not a computer driver of the game's |

So a car's weight, power, gearing, tires, springs and aids are its own, and so is how its tires,
driveline, suspension, body, steering and aids answer the road; a computer's car shifts by Gran Turismo 4's
gearbox as a player's (equal at every call on the computer drivers' cars too). Each car alone at Tsukuba with its
computer driver, best of three laps from rest (the program's on its own physics, `gt4r_diff ai` / ours, s):
Volkswagen 1100 90.8 / 90.8, Honda Z 99.6 / 100.7, Honda N360 100.4 / 100.7, MG Midget 91.6 / 92.3, Ford Ka
83.1 / 83.6, Honda S500 89.2 / 92.0 (❓ the S500's 3 s). With the engine's suspension, attitude and aids:
93.2, 99.6, 102.5, 100.5, 89.0, 97.0 (the S500's least speeds in the slow corners 8 .. 18 km/h lower, the
engine's ASM and TCS on the car's own block cutting in); with the engine's gearbox for a computer's car the Z
and N360 lapped 3 .. 6 s fast and the Z stood on Grand Canyon's climbs. The down-shift speeds' floor is the
launch speed + 500 rpm for every gear (0x34ACA8 reloads the launch speed each gear): with 500 per gear the
Volkswagen's automatic box hunted between second and third. With the driveline Gran Turismo 4's the
figures below do not change: on these four cars its routines are the engine's later builds' to the
bit (the taps show it); with the suspension, attitude, steering and aids Gran Turismo 4's: 400 m Civic 16.05 s,
NSX 13.45 s, Viper 12.50 s; top speed NSX 290.4 km/h, Viper 308.0 km/h. `GT4R_SIM_RULES=engine` runs the
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
- The routines of §3.3 equal at every call for the Honda NSX Type R, Civic Type R, Dodge Viper GTS,
  Mazda 787B, Subaru Impreza WRC, Nissan Skyline GT-R V-spec II Nür (four wheel drive system 5), Honda
  Fit (stepless, kind 1), Audi A3 3.2 quattro (second reduction kind 2), Mitsubishi Eclipse GT, Honda
  S2000 LM race car (24-point curve) and Ford Focus ST170; the Toyota Prius (a one-speed hybrid) differs
  in the axle step (not ported). The race tables the session builds (the rolling resistance, slip bytes,
  globals, the driveline's tables, the brake heat's constants) are equal to the program's after its
  set-up. `GT4R_STEP_ENGINE_TAPS=1` taps the engine's routines at the same calls, to see how they
  differ; `GT4R_LIST_DRIVES` / `GT4R_LIST_NEAR` (params) list the unusual drivetrains and the driveline
  values that differ.
- The suspension's places, suspension, attitude and nudge (0x36A490, 0x36A758, 0x36B0C8, 0x36BA88) equal at
  every call on the NSX, Civic, Viper, 787B, Impreza, S500, Ka and Midget; the steering phase and its helpers
  (0x3F7EA8, 0x3F7CA8, 0x3F7E20, 0x40CC58, 0x36CC40, 0x36CCD8, 0x359568) and the aids' phase and its routines
  (0x3F79A0, 0x3F71E8, 0x3F77B8, 0x3F7828, 0x3F9D00, 0x3F9870, 0x3F94C0, 0x3F9340, 0x3F92D0, 0x3F76E8,
  0x3F7880, ...) equal at every call on ten cars, with made-up settings that run their other branches
  (`GT4R_STEER_VARY`, `GT4R_AIDS_VARY`), and on the computer drivers' cars (`gt4r_diff ai` with
  `GT4R_AI_STEER`, `GT4R_AI_AIDS`, `GT4R_AI_STEPTAPS`: the drivetrain's taps too). The converter's parts
  they read (the wheels' masses, sidewalls, roll centre, held droops, speed-switched part, the aid blocks,
  ABS and steering parameters) equal the program's on all 722 cars, but for a unit in the last place where
  they take the engine converter's values.
- The road's bumps (0x369888) equal at every call at Tsukuba and Grand Canyon; with the computer drivers' grip
  (0x467470) the peak forces and the lateral acceleration over the grip equal at every call on a computer
  driver's car at Grand Canyon (without it 1 in 6 lower: the course's set gives loose tires 1.2). Each car alone,
  three laps there (the program's / ours): the N360's best 4'11.6 (one lap) / 4'28.4, the Z's 5'17.9 / 5'15.1;
  both crawl over the climb near 2,880 m (without the driver's grip they rolled back for over 1,000 s).
- **The collision step and the road query** (`collide_check.cpp`: `GT4R_STEP_COLLIDE` with `step`, `GT4R_AI_COLLIDE`
  with `ai`, the test `sim_program_collision_and_road_query_equal`): at 0x45BB60's entry every body record is read
  into ours (wall pointers as indexes), ours steps them on our walls, and at its return every body field (and the
  cars' rumble F +0x610) is compared; at 0x3E5A30's entry ours answers the same point on our road, and every answer
  field is compared. Six computer drivers' cars at Grand Canyon, three laps of the program's race: 61,456 collision
  steps, every body field equal at every one; 2,203,742 road queries, every answer equal. sincosf (0x57D8A0) is the
  engine's, bit for bit (`leaves`).
- **The whole step from the program's state** (`whole_step.cpp`: `GT4R_AI_WHOLESTEP=<s>`, `GT4R_AI_WHOLECAR=<i>`):
  from that time, before each of the program's steps every car's record and collision body are loaded into our
  session, both step with the drivers' outputs of the program's step (the throttle slewed as 0x464F38), and every
  placed field is compared. Car 0 of the Grand Canyon field over a minute: the position, velocity and attitude equal
  in all but 6 .. 13 of 3,600 steps (a unit or two in the last place there); every step differs in fields no
  routine of the step reads back: the engine's wear and oil, the odometer, the dirt, the view's follower, the flaps
  and the distance along the course (the engine's routines, §8). This found the class grip, the dampers' table, the
  aerodynamics, the query points and the ground's frame.
- The six-car field at Grand Canyon, three laps, walls met as the drivers count them (`gt4r_diff ai` with
  `GT4R_AI_OURS`, `GT4R_AI_INCIDENTS` / `gt4tool ai`): the program's physics (its race as `gt4r_diff` sets it up) /
  ours: Volkswagen 0 / 0, Z 6 / 4, N360 11 / 5, Midget 0 / 0, S500 13 / 4, Ka 5 / 8, in all 35 / 21; no car of ours
  below 5 km/h for long (the program's Z, N360 and S500 152, 142 and 27 s). The two races start differently: the
  harness's set-up places car 0 by the road's grid as the program reads it and cars 1 .. 5 (the road has two grid
  places) at the program's fallback 100 m up (`GT4R_AI_PLACES`), ours on the grid and 8 m steps behind it, so the
  counts compare two different races.
- Not compared yet: the step's routines not in §3.3 and §6, §7.

## 10. Open

- Everything marked ❓ above; the rewritten routines of §1; the three new phases.
- The step's routines not yet ported (the start's hold in the attitude (0x36AE20), the speed-switched part's
  damping (0x3F2650), a special car's spring rate (0x35D248), the driver input (0x349FD0), the masses with the
  fuel (0x354AC0), wear, the proximity and slipstream (0x3F4638), the new phases 0x355E10, 0x35CA60, 0x35D1D0,
  0x3F28B8, the rest of phase 19, the collision step's hit records (0x350530)); our session runs the engine's
  there or nothing (§8). `gt4r_diff`'s race places cars past the road's grid at the program's fallback (§9). A computer's car on Gran Turismo 4's gearbox (above). The hybrid one-speed box (0x368628, 3,700 bytes with its
  helpers), the box of kind 4 (0x356E58), layout 7's motors (0x356418, 0x3560F0, 0x356BB8, 0x355978),
  the fuel and nitrous (0x354B78, 0x354CE8), the top speed P +0xFD8 (0x360520; ours the engine's on
  644 cars) and the rest of the driveline builder (the tires 0x34BA30, the differentials 0x34AAD8).
- The computer drivers (the courses' `.ad` files and `crs/bestline`), the race flow (laps as
  the game counts them, sectors, the finish), pit stops, tire wear's curve per race, the
  rubber band.
- The start: Gran Turismo 4's own set-up routine (0x363B10) and standing start (0x363560); a rolling
  start's places on the driving line (0x46ADE8) and the computer driving the player's run-up.
- Other builds: a program that is not the mapped build serves no tables, and a session then
  runs on the engine's own values and says so (`Session::notes`). Prologue's disc runs a
  session on its database in Gran Turismo 4's layout, its `GTRW` roads and its own physics
  tables in the engine's core ([formats/prologue.md](../formats/prologue.md)).
