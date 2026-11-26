# The pit crew (`pit/pit_001`)

The scene round a car that stops in its box: the men, the wheels they change, the air hoses, the cameras.
Code: `race/pit_scene.*`, `menu/model_motion.*` (the motions), `app/race_world.cpp` (wiring). Read from the
program (the scene is RacePS2Base +0x3628; `fn_003D03E0` steps it, `fn_003D25B0` draws it) and checked against
eight saved states of a stop (`26-endurance/fc_pitwindow_10` .. `_17`: Tsukuba, an R34).

## The file

`pit/pit_001` (`pit/pit_002` in a race of type 2 ❓, not used here; its pit is on the other side). u32 offsets from +0x04:

| section | what |
|---|---|
| 1 | `Mot1`: the crew's motions: object 0 the tires' men, 1 the man with the sign, 2 the man with the fuel |
| 2 | `Mot1`: the cameras |
| 3 | `MDLS`: the crew (symbols: `SKINNING` the man's body, `CRANIUM`, `PELVIS`, `gun`, `fuel`, `LoliPop`; 20 bones; the `BAKE_*` / `BC_*` symbols are lower levels ❓ not used) |
| 4 | `MDLS`: the figure of their shadow (`SKINNING`) |

Info +0x00 bit 0x10 of a car's file moves the waiting men further out; bits 2 and 3 say that the filler's side or the
tires' places are not known (the camera's sequence).

## Sequences (`Mot1`)

An object has clips and sequences. A sequence (0x20 bytes: u16 entries, u32 -> entries, f32 length) is a list of entries
(0x20 bytes: u32 -> s16 clips[], f32 start, f32 offset into the clips, f32 duration, u16 clips, u8 reversed, u8 mode,
u8 held at its end, u32 -> name, s32 number). A caller's clock runs through it with the number it wants (`fn_00429BC0`):
an entry whose number is the wanted one, or past it, holds (at its end, or round its loop); one whose number has been
passed is left at its end (mode 0), as soon as its loop allows (1) or at once (2). `fn_00429AD8`: where the first entry of
a number begins. Several clips in an entry are blended by the caller's place between them (`fn_0042C348`; turns between
their quaternions) or picked (the cameras). A clip has events (+0x16 u16, +0x1C: 0x10 bytes each: u32 -> name, u16 times,
u32 -> {f32 time, f32 value}): `fn_00429740` sums a name's events between two times.

The tires' men: `tiremanLF_1seq` .. `RR`: wait (number 0), stand ready (1), work to the end (2), with the named entries
`release`, `fix`, `tire_end`. Between an entry's two clips by the tire: `1 - (2 R - 0.456) / 0.371`, kept in 0 .. 1 (R its
radius, m). The fuel man: `fuelman_1seq` with a reversed entry `refuel`. The sign: `lollipopman_1seq`.

## The stages

A car's stage (`fn_003C9F38`): 0 unless its driver's kind is 2 .. 4 and it is between the stop line's second point and its
last (`fn_0035AE58`); then 1 on the lane, 2 in the box (hold 0x2D0 .. 0x285), 3 the work, 4 the last second, 5 leaving
(0x35BBD8). The number a man is sent to: the tires' men 0, 1, 2 at stages 1, 2, 3 .. 5; the fuel man 0, at stage 3 1 while
the fuel runs (0x35BEB0) and 2 after it, 2 at 4 and 5; the sign 0 at 1, 1 until the wheels are done, 2 when they are
(before stage 5), 3 after. A stage passed over: each motion begins at its number (`fn_003CD430`).

At stage 1 a man without work is marked (0x35BD10, from the driver's kind and the service bits: with the choice still to
take (0x11) tires if 4 or 2, fuel if 8 or 2; else 0x40 front, 0x80 rear, 0x100 fuel; kind 4 nobody); he is gone, and his
motion stands, from the next change of stage.

