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

## The license scene (`pit/license_001`)

Code: `race/license_scene.*` (the program's LicenseConcourse, 0x408120 .. 0x4098A0), `app/race_world.cpp` (`open_license_scene`,
`step_license_scene`), `app/screen_race.cpp` (the result, the camera, the fade). The staff round a license test's car after a run
that ends with a time: three men in GT shirts, the master and two of his staff. The race object keeps the scene at RaceLicense
+0x24E5C; RaceLicense virtual 38 (0x3BC5D0) loads it only for a test of one car (the conductor's car count +0xF8A0 under 2) and
when memory allows (0x3ED9E0 / 0x3ED9D0). The file has pit_001's layout: section 1 `Mot1` the men's motions (object 1 the master:
`license_stand_master`, `license_success_master`, `license_failed_master`; object 0 his staff: `license_stand_staff_1`, `_2`,
`license_success_staff_1`, `_2`, `license_failed_staff_1`, `_2`), 2 `Mot1` the cameras (`license_cam01_seq` .. `03`, clips
`L_license_cam_01` .. `07`), 3 `MDLS` the men (`SKINNING` the body, `CRANIUM` the cap, `pen`, `board`; 20 bones), 4 `MDLS` their
shadow.

- **Places**: the race's road section (the course object's +0xB0) has places for them (RNW4 section record +0x0E u16 count, +0x10
  -> 16 bytes each: x, y, z, heading; `race::RoadSection::staff`): the 100 m test course's B-1 section the stopping box and a
  second place 15 m past it; Deep Forest's and Tsukuba's sections one place twice. Set-up (0x408F90): no place, no scene; with
  two or more the men stand at place 1 through the run and are drawn (+0x1C), else at place 0, not drawn until the end; each in
  his `stand` motion, drawn 0.94 of his size (0x408F38).
- **The men round the car** (0x408DB8 → 0x408B08): the place's height is the road's under it (0x409B48); the scene's heading is the
  place's + 3.14. In the car's frame (x across, z along with the front at -z; the car file's parts block and the wheels'
  places, NormalCarGeometry virtuals 2, 4, 18 and 0x344AA8 / 0x344C08): the driver's side is the side of the front wheel on
  the seat's side of the gear lever (the seat +0x34's x against the lever +0x4C's); the master at that wheel's x 0.5 m further
  out, at the seat's z; the first of the staff 0.75 m out at the seat's z + 1; the other across the car 0.75 m out from the other
  front wheel, at its z. Each is turned to face the seat (atan2(-dx, -dz), 0x57D118), then all is turned by the scene's heading
  about the place (0x407DE8), each on the road under him (0x407EF0). The draw turns a man by heading x 180 / 3.14 degrees
  (0x408490). ✔ The three men within 3 mm and 0.001 rad of the original's pass scene (15-states
  `state_license_b1_pass_result.p2s`), with the Demio's seat and lever from its car file (`tests/test_license_scene.cpp`).
- **When** (RaceLicense virtual 75 → 0x3BC6E0 → 0x409168, each step): the result is taken once (0x409138): passed when the
  test's result word (conductor +0x6C → +0x88) is set, failed when the step's events hold event 10 with it 0. The test's end
  (0x3F8AB0) sets the word to the grade (1 .. 3) for a time within the targets; a time slower than all three pushes event 10;
  a run failed by the rules (car +0x441 set: off the course, a wall, a cone, past the stopping box) does neither: **no one comes
  out** and the car stays where it is (the original's B-1 overshoot, 18-round4-gt `license_b1_whole_test_overshoot_fail.mp4`).
  Then +0x24: 0 for 2 s; 1 for 2 s (0x409878), the race's picture fading out from the step it begins (race +0xCF54 = -1 / 2);
  2: the men moved to place 0 (the stopping box) in their `success` or `failed` motions, the camera on the next of
  `license_cam01_seq`, `02`, `03` in turn (+0x74, kept from one run to the next), the picture fading in over 1 s (+0xCF54 = 1),
  and, in that step only, the player's car put on the place at the scene's heading (0x409860 → 0x3FAB10); 3 after 1 s (0x409888).
  ✔ The original's pass (`license_b1_whole_test_page_to_bronze_result.mp4`): the stop at 21.3 s, black about 24.5 .. 25.5 s, the
  car close up fading in at 26 s, the staff round it at 26.5 s; and its FAIL page after a slow run (05-gtmode
  `37_license_test_fail_page.png`: 133.366 s, the staff by the car in the inset).
- **Motions**: each man's and the camera's clock at the number 1 (0x427800 → 0x429BC0): every entry plays through and the last
  goes round. ✔ A staff man 14.666 s into his motion 20.466 s on (his 15.167 s sequence's last entry of 1.933 s from 13.233 s).
- **The camera** (0x4091E8 steps it, 0x4094C8 places it): from state 2 on it is the race's (0x4096F8; RaceLicense virtuals 139 /
  148), its sequence's first clip: eye, target and up in the scene's frame (turned by its heading about the place), the lens
  (0x428298). ✔ The eye within 1 mm, the lens 0.6436 of the original's state (its eye is the one its last picture was drawn
  with, a step before its clock).
- **Drawn** by RaceLicense virtual 116 (0x408470, 0x4096C0 → 0x408490: the men and their shadows, as the pit crew's).

## What runs where

| part | state |
|---|---|
| the men, places, motions, idle men, wheels, hoses, shadows, sounds, cameras | GT4 |
| the waits' random steps, the cameras' picks | GT4's rules, our random numbers |
| a replay's pit stops | not shown: a replay's states carry no pit stage ❓ |
| the license test's staff: places, motions, cameras, fade, the car put in the box | GT4 |
| the levels of the men (`BAKE_*`), the spare tires' shadows on the floor (0x3D5528, 0x3D3E80), `pit_002` | not built |
| the license staff's shadows on the road's plane under each man (0x407FA0: the plane from 0x409B68) | flat at the man's height, as the pit crew's ❓ |
