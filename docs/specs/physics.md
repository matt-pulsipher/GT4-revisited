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
| 2 | 0x355E10 | none | a layout 7 car's byte car +0x78D cleared (§3.6) |
| 3 | 0x35CA60 | none | a category 1 car's mood (§3.6; no car of the US disc) |
| | 0x34C210, then per car | 0x1F2B98 | the start's countdown: the cars are held while it runs |
| 4 | 0x357700 | 0x1EA428 (changed) | preparation: 0x35C0B8 while the hold counter F +0x4BA runs, 0x35ACB0, step length (0x3658A0), the masses (0x354AC0: F +0x4D4 / +0x4D8 = P +0x48 / +0x4C + 0.74 x the fuel F +0x4D0), aerodynamics (0x3F3090, §5 "Aerodynamics"), the engine's full-throttle and friction torques at its speed (0x34E9A0 / 0x34EA08 into F +0x4E8 / +0x4EC), F +0x5BC = -(F +0x5B0 + F +0x5A4 - 9.8 x mass x F +0x44), peak force (0x357418, with the wear's grip 0x356EF0), slip angles (0x357690) (§3.3) |
| 5 | 0x349FD0 | 0x1E4D38 (changed, four times the size) | the driver's input (§3.6) |
| 6 | 0x35D1D0 | none | a category 1 car's sitting count (§3.6) |
| 7 | 0x3F7EA8 | 0x1E8978 (rewritten) | each wheel's steer angle (0x3F7CA8), the command's follower (0x40CC58), the slip angle's offset and the compliance steer (§3.3) |
| 8 | 0x351778 | 0x1E7328 (same calls) | slip ratios (0x3513F8, changed; §3.3) |
| 9 | 0x3F79A0 | 0x1E3B18 (rewritten) | the driving aids (§3.3): the indicators (0x3F71E8), a driven car's own aids (0x3F77B8) or its computer driver's (0x3F7828): TCS (0x3F9D00), brakes and ABS (0x3F9870), ASCC (0x3F94C0); the race's speed caps (0x3F7880, 0x3F76E8) |
| 10 | 0x3535B0 | 0x1E8598 (changed) | in order: brakes and clutch (0x3511A0), rolling resistance (0x3534A0), tire curves (0x351850); for a car a computer drives (control source 1 / 2 or a driver kind), the class grip: the rule block's signed byte +0x18 + the drive class P +0x31 (conductor + 0xF830 ..) x 0.01 + 1 times each wheel's slip fade (+0x60) and friction circle (+0x58); a single race's conductor (and the free run's) sets each class's byte that the event leaves 0 to 20 (0x353570, its virtual +0xD8 at the race's start), so computer cars have 20 % more there; the longitudinal force (0x3518E8), the lateral forces (0x352408), the speed along the body (0x351FA8), wheel torques (0x36D230), torque vectoring (0x36D2F8), the axles (0x36D818), the wheel speeds (0x36E4B8); then by the driveline's kind 0x3694D0, or engine speed and gear (0x3519B8, 0x351DC8 or 0x351EB0); 0x350ED0; the forces across the car and the yaw (0x352848) and the standstill hold (0x352C90), both with the airborne flag (the wheels' loads sum to 0) (§3.3). The gearbox's step by its kind: a one-speed box (kind 3) 0x3694D0 (the rpm) then 0x351EB0; a stepless box of kind 1, or a special car of kind 2, 0x3519B8 then 0x351EB0; any other 0x3519B8 then 0x351DC8 |
| 11 | 0x3F38D8 | 0x1E6928 (small change) | brake heat (§3.3) |
| 12 | 0x350308 | 0x1DFD90 | clears the hit record (§3.6) |
| 13 | conductor virtual (+0x68), or 0x34DAA8 | virtual 12 / 0x1F3950 | the collision step (§7): bodies from the cars (0x3F4B20), the step (0x45BB60), results back (0x3F4C18) |
| 14 | 0x3F4638 | 0x1EC098 (small change) | which cars are near which, slipstream (§3.6) |
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
- **The tire wear** (0x352EA0, phase 20; ours `rules_wear.*` `tire_wear`; nothing for a car of category 4, P +0x32): the
  race's wear rules (cond + 4) also hold, after the curves, +0x20 the grip under which a car wants new tires (the pit flow),
  +0x24 the sliding measure's cap, +0x28 / +0x2C the extra of a braked or driven wheel's slip and the slip ratio (negative)
  from which it is whole, +0x30 the second wear's rate, +0x34 / +0x38 / +0x3C its weights of the longitudinal force driving
  and braking and the lateral force (curve A), +0x40 its weight of the sliding measure, +0x44 / +0x48 what it loses a second
  at its greatest and least, +0x4C the force's scale. With the second wear on, each wheel's (+0xA8) moves by dt x rate x
  (sqrt(lateral² + longitudinal²) x scale / the axle's tire width (P +0x2D4) + on surface 0 the sliding measure x its weight
  - the loss at its value, linear between least and greatest), held between them. With the tread wear on: each wheel's
  display (+0xB0) cleared; off the pit lane (F +0x4B1 bit 4), a wheel off surface 1 and loaded: m = its sliding measure
  (+0xB4), for a braked or driven wheel (+0x46 & 0xC) with the extra on x (extra + 1) at slip ratios at or below +0x2C, x
  (extra + 1) x slip / +0x2C between it and 0; at most the cap; x the tire record's wear rate (+0x04); the tread (+0xAC)
  grows by m x (P +0x1088 + 4 a + load x P +0x1090 + 4 a, at least 0) x dt, the display shows min(m x 255, 255). ✔
- **The rule block's tread wear** (0x35E108 copies an event's `RACE` row): its +0x2C .. +0x30 from the row's +0x6B .. +0x6F,
  +0x33 / +0x34 from +0x70 / +0x71, +0x31 / +0x32 from +0x72 / +0x73 (+0x31 4 when the row has a curve and +0x72 is 0). The
  Family Cup's and Tuning Car Grand Prix's rows make the original's rules (reference `26-endurance`): new tires at -10,000
  with 95 % grip (scrubbing in to 100 % at 0), full grip to 40,000, 95 % at 50,000, 60 % at 80,000 (held), new tires wanted
  under 88 %, the cap 1 and the slip extra 1 from any braking slip. The second wear's rules (the program's 0x620B60) are off
  in every race: its switch byte is 0 in the image. ✔
- **The sliding measures** (0x367148, phase 19; ours `rules_effects.*`): per wheel a count (+0xB3) set to 6 while loaded,
  counting down off the ground; at 0 the measures are 0, 1, 0. Otherwise by the kind of surface (jump table 0x69F7B0 on the
  surface less 2: 2 .. 5, 13, 14 kind 1; 6, 20 .. 22 kind 2; else 0) three measures from the tables the race's start builds
  (0x366D68 into 0x843A20 / 0x843C30 / 0x843E40 + 0x84 kind from the bytes 0x6224B0 + 0xA0 measure + 0x28 kind, after
  0x3FAA10's fix-ups): curve a (4 points) at the slip angle or (its row's +0x00 set) the hub's sideways speed in the wheel's
  frame, curve b (a negative side and a positive) at the slip ratio or (+0x1F) the slide speed, sqrt(b² + a²), plus (speed
  ramp - knee) x gain above the knee, plus the base when not 0: +0xB4 the sliding measure (the tread wear's input, the skid
  sound's level), +0xB8 the skid's pitch (the base always added; an odd wheel's x 0.75 at the step its sliding starts from
  the base), +0xBC smoke and dust (surface 12 as kind 1; 0 on surface 8; at most 1). On surfaces 1, 10, 11 and 21 the first
  two from the load ((load / 2,450 - 0.25) within 0 .. 1) and the rim's speed instead (0x844050: its scale, and the pitch
  linear between two speeds). The smoke and dust on surfaces of kind 1 or 2 dirties the car: condition block +0xC += sum x
  0.0005 (x 4 on a course of kind 0) x dt, at most 0.8, none for a fresh engine (+0x14 set). ✔
- **The feel and the views** (phase 19; ours `rules_feel.*`): the pad's vibration (0x36CEE8: into cond + 0xCA58 + 0x10 k,
  byte +1; 0 for a car a computer drives) the strongest of the driving force at the wheels ahead x (their mean squared
  slip + 0.1) x 0.00065, the braking force there / 19,600 squared (each at most 0.333), a wall's hit (F +0x610 x 8 + 0.25),
  and the dampers' forces / 19,600 (each at most 1) plus the kerbs under the wheels ahead (load / 2,940 x speed / 64, each
  at most 0.25), x 256, at most 127; the view's follower (0x345658, state cond + 0xCAB8 + 0x30 k, set at the car's set-up
  by 0x3455F0): the heading at 120 (gain) / 15 (damping) from the words 0x69F4C8 / 0x69F4D0 (Gran Turismo 3's 30 / 10),
  the pitch at 20 / 10; the body's sway (0x34A668 / 0x34A788, on by 0x6202B0 / 0x6202B5): the body's pitch (roll) less the
  ground's, within a limit, followed by a spring (0x34A4B8's constants from the bytes 0x6202B1 .. 0x6202B9); the engine
  sound's levels (0x350080, on the cues' table 0x843630 that 0x34F6A0 makes from the bytes 0x622690 ..: F +0x604 the
  engine's, +0x605 the intake's, each averaged with the last step's, +0x606 the spool and F +0x504 the blow-off scaled; a
  hybrid's halved and silent below idle); the effects' reset of a car not drawn as one in the race (0x366718 by the draw
  mode 0x366678). ✔