Places: the box is the stop line's point 8 less the car's place on the grid, on the road, at the point's heading. Until
stage 2 the men wait there, afterwards at the car (its x and z; the box's height and heading). A man is drawn 0.94 of his
size. A tire man stands at his hub's outer face (hub x -/+ half the tire), looking across the car; waiting, both of a side
at the outermost x and random steps further out (0.1 .. 0.25 front, 0.6 .. 0.7 rear). The sign: 1 m ahead of the front, at
half the body's width (1.708 m at least). The fuel man: beside the filler (the car's parts block +0xAC, the side by the
largest component of +0xB8), moved clear of the wheels' men, 1.1 .. 1.2 m further out while waiting. ✔ Every place, and
the motions' times, beside the states (the random parts by their range).

The fuel man's nozzle goes in as far as the filler is low: it stops at `(car's y + filler's y - 0.612) / 0.29` of `refuel`
(0.01 .. 0.99), the hose then runs; afterwards his clock is turned round in the entry, so the nozzle comes back as it went.
(The car's y is the course's own height, so this is 0.99 on every course above the sea: as the program has it.)

## The wheels

The car's own four are not drawn at stage 3: the scene draws each man's old wheel (node `tire0`, from stage 3) and new
one (`tire1`, stages 1 .. 3), the car's own wheel models (always the left ones), in the hub's frame with half the tire's
width before and after the node; the new one's place along its axis by the width against 259 mm. Where no tires are
changed the wheel stays on its hub. The wheel taken off follows the hub's turn until 0.717 / 1.267 / 0.4 / 0.483 s (left
front .. right rear) into `release`, the new one from 1.9 / 2.433 / 2.167 / 2.35 s into `fix`, over 1/6 s each.

## The hoses, the shadows

Four air hoses (`fn_003CA698`, `fn_003CAAC8`, `fn_003CA168`) from the pit's stand (the runway's pit stop nearest the box,
its heading + pi; without one the box) at x = -1.5, 1.5, -1.375, 1.375 and 2.125 m up, to the wheel guns: a curve through
two points 2 cm over the road at 0.4 and 0.6 of the way, 24 samples, two crossed ribbons 2 cm wide, black at 0.8. Each
swings (`fn_003CF938`): a spring of 10 and a damping of 2.59 on the motion's `cable_accel_x` / `_z`, x 6.24.

The shadow: section 4's figure posed as the man, thrown flat along the course's shadow (in the man's frame, at most 1.5 m
a metre).

## The cameras (`fn_003CBB28`, `fn_003CBD00`, `fn_003CC010`, `fn_003CB2C0`; `CameraSys::CameraPitIn`)

The race's view gives way to the pit's camera while its number is 5 or more (`fn_003FD620` mode 1 by `fn_003D2840`), and
the crew is drawn only then. Number 4 on the lane (nothing shown), 8 from the driver's kind 3 (25 m before the box) or
5 m from it, 12 at stage 2, 14 at 3 and 4, 16 once every wheel is on (or none is changed: a cut), 20 leaving, within 30 m
of the box, then -1. The sequence by the filler's side: `pitcam_fuel_FR` / `RR` (right, ahead of or behind the middle),
`_R` (rear), `_FL` / `_RL`, `_F`, `_unknown` (info bit 2), `pitcam_tire_unknown` (bit 3). An entry that begins picks one
of its clips: the first when it is alone, else one of the others at random; a new clip keeps the car's heading and place.

A clip's camera (op 0x0B: eye, target, up; 0x0A the lens, radians) is placed by two named values, `CameraCoord` (eye and
up) and `TargetCoord`:

