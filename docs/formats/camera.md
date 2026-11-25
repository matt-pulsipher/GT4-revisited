# Cameras

Code: `src/gt4r/race/drive_camera.*` (the driving views and the mounts), `src/gt4r/race/replay_script.*`
(the replay script and the camera that plays it: the place, the target or the car, the lens, the
picture offsets and the roll, the record search; the scenic shots on time; not the operator's
filter, the lens's spring, the roadside points or the marks), `src/gt4r/race/start_camera.*`
(the start's shots). Facts from the program (`CORE.GT4`, SCUS-97328 v1.01;
addresses are the program's) and from the disc's cars and courses. The camera's own angles are
degrees; a car's attitude angles are radians. 🔎 partly understood, ❓ not investigated; the
rest is read from the program and, where data is involved, checked on the disc as stated. The shared engine's counterparts: [RACE.md](../../engine/gt3r/docs/dev/RACE.md)
("Cameras", "Replay (TV) cameras"), `engine/gt3r/src/race/camera_rig.*`, `replay_cameras.*`,
`course_camera_aim.*`.

## The camera system

A race has one camera manager with six screens (one in use; two in a split-screen race). Each
screen owns one camera of every kind and remembers its driving view. The kinds, in the manager's
order:

| # | Kind | Use |
|---|---|---|
| 0 | on board | the player's driving views; any camera mount of a car |
| 1 | designer | a record of the course's `REP4` script (below) |
| 2 | finish | the shot after the finish (field of view 65, table at 0x6211F4) |
| 3 | path animation | a `Mot1` motion: the start sequence |
| 4, 5 | pit in, interrupt | pit stop shots |
| 6 | point | 🔎 a roadside point of the script's second list |
| 7 | auto fix | 🔎 |
| 8 | shot | 🔎 |
| 9 | ride on | on board through two tables of mounts (0x621200: mounts 0, 1, 20, 7, 5 looking ahead; 0x621228: 0, 1, 20 looking back, then 8, 9, 11, 10) |
| 10 | round | 🔎 circling the car |
| 11 | follow | 🔎 trailing a car, with the other cars within 100 m in mind |
| 12 | heli | 🔎 from above |
| 13 | grid | 🔎 the starting grid |

Every 1/60 s the manager runs the screen's mode (nine modes: 0 is the race the player drives),
which picks a camera, runs its control step (the pad) and its update. A camera holds a field of
view angle in degrees (70 when made), a focus distance (the on-board views: 100 m), an eye
matrix, and scale factors for the near and far planes.

**The picture from the angle**: the projection is the usual perspective matrix with
`1 / tan(angle / 2)` on both x and y, times the screen's own 4x4 matrix (identity unless set)
and, for cameras that ask for it, the screen's scale (1 unless set). No call that sets the
screen's matrix or scale during a race was found, only photo mode's (aspect by its "photo
aspect" option: the matrix's x is 1 / aspect for aspect >= 1, else its y is the aspect).
🔎 So how the square frustum of angle A meets the 4:3 or 16:9 picture (which side spans A) is
decided below the camera system and has not been read. The shared engine's convention (the
angle spans the 4:3 picture's width; a 16:9 picture shows more at the sides) is the working
assumption for every angle in this file.

**Near and far**: the screen's defaults are near 0.1 m and far 750 m (set at 0x3FB48C; no
other writer found). The on-board camera uses them as they are; every other camera multiplies
the near plane by tan(35 degrees) / tan(angle / 2) (0.700208 at 0x3794FC), so a telephoto shot's
near plane moves away. 🔎 Whether the course's `Env0` far clip ([course.md](course.md))
replaces 750 m when the scene is drawn has not been read.

## Camera mounts of a car

A car file's info block (`CAR4` +0x10, [model.md](model.md)) lists the car's camera mounts:
`info +0x40 u32 count` (25 on all 256 cars checked), `info +0x44 u32 -> records` (from the
file's start), 0x20 bytes each:

```
0x00 f32 x, y, z     where the car's origin is, seen from the camera before the camera's own turn:
                     x right, y up, z towards the viewer. So the eye is -y metres above the car's
                     origin and -z metres ahead of it (z = -6: 6 m behind).
0x0C f32 pitch       degrees; the camera's own turn at its place. A positive pitch looks down
                     (the chase place's 8; mount 19, ahead of the car at yaw 180, looks back
                     down at it: seen in a picture with the sign either way)
0x10 f32 yaw         degrees (180: looking back)
0x14 f32 roll        degrees
0x18 f32 zoom        1 / tan(half the field of view); +infinity in an unused record
0x1C u32 flags       bit 1: the car's own body is drawn (clear: it is hidden);
                     bit 2: no speed wobble; bits 0, 3 ❓
```

The car's origin is the model's: on the ground under the car's middle (the program moves the
body's reference point down its own up axis by the car's reference height).

| Mount | Flags | Zoom | What it is |
|---|---|---|---|
| 0 | 5 | 2.0503 (52 degrees) | the in-car view: on the centre line at about head height (AC 427: 0.94 m up, 0.52 m behind the origin; a pickup 1.29 m up) |
| 1 | 2 | 2.699 (40.7 degrees) | the chase view: (0, -1.7, -6), pitch 8, on 249 of 256 cars; up to (0, -2.0, -6.5) on tall ones |
| 2, 12, 13 | 0 | | unused on nearly every car |
| 3, 4 | 7 | 2.0503 | beside the cabin looking back along the body (yaw about 160 to 190) |
| 5 / 9 | 5 | 2.4142 (45) | front bumper looking ahead / rear bumper looking back, 0.6 m up |
| 6 | 0xF | 2.0503 | the roof view: on the centre line above the roof (AC 427: 1.25 m up; a pickup 1.80 m) |
| 7 / 8 | 7 | 1.7321 (60) | above the roof looking down ahead / back |
| 10, 11 | 5 | 1.7321 | mount 0's place looking left / right (yaw -90 / 90, pitch 3) |
| 14-17 | 3 | 2.4142 | beside the four wheels |
| 18, 19 | 3 | 1.7321 mostly | outside the car, behind and ahead of it |
| 20-24 | 0 | | on some cars only: shots of their own |

## The driving views

The change-view button steps through a table of three mount numbers, 0x6211E8: **0, 1, 6** — the
in-car view, the chase view, the roof view, and round again. (The program steps through the
table as if it had 12 entries; whatever lies past the third is not a mount the car has and
falls back to view 0, which is the wrap.) The manager keeps the view per screen (❓ whether
and where it is saved between races). There is one chase view: no near / far choice.

Common to the three: the car's position, heading, pitch and roll are the body's, blended
between the last two physics steps for the frame being drawn.

**Look back** (a button of the race pad; the game's key names are `BUTTON_BACKVIEW`,
`BUTTON_LEFTVIEW`, `BUTTON_RIGHTVIEW`) is held, not toggled.

**Field of view** 🔎: views 0 and 1 do not read their mount's zoom. They take the on-board
camera's own angle, which is 90 when the camera is made (0x379C1C) and has one setter (the
manager's, for all six screens at once) for which no caller was found; a special case (the
shot at the finish) switches to 75 or 100 with the screen's scale applied. The roof view, as
every other mount, uses its mount: 2 atan(1 / zoom) = 52 degrees, times the screen's scale.
Mounts 0 and 6 carry the same zoom on every car, which suggests views 0 and 6 are meant to
match. **The on-board lens as the original shows it: 61.2 degrees across a 4:3 picture's width**
(47.9 degrees high). Two measures agree: the view's size factor K (0x456918: one unit's height
at depth 1 on screen x 10 / 7; kept at 0x8465E4) is 721.2 in a saved state of the original's
chase view, and for a picture 448 lines high K = 224 / (0.75 tan(A / 2)) x 10 / 7, so
tan(A / 2) = 0.5916; and the cars in the original's chase pictures are as wide as this lens
draws them (the Ka in 20-race-chase, the R390 in 25-sound-cars: about 300 of 640 pixels for its
2 m). The other states' K (121.4 a wide start shot, 1851.6 a replay shot, 3905.6 a telephoto
opening pass) give those lenses the same way: 2 atan(426.7 / K). 🔎 How the program's 90 becomes
this (the screen's scale) has not been read; the in-car view is taken to use the same lens.

### Chase view (view 1)

The eye rides a frame at the car's origin that has the car's **lagged heading and lagged
pitch and no roll** (the horizon stays level). In that frame the eye is at the mount's place
(6 m behind, 1.7 m up) and looks straight along the frame's forward axis: the mount's pitch
(8) and zoom are not used. There is no lag in position or distance: the eye stays exactly 6 m
from the origin whatever the car's acceleration, and nothing in the view reads the car's speed,
gear or direction of travel (reversing changes nothing).

The lag is kept per car, stepped once per physics step (h = 1/60 s) after the car has moved
(routine at 0x345658):

```
state: heading T, its rate w, the last acceleration a0; pitch P, its rate wp
reset (race start, a car placed anew; 0x3455F0): T = car heading, P = car pitch, the rest 0

e  = wrap(car heading - T)          a  = 120 e - 15 w        (120.0 at 0x69F4C8, 15.0 at 0x69F4D0)
w  = w + (a0 + a) / 2 x h           a0 = a
T  = wrap(T + w h)

ep = wrap(car pitch - P)            ap = 20 ep - 10 wp       (immediates 20.0, 10.0)
wp = wp + ap h
P  = wrap(P + wp h)
```

`wrap` brings an angle into -pi..pi. The view uses T and P (blended between the last two
steps like everything else). The heading spring is four times stiffer than the shared
engine's 30 / 10; the pitch spring is the same 20 / 10.

Looking back: heading + pi and the pitch negated, so the eye goes 6 m ahead of the car and
looks back at it.

### In-car view (view 0)

The car's own body is not drawn. The eye is at mount 0 in the body's frame (heading, pitch and
roll), looking ahead. On top of that, in the order they turn the view (first is nearest the
eye):

1. **Speed shake**: two shakers (pitch, yaw), as the shared engine's in-car shake. The
   amplitude is A = sum over the four wheels of 0.3 |slip ratio| + |slip angle| / (pi / 2);
   the speed factor S = (speed clamped to 2.7778..33.333 m/s) / 33.333. Each frame one random
   draw gates both shakers: under 0.5, each shaker's input is (a draw of its own - 0.5) x A x
   250 (the yaw shaker draws first), else 0. A shaker with input u: v += ((u - u_before) -
   12 v) / 60, x += v / 60, output x clamped to -1..1. The view pitches by 8 S x and yaws by
   2 S x degrees. (No cap on A was seen.)
2. **Impact kick**: when the car's record flags an impact, three turns (clamped to 90 degrees)
   and three shifts (clamped to 0.6, 0.1 and 0.3 m) are driven through springs
   x'' = 750 (u - x) - 17 x' from the impact's two components, scaled by random draws.
   🔎 The exact inputs.
3. **Looking sideways**: an angle that eases towards -60, 0 or +60 degrees by 0.2 of the
   difference per frame while a look-left / look-right button is held (0x37AF94); yaw about
   the eye.
4. the mount's place.
5. **Lean**: the view rolls by -15 x degrees, x following the car's sideways acceleration in g
   (clamped to 3 each way; 🔎 the car record's field) through a spring: the input may change by
   at most 8 / 60 a frame; v += (60 (u - x) - 20 v) / 60, v held within 35; x += v / 60.
6. the body's roll, pitch and heading. The pitch has **the nod** added: x, where
   d = body pitch - the road's pitch under the car (🔎 the car record's second pitch),
   clamped to 3.5 degrees each way; target = 3 d; x'' = 100 (target - x) - 20 x' (h = 1/60).
   The view pitches with the body on its springs up to 10.5 degrees more than the body
   itself: the dive under braking. (Bytes at 0x6202B0: on, 35 tenths of a
   degree, gain 30 tenths, 100, 20; the same set follows for roll, used by the car's drawing.)

Looking back: heading + pi, pitch and roll negated; the sideways angle stays.

### Roof view (view 6) and any other mount

The eye is at the mount in the body's frame (heading, pitch, roll: the view rolls with the
body), then turned by the mount's own yaw, pitch and roll. Field of view from the mount's zoom.
Mount 6 has flags bit 1 set: the car's body is drawn (its roof or bonnet shows). A mount
with flags bit 2 clear gets the speed wobble: two values that, with probability 0.3 a frame,
pick a new random target in -1..1 and move 0.4 x strength of the way to it each frame
(strength = (speed squared - 7.716) / 493.83 clamped to 0..1, speed in m/s); the view turns
0.7 degrees per unit in pitch and yaw, and the eye moves sideways and up by twice the output
(metres) of the two shakers, which outside the in-car view are fed by two values of the car
(🔎 half of one, twice the other: body movement). Mount 6 has bit 2 set: it is steady.
Looking back is defined for view 6 only: the mount's yaw + 180, its pitch and roll negated.

### The rear-view mirror 🔎

The original's default (nose) view shows it at the top of the picture, (190, 18) 259 x 63 of the
640 x 480 frame, with a thin light edge (measured on its in-car view); the chase view and the
higher view have none. The course has visibility lists, a sky and fog for it ([course.md](course.md),
slots 3 and 24, Env0 +0xA90; drawing step 9). Here (`RaceWorld::render_mirror`) it is the in-car
view seen backward and mirrored, the course by its mirror lists, sky and fog, the other cars, 120 m
deep, the in-car view's angle zoomed 1.3 times. ❓ The program's own mirror camera (its lens, depth
and place) was not read: the on-board camera publishes the view's turn (the sideways angle, + 180
while looking back).