- **The wheel spin and the drawn car** (0x344040; ours `rules_wheels.*`): each wheel's spin by its layout axle's inverse
  tire radius (the engine's takes axle i / 2), the drawn points (car +0x888 .. +0x8BB, a special car's raised by the rear
  tire's radius less the special row's +0x10 and the center of gravity's height), the steering view (layout kind 1: from F
  +0x65C), and the driver's animation slots 2 / 3 (car +0x780 / +0x784) following the throttle and the brake at 0.45 / 0.3
  (0x62075C / 0x620760). ✔
- **Layout 7's motors** (four wheel drive system 5: the Acura DN-X; ours `rules_motors.*`): the converter's 0x355978 makes
  25 floats at 0x8438C0 of the bytes 0x62045A .. 0x620474 (the front motors' and rear motor's ratings 20 / 35 kW, their torque
  caps driving and regenerating, the store's capacity 15 x (2 front + rear), its marks, the charging caps, the idle charge,
  the yaw split's gains and dead bands, the flow counter's scale); the set-up fills the store (car +0x788, 0x355DC8), the
  signed byte car +0x78D counts the flow (0x355E40, -100 .. 100). The motors' demand (0x3561D0): -1 at or below the
  engine's share of the full power (its power over that plus twice the rear rating), above it twice the rest over the rest
  of the range x the store's share between its marks (0x356168), held at 1 outside gears 1 .. 7, less 1. In the torque
  vectoring (0x36D2F8) first the front motors (0x356418): F +0x51C 0; the front wheels' braking reaction x their rim speed
  charges the store (at most the cap a second each); the wanted share (the engine's output power plus the rear motor's at
  the demand, x the front share / its rest, over twice the front rating, within -1 .. 1, at most the store's share lifted
  by the reserve); driving: the share split left and right by the wanted lateral acceleration (0x356300: speed² tan(steer)
  / wheelbase, at most twice the front tire's grip at 1.5 x the steering) against the car's (F +0x554) beyond a dead band,
  each wheel's torque the rating x (radius x 2 pi x 9.8 x 716.2 / 44.13) / its rim speed x its share within the cap (a wheel
  under 1 km/h: the cap with the brake pedal up), the power drawn from the store (0x355F08) scaling them, F +0x51C the
  power over the front rating; regenerating the same with the regeneration's gains and cap and no store change; reverse
  gear negates. In the axle step the engine at the throttle over its share (0x3560F0) plus the rear motor (0x356BB8: the
  rear wheels' braking charge, then the rating's torque at the rpm within its cap x the demand, drawn from or charged into
  the store). With the clutch open (0x355F80) below a full store the engine's throttle rises (by the rpm under the idle
  speed and the share of the full torque, at most the idle power) and that torque charges the store. ✔ (every routine
  tapped, equal at every call on the DN-X's test drive.)
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

### 3.6 The input, the added phases, the hits and the start

Each is Gran Turismo 4's own in `src/gt4r/sim/rules_{input,near,phases,start}.*` and equal to the program's at every
call the harness makes (§9).

- **The input phase (0x349FD0).** The car's mailbox (cond + 0xF910 + 4 i, 4 bytes: a pit stop's answer; cond + 0xF8F8
  + 4 i, 2 bytes: a B-spec command, for control sources 0 / 1) is moved into the controller's request (cleared by
  0x3557E8); the frame is read through the controller's virtual +0x28 (RaceInput's 0x426D50 copies the request into frame
  +0x10 for a player's controller). In order: the pit answer (0x35BFE8, frame +0x10 not 0: the pit flags F +0x4B8), the
  B-spec command (0x3F19E0, control below 2 and frame +0x14 not 0: 4 the pace, 5 overtaking, 6 pit, 10 who drives; ❓
  values above 4 read past the five pace levels), the conductor's virtual 25, the nitrous (frame +7 with driver kind 0,
  P +0x10D4 > 0 and F +0x4E0 > 0: F +0x4E4 = 6, F +0x4E8 x= 1 + P +0x10D4), F +0x461 = 1, the steering prefilter (a row
  of the bytes at 0x62219A + 0x20 row by the controller kind (6: P +0x10D3 ? 2 : 1; else 0): order and gain; the steering
  + gain x d1, + gain² x (d1 - d2) for order 2, within ±4096; F +0x454 / +0x456 the raw frame's), then by control source
  and driver kind: 0 with kind 0 the pad (0x3484D0 the HUD bytes, 0x349F48 between the line's hooks 0x465078 /
  0x469E58), else the computer's (0x3F3400, 0x349F80). After: a category 4 car (P +0x32) shifting (F +0x522 = 3) has
  both pedals and its brakes 0; 0x350C20 (the road's height under the line's point F +0x61C, into F +0x624 / +0x628); a
  category 6 car's follower car +0x77C; after a computer's path F +0x45C = 0x348800 (the command) x 0.6667.
- **The pad's controls (0x349418).** The back axis off while the hold counter runs or above 20 km/h (and for category
  4, whose brake cuts the throttle); the pedal styles (F +0x442, a jump table at 0x69F590): 0 .. 2 the back axis
  replaces a smaller throttle, 5 the back axis off, 3, 4, 6 nothing; an analog throttle and brake slew by the ramps when
  the byte 0x622121 is set (not a wheel's); a wheel's pedals by the curves at 0x843548 / 0x843558 (not the one-speed
  box); the clutch by the box: kind 4 always requested (the throttle 0 under the start hold), kind 3 0x348368 and
  0x348428, else 0x3481D8 and the stepless creep for kinds 1 and 2. The two-wheeler's lean (layout kind 1: F +0x480 the
  steering's rate, F +0x65C / +0x660 the lean) runs instead of 0x348850; no car of the US disc has that layout.
- **The steering (0x348850).** The lock by speed: P +0x58 a pad's, +0x94 a wheel's (kind 4), +0xD0 on loose ground,
  kind 6 P +0x54; wheel types (F +0x443) 1 and 3 a least lock (0x62024C / 0x620250), 2 (0x620254 / 0x620258); the
  slip-angle extension by the settings' bytes +0x26 and +0x22 (an atan2 of the lateral speed); kind 4 slews the bounds
  at 0x620248 x dt; kind 6 scales by 0x62025C / 0x620260 / 0x620264 and the curve at 0x843538; the pad kinds the slip
  aid (F +0x4E5 bit 4; bit 3 for a wheel) and the active steering (0x84357C 1, 0x84358C, 0x843580 .. 0x843590: toward the
  driving line's heading above 30 km/h: 0x4649F0 on the line object cond + 0xDA50 with the car's own driver's line and next
  point, D +0x1C / +0x1D, at F +0x4A4, called after the car's decision 0x465078 of the same step; only in the driver's mode
  0, D +0x1E, a straight, and by the settings only with the stick centered, at most 0.2 degrees),
  then the follower 0x3485D0 (gains 0x843598 .., rates by class 0x8435B8 .., faster rates above a speed 0x8435F0 /
  0x8435F8).
- **The entry's aid switches** (0x363CB8 .. 0x363E0C, before the converter): entry +0x3450 ASM, +0x3454 TCS (both taken
  as off on a loose course, 0x34C998), +0x3458 a steering wheel's slip aid (0 for the player in every state seen). ASM off
  clears the spec's channels +0x1B8 (0x3F1700; but bits 6 and 7 with the byte 0x6224A8), TCS off the spec's +0x1C2
  (0x3F1748); a car whose spec has none counts as off (0x3F1728, 0x3F1750). F +0x4E5 is 0 but for control sources 0, 1 and
  3: bit 0 TCS, bit 2 ASM, bit 1 (the cornering throttle hold) by the byte 0x620E09 (1 with TCS, 2 with ASM, 3 always), bit
  3 the third switch, bit 4 (the pad's slip aid) by 0x620E0B (1 with ASM, 2 always). This program has 2 and 2. ✔ The
  original's states: 0x17 with TCS & ASM, 0x10 with the aids off, 0x1F a time trial's ghost (its three switches 1), 0 the
  computer's cars (`sim::aid_switches`).
- **The controls' tables** (0x347A00, the conductor's virtual 26) from the controller settings at 0x622110 (static:
  equal in the original's memory): the digital ramps 0x620270 + 4 k = 100 / (60 b), at most 1; the follower's gains and
  rates 0x843598 .. 0x843630; the active steering 0x843568 .. 0x843590 (0x84357C 0 when the course row's byte +0x12 is
  2); the least locks and kind 6's ratios 0x620248 .. 0x620264; the pedal curves 0x843538 / 48 / 58 (0x35A818); the
  pace levels 0x6220A8 .. (0x3F1790). Ours equal the program's after its set-up, and the original's memory but for 12
  words a unit lower (PCSX2 rounds to nearest where the PlayStation 2 truncates).
- **Phase 14 (0x3F4638).** Every car's driver part made anew (0x4694A0: the contact, the zone cells, the threat let go,
  the side gaps 1,000); for each pair i < j that is near (0x3F40F8: neither left out by 0x343BF0 (F +0x441, or its driver
  on line 4), both on or both off the pit lane, within 100 m, and within the other's body height + 5 m up or down): the
  zone cells (0x4694F8), the codes (0x3F4520), unless the rule block's +2 is 6 the closing (0x3F42D0, along the course
  with the lap's wrap, the rate the difference of the two F +0x650) as a contact (0x469918, its threat 0x469888 first)
  or a letting go (0x469868), and slip[i][j] = 0x3F44A0 (1 + x / (1.5 v) straight behind a leader above 100 km/h within
  1.5 v metres and its width). Then F +0x59C = min(1, the row's sum) and car +0x7E the follow chain. The tables of pairs
  at cond + 0xCC14 (slip) and + 0xCCA4 (codes); the pass-by states 0x4146E8 read them.
- **Phase 15 (0x3F28B8)** by P +0x1077 (jump table 0x6A3FF0): 1 out above P +0x107C, in below P +0x1078; 2 the share
  0x3F2798 between them; 3 none; 4 kind 5's state and a second by F +0x554 / 9.8 (in past ±0.5, out under ±0.2); 5 out
  above the upper speed braking at 0.5 or more, in below the lower or under 0.1; 6 fully out braking from 10 km/h, 2/13
  out above the upper speed, in below the lower or 2 km/h. F +0x594 (and F +0x598) move toward their targets at P +0x1080
  a second. Its damping (0x3F2650 from 0x369DE8, P +0x1076 a row of 0x622268 built by 0x3F2390 from 0x622240): the SL55
  row 1, the Nike One 4, the MTR GMV 2 (3 on a loose course).
- **Phases 2, 3, 6.** 2: a layout 7 car's byte car +0x78D cleared. 3 and 6 run only for category 1 (P +0x32, the spec's
  +0x01; no car of the US disc): a mood (car +0x788 .. +0x78F: the sitting count, the mood and the last, its steps, the
  lamp's fade, a nod, a lean; tables 0x620880 / 0x620890 / 0x6208A0) asked for by the race's end, the wrong way, the pit
  lane, a rival (0x4149D8 on the pass-by states), the slip filter, soft ground; 6 counts car +0x788 to 34 while the
  handbrake is full under 1 km/h.
- **Phase 4 (0x357700).** The pit stop's counter (0x35C0B8, while F +0x4BA runs: F +0x4BA is the pit stop's timer, set
  by 0x35B060 / 0x354D60; flags F +0x4B8; the tires chosen at 645, fresh tires at 60 (0x362E58), the leave request), the
  message timer F +0x5FB (0x35ACB0), the step length (0x3658A0), the masses (0x354AC0), the aerodynamics, the torques,
  F +0x5BC, the peak forces and slip angles. It does not hold the start: the step does (0x34C210, below).
- **The hits (phases 12 and 13).** 0x350308 clears F +0x608 unless F +0x60C is 2. The collision step hands each hit to
  0x350530 (car, kind, part, strength): a wall at a body's point (0x45E288) kind 0, the point, |j|; a wall's corner on a
  body's side (0x45E768) kind 0, the side + 8, |j|; two cars (0x45F340) kind 1, |j| / 15,000 on both, the car whose side
  was met its side + 8, the other its corner; a pylon (0x3E3C48) kind 2, part 9. In order: category 1's moods; a pad
  driver's incident count F +0x62F (at most 100, once per 60 steps: F +0x631, F +0x630 bit 1); the conductor's judge
  (its virtual +0x190: a race's 0x35FCC8, a license test's 0x35FE80 through 0x3F6938, by the rule block's +0x46 pylons,
  +0x48 a car hit's strength x 100, +0x49 a wall hit's |j| / P +0x48, +0x4A the penalty's seconds of speed cap F +0x60E
  or (0xFF) the mark F +0x62E; the part's class 0x35FBC8: 2,1,1,2,3,4,4,3,1,1,1,3,4,4,4,3; 0 always fails, 0xFF never);
  the pad's record 0x350328 (kind 0 strength / 50,000, kind 1 x 2, kind 2 / 50 at least 0.2 when +0x46 is set; at most 1,
  0 under 0.2, the stronger kept in F +0x608 / +0x60C / +0x60D: the vibration reads it). 0x369888's end: F +0x62C the
  wheels on a soft material, F +0x62D on the pit lane, F +0x630 bit 0 every wheel on soft ground (an incident).
- **The car's set-up (0x363B10 from the converter on).** The pedal style (0x362E10: kept for control sources 0, 1, 4;
  0 for 2); for start kinds 0 and 8 the nose at the place; the pose (0x3630C0: the body's query, the height + P +0x3C; the
  layout's wheels at their hubs: ground, surface, load and peak force P +0x1040 + 4 a over the axle's wheels + P +0x344,
  travel P +0x318, the steer angle 0x3F7CA8(0), camber, half track; the ground's frame 0x35D350; the pitch the ground's +
  atan2(P +0x40 - P +0x44, P +0x18); F +0x4A4 the conductor's virtual +0x108), 0x3455F0 (the view's lags and the
  conductor's record at +0xCAB8 + 0x30 i cleared), the standing start (0x363560: first gear, F +0x633 = 30, F +0x604 /
  +0x605 = 0x7F, the lock-out 12 for control source 1, a driver kind or pedal styles 0 / 2), fresh tires, the fuel
  (0x354AF8), the step length, the best lap; kinds 6 / 7 the pit start (F +0x4BA = 120); kinds 2 .. 4 and 8 the rolling
  hold (0x3452F0: F +0x467 = 1, the driver's line 7 → 6; F +0x526 = 12, F +0x590 = 180 or 300) and the moving start.
  The start's countdown (0x34C210: the race information's +0xBC above 0, counting down a step to START) holds every car
  (F +0x4BC = 1); a car's own counter at cond + 0xCBFC + 4 i (60 x the rule block's s16 +0x60 + 2 i) holds it after.
  The release (the conductor's virtual 20, 0x347928): F +0x467 = 1 → 0x345228 (onto line 7 for a pad car, 6 for driver
  kinds 2 .. 4; F +0x467 = 0, F +0x633 = 30) and 0x3F37B8 (F +0x590 = 0). The attitude's start hold (0x36AE20, from
  0x36B0C8 while F +0x4BA runs: the axles raised to the ground + 0x36A298's lift, the heading turned toward the line's at
  up to 0.526 rad/s; that heading is the pit stop line's point F +0x4B7, 0x46BE70: the car's box). F +0x633 is the steering
  force's (below). ❓ The readers of F +0x65B, F +0x590.
