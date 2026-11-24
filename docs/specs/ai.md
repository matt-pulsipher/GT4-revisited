# Gran Turismo 4's computer drivers

What the program does to drive the opponents' cars, as far as it is known, and what our drivers
(`src/gt4r/sim/ai/`) run. Addresses are Gran Turismo 4 SCUS-97328 v1.01's (`CORE.GT4`); "GT3"
addresses and section numbers (ai §n) are the shared engine's specification,
[engine/gt3r/docs/specs/ai.md](../../engine/gt3r/docs/specs/ai.md), whose drivers these grew from.
A fact is from reading the program's listing unless marked: ✔ checked on the user's disc (data
or a run), 🔎 seen but not checked, ❓ not known.

Code: `src/gt4r/formats/course/ai_line.*` (the driver files), `src/gt4r/sim/ai/course_lines.*`
(the lines as the race prepares them), `tables.*` (the program's values), `drivers.*` (the
drivers), `ee_math.*` (the program's own atan2f), `grid.*` (who races), `entrant.*` (an opponent's car as entered), `run.*` and `gt4tool
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
| 5 | per-point run-time records of 8 bytes at the prepared point's +0x38: 0x46B9F0 clears them at the race's start (the conductor's virtual 28); 0x46BA88, which finds one by distance, is called from nowhere in this program, and nothing else reads them: nothing to run; not GT3's side map | 4 |
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
  road under it the last height + 3. The next probe starts from trunc(height) + 2. ✔ A line's first point
  below the last line's end (Suzuka East's stop line: from -1 m, the road at 10.9 m) is found as the
  program finds it, from 1,000 m.
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
(0x46B248 negates x and y and adds 0x40490FDA to the heading in the EE's arithmetic)
(the race's distances then rise along every main line, the arcs evaluated end on their next
points within 0.01 m, and the points lie on the road: `ai_disc_lines_prepared`). The race
evaluates a line at a distance as GT3 does (ai §2.3; the stop line straight). In detail (0x46B480 with
the span search 0x46B310): the distance is wrapped into the lap once; before the first point the span is
(last, first) at t = ((s + lap) − d_last) / ((d_first + lap) − d_last), at or past the last point the same
span at t = (s − d_last) / ((d_first + lap) − d_last); otherwise the points are searched from the hint
outwards (hint, hint + 1, hint − 1, hint + 2, .., wrapped, n + 1 tries; point 0 never ends a span) for
d[i−1] ≤ s < d[i], t = (s − d[i−1]) / (d[i] − d[i−1]); no span leaves the outputs as they were. A
straight interpolates; an arc turns about its centre (x − cos h r, y − sin h r) from its point's angle
to the next one's by t (+2π where the turn crosses), at |r|; the height interpolates. ✔ Equal to the
program's at every call checked (`gt4r_diff ai`, "place").

The angles use Gran Turismo 4's own atan2f (0x57D118): 0 at the origin; |y| < |x|: atan(|y| / |x|),
else π/2 − atan(|x| / |y|) (0x3FC90FDA); π − that for x < 0 (0x40490FDA), negated for y < 0. It is not
GT3's (whose shortcut returns 0 for any x + y = 0); its atanf, sinf and cosf (0x57D008, 0x57D2B8,
0x57D1F0) are GT3's arithmetic (✔ 4,000 values each, `gt4r_diff ai`, "math").

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
| T[2] = S[2] | below 2: the steering slews to its target (else the PD steering, §5.2) | 1 |
| T+0x10 / +0x14 = S[9] / S[0xA], T+0x18 .. +0x24 = S[0xB .. 0xE] / 10, T+0x2C .. +0x44 = S[0x10 .. 0x16] / 10, T+0x5C = S[0x1C] / 100 | the PD steering's gains and the offset's bound | 15, 20, 8.0, 0.2, 0, 10, 5.0, 0, 3.0, 4.0, 0.1, 0, 2.0, 1.0 |
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
throttle is 0 and v > v_t, + 20 km/h outside the braking modes and the kinds 2 .. 4 (GT3 without the
20); in detail §5.3.

## 5. The decision (0x465078)

✔ Ported in the program's arithmetic and equal to the program's at every call (`gt4r_diff ai` with
`GT4R_AI_CHECK=1`, §10: the outputs, the aim, the blend, the offset and every state byte, at Tsukuba and
Grand Canyon, a B-spec driver with a pace command, and the four ends of a run), but where an arc's
speed takes a line point's height in its last bits (§9).

### 5.1 The start: the outputs, the look ahead, the aim

- **Begin** (0x464ED8): the steering D+0x30 from the car's command (F+0x468), throttle, brake, reverse
  and the gearbox's lock cleared; with T[4] = 0 the blend runs off by the step (dt). A car held at the
  start (control 1 or 2 and F+0x4BC) keeps its throttle and ends after the aim. Kind 7: the stop driver
  with a straight wheel (§5.5). Then the braking routine (§4.3) gives the throttle's scale.
- **The look ahead**: kind 3: 5 m; kind 4 on the pit speed (D+0x20): 5 m below 30 km/h, else 10 m;
  recovering: 10 m (backing out) or 40 m; else 0x464E88 (PB+0x1C at rest, PB+0x20 from 1 / PB+0x18,
  linear between), plus the start's: with D+0x1A = 0 (a computer or B-spec car from a standing start on
  a paved course below 30 km/h, 0x463AE0) left = 5,000 − ms + 1,000 x the place's held seconds,
  look + left x 200 / 5,000, else 2,000 − ms + 1,000 x the seconds, + left x 40 / 2,000 (left > 0; ms the
  race's clock, the conductor's +0xF89C / 3, +50 a step); then + D+0x52 / 3.
- **The aim** (0x464868): from the car's course distance F+0x4A4 (not wrapped) + look, the line's
  place (§2.2) less the car's position, tried 10 times, 1 m further while nearer than 5 m (no further
  after the ninth); the look used is returned and the old line's aim takes it. With a blend: T[4] = 1
  runs the blend off by the gap between the two aims (|d| < T+0x74 dt: 0, else blend − T+0x74 dt / |d|,
  at least 0); aim + (old − aim) x blend.
- **The sideways offset** (T[5], racing only): bit 0: the line's aim with look 0 (blended likewise)
  across the heading (cos h x + sin h y) x T+0x78 (T+0x7C on lines 3 and 4); bit 1 (not line 4):
  + (2 − the left gap) when under 2 m, − (2 − the right gap); bit 2 (paved, the mode 0 or the status
  0): + 0.5 x (a wheel on soft ground on the left − one on the right) (the layout's side byte +0x1C + w,
  the ground record's material through the soft table 0x622430); T[8]: the limit by |F+0x58C|: 0 from
  T+0x8C on, (T+0x8C − a) / (T+0x8C − T+0x88) above T+0x88, else T+0x84; D+0x54 slews to it at T+0x80
  dt; the aim + D+0x54 x the car's side axis (F+0x18). F+0x61C / +0x620 = the position + the aim. A pad
  car of kind 0 or held (F+0x4BA) ends here, as does a held start.

### 5.2 Recovery and steering

- ε = (0x36CC40) atan2(−aim x, aim y) − heading in [−π, π] (0x36CB90 folds each angle first, by
  whole turns beyond 3π).
- θ = 60°; backing out: 10° moving forward, 10° − v 50° / 30 km/h above −30 km/h, and it reverses.
  Else |ε| < θ or turning round: forward above −2 km/h (and braking when rolling back with the clutch
  state 0), else braking; else braking above 2 km/h, else reverse.
- The state machine rests while D+0x6B counts down (180 after a change). Backing out: |ε| < θ: racing
  above 3 km/h, else turning round; else hit at the rear: turning round. Turning round: |ε| < 40° above
  30 km/h: racing; else only when hit at the front: the line's heading (0x4649F0: the line's place at s
  and at s + 0.5 m steps until 0.2 m apart) more than 60° off, |v| < 1 km/h or |ε| > 60°: backing out.
  Racing: |ε| ≥ 60°: turning round; hit at the front below 1 km/h: backing out; hit at the rear with
  |ε| > 45°: turning round. (A car whose P+0x32 is 4 never backs out: not mirrored, 0 on every car.) The
  hit flags are the collision bodies' front and rear words (+0x434 / +0x438); the third (0x45CC40)
  answers 0.
- Steering forward: the steered wheels' (layout +0x3C + w above 0) mean peak slip angle a (tire records
  +0xB8 on their surfaces) and mean slip direction; recovering: ±a past ±80°; else the yaw rate wanted
  rd = (−2 |v_xy|) (cos·x − sin·y of the aim in the velocity's direction (the heading's below 5 km/h)) /
  |aim|², negated reversing; the gain PB+0x0C x clamp(−(F+0x46C − a) / a, ±1) with T[2]; rd > 0 above
  the yaw rate: + (rd − r) gain, and past a its share **over = t / a** is kept and t held at a (paved);
  rd < 0 the same mirrored; + the slip direction, clamped ±45°, negated reversing. Otherwise ±30° past
  ±20°, else ε x 1.4999998. Then the lock (P+0x54). Kind 6: the stop driver with that wheel (§5.5).
  The wheel slews at PB+0x60 dt (0x464BB0) for a computer, B-spec or non-racing driver. With T[2] ≥ 2 (never on this
  disc: T[2] is 1) a racing driver steers by the **PD steering** instead (in the yaw-rate branch, after the gain's
  scaling): the rate F +0x484 + dt x Σ, Σ = T+0x10 e1 − T+0x30 Δe1 / dt + T+0x34 Δe2 / dt (+ T+0x14 e2 ky when the yaw
  rate is short of rd) + T+0x38 Δe3 / dt + T+0x18 e3 − T+0x3C Δe4 / dt + T+0x1C e4 amax − T+0x40 Δe5 / dt − T+0x20 e5 −
  T+0x2C x the rate, with e1 the slip direction less the steering, e2 rd − the yaw rate, e3 the line's heading (0x4649F0)
  less the travel's, e4 the car's offset from the line's place (x cos + y sin of the heading, within ±T+0x5C), e5 the
  line's heading less the car's, Δ against the last step's (D +0x00 .. +0x10); the rate within ±PB+0x60; the steering +
  rate x dt within ±π/4 (there the rate cleared). ✔ Equal to the program's at every decision with T[2] set to 2 in both
  races (`GT4R_AI_STEERMODE=2 GT4R_AI_CHECK=1`, test `ai_pd_steering_equal_to_the_program`).

### 5.3 Throttle and brake

Forward: braking 0.09 when the decision said so; D+0x20 = 0; line 4: 25 km/h both, mode 3, D+0x20 = 1;
line 3: between the stop line's points 1/2 and its fourth from the end (the distance + a lap in its
first half): mode 1 at the pit speed PB+0x64 + 2 km/h (D+0x20 = 1; the current speed when lower, its
own mode, D+0x20 = 0), the next at most that; other lines: a car followed (D+0x51) within the
cooldown's first 60 steps: its speed − 20 km/h (status 1, 2), − 10 then − 5 km/h (3 .. 5), else − 5,
at least 30 km/h, mode 4, the next at most that. Turning round: towards 50 km/h (the aim's on lines 3,
4) at gain −0.5. Mode 0: full throttle; 3: towards the next at −2; 4: at −1 (the arc's count on);
1, 2: the aim + PB+0x5C once the arc's count reaches PB+0x68 off the pit lane, and then the straight
bonus PB+0x80 (PB+0x7C − |ε|) / PB+0x7C under PB+0x7C; at −0.5; with the status 0 the filtered
lateral acceleration F+0x560 over the four wheels' mean grip (+0x00): above 0.9 status 5, above 0.5
status 4 (mode 1) or 3. The count stops at 6,000. Throttle x the braking's scale; from 5 s + 0.1 s x
the place (F+0x4AF) the traffic (§6). Not forward: reverse at full throttle or none, the brake full
when braking.

**The speed controller** (0x464770 / 0x4646F8): throttle = clamp(0.75 + gain x clamp(v − (v_t −
2 km/h), ±10 km/h), 0, 1); outside the modes 3, 4 and the kinds 2 .. 4 the brake's speed + 20 km/h; the
brake full at no throttle above it; a part throttle locks the gearbox for 12 steps.

**Past the tires' peak** (0x620DE7 clear, as in this program): with over > 0 in a gear of 2 or more,
the throttle x max(0, 1 − PB+0x10 (over − 1)), the brake + (over − 1) PB+0x14 (at most 1). (With the
byte set the measure is F+0x470 instead, taken as it is.)

**F+0x560** is a second-order follower of the lateral acceleration F+0x554 (0x35DC08, at the step's
end): v += ((a − x) 200 − v 20) / 60, x += v / 60 (0x3C888888).

### 5.4 The outputs (0x464F38)

T[0] = 0: in modes 1, 2 the throttle pedal F+0x488 (and F+0x494) slews to D+0x34 by dt, else takes it;
the steering command F+0x468 = D+0x30; the brake (0x343D60), the reverse, the gearbox's lock.

### 5.5 The end of a run (the conductor's virtual 21, 0x343D78)

A finished car (F+0x4B2) not on the pit lane (kinds 2 .. 4) by the race's mode (the configuration's
+0xCBDE when 1 and no mode is asked): 0 the computer drives on (kind 1, 0x3452F0: a pad car's too;
off line 7 at once), 1 the stop driver (kind 6, 0x345520), 2 braking to a stop (kind 7, 0x345598),
3 cruising (kind 5, 0x3454A8); 1 and 3 leave line 7 for 6 at once. 0x464C38 sets the kind: kind 6, or 5
after another than 4, count D+0x52 = 120.

- **The stop driver** (0x464C80): the begin, the steering slewed to its wheel, no throttle; under
  0.25 km/h and 3°/s (|v| over x, y and z; the yaw, roll and pitch rates) the brake off and kind 8
  (0x3455C8, but for control 4); else kind 6 holds a speed 1 km/h under its own (0x3F76E8) through its
  count, over the last 30 steps x count / 30 with the brake (1 − count / 30) 0.8, 0.8 after; any other
  kind brakes at 0.5.
- **Cruising** (kind 5): the throttle at most what holds the configuration's limit (+0x42 for the
  player's car, +0x43 for the computer's; above 80 none) or 80 km/h; through the count without a limit
  a speed 1 km/h under its own, x count / 30 over the last 30 steps.
- **Holding a speed** (0x3F76E8): (F+0x5BC + friction x the gear's speed factor) / ((full torque +
  friction) x the factor) − (v − (v_t + 0.5 km/h)) / 5 km/h, in [0, 1] (1 without torque; v negated in
  reverse). F+0x5BC = −((rolling + drag) − ground forward z x mass x 9.8), the road's load (0x357700).

## 6. Traffic

✔ In the program's arithmetic, equal at every call (`gt4r_diff ai`, "proximity", and the decision's
traffic part).

**The pairs pass** (0x3F4638, every car with every later one): each car's contact cleared, its zone
cells, the threat record and the side gaps (1,000) reset (0x4694A0); a pair counts when both race
(not a ghost F+0x441, not on line 4), both are on or both off the pit lane (F+0x4B1 bit 4), within 100 m
(over x, y and height) and a body's height + 5 m in height. Each on the other's axes: u along the
forward axis, v along the side axis (3D dot products); w = (wA + wB) / 2, the ahead and behind extents
front + rear of the two.

**Zone cells** (0x4694F8: bytes +0x48 front, +0x49 front left, +0x4A front right, +0x4B left, +0x4C
right, +0x4D rear left, +0x4E rear right, +0x4F rear; "≥2": raised to 2, "≥1": set to 1 where not
above 0), within 20 m and |v| ≤ w + 3: ahead (u > 0.9 ahead), the gap g = u − ahead: |v| > 0.75 w: the
front left / right ≥2 under 5 m, ≥1 under 20 m; else the front ≥2 under 20 m and the side cell past w / 2
likewise; behind (u < −0.9 behind): the rear left / right ≥2 past ±w, else the rear ≥2 and the side it
leans to ≥1; alongside: the left (v < 0) or right ≥2, the side gap −v − w (or v − w) kept at its least,
and the front side ≥1 past half the ahead extent, else the rear side ≥1 past half the behind extent.

**Closing** (0x3F42D0, unless the race's configuration +0x02 is 6): the gap along the course (a lap
added across the line from 3/4 to 1/4 of it) less B's rear and A's front; within 20 m only overlapping
sideways (|v| ≤ (wA + wB) / 2); the closing speed is the difference of the cars' speeds along the course
(F+0x650: the course distance's change x 60, 0x358608). Else a car followed is let go.

**Contact** (0x469918): a car followed lets go past 6 m; a new car closing within 5 m: 2 and followed
(the contact 1); closing over 20 km/h: 1; over 0.1 km/h and within 5 s: 0.5; else 0.05 under 0.5 m, 0.1
under 4 m; kept at the most.

**The follow count** D+0x6A: the length of a car's follow chain (the car it follows, the one that one
follows, ..; at most the field) adds that many metres to its braking (§4.3); with a contact outside the
cooldown + 10 m, and + 8 m more when the car followed is itself in contact.

**Lane changes** (0x469AE8, from 5 s + 0.1 s x the place): the cooldown counts down; line 4 none;
line 3: none, and with a car alongside and ahead on a side below 30 km/h the cooldown + 1, else the
throttle halved; the status 1, 2, 5: none; 3, 4 on a passing line: only the return; a blend: none; a
B-spec driver without passing (D+0x6E): only the return. With a contact (none in the cooldown) the
farthest line on a side (0x46BC80 / 0x46BC98 twice): the next arc to the left takes the left, else the
right unless status 4; to the right the reverse; a straight the left unless status 3, else the right
unless status 4; a change (0x463C28 blended) sets the cooldown 180 and the contact 0. The throttle x
(1 − contact). With nothing at the front, the sides and their fronts a car returns to line 6.

**A line change** (0x463C28): the request's fallback resolved (none: no change); at once (0x463C00):
both lines the new one, the offset and the blend 0; else the blend 1, or 1 − blend when the request
names the line being left, the old line kept for it; then the re-plan (also for the line the car is on).

## 7. Pace

- The **menus' difficulty** picks the event (the car's Arcade rows, `ARCADEINFO_NORMAL`
  [difficulty]); `enemy_level` only steers which opponents the scripts pick (arcade.pp: the
  pick's offset (level - 2.5) / 100 around the player's car); neither changes a driver.
- The **events' percents** (§3.3) are the per-event pace: ✔ e.g. the amateur FF events brake at
  85 % and take the arcs at 90 %.
- The **rubber band** (0x365790 with 0x365910, 0x365688 / 0x365718; set from the event row by
  0x365480: percents x 0.01, the start gaps in metres, the full gaps x 10 m, the overall percent (0: 100)
  x 0.01): the reference is the first car of the order the computer does not drive (none: car 0); in
  mode 0 from it outwards along the order each car against the car between it and the reference, the
  factors chained (mode 1: every car against the reference; mode 2: each against its neighbour, not
  chained): the reference's lead g in whole laps and metres (a lap taken over where they disagree in
  sign); behind (laps > 0 or g > 0): 1 + fast, ramped (g − start) / (full − start) past the start, full
  past the full gap or a lap (none on the pit lane); ahead: 1 − slow likewise; a finished car 1; x the
  chain's factor, at most 1.3 (0x3FA66666); the car's time scale F+0x444 = that x the overall factor.
  ✔ Equal to the program's for every car at every step (`gt4r_diff ai GT4R_AI_BAND=1 GT4R_AI_CHECK=1`,
  the conductor's own values: 69,960 compared). ✔ Only the 12 license-test events of the US disc carry
  it (slowing by 50 %); no race does.
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
start's long look ahead by them (5 s + delay); the conductor's virtual 41 (0x34DA58) answers whether
place 0 has one. 🔎 The cars' held start itself is not found: ours holds such a car on the line (the
count's engine hold, the brake on) until the race's clock passes its seconds (judged).

**The demonstration runs** (a license test's "View a demo run"): recorded, not computed. The disc keeps one per test
and region under `replay/license/<event label>`, the program's `Serialize::ReplayData`: the race's header (the
entries' cars, places, gearboxes and aids) and the drivers' pad records, coded and enciphered
([formats/replay.md](../formats/replay.md)). The race runs them again: car 0 takes the recorded record every step
from the race's first, then the empty one; the drivers do not drive a demonstration (a training test's pace car is the
computer's as in the test). `sim::ai::DemoRun` (`demo_driver.h`) gives the entries and car 0's input per step. ✔ The
US B-1 recording on the program's own race step and on our session runs the same to the millimetre, the car at rest in
the stopping box at step 1083 (§10).

**The reference race** (03-race, Arcade, Ford Ka '01 at Tsukuba): the Ka has no Arcade event rows,
so the race is `sample_entry_6` (every percent 100, no rubber band) with opponents the menu's
script picks by power (`PickUpEnemyCars`) and enters by their car labels: ✔ in the saved states
their equipment records equal our cars as sold byte for byte (+0x08 -1), the arcade's tire class
at +0x160 / +0x161 (4: sports medium, applied by RaceArcade::vf92 when +0x165 is set) their own.
The drivers' parameter blocks in those states equal ours (the yaw spread 2.06 / 1.94 / 2.0 by grid
index, multipliers 1.0).

**B-spec** (the player's car with a computer driver: control source 1). ✔ Equal to the program's at every
decision with a pace command midway (`gt4r_diff ai GT4R_AI_BSPEC=1,4,20`).

- **Its pace levels** (0x3F1790 at boot, from the bytes 0x622088 .. 0x6220A4; five levels): ✔ the
  shift byte (s x 128 + 12,800) / 100 = 115, 121, 128, 129, 129; the braking factor D+0x70 = 0.90, 0.95,
  1.00, 1.05, 1.10; the arcs' factor D+0x74 the same; a fourth (0x6220E0: 0.8 .. 1.2) ❓ its use. Its
  setup (0x3F1890): level 0x622080 (✔ 2) for a pad or B-spec car, passing off; 0x622081 (✔ 2) for a
  computer car, passing on (off where the conductor's virtual at +0x30 says so); the shift byte 128 where
  F+0x4CC ❓ is not above 0.
- **Its commands** (0x3F19E0, from the race's per-car step 0x349FD0): 4 the level (the three values
  above), 5 passing (turned off with kind 0 on a passing line: back to line 6, blended), 6 the pit
  (F+0x4B1 bit 3), 10 the control source. The driver keeps them until the next.
- **In the drivers**: the arcs' scale x D+0x74 (after the spread) and the braking factor x D+0x70 for a
  B-spec driver of kind 0; its draws seeded with 1, no spread by grid index (0x467148: computer cars
  only); without passing a lane change is only the return to line 6.
- **Its shift speeds** (0x36BEE0 / 0x36BF28 → 0x3F1E30): the speed x the car's byte +0x83 >> 7 (the driver's shift byte
  copied to the car): the up-shift speeds for control source 1 and kind 0, the down-shift speeds for control source 1; the
  opponents shift at the gearbox's own. In the session's gearbox (sim-parts' `rules_drive`) by
  `Field::apply_shift_factors(session)` (each driven car's byte, `Session::set_shift_factor`). ❓ The session still runs a
  computer-driven car as control source 2, the B-spec car's too, so its gearbox does not scale yet (asked of sim-step: the
  control source through `CarInput`).
- **Its skill** (0x468C50 from the conductor's virtual 28, for a car of control source 0 or 1 not a ghost, with the switch
  0x623E48 set): the race information's +0x20 .. +0x5F (the B-spec driver's record of its races, from the save: u16 a[8]
  +0x00, b[8] +0x10, c[8] +0x20 by the cars' class, d[4] +0x30 and e[4] +0x38 by half the class) copied to car +0x8C. The
  car's class k (0x468458): its parameter block's +0xFE0 (0x3F86E8 at the conversion), (x − 0.35) / 0.55 within [0, 1],
  x 8, at most 7. Over the classes [max(0, k − 4), min(8, k + 5)) (0x467BE8) with the weights 0x623E40 (8 .. 15): A = Σ
  min(a, 1024) w x 700 / Σ 1024 w, B likewise from b, C = Σ min(c, 256) w x 700 / Σ 256 w (each at most 700); with the
  race's byte cond + 0xCBE7 set (0x468A40 ❓ what it marks) D = d[k / 2] >> 8, E = e[k / 2] >> 8, else 255; D' = D x
  300 / 255, E' likewise (at most 300). Then (0x468BC8: p / 100 + (q / 100) x (x / 1000), the bytes 0x623E4A ..: 90 / 10,
  90 / 10, 130 / −30): D +0xB8 = f(90, 10, A + D'), D +0xBC = f(90, 10, B + D'), D +0xC0 = f(130, −30, C + E'). So a
  driver with no record takes the arcs at 90 % and brakes at 90 %, a full one at 100 %. ✔ Equal to the program's
  (`gt4r_diff <disc> bspec`: 320 made-up records on five cars of classes 0 .. 7, the byte both ways; test
  `ai_bspec_skill_equal_to_the_program`); `Field::set_bspec_skill` sets it. ❓ Not wired: the save's record (the menus)
  and the car's +0xFE0 (asked of sim-parts).

## 9. What our drivers run

All of the drivers' arithmetic above runs in the program's (`gt3r::ee::F`): the parameters, the tuning
block, the arcs' speeds, the braking, the decision, the traffic, the rubber band, B-spec and the end of a
run. Per step: the order by laps and distance (or the cars' own places), the cars' filtered lateral
accelerations and speeds along the course, the rubber band's scales, the pairs pass, then per driven car
its contact flags, the decision and the outputs. The cars are read through `ai::Cars` (a session's, or
another race's for checks: then its own F+0x560, F+0x650, places, clock and finished flags). Departures:

- **The outputs** go to the session as the program's 0x464F38 writes them (`race::CarInput::computer`:
  throttle and brake pedals, reverse, the gearbox's lock, the clutch engaged; the pad's path not
  run), the steering as the road-wheel command (`steer_angle`); the time scales by
  `Field::apply_time_scales` (`Session::set_time_scale_raw`).
- **The collision flags**: from the cars when they give them (the program's own in `gt4r_diff`), else
  a wall within the body's ends or a car overlapping.
- **The road's heights** come from our road query in host floats: some line points' heights, slopes
  and banks differ from the program's in their last bits, and so then do the arcs' speeds there (and
  the lines' lengths and pitches, which the drivers do not read).
- **The pits** (§11): the program's pass, request, service and the stop's timing, `PitStops` on the session; the car
  stands in its box by the brake (no placing in the box without the session's hook), tires and fuel through hooks.
- Not done: B-spec's
  skill, the line-5 records, the aero bonus (P +0x1077 / +0x1084 not mirrored:
  0), the two ❓ flags of §4.2 (P +0x104A, F +0x5C2: 0 on every car checked), the arcs' grip with tire
  wear on, T+0x158's factor (1 throughout in this program), the threat
  record (D+0x53 / +0x60 / +0x64). The B-spec shift factor (`Field::shift_factor`) is not applied to the
  session's gearbox.
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
speed (0x4674F0) every 2 s, the braking routine (0x463EA8) every 4 steps, a line's place (0x46B480)
every second, the math routines, and **at every call** the decision (0x465078: its outputs and every
state byte, ours from the driver object and the cars as they are at the call's entry) and the pairs pass
(0x3F4638), against ours, bit for bit; with `GT4R_AI_BAND=1` every car's time scale every step.
`GT4R_AI_BSPEC=<car>[,<level>,<s>]`: that car's driver B-spec's in both races, a pace command at <s>;
`GT4R_AI_FINISH=<mode>,<s>`: every run ended so at <s>; `GT4R_AI_SECONDS=<s>` a set length;
`GT4R_AI_DECISION=1` prints the first decisions that differ. `gt4r_diff <disc> demo <label>`: a demonstration's
recording played on the program's race step (car 0's distance and speed each step); `gt4tool ai <disc> demo-run
<label> [hold=480]` the same on our session. Results at Tsukuba (US disc):

- ✔ Parameters, braking (multiplier, status, mode, point), the lines' distances, kinds, surfaces,
  crests, positions, radii and headings equal at every call; the arcs' speeds equal but where a point's
  road height differs in its last bits (§9).
- ✔ The decision: 76,872 calls in a lap of six cars, every output and state equal (the arcs' speeds
  v_cur / v_next differing in 2 / 92 calls by the line heights); the pairs pass's zones, gaps,
  contacts, follows and counts at every call; the same at Grand Canyon (177,726 calls), with a B-spec
  driver, and through each of the four ends of a run (kinds 1, 5, 6, 7, 8).
- ✔ **Our drivers on the program's physics lap as the program's drivers** (`GT4R_AI_OURS=1`, three
  laps from a standing start, totals; the program's / ours): Beetle 4'33.17 / 4'32.72, Z 4'56.75 /
  4'57.77, N360 5'00.52 / 5'00.17, Midget II 4'35.10 / 4'34.98, S500 4'27.43 / 4'27.57, Ka 4'09.97 /
  4'10.52 (the arcs' speeds at a few points apart, the races part within the first lap). Grand Canyon:
  Beetle 11'39.82 / 11'39.75, Midget 11'35.38 / 11'35.53, Ka 11'39.83 / 11'40.02; the Z, N360 and S500
  crawl and stop on the climbs in both (90 .. 160 s under 5 km/h in three laps). The program's
  times reproduce the reference race's gaps (03-race: N360 and Z some 17 s behind the Beetle in two
  laps, the S500 stuck behind the Beetle).
- **On our own simulation** the same field laps Beetle 92.6, Z 93.6, N360 97.6, Midget 93.6, S500
  91.8, Ka 88.3 (two laps from a rolling start: Beetle 3'07.9 against the reference's 3'04.5, N360
  3'21.6 against 3'21.4, Z 3'14.3 against 3'22.1): the remaining differences are the simulation's
  (the S500 alone: 89.2 s on the program's physics, 92 .. 97 s on ours, lost in the slow corners,
  where our car's least speeds are 8 .. 18 km/h lower).
- ✔ Six small cars finish three laps of Tsukuba (circuit), Seattle (city) and Grand Canyon (dirt)
  with no car standing still.

## 11. The pits

A stop is the race's own (`sim/ai/pit_flow.*`, the program's routines; `sim/ai/pit_stops.*` on our session). The car's pit
record is in its state block F: +0x4B1 flags (bit 0 the line crossed this step, 2 service wanted, 3 the pits asked
for, 4 on the pit lane: the ground's flag), +0x4B2 ❓, +0x4B7 its box, +0x4B8 the service (0x4 / 0x8 tires / fuel
wanted, 0x10 the player's choice asked, 0x20 the hose on, 0x40 / 0x80 front / rear tires, 0x100 refuelling, 0x200 the
change done), +0x4BA the hold counter, +0x5C2 a tire past its wear, +0x5C3 the pit lane's speed (1 over 10 km/h, 2 over
40 km/h), +0x5FA / +0x5FB the pit message and its time, +0x62E a pad car's own visit, +0x64C a B-spec car's wait.

- **On the pit lane** (F +0x4B1 bit 4, phase 18 0x369888): any wheel on the ground whose road triangle has its flags
  bit 0 set, in a race with pit stops (the conductor's virtual +0x180).
- **The box** (0x363B10 by 0x46BDA0): the stop line's (line 4) point 8 less the car's grid place (0 without lines).
- **The request** (0x353218, the wear phase, every step, with the race's pit stops on, the car not asked yet, F +0x4B2
  0 and racing, 0x343C20 = 0): fuel counted and over 500 m since the last fill, before the stop line's point 1: the fuel
  to reach it a lap later ((point 1 − d) + lap) / (metres per unit since the fill) x 1.05 more than the fuel left; or the
  wear switch on and a wheel's tread (+0x50) under the race's limit (+0xAC < 0: the tire gone, F +0x5C2 only); or
  (0x35FA50 = 4 ❓) a wheel's +0x1AB. Then bit 2, and bit 3 for a computer's car or a B-spec car (F +0x5C1 0).
- **The pass** (0x359008 after the per-car phases, none during the start's count): the line crossed (0x358608) sets
  bit 0 and the message 1 (moving over 5 km/h, not touched, a pad car racing) or 0 (under 3 km/h); not crossed clears
  it, message 0. Then 0x35B060 on the course distance d and the last d' (each a lap on in the lap's first half), with
  the stop line's points m(k) (k < 0 from the end) and its box b:
  - a car of kind 2 (down the lane): out of the lane past m(1) back to racing (message 0); crossing m(1) the stop is
    counted (0x35AD10) and its service decided (0x35AF18); crossing b − 25 m: onto the stop line (kind 3), or on through
    with message 8 (its race over), 9 (no service) or 7 (100 km/h or more); past m(2) message 10.
  - kind 3 (on the stop line): crossing b with a service: stood in the box (0x363560) and held 721 steps (12 s); the
    next step turned to the box's heading, message 11; at the hold's last step the service cleared; held at 645 the hose
    on; from 554 down a step of refuelling each step (0x354D60); with no service past b + 0.5 m off the stop line (kind 4).
  - kind 4 (leaving): over the lane's end (m(−1), or m(−2) for a pad car) back to racing (0x345228) and the order
    recounted (0x35ADF0), or the run ended there (F +0x4B2: 0x343D78 mode 3); before it messages 4 / 5 / 10.
  - a computer's car asked (bit 3), its race not over: onto the pit lane (kind 2) crossing m(0); on the lane touching
    another, put back to kind 2 and its driver told (0x463E80). A pad car's own visit (F +0x62E) the same from m(0) / m(1).
  - over the line and in the lane's first stretch (m(3) .. m(−2)) with the hold over: the speed message (F +0x5C3), the
    car backing out past m(−3) off the stop line (message 12), moving over 5 km/h message 2 unless touching.
- **The service** (0x35AF18, the race's pit byte cond + 0xCBE0 = 1, not a pad visit, the race not over, not a layout of
  kind 4): tires (4), with fuel counted fuel (8), the conductor's virtual +0x198 bit 1 ❓; a computer's car (or a race
  without the pit menu) gets them at once (0xC0 both axles' tires, 0x100 refuelling), a player's with the menu bit 0
  (0x35BFE8 then takes the menu's choice: 0x40 / 0x80 / 0x100 and the tires at F +0x64E / +0x64F).
- **The stop's timing** (0x35C0B8, phase 4 while the hold runs): held at 721 while the player chooses (a B-spec car's
  wait F +0x64C counted down, then the choice made for him ❓), held at 90 while the fuel still runs in; else counted
  down; chosen tires fitted at 645 / 644 (the spec and parameters rebuilt); at 60 each wheel of an axle served given a
  new tire (0x362E58: a layout byte > 0 the front, < 0 the rear, 0 either) and the tire bits cleared; at 59 0x200.
- **The choice** (0x35BFE8 in the input phase, from the controller's frame; command 1 with the menu asked, bit 0x10): the
  front / rear tires (0xFF none, 0xFE the kind fitted) into 0x40 / 0x80 and F +0x64E / +0x64F, fuel (not 0) into 0x100
  when wanted, the asked bits 0x1C cleared, the hold ended when nothing is left; command 2 stops the refuelling. A B-spec
  car's wait (F +0x64C: 300 steps set as it stands in its box) runs out into 0x35BF08's choice for it: both tires as
  fitted, fuel. `PitStops::choose` for the player's menu.
- **Refuelling** (0x354D60): the fuel + the byte 0x620E25 x 0.1 a second x the step, up to the tank; full: the bit
  cleared and, with no tires left, the hold set to 90 (1.5 s more).
- **The race's records** (0x35AD10 when the stop is counted): the stop count (cond + 0xD9E4 + 2 i), a record of the
  last four stops shifted along (cond + 0xD9F0 + 8 i: the lap, 0x343B50: the car's lap, less one with F +0x4B2, at least
  1; cond + 0xDA20 + 8 i: the tires, −1, then at the stop's 645th step from its end the tires fitted: the car's own kind
  (cond + 0xFB20 + 2 i) for a choice of 0xFE, the kind chosen); and the race display's event 1 (0x35E000); out of the lane
  (0x35ADF0) its event 6 (not for a car whose +0x56A is 1 or 2 ❓). `PitStops::stops`, `stop_record`, `PitHooks::display_event`.
- **The race state** (0x343C20): 0 when the car's result is in (F +0x4B0); 1 once the race's time limit (cond + 0xF850,
  minutes) has run (cond + 0xF898 / 3 ms); 0 with no laps (the race information's +0, the conductor's virtual 37) or before
  the car's laps (F +0x4AC) reach them; then 1 past half the lap, 2 before it.
- **The messages** (0x35ACD8): a code with a time in the table at 0x6207E4 (6 .. 9 and 12: 120 steps, 13, 14: 255)
  always replaces the message; one without only once the last one's time has run out.

✔ The pass, the request's flags, the service and the stop's timing equal the program's at every call in its own race
(`gt4r_diff <disc> ai <course> GT4R_AI_PITCHECK=1 GT4R_AI_PIT=<car>,<s>`: the car's pit request set at that time; the
record, messages, the routines called and the wheels given new tires compared): Tsukuba, Seattle and Suzuka East, a
whole stop each (24,000 .. 36,000 calls, 721 timing calls, all equal). The program's stop at Tsukuba (an S500 sent in at
20 s): the pit lane at 1,365 m, the stop counted at its lane's start, on the stop line, standing 12 s at 58 m, the four
tires at the stop's last second, out and back on the racing line at 270 m. ✔ On our session (`PitStops`, test
`ai_pits_on_our_session`) the same car runs the same stop: down the lane, 721 steps held (649 standing), out again.
✔ **Against the original's own stop** (the reference's Family Car Cup at Tsukuba, B-spec R34, `26-endurance/fc_pitwindow_*`,
the car's record in each state): onto the pit lane between 1,342 and 1,508 m (kind 2), the service decided at the lane's
start as 0x1C (tires and fuel wanted, the menu asked; 0x35AF18 itself returns the wants | 1 ❓ what makes it 0x10), the
stop at 58.3 m with the hold at 721 while the B-spec wait counts down (228 left 72 steps in), the choice made for it
(0x1C0, tires 0xFE / 0xFE), the hold then counted down 70 / 300 / 302 steps between states 5 s apart, the fuel full
(78.97 → 80.00) before the tires, kind 4 out, kind 0 at 270 m: as ours (the 300-step wait, 721 more; test
`ai_pits_bspec_stop_as_the_reference`, service 0x10 | the wants for a car with the menu, judged from the state). ❓ The
change bit 0x200 reads 0 at the hold's 49 in the original's B-spec stop (set at 59 in the program's computer stops).
✔ The request (0x353218's pit part) equal at every call with the race's wear on (`GT4R_AI_WEAR=<limit>`: the switch and
limit set; at 1.5, above a new tire's, the cars ask, go in and get new tires: 24,000 calls, 3 requests, 2 whole stops, all
equal; test `ai_pits_asked_by_the_wear`). ✔ The race state equal at every call too (23,814 .. 32,371 calls with the race's laps set). On our session the pit lane
flag, the hits (the collision body's +0x43C a wall, +0x440 a car) and the race state are the program's rules on the
session's values (the car's laps as the race flow counts them, the session's clock). ❓ Not wired: the pad car's own
visit and the pit menu (0x35BFE8); the car stood in its box (asked of sim-step: braked to a stand meanwhile). The fuel and
wear come from the session (`session_pit_hooks`: the tank above 0 counts the fuel, the economy readings, the wear rules'
switch +0x68 and limit +0x20, each wheel's wear grip +0x50 and tread +0xAC, `Session::new_tire`, `set_fuel`).

## What runs where

Every routine of the drivers runs the program's own code (ported, checked at every call where marked ✔ above), none of the
shared engine's GT3 drivers. What is still not the program's:

| Where | What | The program's routine |
|---|---|---|
| `course_lines.cpp` `prepare` | the road's heights under the lines in host floats (last bits) | the road query 0x3E5A30 in EE floats (sim's `RoadGround`) |
| `drivers.cpp` the start's hold for missions 25–34 | judged: held on the line until the race clock passes the delay | not found |
| `grid.cpp` the paint draw | our generator | the race's generator 0x621AE8 |
| `pit_stops.cpp` | the car held in its box by braking, not placed | 0x363560 and phase 4's hold (sim-step) |
| `demo_driver.cpp` `pad_input` | the recorded pad record mapped onto `CarInput` (steer, pedals, shift) until the session reads the record itself | the input phase 0x349FD0 on the record (sim-step) |
| `Field::shift_factor` | not applied in the session's gearbox | 0x3F1E30 (control source 1) |
| B-spec skill | ported, not wired: the save's record and the car's P +0xFE0 | 0x468C50 (✔), 0x3F86E8 (sim-parts) |
| the threat record | not ported | D +0x53 / +0x60 / +0x64 |
