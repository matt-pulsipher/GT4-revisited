# Gran Turismo 4's computer drivers

What the program does to drive the opponents' cars, as far as it is known, and what our drivers
(`src/gt4r/sim/ai/`) run. Addresses are Gran Turismo 4 SCUS-97328 v1.01's (`CORE.GT4`); "GT3"
addresses and section numbers (ai §n) are the shared engine's specification,
[engine/gt3r/docs/specs/ai.md](../../engine/gt3r/docs/specs/ai.md), whose drivers these grew from.
A fact is from reading the program's listing unless marked: ✔ checked on the user's disc (data
or a run), 🔎 seen but not checked, ❓ not known.

Code: `src/gt4r/formats/course/ai_line.*` (the driver files), `src/gt4r/sim/ai/course_lines.*`
(the lines as the race prepares them), `tables.*` (the program's values), `drivers.*` (the
drivers), `grid.*` (who races), `entrant.*` (an opponent's car as entered), `run.*` and `gt4tool
ai` (fields run headless), `tools/gt4diff/ai.cpp` (against the program), and
`src/gt4r/formats/db/race_events.*` (the events). Tests: `tests/test_ai.cpp`, `tests/test_ai_program.cpp`.

## 1. How it relates to Gran Turismo 3's

The drivers are GT3's grown. Of the 84 routines GT3's specification cites, 72 have a
counterpart (`~/gt4re-out/maps/map_us_gt4.tsv`), by the map's classes:

| GT3 | GT4 | class | what |
|---|---|---|---|
| 0x1F7AE0 | 0x465078 | larger change | the decision (6,828 bytes for 2,900): §5 |
| 0x1F7088 | 0x463EA8 | larger change | the point bookkeeping and the braking point: §4.3 |
| 0x1F6678 | 0x4674F0 | larger change | an arc's speed: §4.2 |
| 0x1F5FD8 | 0x466C20 | larger change | a driver's parameters from the course's set: §3 |
| 0x1F6B90 / 0x1F6CC0 | 0x4694F8 / 0x469918 | larger change | the zone cells and the contact: §6 |
| 0x1EBDA0 | 0x3F42D0 | larger change | closing on a car ahead: §6 |
| 0x1F79F0 | 0x464F38 | larger change | the outputs into the car |
| 0x1F7810 | 0x464C80 | larger change | the stop driver |
| 0x1F6D70 | 0x469AE8 | small edit | traffic and lane changes: §6 |
| 0x1F6380 | 0x467148 | small edit | the event's changes: §3 |
| 0x1F7590, 0x1F6A20, 0x1F8980 | 0x464770, 0x467AA8, 0x463C28 | small edit | the speed controller, the re-plan, a line request |
| 0x1EC098 | 0x3F4638 | small edit | the cars' pairs pass |
| 0x1ECEB8 | 0x365790 | small edit | the rubber band: §7 |
| 0x1F7658, 0x1F7948, 0x1F2F90 | 0x464868, 0x464E88, 0x34C998 | same | the aim on a line, the look ahead, the course kind |
| 0x1FA018, 0x1FA368 | 0x46B480, 0x46B7B8 | constants, calls only | a line's place at a distance, the stop line's |
| 0x1F92F8, 0x1F8BF8, 0x1F8BA0, 0x1F9F08, 0x1FA530 | — | gone | GT3's line loader and its tracing, the span search, the side map |

GT4's own: the line loader 0x46A510 (§2.2), the tuning block 0x468EE8 (§3.2), the driveline
class 0x466B28, the lateral offset of the aim (§5.3), the B-spec driver's records
0x467C58 .. 0x468EE4 (not read). The drivers' objects: the conductor's AI course object K at
+0xDA50 (its +0x08 the line table), a driver's object D at the car's +0x14, its parameter block
`*(conductor + 0x10024 + 4 x car index)` (0x463BB8).

## 2. The lines

### 2.1 The driver file

`crs/<course>.ad` (110 on the US disc, one per course): the layout is in
[formats/course.md](../formats/course.md) "The drivers' file". The race keeps a table of eight
lines (K +0x08 + 4 k); line 0 is never used.

| line | role (by the code that asks for it) | on the US disc's 110 files |
|---|---|---|
| 1, 2 | the passing lines: 1 on the right, 2 on the left (0x46BC00 / 0x46BC40) | 84 / 85 |
| 3 | the pit lane | 67 |
| 4 | the pit stop line, a part of the lap | 66 |
| 5 | per-point run-time records (0x46B9F0 clears them, 0x46BA88 finds one by distance) ❓ its use; not GT3's side map | 4 |
| 6 | the main line: the start's, and the one the drivers return to | 110 |
| 7 | a variant; asks fall back to 6 | 41 |

A request for a missing line falls back by the table at 0x624030 (per line, up to six lines to
try, -1 ending a row; ✔ read on the disc: 1 → 1, 6, 2; 2 → 2, 6, 1; 3 → 3; 4 → 4; 6 → 6, 1, 2;
7 → 7, 6), resolved by 0x46BB48.

### 2.2 Preparing the lines (0x46A510, from 0x46B0A8)

In the order of the table at 0x624018 (✔ 6, 0, 1, 2, 5, 3, 4, 7), for each point i of a line:

- **The road's height**: probed straight down at the point (0x46A0C8 → the road query
  0x395760 at (x, h, -y)) from 1,000 m for the first point of the first line, else from the last
  height + 0.1 x the chord from the last point + 1 m, then from the last height + 100 m; with no
  road under it the last height + 3. The next probe starts from trunc(height) + 2.
- **The kind**: a non-zero kind becomes 1 when the radius is positive (an arc to the left), 2
  otherwise (to the right); on line 5 always 1.
- **Line 4, points 3 .. count - 4**: moved onto the nearest pit stop of the road (RNW4 +0x90)
  within 5 m, its heading turned half round.
- **The distance** along the course (0x3955E8 → 0x3E6858) at +0x14: the road cluster under the
  point (the height probe's, else a cast from 1,000 m above) lists checkpoint indices (cluster
  +0x02 count, +0x04 first, into RNW4 +0x64); each checkpoint i and the next (i + 1, the last's
  next the first) make two quads across the ground, (left, middle) then (middle, right) of both
  gates. 0x3E6610 inverts a quad's bilinear map for the point (u from the gate to the next, v
  across; a quadratic solved with one square root; false outside the four edges or for a quad of
  no extent) and gives the point's height above the quad. Of the quads that hold the point the
  one nearest in height wins (the search starts from 0x7FFFFFFF, the EE's largest float): the
  distance is the gate's + (the next's − the gate's, + a lap when it falls) × u, less a lap past
  it. On line 4 a lap is added from the first point in the lap's first half on. A line whose
  distances do not rise is flagged; lines 1, 2 and 7 so flagged are dropped.
- **Slope, bank and surface** (0x46A140, all lines but 5): +0x0C the height, +0x12 the surface
  index (0x3F4E40), +0x13 the road record's byte +0x32; +0x1C the slope and +0x20 the bank from
  four road probes (0x46A0C8) half a metre ahead, behind, left and right of the point along its
  heading: atan2(ahead − behind, 1), or with one probe off the road atan2 of the other against
  the point's own height over 0.5; the bank the same with left and right (+ the left side higher).
- Then each point's stretch to the next (+0x2C: the chord, or |r| x the heading's turn on an
  arc), the rise to the next point (+0x30) and its pitch atan2(rise, length) (+0x34); and +0x28
  = 1, a **crest**, where the pitch falls by more than a degree from the last point's.

✔ The file's frame is the simulation's turned half round: a point (x, y) of the file stands at
(-x, -y) in the car record's frame (course (x, z) = (-x_file, y_file)), its heading h at h + π
(the race's distances then rise along every main line, the arcs evaluated end on their next
points within 0.01 m, and the points lie on the road: `ai_disc_lines_prepared`). The race
evaluates a line at a distance as GT3 does (ai §2.3: the span around the hint, straights
interpolated, arcs turned about their centre; the stop line straight).

## 3. A driver's parameters

### 3.1 The course's set (0x466B90, 0x466C20)

The car's **driveline class** (0x466B28, from the converter 0x361908): FF 1; four wheel drive by
its system: 3 → 3, 0 → 1, 5 → 0, any other → 2; FR, MR and RR 0. The set is the driver file's
slot 2 + class; without one, the built-in set at 0x623DC0 (+0x40 on a loose course). Its bytes s
into the parameter block PB:

| PB | from | | PB | from |
|---|---|---|---|---|
| +0x00 braking factor | s0 / 100 x T+0xF4 | | +0x58 the front's share of the arcs' grip | s14 / 100 |
| +0x04 arcs' speed scale | s1 / 100 | | +0x5C arc bonus (m/s) | s15 / 3.6 |
| +0x08 arcs' speed offset (m/s) | (s2 - 100) / 3.6 | | +0x60 steering rate (rad/s) | s16 x 10 degrees |
| +0x0C yaw gain | s3 x 5 / 100 | | +0x64 pit speed (m/s) | s18 / 3.6 (0: 80 km/h) |
| +0x10 / +0x14 saturation's throttle cut, brake | s4, s5 / 100 | | +0x68 arc bonus delay (steps) | s17 x 10 |
| +0x18 1 / the full look ahead's speed | 1 / (2 s6 / 3.6) | | +0x6C the event's throttle share | 1 (§3.3) |
| +0x1C / +0x20 look ahead at rest, at that speed (m) | s7, s8 | | +0x70 downforce term | (lift coefficients) / mass (0x4673D8) |
| +0x24 .. the driving aids | s9 .. s13, s19 .. s24 as GT3's | | +0x74 an aero bonus | (P+0x1077 = 7) x P+0x1084 x 0.8 / mass, else 0 ❓ |
| +0x78 the straight bonus on | s28 and s29 both non-zero | | +0x7C / +0x80 its angle, speed | s28 degrees; s29 / 3.6 x the class's percent at 0x623FD8 unless 0 or 100 |
| +0xA8 .. +0xB0 the arcs' grip by tire class | 1, or s32 .. s34 / 100 when s27 = 0x20 | | | |

D +0xB8 / +0xBC / +0xC0 (corner, braking and alongside multipliers) start at 1.

✔ The program builds them in its own arithmetic (whole numbers converted, then x 0.01 =
0x3C23D70A, / 3.6 as x 0x3E8E38E3, degrees as x 0x3C8EFA34, the percent divisions by 100.0):
ours are bit for bit the program's on every car checked (`gt4r_diff ai`, §10).

✔ Every one of the 110 files has its four sets. Tsukuba's: braking 110 %, arcs 100 %, look ahead
3 .. 25 m to 140 km/h, yaw byte 40 (gain 2.0), steering 90 degrees/s, pit speed 80 km/h, the
straight bonus 10 degrees / 30 km/h.

### 3.2 The tuning block (0x468EE8 → 0x623E50, from the bytes S at 0x623FB8)

Built at every car's setup (0x469440), the spreads' two switches after it from S[0x38] / S[0x3F]
(0x469468; 0x468EE8 itself clears them); tenths are x 0x3DCCCCCC, hundredths x 0x3C23D70A, the
tables' points / 100.0. ✔ The US program's values (read in the running game's memory too):

| | | value |
|---|---|---|
| T[0] = S[0] | 0: the throttle slews in the arcs (0x464F38) | 0 |
| T[2] = S[2] | below 2: the steering slews to its target (else a PD path on T+0x10 ..) | 1 |
| T[4] = S[0x24], T+0x74 = S[0x25] / 10 | a line change's blend runs off by distance at that rate (m/s) | 1, 2.0 |
| T[5] = S[0x28] / S[0x29] / S[0x2A] | the aim's offset: hold the line's own side, keep apart, come back onto the road (§5.3) | all on |
| T+0x78 / +0x7C / +0x80 / +0x84 | the side share (pit lines' share), the offset's slew (m/s), its limit (m) | 0.1 (1.0), 4.0, 0.5 |
| T[8], T+0x88 / +0x8C | the offset limited by the sideways load between | on, 0.5 .. 0.8 |
| T[9], T+0x90 | arcs slower with a car alongside, by | on, 5 % |
| T[0xA], T+0xAC / +0xB8 | the arcs' speed's random spread: [lo, hi] by the corner multiplier m (x 0.8 .. 1.0) | on, lo -12 % .. -4 %, hi +18 % .. +2 % |
| T[0xB], T+0xDC / +0xE8 | the braking's random spread, the same shape | on, the same values |
| T+0xF4 = S[0x46] / 100 | every set's braking factor | 1.0 |
| T+0x158 | the arcs' speed by the tire's ... (S[0x47] points) | all 1.0 |

The random draws: a 15-bit generator per driver at D +0x68 (x' = (x 0x41C64E6D + 0x3039) >> 16 &
0x7FFF, 0x463960), a value a + (b - a) x / 32768 (0x463988); seeded at setup with the grid index
+ 1 for a computer car (1 otherwise), stepped at every new segment.

### 3.3 The event's changes (0x467148)

From the race's copy of the event row (§8): per class the throttle (0 or over 100 → 100),
braking and arcs' percents (0 → 100). With the byte at 0x620DE9 set (✔ 1) and a computer car of
grid index i: the yaw gain x ((i + 1) mod 3 x 3 + 97) %; only with the spreads of §3.2 off, the
braking less (i mod 3) x 7 %, the arcs less (4 i) & 4 % and 2 ((7 i) mod 5) km/h. Then PB+0x6C =
throttle / 100, D +0xBC x braking / 100, D +0xB8 x arcs / 100, PB+0x08 + the offset. At the end
of a run 0x467360 halves PB+0x04 and PB+0x6C (at least 0.4) and takes PB+0x00 x 0.8.

## 4. The plan

### 4.1 Segments (0x4679C0, 0x467AA8)

As GT3's (ai §4.1) with two changes: past the last point the next segment is **point 0's**
(GT3: the last point's), and a point whose speed byte is -128 is passed over (the next one
serves). Crossing the start line (next = count, s < half a lap) sets next = 0 without a new
segment. A new segment steps the random draw. The re-plan finds the first point beyond the car.

### 4.2 An arc's speed (0x4674F0)

μ = w μ_f + (1 - w) μ_r with w = PB+0x58 (halfway to 0.5 for ❓ two flags of the car), each axle's
compound grip x its surface's percent, both x the class grip PB+0xA8[tire class]; with tire
wear on, the wheels' wear grips weigh it as GT3; x 0.9 on ❓ F+0x5C2. The bank and the three
square roots as GT3 (ai §4.2) with the aero term: v = √(a q), q = PB+0x70 v² + 1 + e, e = PB+0x74
(0 from a crest point). The scale PB+0x04 x the corner multiplier (D +0xB8, plus the random
spread for a computer car), x (1 - 5 %) with a car alongside (zone cells left, right, front left
or right); the offset PB+0x08 + the point's speed byte / 3.6; x T+0x158's factor on tire class 0.
The speed is the car's top speed (P +0xFD8) when above it, else at least 20 km/h (0x40B1C71B). The
tire records are Gran Turismo 4's (P +0x410 + 0x1EC r, 24 surfaces): the front's is wheel layout
byte +0x0C's, the rear's +0x0D's (0x360F80). ✔ Ours runs in the program's arithmetic and equals
its result at every call where the line point's own values do (§10).

### 4.3 Braking (0x463EA8)

✔ Ported in the program's arithmetic; equal at every call checked (§10). In detail: the grip is the
front record's (layout +0x0C) grip on the point's surface x the class grip x the load curve at
(P +0x103C x 0.5) / the axle's width (layout +0x10's) x the record's braking peak x P +0xFF4's (the
same axle), then off the pit lane x the steered wheels' mean wear grip; coefficient
max(P +0x101C, 1 / (2 x (μ 9.8 ((q_h + e) + (q_l + e))) x 0.5)) x (1 − sin slope); a pad car adds its
speed; x max(0.1, 1 + adjustment x 0.01); the follow chain, contact (+10 m) and the followed car's
braking (+8 m) only for a computer car outside the cooldown; the status-1 margin is 0x469490's
25 m.

As GT3's (ai §4.3) but: the braking factor kb = PB+0x00 x the braking multiplier (plus its random
spread) **divides** the distance when above 1 (GT3 multiplied below 1); the braking grip takes
the class grip and the aero terms (q = 1 + PB+0x70 v² + e at both speeds: coefficient
max(C_car, 1 / (μ 9.8 (q_h + q_l)))); the soft zone is d (1 / kb - 1) for kb < 1; the status 1
margin is 25 m (GT3 50); the distance grows by the follow chain's length in metres (§6), 10 m in
contact (GT3 20 c), 8 m when the car followed ❓ brakes. The throttle multiplier with PB+0x6C
below 1 weighs the full-throttle torque (as GT3's later builds).

### 4.4 The speed controller (0x464770)

throttle = clamp(0.75 + G clamp(v - (v_t - 2 km/h), ±10 km/h), 0, 1); the brake full when the
throttle is 0 and v > v_t, + 20 km/h outside the braking modes (GT3 without the 20).

## 5. The decision (0x465078)

### 5.1 The look ahead and the aim

The look ahead (0x464E88) as GT3's; recovery states 10 / 40 m; the start: for a computer car on
a paved course from a standing start (below 30 km/h) + 200 m x (5 s - t) / 5 s, else + 40 m x
(2 s - t) / 2 s (+ the car's start delay ❓). The aim on the line (0x464868, ≥ 5 m) and the blend
with the old line as GT3's, the blend running off by distance (T[4]).

### 5.2 Recovery, steering, throttle

As GT3's (ai §5-§7), with: the yaw-rate steering's gain PB+0x0C scaled by T[2] (on in this
program) by clamp((a_max − F+0x46C) / a_max, −1, 1), a_max the steered wheels' mean peak slip angle
(their tire records' +0xB8, the wheels whose layout byte +0x3C is above 0) and F+0x46C their mean
slip angle: the gain falls to nothing as the front reaches its peak (✔ without it our drivers
steer up to five times the program's angle into the first corner and scrub off 15 km/h); the
non-driving branch's eps x 1.4999998 (0x3FBFFFFE); the state machine rests 180 steps (D +0x6B) after a change; turning
round towards 50 km/h; the yaw-rate steering on GT4's car fields; a pad car's driver ends after
the aim. The saturation's throttle cut and brake (0x620DE7) is ✔ off in the US program.
Following a car (D +0x51): the next speed is the car's less 20 km/h (braking), 15 (an arc's
status) or 5, at least 30 km/h, in mode 4.

### 5.3 The aim's sideways offset (T[5], GT4's own)

With the state 0: the line's own point beside the car x 0.1 (1.0 on the pit lines); 2 m less the
gap to a car alongside, away from it; half a metre away from the side whose wheels are on soft
ground (paved courses, outside braking); limited by the sideways load (|F+0x58C| ≥ 0.8: none,
0.5 .. 0.8: ramped ❓, else 0.5 m); slewed at 4 m/s.

## 6. Traffic

The pairs pass (0x3F4638, the pairing 0x3F40F8 as GT3's) fills each car's **zone cells** (0x4694F8:
front, front left and right, left, right, rear left and right, rear; levels 1 and 2 by the gap)
and the side gaps (D +0x58 / +0x5C), then **closing** (0x3F42D0: the gap along the course less the
two cars' lengths; within 20 m only when the cars overlap sideways) and **contact** (0x469918):
2 and a car to follow when closing within 5 m, else 1.0 closing at over 20 km/h, 0.5 when it
closes within 5 s, 0.05 / 0.1 within 0.5 / 4 m. A car follows until the gap opens past 6 m;
the follow chains' lengths (D +0x6A) lengthen the braking.

**Lane changes** (0x469AE8, after 5 s + 0.1 s x the place): no change while braking or near a
braking point (status 1, 2, 5), none from a passing line in an arc near the grip's limit, none
during a blend, none in the cooldown (180 steps after a change). With contact: the farthest line
on a side (0x46BC00 / 0x46BC40 then once more), by the next segment: an arc to the left takes the
left, else the right unless status 4; to the right the reverse; a straight the left unless
status 3, else the right unless status 4. Without a change the throttle x (1 - contact). With
nothing around (front, the sides and their fronts) a car returns to line 6. ✔ The program's left
and right: left of 1 is 6 (2 without it), of 6 is 2; right of 2 is 6 (1 without it), of 6 is 1.

## 7. Pace

- The **menus' difficulty** picks the event (the car's Arcade rows, `ARCADEINFO_NORMAL`
  [difficulty]); `enemy_level` only steers which opponents the scripts pick (arcade.pp: the
  pick's offset (level - 2.5) / 100 around the player's car); neither changes a driver.
- The **events' percents** (§3.3) are the per-event pace: ✔ e.g. the amateur FF events brake at
  85 % and take the arcs at 90 %.
- The **rubber band** (0x365790 with 0x365480): a car's time scale against the reference, as GT3's
  (ai §9.3), from the event row (§8), capped at 1.3. ✔ Only the 12 license-test events of the US
  disc carry it (slowing by 50 %); no race does.
- Per-car skill: the random spreads (§3.2) and the grid-index spread (§3.3).

## 8. Who races

The event row's columns are in [formats/database.md](../formats/database.md) "Events". The race
copies the row into its configuration block (0x35E108, the conductor's +0xF818, 0x80 bytes). The
opponents: the event's pool (`RACES/<35, row>`: ENEMY_CARS rows, each a car, its parts and its
settings); RaceInformation::vf15 (0x3BB5A8) draws one entry per opponent place at random from the
race's generator (0x621AE8), the next undrawn one from there, skipping cars the race may not use
(0x448C10 ❓ which).

**A place's car** (0x4474B0 → 0x444440 → 0x444690 for an `ENEMY_CARS` id, table 0x26; any other id
0x444480, the car as sold): the record cleared, the row's car at +0x00, **the `ENEMY_CARS` id itself
at +0x08** (-1 for a car as sold), the row's `DEFAULT_PARTS` set's 26 parts in their places, each
part's default settings (0x4449C0), the paints' id, then the row's `DEFAULT_PARAM` settings
(0x444860, copied as they are; the output factor only when +0x08 names a row, 0x445B78), then a
computed gearbox's ratios from the settings' slider (0x443240). The layout of `DEFAULT_PARAM` is
in [formats/database.md](../formats/database.md). ✔ All 2,558 rows build; 2,078 carry other parts
than the car as sold, 766 other settings, 560 another power (e.g. the North American series' BMW
M3 '04: 400 PS at an output factor of 1,150 against 344 as sold; `gt4tool ai entrants`).

**The paint** (0x445880): the pool entry's second word less one is the place's paint when the car
has that many (+0x18); -1 (a word of 0) leaves 0xFF, **the race's choice** (0x40AA48 after the
draw): for each such car, its paints (0x445828: the car's paint rows, less the last when the car
table hides it) each get a weight 1, 0.984375, 0.984375² .., divided by 1 + the sum over every
other car's candidates of their colour likeness (0x40A338) — ^64 x 10,000 for a car whose paint is
already fixed — normalised; the paint is drawn from the race's generator by those weights: a car
avoids the colours already on the grid. ❓ 0x40A338's likeness (not read).

**The per-car start delay** (missions): the race keeps per place 0..5 the row's bytes +0x91,
+0x8C .. +0x90 (0x447828) as seconds at CFG +0x60 + 2 i (s16); ✔ only the ten "catch the cars ahead"
missions (usl0m0025 .. 0034) set them (e.g. 35, 0, 4, 11, 16, 19). The decision lengthens the
start's long look ahead by them (5 s + delay); 🔎 the cars' held start itself is elsewhere (not
found).

**The reference race** (03-race, Arcade, Ford Ka '01 at Tsukuba): the Ka has no Arcade event rows,
so the race is `sample_entry_6` (every percent 100, no rubber band) with opponents the menu's
script picks by power (`PickUpEnemyCars`) and enters by their car labels: ✔ in the saved states
their equipment records equal our cars as sold byte for byte (+0x08 -1), the arcade's tire class
at +0x160 / +0x161 (4: sports medium, applied by RaceArcade::vf92 when +0x165 is set) their own.
The drivers' parameter blocks in those states equal ours (the yaw spread 2.06 / 1.94 / 2.0 by grid
index, multipliers 1.0).

**B-spec's shift speeds** (0x36BEE0 / 0x36BF28 → 0x3F1E30): the gearbox's up- and down-shift speeds
x the driver object's byte +0x6F (car +0x83) / 128, only for a car of control source 1 (the
player's car with a computer driver: B-spec); the opponents (source 2) shift at the gearbox's own.

## 9. What our drivers run

The parameters (§3.1, §3.3), the tuning block (§3.2), an arc's speed (§4.2) and the braking routine
(§4.3) in the program's arithmetic (`gt3r::ee::F`); the lines' distances, slopes and banks as §2.2;
the rest of the decision, the traffic and the rubber band still in host floats. Per step: the order
by distance, the rubber band's scales, the pairs pass, then per driven car the recovery's contact
flags, the decision and the outputs. The cars are read through `ai::Cars` (a session's, or another
race's for checks). Departures:

- **The outputs** go to the session as the program's 0x464F38 writes them (`race::CarInput::computer`:
  throttle and brake pedals, reverse, the gearbox's lock, the clutch engaged; the pad's path not
  run), the steering as the road-wheel command (`steer_angle`).
- **The collision flags**: from the cars when they give them (the program's own in `gt4r_diff`), else
  a wall within the body's ends or a car overlapping.
- **The time scale** of the rubber band is computed (`Field::time_scale`) but the session does
  not apply it: it needs a per-car step length (the engine's record has `time_scale`; the session no
  setter).
- **The road's heights** come from our road query in host floats: some line points' heights, slopes
  and banks differ from the program's in their last bits, and so then do the arcs' speeds there.
- Not done: the pit lane and the stop (lines 3 and 4 are prepared, never asked for), the stop
  driver and the end of a run, B-spec's commands and shift speeds, the line-5 records, the start's
  per-car delay, the aero bonus (P +0x1077 / +0x1084 not mirrored: 0), the two ❓ flags of §4.2
  (P +0x104A, F +0x5C2: 0 on every car checked), the arcs' grip with tire wear on, T+0x158's factor
  (1 throughout in this program).
- Without the mapped program (Prologue, another release) the drivers run with neutral values: no
  spreads, no offset of the aim, a fallback of the passing lines to the main line, the shared
  engine's tire records (16 surfaces) and no slip scaling of the steering gain; on a road without
  the clusters' checkpoint lists (Prologue's) the lines' distances are taken between the
  checkpoint lines. Prologue's
  disc has no driver files (`crs/*.ad`): its courses carry the lines and sets in slots 31 and
  37 .. 40 ([formats/prologue.md](../formats/prologue.md) "The drivers").

## 10. Checks

`gt4tool ai <disc> <course> [cars] [laps] [rolling=<km/h>] [trace=<car>]` runs a field and prints
each car's laps and incidents (a rolling start at the race's rule: the last place 400 m before the
line, 20 m apart); `lines`, `survey`, `events`, `event`, `race <label>` (the pool's parts and
settings; `stock` as sold), `entrant <enemy>`, `entrants`, `equipment <label>` the data.

`gt4r_diff <disc> ai [course] [cars] [laps]` runs **the program's own race** in the interpreter with
**the program's own drivers** (the course's driver file in the course object's +0x9C / +0xA0, the
conductor's rubber band off: without the event row its constructor's values hurry every car behind
car 0 by up to 10 %), and prints each car's laps. `GT4R_AI_OURS=1`: our drivers instead, on the
program's cars and physics (the decision 0x465078 replaced by ours; `ai::Cars` over the program's
records). `GT4R_AI_CHECK=1`: the prepared lines, every driver's parameters, every line point's
speed (0x4674F0) every 2 s and the braking routine (0x463EA8) every 4 steps against ours, from the
program's driver state, bit for bit. Results at Tsukuba (US disc):

- ✔ Parameters, braking (multiplier, status, mode, point), the lines' distances, kinds, surfaces
  and crests equal at every call; the arcs' speeds equal but where a point's road height differs in
  its last bits (§9).
- ✔ **Our drivers on the program's physics lap as the program's drivers** (from a standing start,
  three laps, best of the flying laps, s; the program's / ours): Beetle 1100 90.8 / 90.5, Z 97.5 /
  95.0, N360 99.5 / 99.7, Midget II 91.7 / 91.2, S500 89.0 / 88.9, Ka 83.2 / 83.1. The program's
  times reproduce the reference race's gaps (03-race: N360 and Z some 17 s behind the Beetle in two
  laps, the S500 stuck behind the Beetle).
- **On our own simulation** the same field laps Beetle 92.6, Z 93.6, N360 97.6, Midget 93.6, S500
  91.8, Ka 88.3 (two laps from a rolling start: Beetle 3'07.9 against the reference's 3'04.5, N360
  3'21.6 against 3'21.4, Z 3'14.3 against 3'22.1): the remaining differences are the simulation's
  (the S500 alone: 89.2 s on the program's physics, 92 .. 97 s on ours, lost in the slow corners,
  where our car's least speeds are 8 .. 18 km/h lower).
- ✔ Six small cars finish three laps of Tsukuba (circuit), Seattle (city) and Grand Canyon (dirt)
  with no car standing still.