- **The steering wheel's force (0x353EE8, phase 19 after the pad's vibration).** The pad block cond + 0xCA58 + 0x10 x F
  +0x440 (0x353EB8; sent by 0x354A78 through the controller's virtual +0x30): +0x08 .. +0x0B, +0x0D .. +0x0F cleared each
  call. Two shorts from the controller (RaceInput::vf4 0x426CE0, 1 / 1 unless its port sets them): the strength's index and
  the steering change's switch. The mode 0, or with F +0x465 = 3 1 + (P +0x10D3 not 0); loose 0x34C998. A computer's car:
  +0x02 = 0x80 - the frame's steering F +0x454 (x 2 for kind 3) / 32 in 0 .. 255. A player's: the front wheels' curve A
  force (wheel +0x68, with T +0x50 on x clamp(T54 + (1 - |slip| x T5C) x T58 + the wheel torques over half P +0x1040 x T60,
  -1, 2)) over P +0x1040, within 1, through the pedal curve T +0x0C (paved) / +0x1C, x the gain T +4 / +8 and for kind 3 the
  strength T +0x80 + 4 i (T +0x7C says which limits which); the offset term (0x845BF0, the byte 0x622173: 2, or 1 under
  2 km/h): the curve T +0x40 of the wheel's steer offset over its limit x T38, less its rate x T3C; the steering's rate
  (F454 - F456) x T2C; the steering F +0x468 within T34 x T30; the aligning moment by the ground's slope x the loads x T +0;
  under 2 km/h without the offset term the steering alone; kind 3 the change's factor (the table T +0xA4); x (30 - F +0x633)
  / 30 while F +0x633 counts down (30 from the standing start and the release; a pad car's only). +0x02 = -0x80 - int(f x
  127). **Its tables** (0x353938, the tail of 0x347A00): three modes of 0xB0 bytes at 0x8436B0 from the settings' rows
  0x62218C + 0x20 m and the bytes 0x622184 .. 0x62218A. The byte 0 turns the wheel clockwise, 255 counterclockwise (a
  computer's car is given its steering's opposite, the force that centers; ✔ a car steered left is pushed clockwise: input.md).
  ❓ The readers of T +0x64 .. +0x78.
- **The engine's condition in the race** (phase 4, 0x34E9A0): the full torque x 0x350868 of the record's condition block
  F +0x5D8 (the entry's +0x3468: the odometer, the engine's and the oil's distances, the dirt, the body's, +0x14 no wear),
  which is 1 with +0x14 set. The default block (0x3433E8) is all 0 with +0x14 = 1: an arcade race's entries; a garage car
  brings its record's (+0x498 of it); a Gran Turismo mode race's opponents all 0.
- **The wheels' emits (0x3668F0 / 0x3667A8, phase 19 after 0x344040).** Each wheel of the layout (a category 6 car: each of
  its two groups once, at the middle of the pair, by the wheel whose ground record +0x14 is larger) hands a 0x3C-byte
  record to the course's smoke (0x395C70) and, on a triangle whose flags have bit 5, spray (0x395C98) pools: +0x00 the
  wheel, +0x04 its surface, +0x08 the point, +0x14 heading + steer, +0x18 .. +0x28 the ground (+0x28 less its normal y x
  0.05), +0x2C wheel +0xBC, +0x30 the slip ratio, +0x34 the hub's and +0x38 the rim's speed. Nothing of the car is written.
  ❓ A triangle with bit 5: none met on the courses tried.