## Replay cameras: `REP4` (course slots 44, 45, 46, 48)

The course loader also relocates a `REP4` in slot 41; no course on the US disc has one.
Checked on all 110 courses: every byte up to the block's stated size is the header, the
records, the curves or the two lists, apart from zero padding (8 bytes after the header in
slots 44 and 48, none elsewhere). Bytes between the stated size and the next section are
leftovers.

| Slot | What | On the disc |
|---|---|---|
| 44 | the course's replay script: records by distance along the course | 3 to 162 records; 6,386 in all |
| 45 | unused: a shot round the car (flies in from 30 m ahead and 2 m up to the car's nose) | one record, the same bytes on all 110 courses; the same path as `cameras/start.cam` |
| 46 | unused: shots round the car, cut at 0.8, 0.85, 0.9 of the way | one record; one version on 104 courses, own versions on Assisi, Grand Canyon, Tsukuba |
| 48 | the course's scenic shots, run on time: the scene behind the race's menus | 4 to 6 records, 475 in all |

There is one replay set per course (slot 44): its lap variants (below) are what changes from
lap to lap. **Slots 45 and 46 are not used by the program**: the loader relocates them
(0x3966D4, 0x3966F0) and `RaceCourse` has getters for them (0x5F73E8, 0x5F73F8), but nothing
calls those getters and no other code reads header +0xB4 / +0xB8. A race's start is the `Mot1`
motion of slot 52 (below).

### Layout

It is the shared engine's `GTRC` rearranged: the same curves, the same record search, a
0x7C-byte record. Offsets from the magic; pointers are offsets from the magic too.

```
header (0x40 bytes)
0x00 "REP4"            0x0C u32 size of the block
0x14 f32 lap length L (0 in slots 45, 46)
0x18 f32 script end T: the largest record end (the program works it out again when loading)
0x1C u32 lap variants (see below; 0 in slots 45, 46)
0x20 u32 records       0x24 u32 -> records (0x48 in slots 44 and 48, 0x40 in 45 and 46)
0x2C u32 points        0x30 u32 -> points, 0x30 bytes each     (slot 44 only)
0x34 u32 marks         0x38 u32 -> f32 distances, ascending     (slot 44 only)
0x04, 0x08, 0x10, 0x28, 0x3C: 0

record (0x7C bytes), sorted by start
0x00 u32 kind
0x04 f32 start, 0x08 f32 end: distances D along the script (slot 44); 0 and 1 (slots 45, 46);
     0 and the shot's length in seconds (slot 48)
0x0C f32 > 0: the camera's flag 0x2000 (227 records)
0x10 f32 > 0: flag 0x4000 (2,109 records: exactly those whose aperture is not 8)
0x14 f32 > 0: flag 0x8000 (57 records)
0x18 f32 🔎 0 on 5,034 records, else 0.1 to 1.0 (0.5 on every record of slot 48): by its
     values the seconds of dissolve at the cut; its reader was not found
0x1C f32 aperture as an f-number: 8, 2.8, 5.6, 1.4, 4 ...; under 1 reads as 8. Kept x 100 as the
     camera's aperture for the depth-of-field pass.
0x34, 0x38, 0x3C -> curves: the camera's x, y, z
0x40, 0x44, 0x48 -> curves: the look-at point's x, y, z
0x4C -> curve: the lens (see kind 0x100)
0x50, 0x54 -> curves: where the target sits in the picture, across and up, in half fields of
     view (0: the middle); in slots 45 and 46 empty curves
0x58 -> 0 in slots 44 and 48; an empty curve in 45, 46
0x5C f32 smallest field of view, 0x60 f32 largest (degrees: 0.1 or 1, and 100, on most)
0x64, 0x68 -> curves: 🔎 the operator's following of the car, across and up (1 on most; a value
     of 1 or less selects one mode of the aim filter, more the other); 0 in slots 45, 46
0x6C -> curve: roll in degrees (0 pointer: none)
0x70 -> curve ❓ (the same curve as 0x6C on 5,853 records of slot 44)
0x20-0x30, 0x74, 0x78: 0

curve: u32 n, then n x {f32 t, f32 value}; piecewise linear in t (0..1). On the disc n is 4
(a constant: the value four times) or 100 (a path) in slots 44 and 48.

point (0x30 bytes; 23,492 on the disc): u32 kind (0xC; 8 on 74), f32 x, y, z (beside the road,
every 20 m or so), f32 start, f32 end (a stretch of about 120 m around the point), three
words (0 on all but a few), f32 field of view (30; 90 on 55), two zero words.
```

**Kind**:

- `& 3`: 0 world coordinates (slots 44, 48), 1 the car's frame (slots 45, 46: kind 5).
- `& 0xC`: not 0: look at the car (its origin + 0.4 m up); 0: look at the look-at curves.
- `& 0x30` = 0x10: the record runs on time: t = seconds since the cut / its end field
  (all of slot 48). Otherwise t = (D - start) / (end - start), clamped to 0..1.
- `0x100`: the lens curve is the **width of the picture at the target in metres** (2 to 45
  on the disc, 5 on most): field of view = 2 atan(width / (2 x distance)). Without it the
  curve is the **field of view in degrees** (30 on most; down to 1 for the telephoto shots).
  Either way the result is held between the record's smallest and largest.
- `0x200`: a shot entered within its first 5 % (0x3FD940) aims at the look-at curves' point on
  its **first frame only**; from the second frame the target is the car and the aim's dampers
  swing the camera onto it. A shot entered past its half ignores 0x200.
- `0x400`: the roll curve at its full size (each other shot scales its roll by a random 0..1
  drawn at the cut, 0x376DE0); the scenic shots (camera mode 5) use the full size too.

On the disc (slot 44): 4 (4,709 records), 0x104 (705), 0 (538), 0x204 (361), 0x100, 0x304,
0x404, 0x200, 0x400 (a few each).

**The shot** each frame (`CameraDesigner::vf10` 0x37C138; `race/replay_script.cpp`
`ReplayCamera`): the camera's place from the three curves at t; the target (the car + 0.4 m or the
look-at curves); pitch = -atan2(dy, |dxz|), yaw = 180 - atan2(dx, dz) in degrees. The lens
angle F (clamped to the record's limits) goes through a spring (k 100, c 20; falling at most 150,
rising at most 100 degrees a second) and is used as F' = max(1, F / zoom). Then yaw -= across x F'
/ 2 and pitch += up x F' / 2, the two picture offsets (curves +0x50 / +0x54) each through a
spring (k 70, c 20, slew 2 a second, at most 150 a second); a car shot also lowers the aim by
0.15 F' cos(pitch) (the car sits below the middle, 0x36EBE8). **A car shot's yaw and pitch go
through the aim dampers** (0x384E58 / 0x384EE8): the target led by five frames of its motion
(error = wrap180(target - out) + 5 x its last step), v += (600 n error g - 60 v) / 60, out +=
v / 60, with g = the follow gain (curves +0x68 yaw, +0x64 pitch: 1 on most records, each
through a spring k 50, c 20, slew 1.5 a second, at most 100 a second) clamped to 0..1, and n = 1
when the gain is over 1, else 0.6 + 0.9 x a slow random hand (a value moving by a spring toward a
random target that changes now and then: 0x384C48). The roll (curve +0x6C) x a random share drawn
at the cut (1 with kind 0x400) through a spring (k 300, c 40) for a car shot. **At a cut** every
spring starts on its value and the dampers on their target: the shot starts exactly aimed. The
view: roll about z, pitch about x, yaw about y, then the eye. Focus on the target (its distance
+ 0.7 m when it is the car).