| value | frame |
|---|---|
| 0 | the car as it moves |
| 1 | the box |
| 2 .. 5 | over wheel 0 .. 3 on the road where the car was at the cut (its hub as the race began + the tire's radius) |
| 6 | the car, about the driver's head (seat + 1 m up, the sum at most 1.625) |
| 7 | where the car was at the cut |

✔ Against the states' eyes and targets. The picture is held by hand: the view turned about x and y by two values that go
a twenty-fifth of the way (0.1 x 0.4) to a random -1 .. 1 (a new one with 0.15 a step), x 1.4 x lens / 80.

## The sounds

For the car watched, between a man's times before and after a step: `wheel_gun` -> wheelgun, `wheel_nut` -> dropnut,
`tire_sound2` -> droptire, `jack_up` -> jackup; `oiltank` as the fuel man's refuelling begins and ends; the hose's held
sound while it runs; `jackdown` at the stop's "tires on" bit. ✔ Each time within a step or two of the original's recording
of a stop (`26-endurance/family_cup_bspec_pit_stop_whole_race.mp4`; `tests/test_pit_scene.cpp`).

## The license scene (`pit/license_001`) 🔎 read, not built

The staff round a license test's car after the run (the original's B-1 pass: three men in GT shirts round the car in the box,
11-license-tt `b1_pass_result_live_scene.png`). The race object keeps it at RaceLicense +0x24E5C; it is loaded by RaceLicense
virtual 38 (0x3BC5D0) only for a test of one car (the conductor's car count +0xF8A0 under 2) and when memory allows
(0x3ED9E0 / 0x3ED9D0). The file has pit_001's layout: section 1 `Mot1` the men's motions (`license_stand_master`,
`license_stand_staff_1`, `_2`; `license_success_*`; `license_failed_*`), 2 `Mot1` the cameras (`license_cam01_seq` .. `03`, clips
`L_license_cam_01` .. `07`), 3 `MDLS` the men, 4 `MDLS` their shadow.

- **Places**: the road's section the test runs on (the course object's +0xB0; RNW4 section record +0x0E u16 count, +0x10 -> 16
  bytes each: x, y, z, heading; `race/sections.h`). Set-up (0x408F90): none: no scene; with two or more the men stand at place 1,
  else place 0, in their `stand` motions, drawn 0.94 of their size (0x408F38). The place's height is the road's under it
  (0x409B48); the scene's heading is the place's + 3.14 (0x408DB8). Each man is then put by the car's outline (0x408B08 through
  CarGeometry virtuals 2, 4, 18 and 0x344AA8 / 0x344C08): man 0 beside a side of the car 0.5 m out, turned a quarter turn
  (1.5700); men 1 and 2 at the car's ends 0.75 m out (the 1.0 m added up the car) ❓ the sides and signs not followed.
- **When** (stepped by RaceLicense virtual 75 → 0x3BC6E0 → 0x409168, dt 1/60): the result is taken once, 1 when the race's
  result word (+0x6C → +0x88, written by the test's end 0x3F8AB0: 1 .. 3 the grade, 4 a time under none) is set, 2 when the
  step's events hold event 10 (a failed test) with it 0. Then +0x24: 0 for 2 s; 1 for 2 s, the race's picture fading out
  (race +0xCF54 = -1 / 2); 2: the men moved to place 0 (the stopping box) in their `success` or `failed` motions, the camera on
  the next of `license_cam01_seq`, `02`, `03` in turn (+0x74), the picture fading in over 1 s (+0xCF54 = 1); while in 2 the
  player's car is put on the place, at the scene's heading, every step (0x3FAB10); 3 after 1 s.
- **The camera** (0x4091E8, while the scene is up and before state 2 ends): the cameras' sequence at its own clock (0x429BC0);
  RaceLicense virtuals 139 / 148 ask 0x4096F8 whether it is the race's camera.
- **Drawn** by RaceLicense virtual 138 (0x4094C8) and 116 (0x408470, 0x4096C0).
- 🔎 The original's B-1 overshoot (a failure by a cone hit, no time: 18-round4-gt `license_b1_whole_test_overshoot_fail.mp4`) shows
  no staff and the car left where it stopped, under a camera turning round it, for 25 s; how that agrees with the event-10 test
  above was not followed.

## What runs where

| part | state |
|---|---|
| the men, places, motions, idle men, wheels, hoses, shadows, sounds, cameras | GT4 |
| the waits' random steps, the cameras' picks | GT4's rules, our random numbers |
| a replay's pit stops | not shown: a replay's states carry no pit stage ❓ |
| the levels of the men (`BAKE_*`), the spare tires' shadows on the floor (0x3D5528, 0x3D3E80), `pit_002`, the license scene's own crew (`pit/license_001`, read above) | not built |