- **The spring rate (0x35D248)** is the axle's, but for a car of category 1 (none on the US disc) sitting down (car +0x788
  not 0): the wheels give up their springs in turn, seven steps apart, in an order by the car's index.
- **Between phases 12 and 13**, 0x3F12B0 and 0x3F1368 run only while the race object's virtual 83 answers yes (0x3C0E78:
  `RaceNetBattle` alone): a LAN battle's cars sent (0x3EFF98, 0x3F0230) and the other machines' (control source 4)
  received (0x3F0498). 0x3F1148 before phase 1 is theirs too. Not ported.
- **The pit stop's tire change (0x35C0B8).** The race's preparation (0x35C4D8, virtual 27) lists for each car the kinds
  fitted (0x4462E0 the FRONTTIRE row's +0x3A, 0x446320 the REARTIRE row's +0x38; K +0xFB20 + 2 i), the kinds a stop can fit
  (the bytes 0x620860 / 0x620870 + the kind: 0 .. 2, 3 .. 5, 6 .. 10, 11, 12) and their parts (0x44A048: the first part of
  table 0x19 / 0x1A with that kind byte in the car's `CARS/<part code>` file: u64 count, then {u64 part, u8 kind} x 16
  bytes). At the stop's 645th step from its end, with an axle served (F +0x4B8 bits 0x40 / 0x80) and a kind chosen below
  0xFE (F +0x64E / +0x64F): a copy of the entry's equipment with the chosen parts (0x35C6D8, 0x4468F8), its spec (0x3F3B68)
  and the converter's tire records for the served axles (0x361570); at the 644th their base grips (0x361798) and the
  choices cleared. The converter does not run again and the entry's equipment is not changed: a later stop starts from it
  (❓ read, not run). ✔ 16 calls on 5 cars and 4 courses, the whole block equal (`GT4R_AI_PITTIRES`).

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
| 0x80 | `RACINGMODIFY` | | 0xF0 / 0x100 / 0x108 | `WHEEL` / `WING` (nothing read of either by the spec; the places by the part setter 0x4468F8) / `SUPERCHARGER` (two engine factors) |

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
(`GEAR` +0x25), the ratios from the slider (0x443240 → 0x354FF8, GT3's 0x1E28A8 with other constants; ours
`sim::computed_gears`). 0x443240 builds the spec of the equipment (0x4413C8) and calls 0x354FF8 with the slider
(equipment +0x12A) x 10 as s, in km/h: the gears n (spec +0x155), the row of the steps table 0x620448 (6 bytes a row,
the rows of 5, 6 and 7 gears; fewer than 5 take 5's, more than 7 take 7's), the driven axle's tire radius r (0x34BC98's
axle, 0x34BA30's radius), t = (s - 150) / 250; the top gear's ratio x = 100 x the rev byte (spec +0xBD) x 0.1047198
(0x3DD6774E) x r / (s x 0.2777778 (0x3E8E38E3)) / (the final drive's s16 +0x150 x 0.001 (0x3A83126E)); each row's
pairs eased q(a, b) = 200 / b + t (200 / a - 200 / b) for (b0, b1), (b2, b3), (b4, b5); from gear n down to 1: gear 1
first takes x q3; the gear's word is x x 1000 converted (toward zero), then x x q1, q1 x q2. Reverse (3 first +
second) / 4, toward zero. ✔ word for word with the program's on every made gearbox at every slider (test
`disc_gear_ratios_equal_the_programs`). The generator's caller copies twelve
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
  share in degrees); capacity 15 when 0), 5 layout 7 (0x355978: the motors' constants, below), 6 layout 9 (the
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

**Ours** (`convert.*`) is 0x361908 whole, in the program's order; `gt4r_diff <disc> params` compares the block word by word
(and once more with a computer's car's inputs, a power cap, a worn engine, ballast and percentages): equal on all 722 cars.
What it showed past the list above:

- The arguments (0x363B10): a2 the control source (2: the axle grips times the rule block's bytes +0x04 / +0x08 + the drive
  class, percent, 0 read as 100), t0 the rule block (+0x03 x 10 PS the power cap), t1 the entry's condition block (null: a
  fresh engine). The converter changes the spec it reads: a drive above 4 reads as 0, the last torque word when 0, the tire
  blocks' curves' first points (0x361008, 0x361220) and the load curve's first load.
- The block's tables are a count (s16), two zero bytes and two pointers into the block (0x359A38).
- **Special cars** (0x35A058: the layout row 1 or the category 6): the wheelbase, the origin's offset, the steering's camber
  angle and the tires' widths and radii from the program's table 0x6205C8 (0x78 bytes a row, by the spec's +0x23).
- **The differentials** (0x34AAD8): the type letter's place among the eight at 0x620300 (a letter not among them stays as
  it is); an axle the layout does not drive (0x34AC48: layouts 0 and 7 the rear, 1 the front, 2 .. 6 and 8 .. 10 both) has
  none, a driven one at least type 2, a group of one wheel type 1; the settings by type: 3, 4 and 6 the initial torque x 9.8
  and the shares in percent, 5 the initial squared and the acceleration's x 0.1 x 9.8, 7 all three in percent, 0 .. 2 none.
- The half tracks are narrowed where the tire stands out past the chassis' width across the tires (spec +0x30, with the
  tire's width percentage +0x1E4 neither 0 nor 100). The yaw inertia's inverse (+0x118) is of the yaw without the ballast's.
- **The tire records** are built only for the classes a surface uses (0x361570); a record's base grip is 9.8 x curve D at
  no slip (0x359568) x its load curve at half its axle's static load plus the unsprung weight over the tire's width (0x361798).
- **The suspension** (0x360B60): the spring rate the byte x 980 x the motion ratio; the damper's two points x 100 / the
  motion ratio fitted to a square law (0x360980), or kept as they are with the static byte +0x101.
- **The top speed** (0x360780, 0x360520): first the rev limit in the top gear (at most 333.3 m/s); then from rest in steps
  of that / 500 km/h, the highest speed where the engine's torque (its curve x the condition's factor 0x350868, under the
  power cap 0x34EA68) in the gear it would be in beats the rolling resistance (the race's curve 0x620938 x the paved
  surface's factor 0x620984 x the weight and the downforces) and the drag; 50 m/s for a one-speed box. A stepless box of
  kind 1's hold speed (D +0x30) is the lower of the peak power's rpm (0x34F3F8) and the up-shift speed less 500.
- **P +0x10DC** (0x3F3278): by the spec's kind +0x120 and a turbo, from the table 0x622320's two powers (x 10 PS): 0 below
  the first, 0.75 from the second, 0.25 .. 0.75 between. **P +0xFE0** (0x3F86E8): the fourth root of the base grip
  squared x the (peak power + the motors' extra)² x the square root of the peak torque over the mass^1.5, x the downforce's
  half less the drag plus 1, x the axle grips' mean, / 2.5, within 0.01 .. 1.
- **The engine condition's limits** (0x350678, the program's 0x620320 ..): L0 = -(byte +0xD0 x 100,000), L1 = +0xD1 x 0.001,
  L2 = +0xD2 x 100,000, L3 = L2 + +0xD3 x 100,000, L4 = +0xD4 x 0.001, L5 = -(+0xD5 x 100,000) / 2, L6 = +0xD6 x 0.001,
  L7 = +0xD7 x 100,000, L8 = +0xD8 x 100,000; the factor (0x350868) of the condition block's two words.
- The globals it writes: the dampers' table (0x360A78), four wheel drive system 5's fade (0x34BC98), layout 7's motors'
  constants (0x355978, 0x8438C0 ..).
- `gt4r_diff` runs the static tables' builder 0x35F808 with a conductor (a cleared one: a paved course), as a race's
  configuration does: without one the per-surface factors stay 0 and the program's top speeds come out higher.

**The hybrid one-speed box** (0x368628, from the axle step for gearbox kind 3; ours `rules_hybrid.*`, equal at every call on
both Priuses in a long drive, `gt4r_diff step` with `GT4R_STEP_LONG`): its state at car + 0x788 (the engine speed's target, the
store 0 .. 1, the display's flags, a mode by the store's eighths: 0 full, 1 from below 6.2, 2 below 1.5, back from 2.5 / 6.8).
The motor's torque on the driveline (0x367FD8: by the driveline's speed, two curves by the up-shift class; the demand from the
throttle 0x3685A0, or the brake's regeneration when the clutch is requested); the engine runs at a target speed (0x368040's
by the class, the class 3 car to the speed that gives the power the speed and throttle ask for, 0x3681E0 with the tables
0x620C80 / 0x620C90 and the power-to-speed table 0x620CA0 that 0x368260 makes once from the race's first hybrid's engine
curve), followed at 1,047 / 524 rad/s² (5,236 to start or stop it) and blended 1 : 2.5 with the driveline's speed; its torque
(0x3554A0) splits between the wheels and the store by 104.7 / 209.4 rad/s of margin; its fuel by 0x368128 (0x620BA8 /
0x620BF8 by rpm, no tank kept); the store moves by the power in and out x 1000 / (500 or 274) / 23,400 a second. The box
returns the engine's share plus the motor's (none while the clutch is open).

## 5. The program's tables

Gran Turismo 4's own values, as SCUS-97328 v1.01 holds them, carried in our code (`sim/program_tables*`, the
"Program tables" row below) and served on every Gran Turismo 4 disc by their address in that build; the disc's own
program is checked against them (v1.01 recognized by a hash of the three routines above, another build's tables found
by the routines that address them).

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