**Record +0x18**: the dissolve in seconds when the shot ends (`CameraDesigner::vf25`
0x37C9C8; 0.5 on every record of slot 48; 0 on most of slot 44's), handed up as the cut's
dissolve. Here the last picture shown before the cut is frozen and drawn over the new shot at a
weight falling from 1 to 0 over those seconds (the shared engine's dissolve pass); 🔎 the
program's own blend was not read.

**Which record** (0x3FD980 -> 0x396078 -> 0x3DDFF0): D at or past T wraps by the period; a
binary search finds the **last record with start <= D**, taken only if D < its end (records are
in the order of their starts, so an earlier record that still covers D is not found); past the
last record the search goes on from record 0 with D - period. No record: the on-board camera,
and the search runs again every frame. The record found plays until its own progress leaves
0..1 (`CameraDesigner::vf10` 0x37C138); then the next frame searches anew at the car's D. A
record that starts meanwhile does not cut in.

**The scenic shots** (slot 48; the camera manager's mode 5, 0x375980, forced on screen 0 while the
race's menus show, `RaceBase::vf40` 0x388880): the records in file order from record 0, each
run on time for its `end` seconds (t = frames since the cut / (end x 60), 0x3FC9A0), then the
next, after the last record 0 again, cut with the ended record's dissolve; roll at full size. A
course with no slot 48 record shows the on-board view.

**D and the lap variants** (slot 44; 0x3FD980 -> 0x396078 -> 0x3DDFF0): the camera manager keeps the
followed car's place per screen (block at manager + 0x10 + 0x19C x screen: +0x14 its position, +0x2C its
crossings of the line, +0x30 its distance along the lap, +0x34 the crossings x L + the distance, +0x38
its speed; ✔ read in the reference's saved states: during Tsukuba's rolling run-up +0x2C 0 and +0x34 =
+0x30 = 1872.7 m, 242 m before the line; just after the start +0x2C 1, +0x30 20.6, +0x34 2135.7 =
2115.1 + 20.6). D = +0x34 + +0x3C (0 here) x L, L the script's lap length (+0x14). The script's period
is L x (int(T / L) - 1 - int(first record's start / L)); **a D at or past T becomes D - period x
int(D / period); a negative D or one under T is kept** (0x3DDF18). So the run-up of a rolling start
(no crossing yet) takes the records of the end of the first variant, the first lap after the line the
second variant, and so on; a standing start's cars, behind the line with no crossing, likewise. The
records are chosen (0x3DDFF0 via 0x3DDF88): none when D is before the first record's start; else the last
record with start <= D when D is under its end; when that search ran past the last record, the first
record from record 0 that covers D less the period. Header +0x1C states the variant count (3 on 72
courses; 1 to 6); it equals the formula on 103 of the 110 courses and differs on drag strips and short
layouts, where the program still uses the formula. Within a record the camera brings the car's distance
into the record's stretch by whole laps before taking t.

**Marks** (slot 44's last list): distances D. The manager checks each frame whether the
followed car has passed one, and one mark, drawn at random when the race is set up, is told
apart from the others. ❓ What a mark triggers.

**Versus `GTRC`** — what maps onto the engine's structures: the header's L, T, record count
and record pointer (at other offsets); the record's kind bits 0-3, start, end, the seven
position / look-at / lens curves, smallest and largest lens; the curve format; D, the
variants and the record search. What differs: the lens is an angle in degrees or a width in
metres (GTRC: 1 / tan(half angle), or a factor of the distance under kind 0x10); kind 0x10
now means "on time" (GTRC: a look-at record of 1 s or more runs on time); the haze flag moved
from +0x1C to +0x0C. What is new: the aperture and the flags for the depth-of-field pass, the
picture offsets, the operator curves, the roll curve, the stated variant count, the roadside
points with their stretches (GTRC's second list had no stretches), the marks as plain
distances. Gone: the on-board mount number (+0x18 in GTRC) — on-board shots are the ride-on
camera's, from its two tables.

## Camera motions: `Mot1` (course slots 52, 53)

Code: `src/gt4r/race/start_camera.*`. A set of named clips keyed with cubic curves. The
path-animation camera (kind 3) plays slot 52's `start_camera1` for a race's start (camera mode 3,
0x3756D8) and slot 53's `replay_start_camera1` for a replay's (mode 4, 0x375860; names at
0x69FB60 / 0x69FB48). All 110 courses have both: slot 52 holds one of `start_jingle3` ..
`start_jingle9` (7 s) with `start_countdown_B1` (1.0167 s) and `_A1` (1 s); slot 53 holds
`start_jingle2` (7 s) and `start_countdown_A2`, `_B2`, `_C1`, `_D1` (1 s each).

```
0x00 "Mot1"
0x10 u32 -> root: u16 clips, u16 sequences, u32 -> clips, u32 -> sequences, u32 -> ❓
0x14 u16 1, u16 names      0x1C u32 -> name index, 0x10 bytes a name: u32 -> name text,
                           then a search tree's links
0x20 the names' text
clip (0x20 bytes): f32 length in seconds, f32 ❓, 0, 0, u32 -> name, u32 channels (10),
                   u32 -> channel pointers, u32 -> the key data
channel (0x10 bytes): u32 1, 0, 0, u32 -> keys
keys: u32 n, 0, 0, f32 first value, then (n - 2) / 4 pieces of
      {f32 control 1, f32 control 2, f32 end time (seconds), f32 end value}
sequence (0x10 bytes): u32 clips, u32 -> ❓, f32 total seconds (9.0167 in slot 52, 11 in 53), 0
```

A piece is a cubic Bezier in value from the previous key to its end over the time between them
(🔎 time taken as linear in the curve's parameter; the disc's controls lie on thirds of a
straight where a channel moves evenly). A clip's 10 channels: eye x, y, z; look-at x, y, z; the
up vector (unit length); the field of view in radians (Tsukuba 0.722 = 41 degrees).

### The start's shots

- **How long** (`CameraPathanim` vf39 0x37D338, vf10 0x37D620): entered while the start's hold
  runs; it runs for the hold less 60 frames (race) or the hold plus 60 (replay), counting its
  remaining seconds r down. The clip is chosen by r, not by the time played (0x37D5B0; table
  0x621270 race, 0x621290 replay); the motion advances 1/60 s a frame and restarts at each
  change of clip. Race: the long clip while r >= 2, the second for 1 <= r < 2, the third for the
  last second. Replay: the long clip while r >= 4, then one clip a second. A 10 s hold: the
  whole 7 s clip, B1, A1, and the camera ends 1 s before the start signal (the driver's view
  takes the last second). An 8 s hold shows only the first 5 s of the long clip.
- **Where**: the long clip at the course's **start place 0** (0x3FCA08: its place and heading,
  the height from the road under it); the others at the **followed car's place and heading
  when the camera began**, mirrored left to right for a car with entry flag +0x3444 bit 0
  (0x37D874, ❓ that flag). The clips' frame: x across, y up, z toward the back of the anchor
  (the program's frame has the course's z the other way round). Seen on Tsukuba: the long clip
  flies from the line back over the grid to the last car, B1 is the player's car from in
  front, A1 from behind.
- A skip was not found in the camera's code.
## `cameras/attach.cam`, `replay.cam`, `start.cam` 🔎

Three files in the shared engine's `GTRC` layout, which its parser reads. `start.cam` and
`replay.cam` are the same bytes: one car-frame record (kind 0x420, zoom 2.414, 0.3 s
dissolve) whose path is the one of course slot 45. `attach.cam` has 33 car-frame records
(kind 0x21), each a fixed eye and look-at point round the car. The program has no string with
their path and no reader for them was found: the per-course `REP4` slots and the cars' camera
mounts appear to have taken their place.