### The releases

Three Gran Turismo 4 programs are told apart, each by the raw code of the three routines above as it has them
(`sim::release_of`): **SCUS-97328 v1.01** (NTSC-U/C), **SCES-51719 v1.00** (PAL) and **SCPS-17001 v1.05** (NTSC-J). Each
is served its own tables: v1.01's with that release's own values written over them, every table still named by its
v1.01 address (`program_tables.cpp`, `own_values`, `kGaps`, `kRates`; each release's tables pinned by a hash). Another GT4
build is served v1.01's and its differences are logged. `gt4tool tables <disc>` shows the check, and
`gt4tool regions <disc> map` finds every v1.01 routine in a disc's program, for the code comparison below.

- **Europe** has the same data layout as v1.01, with its code moved. Its program steps **50 times a second**: dt 1/50 in
  the code (about 57 places where v1.01 has 1/60, about 60 where it has 60.0) and its step counts rounded to 50 a second.
  The two routines that address the view follower (0x69F4C8, Europe 0x3457B0: 0.02 for 1/60) and the steering curve
  (0x6A4028, Europe's spec builder 0x3F3E70) have their own sites in the locator. Its tables equal v1.01's except 13
  step counts in the car globals: the pit messages' times (0x6207E4 + 8k: 100 for 120) and special car 1's mood times
  (0x620880: 150 for 180), the same in seconds. **We race at 60 steps a second on every disc** (never a 50 Hz race), so
  Europe is served v1.01's counts. The check finds the program's at x 50 / 60. A count rounded differently in the code
  (for example 29 for v1.01's 34) is a conversion and is not carried.
  - **Europe's own rule**: its spec builder raises the yaw, pitch and roll inertia bytes (spec +0x24 / +0x25 / +0x26) to
    the static block's +0x153 / +0x154 / +0x155 where those are not 0. The values are 0, 0, 4, so the roll's is at least
    4. v1.01 holds the same bytes, but no v1.01 code reads them. ✔ Ported (`sim/car_spec.cpp`).
  - **Europe's converter** (0x361B10 there, with 0x360A60 and 0x360D28) scales the yaw, pitch and roll inertias (the
    spec's bytes x 100) by static +0x14E / +0x14F / +0x150, each axle's spring (+0x14 of its block) by +0x151 and the
    dampers' forces by +0x152, x 1/100, wherever the byte is neither 0 nor 100. Europe's bytes are all 100, so nothing
    changes. ✔ Ported (`sim/convert.cpp` `europe_share`).
  - **Europe's stopping box** (0x3F9560, a license test's end in the box): the car counts as stopped below 0.4167 m/s
    (1.5 km/h; v1.01 0.2778, 1 km/h), timed by the same share. ✔ Ported (`race/license_rules.cpp`).
  - **Europe's recordings** (its license tests' `uk*` files, the Replay Theater's `Rdemo<n>p`) are 50 records a second.
    They play by time at our 60 steps (step k takes record k x 50 / 60, a shift and a mail on a record's first step:
    `sim::ai::DemoRun::rate`), as a look at them and a check, never on their times. Of the 79 timed license files
    (`gt4tool race <eu disc> demos uk`), 16 end within 100 ms of their recorded time (10 within 20 ms), most of the B license's straight
    tests within 20 ms (B-1 +10, B-2 -11, B-3 +4, B-4 +18, B-6 -5, B-7 +4); the rest part from the recorded run, as a 50 Hz
    run on a 60 Hz step must.
  - The license tests' and missions' labels are the program's region's (0x430D20: "uk" + the code, then "jp";
    `db::race_labels`): `ukl0b0001` on Europe's disc, `jpl0b0001` on Japan's, as their RACE rows and demonstration files are.
  - Not rules: the camera's picture height (224 -> 256 lines) and its two aspect factors (1.905 -> 1.667, 1.429 ->
    1.25) in 0x3E0FF8 are the PAL picture's. Our views keep their own (the window's shape).
- **Japan** is an earlier build with its data moved (0x620AD8 there 0x618040). The address words in its tables are its
  own (11 of them, not counted as differences).
  - The engine condition's oil is old from **5,000 km** (static +0xD7 = 50; v1.01 60). ✔ Served.
  - Its static block lacks v1.01's +0x14E .. +0x155 (100 x 5, 0, 0, 4), 8 bytes: what follows is 8 bytes earlier there,
    and Japan's code reads it there (v1.01's 0x620E30 is Japan's 0x618390). Served as 0.
- 🔎 **The code**: of the routines our simulation and race code port, Europe changes 81 and Japan 106 (`build_map`
  classes). Europe's are read in full: the 50 Hz conversions (every changed constant or step count is v1.01's x 5/6 or
  x 6/5, rounded), field offsets, and the rules above. Japan's are not read yet. Each one that changes a rule is listed here when it
  is ported.

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
flag (+0x24) and, with the switch byte 0x6202BA (1 in every race state seen), the change of velocity the step made in the
car's frame (its two horizontal axes) when its size is past 0x6202E8 (2.78 m/s), scaled by the words 0x6202EC .. 0x6202F8
(+0x1C / +0x20; the flag 1 for that step). Its one reader is the in-car view (CameraSys::CameraOnBoard::vf10 through 0x34A8B0):
a jolt of the view with random draws (0x578598) after a hit: [camera.md, "A hit's jolt"](../formats/camera.md).

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
- Which races meet other cars: the conductor's virtual +0x68 of a single player's race (Arcade, Championship, GT mode,
  photo), the two battles, free practice and the training's table (training, missions, licenses) is 0x34DA70: 0x45BB60 on
  every body together. The base conductor's (free run, machine test) is 0x34DAA8: 0x45BF20 for each body alone, with car
  contact off (0x45C530 given 0): walls only. Those are races of one car, so ours runs 0x45BB60's step for them too.

## 8. What runs where in our simulation

`sim::Session` steps the cars with the shared engine's vehicle step (the "exact core", GT3's
rules in the PlayStation 2's arithmetic), fed with Gran Turismo 4's data:

| Part | Runs | Follows |
|---|---|---|
| Parts, equipment, default settings, the spec, its fix-ups | `equipment.*`, `car_spec.*` | Gran Turismo 4 (§4.1, §4.2): equal to the program's on all 722 cars, byte for byte (§9); computed gearboxes' ratios by the engine's generator (7 gears at most) |
| Spec → parameter block | `convert.*` (`sim::convert`, `sim::build_record`) | Gran Turismo 4 (§4.3): the converter 0x361908 and every routine it calls, in the program's arithmetic: the whole 0x1100-byte block equal to the program's in every word on all 722 cars, as sold and with a computer's car's inputs, a power cap, a worn engine, ballast and the entry's percentages (§9). The step's record is read out of the block: the engine mirror's members by their places, the mirror's members the block keeps in another shape made from Gran Turismo 4's (the 24-point curve thinned to 16 points, 7 forward gears at most, the tire records' 24 surfaces folded to 16 rows, the steering and camber curves, the inverse masses: the engine's routines that still run read these), and Gran Turismo 4's own part. The engine's converter (`exact_bridge.*`: `core_spec`, `gt4_params`, and `gt4_extras`) serves only the engine's rules (`GT4R_SIM_RULES=engine`) |
| Program tables | `program.*`, `program_tables.*`, `program_code.*`, `rules::tables` | Gran Turismo 4's own on every Gran Turismo 4 disc: the tables the simulation, the computer drivers and the race's rules read are values in our code (`program_tables_values.inc`: 14 areas, 6,180 bytes, SCUS-97328 v1.01's, named fields, a pinned hash; each release's own over them, "The releases" in §5), served by their v1.01 address (`sim::Program::b`); a read outside them gives 0 and is logged. The disc's program is checked against them once: v1.01 at their places, another build where the routines that address each table put it (masked-code hashes, `ProgramCode`: no loaded value hashed, an index added to an upper half followed; one to three routines a table); a table that differs or is not found is logged (`gt4tool tables`). ✔ v1.01 bit for bit; a copy with the code and data moved finds every table at its new place (test `disc_program_tables_found_by_their_routines`); Europe's SCES-51719 and Japan's SCPS-17001 equal their own (test `disc_program_tables_per_release`). The race's globals the start makes (`rules::tables`, the converter's, `speed_tables`, `wear_rules_block`) are made from them. `core_program` (the engine's `ProgramData`: GT3's values but for the static block and the soft materials) reaches no step on a Gran Turismo 4 disc: only the engine's rules read it (`GT4R_SIM_RULES=engine`) and Prologue's path (Prologue's own tables over some of it, program_prologue.h). A program that is neither Gran Turismo 4's nor a placed Prologue build races nothing ("this release is not supported yet") |
| Releases | `sim::Release`, `program_tables.cpp` (`own_values`, `kGaps`, `kRates`), `car_spec.cpp`, `convert.cpp` `europe_share`, `race/license_rules.cpp`, `sim/ai/demo_driver.*` (`rate`) | US v1.01's rules on its disc. Europe's own rules on its disc, at 60 steps a second (its inertia floors, its converter's scale bytes, its stopping box), its 50 Hz step counts served as v1.01's, the same in seconds; its own recordings played by time as a check. Japan's tables (oil limit, the shorter static block) on its disc; ❓ Japan's own routines (TCS, slipstream, ground, AI, the lap step and more, "The releases" in §5) still run v1.01's |
| The road query (0x3E5A30 with 0x3E5BC8 / 0x3E5FD8), surface index | `ground.*` | Gran Turismo 4 (§6), in the PlayStation 2's arithmetic: equal to the program's answer at every call (§9) |
| The collision step (phase 13: 0x3F4B20, 0x45BB60 and its routines, 0x3F4C18; the walls near a body, 0x395A38 / 0x3E6A58) | `collide.*` through `session.cpp` | Gran Turismo 4 (§7): every body field equal to the program's at every call (§9); the bodies built as 0x45B978 builds them |
| The query points (phase 16, 0x3697A8), the steep ground's normal (phase 17, 0x344458), the wheels' surfaces as 24-row indexes, wheel 0's triangle as the collision step's hint (phase 18), the ground's frame (phase 19, 0x35D350) | `rules_body.*`, `session.cpp` | Gran Turismo 4 (§6) |
| The aerodynamics (phase 4, 0x3F3090 with 0x3F2DB8; its globals as 0x3F2F78 and the converter's coefficients as 0x3621B0 make them) and F +0x5BC | `rules_chassis.*`, `exact_bridge.*`, `rules_step.*` | Gran Turismo 4 (§5) |
| The class grip of a car a computer drives (phase 10; a single race's 20 % for every drive class, 0x353570; `rules::Tables::class_grip`) | `rules_step.*` | Gran Turismo 4 (§2) |
| Phase order | `session.cpp` | phases 1 .. 22 as the program's step (each over every car before the next: the phases before the driver's decision, the decision and input, the rest to the hits' clearing, then the collision step on); the engine's remaining routines with the program's C library and constants (`rules::behaviour()`: 2002 Tokyo-Geneva's) |
| Gran Turismo 4's own part of the record | `convert.*` `load_params` | the converter's block: every field bit for bit on every car (the tire records' base grip and the driveline block's tires, differentials and inertias too) |
| The engine's torques (phase 4), brakes and clutch, wheel torques, vectoring, the axle step, wheel speeds, engine speed and gearbox (phase 10), brake heat (phase 11) | `rules_drive.*`, `rules_wear.*` through `rules_step.*` | Gran Turismo 4 (§3.3) on the engine and driveline blocks; the four wheel drive's fade, the stepless rates, the controller settings and the brake heat's constants as the program has them (checked, §9). The Priuses' hybrid one-speed box, the box of kind 4 (the MTR GMV) and layout 7's motors (the Acura DN-X) are Gran Turismo 4's; the fuel is counted in an event's race. A car our computer drivers drive shifts by the engine's gearbox: the program shifts a computer's car at its driver's speeds (0x36BEE0 / 0x36BF28 through 0x3F1E30, the speeds x a driver byte / 128 ❓), and with the program's own speeds an automatic box can hunt (the Volkswagen Beetle 1100's down-shift into third and fourth at 3,500 and 4,000 rpm, its up-shift at 4,000). With the rest of the driveline Gran Turismo 4's, the original's arcade field at Tsukuba laps in 96.4 .. 101.7 s (the original's two-lap race: 92 .. 101 s a lap; the engine's throughout: 99.5 .. 105.4 s) |
| A rolling start | `rules_start.*`, `session.cpp` | the car set-up's moving start (0x363868) after the session's hold; its places (rules::rolling_start_distance, the race's rule) on the road's middle, not the driving line |
| The wear's grip, peak forces, slip angles (phase 4's tire part), slip ratios (phase 8), rolling resistance, tire curves, the longitudinal and lateral forces, the speed along the body, the forces across the car and the yaw, the standstill hold (phase 10) | `rules_*.cpp` through `rules_step.*` | Gran Turismo 4 (§3.3); the race's tables (`rules::tables`: the rolling resistance by surface and speed, the slip bytes, the globals) as the program's race set-up makes them (checked, §9); the wear rules off, as in every race seen |
| The suspension's places, suspension, attitude, bump-stop nudge, body axes (phase 19's first four and its axes), the steering (phase 7), the driving aids (phase 9: a computer's car by its driver's block, `Session::set_driver_aids`, as control source 2) | `rules_body.*`, `rules_steer.*`, `rules_aids.*` through `session.cpp` | Gran Turismo 4 (§3.3); while the hold counter runs, the engine's start hold |
| The road's bumps (0x369888's: Gran Turismo 4's 24 surfaces, the soft materials' halving) and a computer driver's grip by tire class in the peak forces and the lateral acceleration over the grip (0x467470: the driver's parameter block +0xA8, 1.2 on a loose course's set; `Session::set_driver_aids(car, aids, grip)`) | `rules_body.*`, `rules_tires.*`, `rules_chassis.*` | Gran Turismo 4 |

| Phases 1, 2, 3, 6 (0x343ED0, 0x355E10, 0x35CA60, 0x35D1D0), the whole preparation (phase 4, 0x357700: the pit stop's counter 0x35C0B8, the message timer 0x35ACB0, the step length, the masses 0x354AC0, the aerodynamics, the torques, the peak forces and slip angles), the hit record and hit records (phases 12 and 13: 0x350308, 0x350530, 0x350328, the judges 0x35FCC8 / 0x35FE80) and 0x369888's tail (the wheels on soft ground and on the pit lane, leaving the course) | `rules_phases.*` through `session.cpp`; the collision step's hits in the program's order (`collide::Hit`) | Gran Turismo 4 (§2, §9): equal at every call; the pit stop's tire change `sim/pit_tires.*` with `convert_tire_records` / `convert_base_grips` (`Session::change_tires`, `retune_tires`), called by the stops' timing (`sim/ai/pit_tire_change.*`); no car of the US disc is of category 1 (P +0x32), so phases 3 and 6 and the hits' moods run for none |
| The driver's input (phase 5, 0x349FD0: the pad's controls 0x349418, the steering 0x348850 and its follower 0x3485D0, the clutches 0x3481D8 / 0x348368 / 0x348428, the pedal curves, the prefilter, a pit stop's answer and a B-spec command from the frame, the nitrous's trigger; the controls' tables 0x347A00 and the pace levels 0x3F1790) | `rules_input.*` through `session.cpp` | Gran Turismo 4 (§3.4): equal at every call for controller kinds 0, 2, 3, 4 and 6, digital and analog axes, pedal styles 0 .. 7, the back and third axes, wheel types, the one-speed, stepless and kind 4 boxes; a frame given (`race::PadRecord`: a recording's, or the player's, input.md) or one made from `race::CarInput`'s fields (an analog pad of kind 3: the sweeps and checks); the computer's paths are the session's (0x464F38's outputs, 0x3F3400); the active steering runs for a car whose caller gives its line's heading and mode each step (`set_line_heading`; the race world does, from `Field::line_heading_at` / `line_mode_at`); the course's kind and road byte from its COURSE row (`sim::course_row`; without a row dirt is guessed from the first start place) |
| Proximity and slipstream (phase 14, 0x3F4638 and its pair routines), the part that changes with speed (phase 15, 0x3F28B8) | `rules_near.*` through `session.cpp` | Gran Turismo 4: equal at every call (six computer cars at Tsukuba and Grand Canyon, the nine cars with the part); the distance along the course and its rate (F +0x4A4, F +0x650) are ours (`distance_along`), the drivers' line 4 not known (a car on it is not left out) |
| The start: the car's set-up (0x363B10 from the converter on: the pose 0x3630C0, the standing start 0x363560, the rolling hold 0x3452F0, the moving start 0x363868), the countdown's hold per car (0x34C210, cond + 0xCBFC + 4 i into F +0x4BC), the attitude's start hold (0x36AE20), the rolling start's release (0x345228 through the conductor's virtual 20) | `rules_start.*` through `session.cpp` (`setup`, `release_start`, `place_standing`, `set_pit_hold`, `set_countdown`) | Gran Turismo 4: equal at every call; the grid pose at Grand Canyon equal to the original's saved states; a rolling start's place on the road's middle, not the driving line (0x34C5E0 / 0x46ADE8); the session's countdown is its last 2 s (120 steps) before time 0 |

**What still runs Gran Turismo 3's code** in a race: nothing of the step. `session.cpp` calls the shared engine's step
routines only on its own rules (`GT4R_SIM_RULES=engine`) and Prologue's; its math
(`ex::lookup_bisect`, `ex::bump_wave`, `ex::atan2`) is shared. Not ported, because nothing here asks for them: a LAN battle's
packets (0x3F1148, 0x3F12B0, 0x3F1368). The frame a player's pad or wheel gives and the pad block's sending (0x354A78):
[input.md](input.md). Judged or ours:

- Phase 13: a body's driver on line 4 (0x343BF0) is not known to the session: such a car still meets the others.
- The hybrid state and a category 1 car's state share car +0x788 in the program; ours are two members.
- The constructor's hold steps run every car as a pad's: F +0x633 runs out for a computer's car too (the program leaves it
  at 30; only the steering force reads it).
- The flaps' random stream starts from 0 (the program's 0x874D50 has run before the race).
- The drawn car (`Session::publish`, race::CarState::body_placed) as the game draws it (RaceCarParamSponsor: virtual 7 the
  body's angles, virtual 8 its point, virtual 10 a wheel's place): the body at 0x344040's point (the center of gravity less
  P +0x50 along the forward axis) with the body's axes; each wheel of the layout in the body's frame at that point (0x344AA8,
  0x344B40, 0x344C08): across, the wheel's half track at its travel (wheel +0x08); up, its axle's suspension offset (the
  suspension block +0x0C, P +0x308 + 0x58 x the layout's byte +0x14 + w) less its suspension travel (wheel +0x94), the road's
  bump in it, its tire loaded or not; along, half the wheelbase (P +0x18), ahead for a wheel the layout's byte +0x3C + w puts
  ahead of the center of gravity, behind for one behind, none for one at it. The program blends each of these between the last
  step's (car +0x8A4 .., +0x8D0 + 4 w, +0x900 + 4 w) and this step's by the frame's place; ours blends the places. Ours draws
  the model's origin a constant (the mean ride height less the center of gravity's height) along the up axis below the point.
  ✔ the Impreza WRC at Grand Canyon: its loaded wheels on their bumped road (3 mm), the body exactly at the simulated pose,
  the wheels' height on the body 9 mm rms from step to step (the suspension's own travel; ours drew 17 mm before, two
  places for loaded and unloaded wheels, and raised the body by their misfit: 8 mm rms, 11 mm a step), at Tsukuba 1 mm
  (test `drawn_car_rides_its_suspension`). Prologue's disc and the engine's rules still draw by the earlier rule.

| Distance along the course (F +0x4A4 at each step's end: the conductor's virtual +0x108, 0x3585F0 → 0x358558 → 0x3955E8 → 0x3E6858 / 0x3E6610: the point (F +0, F +8, -F +4) in the checkpoints' quads (left, middle) and (middle, right) of the cluster under the hint F +0x4A8, the nearest in height; v_i + (v_j - v_i, + a lap) x u, less a lap past one; 0 outside every quad) and its rate F +0x650 | `ground.*` `RoadGround::course_distance`, `session.cpp` `publish` | Gran Turismo 4: equal at every step (§9); the laps the race view counts are ours (`distance_along`) |
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
- **params, the block**: our converter's block against the program's in every word, on all 722 cars as sold and with
  varied inputs (`GT4R_VARY=cond|cap|spec|grip` one at a time, `GT4R_BLOCK_EXAMPLES`); the test `sim_program_converter_block_equal`.
  The session's record from it (`build_record`) against the program's block's fields: every field of Gran Turismo 4's own part
  bit for bit (`sim_program_params_equal`). `GT4R_RECORD_CHECK` lists what changed against the engine converter's record (a
  unit or two in the last place on most fields).
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
- **The wear, the sliding measures, the feel and the drawn car** (`step`, `ai` with `GT4R_AI_STEPTAPS`): 0x352EA0, 0x367148,
  0x36CEE8, 0x345658, 0x34A668, 0x34A788, 0x350080, 0x366718, 0x344040 equal at every call (the conductor's blocks compared
  as memory) on the NSX, Civic, R34, Impreza, Prius, Midget, Benz Patent Motor Wagen, Insight on Tsukuba, Seoul and Grand
  Canyon, with `GT4R_STEP_TREAD` (the tread wear's curve in the rule block) and `GT4R_STEP_WEAR` (every wear rule made up),
  and 177,726 calls each in the six computer drivers' lap of Grand Canyon; the tables of 0x366D68, 0x34F6A0 and 0x34A4B8
  equal to the race's. Three laps of Apricot Hill Reverse in the R34 with Tuning Car Grand Prix's rules wear its treads
  16,190 / 13,637 / 15,859 / 13,238 and burn 6.23 liters (the original's, from its saved states at the finish: 16,231 / 13,886 / 16,184 /
  13,621 and 6.35 liters; test `sim_disc_long_race_wear`).
- **The collision step and the road query** (`collide_check.cpp`: `GT4R_STEP_COLLIDE` with `step`, `GT4R_AI_COLLIDE`
  with `ai`, the test `sim_program_collision_and_road_query_equal`): at 0x45BB60's entry every body record is read
  into ours (wall pointers as indexes), ours steps them on our walls, and at its return every body field (and the
  cars' rumble F +0x610) is compared; at 0x3E5A30's entry ours answers the same point on our road, and every answer
  field is compared. Six computer drivers' cars at Grand Canyon, three laps of the program's race: 61,456 collision
  steps, every body field equal at every one; 2,203,742 road queries, every answer equal. Car contact made on purpose
  (`GT4R_AI_COLLIDE_SHOVE=<calls>,<m/s>`: every so many steps a body's velocity in the program's own record gains that speed
  toward its nearest neighbour before either step reads it, and the program's race goes on from the contact): one lap of
  Grand Canyon shoved every 30 steps by 10 m/s, 30,089 steps (583 with cars met, 188 a wall), and of Laguna Seca, 15,904
  (882 cars, with every road query equal): every body field equal at every one (test `sim_program_car_contacts_equal`,
  40 s of Laguna Seca). sincosf (0x57D8A0) is the
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
  `GT4R_AI_OURS`, `GT4R_AI_INCIDENTS` / `gt4tool ai`): with the harness's grid as the session's the two races are the
  same race step for step (below) when they start alike; `gt4tool ai`'s own race (26 walls against the harness's 43)
  holds the cars 240 steps before the start where the harness's has no countdown, and the field's race is chaotic from
  there. Each car alone the laps agree (Ka 3'17.40 / 3'17.07, Z 5'26.8 / 5'23.5, N360 4'09.97 / 4'09.43).
- **The whole step running free** (`GT4R_AI_WHOLEFREE=1`: the program's state loaded once, then ours runs on;
  `=2` our drivers on our session, their outputs compared): six cars at Grand Canyon bit-equal in every position,
  velocity and placed field for 400 s with the program's drivers' outputs, and over a three-lap race with ours (54,000
  steps; the harness's grid as the session's, `GT4R_RACE_GRID`: the program reads the places of the course's layout
  0, the rule block's +0x21, and 0x3952C0 does not bound the index). This found the distance along the course (it
  departed by up to 4.5 m: the race cars' spins at Seoul, the Honda S500's lap at Tsukuba, 90.73 → 89.22 s against the
  program's 89.20), the hit's count F +0x631 (it counts down between phase 12 and the collision step, 0x34E040) and,
  in the computer drivers, the downforce term over the weight P +0x103C (0x4673D8; the race cars' spins at Tsukuba).
- **The original's saved states** (`gt4r_diff <disc> states <state 0> <state 1> [keys]`, `states.cpp`, the test
  `sim_states_*`; the reference's sets with the pad held between them): the program's memory of state 0 copied over
  the interpreter's, the pad read (0x426E80) answering the held keys, 0x34DB30 run once a frame for the boot counters'
  difference (0x0087FA88). Every pair is reproduced byte for byte by the program's step with a model of the emulator's
  float arithmetic (add, subtract, multiply rounded toward zero, denormals flushed; divide and square root to nearest;
  `GT3R_FLOAT_ORACLE`, kept outside the repository): the values in the states were made by the emulator, not the
  console (e.g. the dampers' knee 0x620CB8 is 0x3DA3D70A there, 0x3DA3D709 by the console's multiplier). Our session
  from state 0 stays within 0.1 mm and 0.1 mm/s of the program's run on every set, given the driving line's heading
  for the active steering (the sets with TCS and ASM on). The race information's countdown (+0xBC) counts down before
  0x34DB30 and the step holds the cars while it is still above 0. The keyboard pad's frame (controller +0x150): Cross
  `02 00 00 06` with +8 = 0x1000, Square `04 ..` with +0xA = 0x1000, Right / Left `01 03 00 06` with +4 = 0xF000 /
  0x1000; the controller is RaceInput (0x426D50 the frame read). At Grand Canyon's standing start the launch follows
  the original's from its state; from our own set-up the first difference is where the rev limiter's cut falls at
  START (F +0x521), which depends on the countdown's length and when the throttle went down.
- Not compared yet: the step's routines not in §3.3, §3.6 and §6, §7.

- **Round six's routines**, each equal at every call: the steering force (1,200 of 1,200 a run on the NSX, the two motor
  carriages with their variations, `GT4R_FORCE_VARY`; 21,600 a course in fields of computer cars with a pad car; the whole
  float, not only the output byte), the spring rate, the speed-switched damping inside the suspension, the emits (six
  computer cars, 90 s: 32,400 suspension, 61,447 damping, 129,600 spring rate and emit calls; categories 1 and 6 forced),
  the pit's tire change, and the active steering's heading and mode (`gt4r_diff ai <course> <cars>` with `GT4R_AI_CHECK=1
  GT4R_AI_PAD=0 GT4R_AI_ACTIVE=1`: 998, 503 and 338 calls at Tsukuba, Grand Valley and Seoul).
- **A car left alone**: from the original's time trial state, the brake held to a stop and let go: the program's car rolls
  with the road's slope, its clutch open (-0.182 m/s after 9 s), ours the same to 0.4 mm/s. The automatic box does not
  creep.

## 9b. Floats

The session and the interpreter run the shared engine's EE float model (`engine/gt3r/src/base/ee_float.*`: the multiplier's
Booth array with its lost carries, the SRT divider and square root, the adder with one guard bit, all black-boxed and proven
by Gran Turismo 3's 58 hardware-recorded demonstrations). This section is what Gran Turismo 4's own recordings say.
**The saved-state checks (`gt4r_diff states`, §9) test rules, not floats**: their numbers were computed by PCSX2, whose
arithmetic differs from the console's (a Gran Turismo 4 demonstration run on it does not end as recorded, below). Float
fidelity is judged only by recordings made on the console.

### The evidence

- **The cipher of the replay files** (formats/replay.md): one multiply and one divide a byte, the console's, then a CRC of the
  result checked against the file's key. ✔ The program's own routine (0x579E00, `gt4r_diff <disc> floats cipher`) passes 466
  of 466 files under `replay/license` and 32 of 32 of the Replay Theater's demo replays (`replay/Rdemo<n><p|k>/`: a memory
  card's replay file unscrambled with the factors FileGT4Replay's loader gives, key 0: 0x3F322311 for the head's first word and
  0x3E6B3EF8 for the second) on our model. With a scratch model of PCSX2's EE FPU in its place it passes none of the 498, and
  none with that model's multiplier alone or its divider alone. So this check is the one PCSX2's arithmetic cannot pass; that it
  is what the emulator's "Data is corrupted" reports is an inference ❓ (the message's path is not read).
- **The license demonstrations**: 85 files named for the US disc's tests, recorded on the console with a steering wheel, through
  the program's own race step (0x34DB30) in the interpreter (`gt4r_diff <disc> floats [label,... | all]`) under the license
  conductor (`DynamicsConductorLicense` 0x3F5F68, a training test's with the training's table 0x685F50 over it): the
  event's row into CFG (0x34D418), the file's race information, equipment and entry words, the course's driver file
  (the pace car's driver, the driving line's heading), the run judged by the program's own judge (a probe on 0x3F8AB0,
  its time in ms, and 0x3F8980, the failing kind, for car 0) and compared with the recorded time (file +0xC0).

### The score

How the harness was settled, for weighing the score: its set-up was completed by looking at failing files (the laps in the
race information from S-1, the judge's car from B-5, the pace car's lead from B-5), and the start's hold, 480 steps when the
car stands after its set-up and 0 when it moves, was chosen on B-1 and A-1. Both are now read: the hold is 0x3C0208's (none
when the conductor's virtual 10 answers a moving start, else the row's type +0x40: 1 8 s, 2 10 s), and the course's start code (0x447F08: the
UnistrDB string its row names, K +0xCBE6 ..) is given to the set-up, which the harness left out before: its fourth digit (K +0xCBE9, 0x463AE0)
turns off the computer driver's long look ahead at a standing start, the cause of the three training tests' misses. No choice is made per file.

| Files | |
|---|---|
| 85 | named for the US tests; B-8's car is not in this disc's database (no car), 84 run |
| 14 | record no time (0): they end timed by the program, in no recorded time to compare |
| 70 | with a recorded time: **66 end on it to the millisecond** (runs of 10 .. 419 s, all sixteen Super License laps but S-3, which records no time, among them) |
| 3 | coffee breaks with cones (C-2, C-3, C-5): the harness sets up no cones, so they are not judged (they run past their end) |
| 1 | misses: B-6 (-34 ms) |

**The Replay Theater's demo replays** (`gt4r_diff <disc> floats replays <name> run`; formats/replay.md): the disc's own 13, twelve arcade
races of two and six cars (A-spec and B-spec, standing and rolling starts, pit stops; 81 .. 221 s) and a free run's lap of the Nürburgring
(472 s), all end on their recorded times and best laps to the millisecond through the program's step: **13 of 13**. Our own race set up from
each file alone does too, so the computer drivers, the collisions, the tire wear and the pit stops they run are the console's. The harness
had them wrong before by the start code, the start's hold, the race's replay mode and the free run's moment.

**The controls** (the interpreter's FPU arithmetic replaced from outside, `GT3R_FLOAT_ORACLE`; a scratch model of PCSX2 2.8.2's
EE FPU, outside the repository): (measured when 63 of the 70 were exact) with its multiplier, divider, square root and adder all replaced 9 of the 70 end on their times; with
its multiplier alone 13, its divider alone 10, its square root alone 11, its adder alone 63 (it does not differ from ours
on these runs). So the demonstrations do discriminate the multiplier, the divider and the square root, and the adder's
difference between the two models is not exercised by them.

B-1 is one that does not turn on the model. With all four operations replaced it ends at the same step on 10,083 ms. Car 0's
place differs in the height's low bits alone: x and y are equal in every bit at the stop. Its speed is never more than
0.00002 m/s apart. A short straight run at full throttle and full brake is not where the two models part. The emulator's
own B-1 demonstration does not play as recorded (the reference's 08-more and 11-license-tt). Its file fails the key check
under PCSX2's arithmetic, as all 498 do (above), so what it shows is not a measure of the race step ❓ (what the program
does with a file that fails the check is not read).

**The misses**:
- B-6 ends at 22,715 ms under our model and under PCSX2's alike (the recording 22,749), so it does not turn on the float model.
  Its stop is at the same step with holds of 476, 478, 479, 480 and 481; a hold of 478 gives 22,748. ❓ Not explained, and 478 is
  not used: it would be a value fitted to this one file.
- The training tests B-10, IA-15 and IB-15 missed before the start code was given to the set-up (above); they are exact now.

**The other regions' files** do not play on this program. 99 Japanese and Korean recordings that differ from every US-named one were run
with this disc's row of the same test (12 cars and 6 events are not in its database; the 34 missions run are copies of one two-car
run on different events and are left out): of 43 with a recorded time **5 are exact**, 8 are 5 to 89 ms off, 30 fail or do not end.
Two European files tried (B-1, A-1) do not end or fail. So 66 of 70 is the
score of the files this disc's program itself asks for, not of every console recording on it.
❓ Why: the recordings may be of other builds (the European at 50 steps a second). Fitting that, the 85 US-named files are either the
Japanese recording unchanged, under the Japanese row (32 of 32), or recorded again, under their own (53 of 53), as if those that
still played were kept; with a recorded time 25 of 27 of the first kind and 41 of 43 of the second are exact. It is not proven: the other
builds' programs are not on this disc, and the same was once supposed of Gran Turismo 3's demonstrations, whose cause was the
float model. B-6 is of the first kind, its miss (-34 ms) of the size of the other regions' near misses.

### The operations (a census of the race step)

`gt4r_diff <disc> floats census <label>` counts every FPU and VU0 float instruction the program executes (a probe at each
float instruction of the text). Over 83 of the license runs (333,003 steps; B-8 and C-3 not): 5,305 multiplies, 5,278
adds/subtracts, 967 multiply-adds, 299 divides, 49 square roots, 6.2 reciprocal square roots, 487 conversions per step, 3,442
compares, moves and absolute values; **no VU0 float instruction at all**. Operands at the model's edges (zero-exponent operands, exponent 255, division by zero, underflowing products) occur in the
three runs looked at (B-1, B-6, B-7).

The VU0 macro float instructions: 114 between 0x340000 and 0x470000, in 19 functions (72 functions in the whole text). By
direct calls from the race step (0x34DB30) and the conductor's virtuals tried as roots (the collision step, the license's
judge, the order, the crossings, the distance), only three functions with such code are reachable: 0x3E21D8 and 0x3E2220 (matrix
transforms, from the cones' contact 0x3E3C48 → 0x3E3ED0) and 0x48D4C0 (from 0x3F71C8, unread); calls through other virtuals are
not in the analysis's call graph. The census never ran any of them, but the harness sets up no cones. The others' known
callers draw: `RaceCarModel::vf36` / `vf39`, `RacePS2Base::vf109` and its relatives, the model sets, the environment camera.
So the engine's VU0 model (the FPU's functions) is **not proven by any recording**: the physics in these runs does not use it.

### Our session

Started from the program's set-up state (every car's record and collision body, the random state) and fed the recorded
pad records, the driving line's heading the program's active steering read and, for other cars, the program's
record of them each step, car 0's place equals the program's in every bit over the whole run (`FloatDemoOptions::ours`) on
all 84 runs (cars other than car 0, in the twelve training tests, loaded from the program's records every step; left to run free, the pace
car's path and so car 0's part from the program's from step 730 to 1,360 on; the test `floats_demonstrations_end_on_their_recorded_times`
plays seven). ✔ Our own set-up of a license race (race/race_start.*, race/demonstration_run.*) run alone, with no state of
the program's, ends 69 of the 70 recorded runs on their times (the program's 66 and the three cone coffee breaks, whose cones it has),
B-6 where the program's run ends (race_flow.md), and the 13 demo replays. Two session settings a license race needs, found here:
`Session::set_class_grip` (the license conductor keeps the event's drive-class grip bytes, CFG +0x18 .. +0x1B) and
`set_course_kind` (dirt and snow by the COURSE row's kind, K +0xCBDD: the bump tables' surface 5 takes row 15 on snow), and
a player's car in its rolling start (driver kind not 0) takes its driver's aid block and grip (0x3F79A0, 0x467470). A license
test's rolling start runs capped at CFG +0x4E (0x3F7880, the conductor's virtual 6 answers 1).

### Not done

The VU0 model is unproven. The cones are not set up.

## 10. Open

- Everything marked ❓ above; the rewritten routines of §1; the three new phases.
- `gt4r_diff`'s race places cars past the road's grid at the program's fallback (§9). The economy display's units
  (0x6244E8, the game's option: km per liter here). The entry of 0x35DE20 is not reached by `gt4r_diff`'s taps (its parts
  are; the whole step compares it).
- The computer drivers (the courses' `.ad` files and `crs/bestline`), the race flow (laps as
  the game counts them, sectors, the finish), pit stops, tire wear's curve per race, the
  rubber band.
- The start: a rolling
  start's places on the driving line (0x46ADE8) and the computer driving the player's run-up.
- Other builds: a program that is not the mapped build serves no tables, and a session then
  runs on the engine's own values and says so (`Session::notes`). Prologue's disc runs a
  session on its database in Gran Turismo 4's layout, its `GTRW` roads and its own step
  ([formats/prologue.md](../formats/prologue.md)).
