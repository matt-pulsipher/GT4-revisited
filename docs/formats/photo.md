# Photos: Photo Travel, the shoot, Photo Lab

Code: `src/gt4r/photo/` (the locations, the camera, the picture), `src/gt4r/save/photo_file.*` (the
card), `src/gt4r/menu/photo_natives.*` and `game_photos.cpp` (what the scripts see),
`src/gt4r/app/screen_photo.cpp` (the shoot's screen) and `photo_faces.h`; `gt4tool photo`. Facts from
the US program (`CORE.GT4`, SCUS-97328 v1.01; addresses are the program's), the disc's files and
scripts, and the reference card with one photo (Times Square, a Ford GT). ❓ not read; 🔎 judged.

## What runs where

| Part | Code | Whose |
|---|---|---|
| The pages (`photo_shoot`, `labomode`, `photo_save`, `print`) | the disc's scripts and layouts, run by `menu/` | Gran Turismo 4's |
| The locations' block, the camera's modes, the map | `photo_course.*`, `photo_camera.*` | Gran Turismo 4's |
| Lens, samples, focus, filters, exposure, JPEG, icon | `picture.*`, `photo_file.*` | Gran Turismo 4's |
| The card's files and their scrambling | `save/photo_file.*`, `codec.*` | Gran Turismo 4's, byte for byte on the reference card |
| The scene (course, car, lights, shadows) | `app/race_world.*` | as in the races: Gran Turismo 4's files and rules drawn by the shared engine |
| The finder's depth of field | the shared engine's pass (the program's own, `0x399550`, has the same steps) | 🔎 the blur steps are of the shared engine's 448-line picture; the program counts in the finder's own 190 lines and shows none of the smallest |
| The finder's color correction | the shared engine's grade pass, a curve through the gamma's middle value | 🔎 the photo itself uses the program's gamma table |
| The printer | `menu/photo_natives.cpp` | as the original with no printer: its driver loads, no printer opens, `scanPrinter` answers `no_printer` (0x1CA878) and the print page says "Printer not connected." |
| The USB memory | `menu/photo_natives.cpp` | the data folder's `photos/` stands for it: `img0001.jpg` on, the photo's JPEG (the program's `/GT4/img%04d.jpg`) |
| The photo pause (its cameras, pad, steps, movie, finder, photo) | `race/photo_pause.*`, `app/race_photo.*` | Gran Turismo 4's; the random cameras' numbers ours 🔎; the B-spec panel's refusal not done |
| Films: their data, saving, developing | `photo/film.*`, `save/photo_file.*`, `app/race_photo.*`, `screen_race.cpp` (sequence `photo_develop`) | Gran Turismo 4's chunks and card files, byte for byte on the original's film; a car's parameters from our drawn state (the ones the drawing does not use written as 0 🔎); the frames' drivers, skid marks, gadgets and particles not written (DRVR, SMOK's marks, GDGT, PTCL: read and kept, not drawn) |

## The sequence

`photo_shoot` (0x1C79A8): the course of the request loads, the project `photo_shoot` runs on its
pages over the scene (the class `RacePhotoMode`), and the day of the diary is a course run of kind 5
(travel) at the location: type 8, b3 5, b1 15, b2 14, no time. The car is the scripts' `car_code`
when they set one, else the garage's driven car.

`photo_labo` (0x1C7A88): the project `labomode`, no page named, again and again until
`main::game.menu_result` is `exit`; `print` runs the project `print` in between, `develop` a film's
development (0x1C7BF8, not built).

## The location: `CRPM` (course slot 51, the 15 `ss_` courses)

```
0x0C u32 size                 0x14 u32 -> the map, an MDLS
0x18 u32 cells   0x1C -> 0x40 bytes each      0x20 u32 shapes   0x24 -> 0x10 bytes each
0x28 f32 x4  the pointer's bounds x0 z0 x1 z1
0x38 f32 x2  where the camera starts     0x40 f32 x2 where the car starts   0x48 f32 its heading (degrees)
0x4C f32 x4  what the map may show: x0 z0 x1 z1
```

A **shape** is `u16, u16 outlines, u32 -> u32 pointers -> {s32 n, n x (x, z)}`: closed outlines, the
inside on one side of each, holes running the other way. A **cell** is a place for the camera:

```
0x00 u16 flags: bit 0 a spot (one place: its shape's middle; the camera snaps to it)
0x04 u16 shape   0x06 u16 targets   0x08 u16 car shapes   0x10 -> targets (x, z)   0x14 -> u16 shapes
0x18 f32 x2 what the camera looks at when it is put here
0x20 f32 (❓ 25 or 35)     0x24 f32 least height of the eye above the ground     0x28 its highest place
```

The car may stand inside the cell's car shapes; with targets the camera must look between them
(`PhotoCourse::limits`, 0x3FEF60). A move that would leave a shape slides along its border with the
share of it that runs along the edge (0x3C9A38; nothing under a share of 0.3), round its corners.

Course space is y up; the car is placed by its x and heading, the road's height under it found from
the eye's (0x3C75F0). View = Rz(roll) Rx(pitch) Ry(yaw) T(-eye), yaw 0 looking along -z.

## The modes (0x377F60; names 0x618A70)

The scripts set `operate_mode`: `none`, `car`, `camera`, `pan`, `roll`, `orbit`, `move`, `climb`,
`map_zoom`. The pad's directions are the d-pad's -1 / +1 plus twice the left stick (0x4272A0; a
stick's dead zone 8 of 120, 0x3FAFE8).

| Mode | Does |
|---|---|
| car, camera (0x378778) | the thing follows the pointer over the map and is put down by Cross or Circle where it may stand, taken back by Triangle or Square; L2 / R2 turn the car (less twice the right stick); a mark warns where it may not |
| pan (0x371800), roll (0x371A58), move (0x3716A8), climb (0x3719D0) | the camera, faster the longer held (0x36F6A0: 24 pictures to full) |
| zoom (0x370240) | L1 wider, R1 closer, the map's length x 1.07 a picture between 20 m and the location's |

In the pages: Cross held on one of the four round buttons, or L1 (pan), L2 (roll), R1 (move), R2
(climb) held anywhere, with the left stick; L1 and L2 together the map's zoom, R1 and R2 the focal
length's slider. The shutter takes the photo when Cross is let go. A slider's step grows while its
key is held (the scripts' `repeat_start`).

## The lens (0x36F480)

Kept as whole tenths of a millimetre and hundredths of an f-number; the film is 36 x 24 mm. The view's
angle is 2 atan(24 / 2f) over the picture's height, or its width when the camera is held upright. The
page's focal length is 16 to 300 mm (Amateur: 1 to 10 of it), its f-numbers the program's 16
(0x620E70, from F1 up). A cell with targets narrows a view that does not fit between them (0x370EF8).

**Focus**: at the car by itself (0x3C7928: where the car's nearest point blurs by 1/960 of the
picture), or at the place under the frame's middle (`doFocus`, 0x1952D0). A picture without the car
(no corner of its box in it, 0x3C7838) is taken at f/500.

## The photo (0x333A00)

A picture twice the aspect's tile each way: 4:3, 16:9 and 3:2 of 640 x 480 tiles, 1:1 of 512 x 512
(0x692380), upright ones turned. It is the average of 32 pictures (0x1943C0), each from another point
of the lens and another place in the pixel (a Mersenne Twister of seed 0x12345678, 0x3323B0): the eye
moves over the lens' opening and the projection is sheared so that the plane of focus stays (0x334730,
0x332550, film width 32 mm). The scene is drawn whole: every model of the course (not the place's
visible list), the cars' nearest level. Over each picture go the color correction, the course's
vignette (Env0 +0x950) and the photo's own, and the picture is blended **under the average so far** with
the GS's fixed weight trunc(128 (1 - 1 / (k + 1))) of 128, rounded down: the average loses half a
level each time, so a photo comes out about 8 levels darker than its pictures (`photo::expose`).
The course's animations run on over a second during it (0x397568; here a picture, 1/60 s, a sample).

**Correction** (0x3C7DA8, 0x3EFC10, 0x4A8CB8): per channel a gamma of 2^(brightness / 8), red and blue
moved by the white balance (x (1 -/+ balance x 400 x 0.2 / 2000)), white 1 - 0.1 contrast, saturation 1,
0.6 (Mature) or 0 (Monotone). **Vignette** (0x3C7CD8, 0x3BBBC0): a black ring from radius 200 to 420 of
a 640 x 448 frame, alpha vignetting x 0.007 at its outer edge.

**JPEG**: quality 50, 80 or 95 by the option `photo_quality`; baseline, 4:2:0, the standard tables;
JFIF 1.02; a segment APP8 `G4PI` `PDI` with `{u32 1, u32 3, f32 x, y, x0, y0, x1, y1}`: where the car is in
the picture (-1 .. 1, y up; 0x3785D0, its box a little longer and taller), which the slide show moves
from. The same encoder's bytes: the reference photo decoded and coded again is within a level (rms).

## The map (0x194DB0)

The location's map model from above, all heights on one plane, its length over the window's height;
over it the camera's cells (not the spots) and, in the camera's cell, the car's shapes, which brighten
with the blink while the car is moved (colors 0x618A98, 0x618AB8); a wedge of the view's angle, and the two
headings a cell keeps it between. The marks are the page's widgets, placed by the program.

## On the card (0x1D0478 .. 0x1D0D00)

A folder `BASCUS-97328P` + seven letters: 42 bits, six a letter from the lowest, letters of the text at
0x694C80; the photo's number from bit 32 on, chance below, bit 0 a portrait (films: `F`). In it:

- the picture, a file of the folder's name: the JPEG, scrambled as the saved game's payload is
  ([save.md](save.md) "Scrambling") with swap constants 0.6809 and 0.404;
- `thumbnail`: a record of 0xE8 bytes {u64 car, u64 course, text[64] name `P0000`, text[64] course file,
  text[72] car name, ..., +0xD8 the race's mode (13 the photo drive's), +0xD9 0xFF, u32 portrait at +0xDC}, scrambled with 0.314159 and 0.468643 and its
  bytes past the first four shuffled (a film's record the same; its data with 0.654321 / 0.345678, shuffled too: "Films");
- `icon.sys`, and `icon0.ico`: the disc's `icon/GT4_photo_4_to_3_.ico` (`3_to_4_` upright) with a
  64 x 64 of the photo in its texture's top left, packed again when it fits (0x1D3E20).

Its size on the card counts each file up to a KB, half a KB an entry, and one more (0x1CC9A8). Checked
with `gt4tool photo <disc> check <card>` on the reference card: the record, both files scrambled again
and the icon come out the same bytes; a photo taken here lists, shows and checks the same.

The app's save files are no card: the photo pages' card manager always answers a card, formatted, with room (`free` 8,000), in
either slot the one folder; the page that asks where to save (`DestinationDialog`) is answered without
showing it (the photo, the save files); the photo pause's, which asks as what too (`SaveDialog`), is shown as the program's; a
write that fails says so once. The other questions of the scripts about
a card are answered in the menus.

The **slide show's list** is the folder `BASCUS-97328LSTSLIDE` (76 KB by the program's count): 400
places of u64 (the folder's number, its letter in the top byte), u32 count at 0xC80, padded to 0xCC0
bytes and not scrambled (0x1D0F38). The slide show face shows the list's photos, or the disc's
(`photo/%03d.JPG`) when the card has none or the face's `demo` is set (0x1C6BE8).

## The photo pause (Replay Theater Photo Mode, the photo drive)

Code: `src/gt4r/race/photo_pause.*` (the cameras, the pad, a step of the camera), `src/gt4r/app/race_photo.*` (the pause over
the race: the finder, the movie, the photo), `src/gt4r/app/photo_exposure.*` (the 32 pictures, shared with Photo Travel), the
race screen's `open_photo` / `update_photo`.

- **Where a player meets it**: Select in a replay of the Replay Theater (0x10CA50 makes a `PhotoPause` and sets it as the race's
  second pause, race +0xE404) and of the photo drive (sequence `photo_drive`, 0x11ABE0; "Photo Drive" Cr 3 on a course's page),
  and in the photo drive's B-spec run. `RaceBasic::vf149` (0x38B0E0) opens that second pause on the menus' bit 0x80 (Select)
  when the play mode (+0xCC8) is not 0; elsewhere it is `SimplePause` (the replay held, "Press SELECT to resume"; it ends on
  Select or cancel). The photo drive's commands (0x68D9C0): 1 the drive (play mode 0, the race's own pause) and then **its
  replay at once with the photo pause** (play mode 2), 3 the replay (2, or 3 after a B-spec run), 14 the B-spec run (1) with
  it, 9 save the replay (`labomode`, `MODE_SAVE_FULL_REPLAY`), 15 the settings, 22 the options, 2 leave (one day, 0x11B0D8).
- **The pause** (`PhotoPause`, vtable 0x681B98; made at 0x3C3C20): its photo 640 x 480 tiles, 2 x 2, 4:3, 32 pictures
  (0x333560); the movie `menu/common/racephoto.strb` (0x3C4640), played without the pad (0x3C46C8: player +0x10 = 0), a
  twelfth of a second a step (0x475488 with 0.0833). Each step (vf4, 0x3C4A48): the first refuses, and the
  pause ends at once (vf4 answers 3), with the sound `disable` while the race's B-spec panel (race +0xE424) has its +0x28 at 1
  (❓ which state of the panel that is), or with the race's text `CantCaptureWhenPitSequence` ("You cannot take pictures when
  pitting in.") for 3 s on the event line (race +0xE420's vf19 0x3A2198 through 0x38BC98) while the followed car is in a pit
  visit (0x35BBD8 not 0; the program's override 0x6201A8 is 0 and nothing sets it); here the pit's (the step shown's pit stage,
  kept beside a replay's states), not the panel's; else the camera manager's slot 0 becomes kind 7
  (0x372110) and the race's picture is the pause's (+0xD84 / +0xD8C, 0x3C48D8 / 0x3C4900). Select (0x80 of 0x426A08) leaves
  after two more steps (camera kind 9 again, 0x3C49C8). Triangle (0x20 of 0x426A90) steps the color filter +0x1A8 through 0
  none, 1 Mature (saturation x 0.6), 2 black and white (0x3C58C0). The held shoulder button (0x426A88) is the movie's
  `GETBLINK` (+0x1AC: 0 L2, 1 L1, 2 R2, 3 R1, -1 none or two). The camera steps (manager vf22 -> kind 7, 0x375D20); its
  shutter (vf55 -> CameraShot vf32 +0x1BA) starts the photo: sound `shutter`, the movie gone, the photo upright when the
  camera is (vf18), a picture a frame (0x3C5430 -> 0x333E50).
- **The cameras** (the ShotManager at manager +0xC04, records of 0x74 bytes, at most 64; made at the pause's opening by
  0x375C08 -> 0x3FFD10): first the replay's own camera (its eye, its angle no wider than 83.2 degrees, its turn without the
  roll, held at its height); then the course's roadside points (the replay script's second list) whose stretch covers the
  followed car's place on the lap (0x3FDC50 -> 0x3DE268, within 500 m, at most 63), aimed at the car with the car a little below
  the middle (0x36EBE8: the pitch less cos(pitch) x the view's angle x 0.225), a point of kind bit 2 between 0.4 and 3 m over the
  ground; the rest round the car (0x3FFA48): at even turns with a random share of a step, the car's farthest corner + 0.2 m +
  0..8 m off and 0.8 m up, short of what stands there by 0.15 m (0x3FE7B0), no higher than 3 m over the ground, between 0.4 and
  3 m. 🔎 The program's random numbers (0x578598) are not read: ours. A new camera's view is 2 atan(8 / d) (a 16 m stretch at
  the car's distance d, 2 .. 83.2 degrees, 0x3FFC68), its settings F2.8, 1/125 s, 0 EV, 5,500 K (indices 5, 3, 10, 6); each keeps
  its own turn and settings while the pause lasts. Left / Right (the menus' repeat) the next or last, round (0x400408).
- **The pad** (the race input in mode 2, 0x3FAD78; the replay's words of 0x43D288: 0x1 L1, 0x2 R1, 0x200 L2, 0x400 R2):
  nothing held: Left / Right another camera, else Up / Down the eye up or down, the sticks, and when they are still the first of
  Circle (upright or not), Triangle, Square (focus), Cross (shutter); L1 alone: Up / Down the exposure (and Cross the shutter on
  the focused place: with a place focused (Square), the camera's eye and that place carried with the followed car over the step
  (0x380A04 -> 0x37FA38: from the car's frame at the step's start, 0x3FD0A0 with 0, to its frame at the end, 0x3FCEA8 with 1)
  and the camera aimed at the place again (0x36EA98, 0x36EA50); the photo's pictures take the camera's view between the two at
  their time (CameraManager::vf2 -> CameraVariable::vf16 0x3796C8): a panning shot, the car sharp, ✔ ours by picture against the
  plain shutter's; without a focused place it is the plain shutter); L2 the white balance; R1 the aperture; R2 the shutter speed.
- **A step of the camera** (`CameraShot::vf10` 0x37FFB0; begun by vf39 0x37FBE0): the eye's height by up to 0.05 m a step
  (faster over 30 steps, 0x36F6A0); the zoom by the right stick up and down, up to 5 % a step (18 steps), between the closest view
  (2 atan(0.35 / d): a 0.7 m stretch, at least 2 degrees, set once a camera) and 83.2 degrees; the check of the new view (0x380C50 ->
  0x3FDD60): refused with the eye in the followed car's box (its width +0x0C, its length +0x00 + +0x50 ahead and +0x04 - +0x50
  behind, from its floor to its height +0x08) grown by 0.5 m + 0.7 x the camera manager's +0xA04 / tan(half the view's angle)
  (+0xA04 the near plane, 0.1 m: ✔ the original's memory in the photo drive's pause; +0xA08 the far, 5,000; ours the drawn car's
  box 🔎), or closer than 2 atan(0.35 / d); the
  first refusal keeps the view and makes it the camera's closest (when wider; +0x6C set, 0x3801D4); the turn by the left stick, up to 2 / 60 of the view's angle a step (18 steps),
  turned by the roll, the view's edges kept within the widest view's frame across (x 4/3) and between straight up and 41.6
  degrees under the first way (0x37FEE0); the roll by the right stick across, up to 2 degrees a step, held at 0 for 30 steps
  when it crosses it, within 90 degrees; a limit met says `disable` once. The settings' steps (0x620EB0 ..): F1.4 1.8 2 2.2 2.5
  2.8 3.2 3.5 4 4.8 5.6 6.7 8 11 16 22; 1/60 .. 1/4000 s in 19 steps; -1 .. +1 EV by 0.1; 2,500 .. 7,500 K by 500. The focus is
  on the car by itself (0x3FD3D8, as Photo Travel's), or after Square on what the middle shows, kept on that place as the camera
  turns (manual until another camera; 🔎 0x37F640 / 0x37F4E0 not read in full: the middle's place along the view).
- **The finder** (0x3C5430 -> 0x3C5AB8): the race through the camera with its depth of field and correction (0x3C58C0 ->
  0x3EFC10: a gain of 2^EV, the white balance against 5,500 K, the saturation 1 + `photo_saturation` / 10, then the filter's),
  410 x 308 at (20, 20) of the 640 x 480 picture, upright 308 x 410 (turned a quarter about (328, 20)); red marks at the edges
  the view is turned to (0x3C4F38), turned with the roll. The movie's words and values (0x3C40C0): `GETZOOM` "f = %dmm" (the
  focal length on a 24 mm high film, CameraVariable::vf33 0x3793A8), `GETIRIS` "F %.1f", `GETSHUTTER` "1/%d''", `GETEXPOSURE`
  "%c%.1f EV", `GETCURCAM` / `GETNCAM`, `ISPORTRAIT`, `ISFOCUSTRIGGER` (sound `zoom`), `GETFILTER` (the white balance under,
  at or over 5,500 K), `GETMF`, `ISWIDE` (the option `wide_mode`; here 0, the canvas is 4:3), `GETBLINK`. Its camera numbers ask
  for a font the disc lacks ("Futura CondensedBoldOblique"): the program then keeps its text object's own (0x490420); ❓ which
  is not read, drawn with `hb_24` 🔎.
- **The photo**: 1280 x 960 (upright 960 x 1280), the 32 pictures each from another point of the lens (as Photo Travel's) and
  at k / 32 of the shutter's time on (0x333680: the shutter in 60ths of a second, at most one step; 0x334730: the race drawn
  that far on, here its cars between this step's states and the next's), corrected as the finder, the course's vignette as the
  replay draws it. Then (0x3C51A0) the race's 0x17E0-byte display state is kept, the project `photo_save` runs with the argument
  `filmphoto` over the still race (its `SaveDialog`: where, and Film, Film and Photo or Photo), and the state comes back. The
  picture's record names the followed car and the course, and the race's mode at +0xD8 (the race's +0xD68 through vf96: 13 in
  the photo drive, ✔ the original's; 0 Photo Travel's).

## Films

Code: `src/gt4r/photo/film.*` (the data), `save/photo_file.*` (the card), `app/race_photo.*` and `screen_race.cpp` (saving at the
photo pause, the sequence `photo_develop`), `menu/game_photos.cpp`; `gt4tool photo film <file>` lists one and writes it again,
`gt4tool photo import <card image> <folder>` takes a card image's photos and films. ✔ A film the original saved in its photo
drive's pause (Tsukuba, the Ford GT, 140 mm): read and written again, the same 3,246 bytes; its card files (record, data
scrambled, icon) made again the same; developed here, its photo has the original photo's framing and the car's place and pose.

- **Saving** (`photo_save`'s `SaveDialog`: Destination slot 1, slot 2 or USB, Method Film, Film and Photo, or Photo; the options
  `photo_save_slot` and `photo_save_method` keep the choice; USB takes the photo only). Film and Photo writes the film, then the
  photo. A **film** is `MODE_SAVE_PHOTO_FILM`: `GT4MC::FileGT4FilmData` (0x1D0698): the folder `...F` + letters, the record named
  `F%04d` (films numbered on their own), the data scrambled as the film kind (0.654321, 0.345678, the bytes after the first word
  shuffled), the icon `icon/GT4_film_4_to_3_.ico` (`3_to_4_` upright) with the photo's small picture; its size on the card counted
  as a photo's (30 KB for the original's: the icon is most of it).
- **The data** (0x334E38 -> 0x334FC8): chunks of a tag (its four letters in order), a u32 size with the 8-byte head, the payload
  (0x45B4D8 opens one, 0x45B548 writes the size at its close; fields little-endian: u8 0x45AF18, bool 0x45AF58, u16 0x45AF78,
  u32 0x45AFC8, f32 0x45B018, NUL-ended text 0x45B040), nested:
  - `PMDI` u8 (the photo's +0x124, 1). `FILM`: u8 2, u16 640, u16 480 (a tile), u8 2, u8 2 (tiles), f32 4/3, bool upright, u8 32
    pictures, bool +0x124, f32 the shutter in 60ths of a second (+0x120: 0.48 at 1/125), then the photo's +0x128 the filter (the
    pause's +0x1A8), +0x12C (0 in the pause), +0x130 the saturation (1 + `photo_saturation` / 10), +0x134 the exposure (EV, the
    camera's virtual 16), +0x138 the white balance (K, virtual 17), +0x13C (0) (0x3C4DC4; 0x334B28 gives +0x134, +0x13C, +0x130,
    +0x138 to the correction 0x3EFC10), and 32 bytes +0x144: the car's frame in the picture, the JPEG's `G4PI` record.
  - `RHDR` (0x33A2A0): u8 2, the course's label, the race's kind ("photo_drive"), u8 cars, each car's label, its paints' label (the
    VARIATION rows' label less "_n": "ford0006") and a u8 (its paint here), a u32 the frames; `ENVP` (0x17F4 bytes of the race's
    display state, not in the pause's films), then a `CDTL` a car (0x33A1B0): u8 1, four texts (the driver's two looks
    "pdi_0014", two "default"), u8 (the entry's +0x345C).
  - `SFRM` a frame (0x33A018), two for a shutter with time (the step before at time 1, the step shown at 0; here the step shown and
    the next): `PCAM` (0x33AEA0 -> 0x33ADE8): u8 1, the eye, a target the focus distance (at least 1 m) along the view, the up axis,
    the angle across a 4:3 width (2 atan of the projection's x scale: 13.02 at 140 mm), the scales' ratio 0.75, the near plane
    (0.818 at 140 mm: 0.07 / tan of half the angle over the height, CameraVariable::vf12 0x3794A8), the far (5,000), the f-number,
    the shutter (s), EV, K. A `CARD` a car (0x339F38): `CPRM` (0x459E30, 370 bytes with its head): u8 0, then the car model's
    parameter sponsor (CarParamSponsor; the race's RaceCarParamSponsor 0x3D6F40 ..) at the frame's time: virtuals 4, 5 the road's
    frame under the car (angles, point: the shadow's), 6 the velocity (course space: F +0x648 as x, z, -y), 7 the body's angles
    (degrees: pitch, yaw, roll; the body turned by yaw about y, pitch about x, roll about z; front -z, right +x), 8 its point,
    15, 16 (0.02), 18 (a bool), 19 .. 22, a wheel each: 9 its angles (the spin, negative forward, unwrapped; the steering), 10 its
    place in the body's frame (x right, y up, z back: the Ford GT's (-+0.800, 0.227, -1.355) front), 11 the tire's width (0.235),
    12 its radius (0.334), 13 its sidewall (0.106), 14 the spin over a step, 9's spin now less a step before, 24, 25; then 26 a
    wheel each (-1), 23 the engine's rpm, 27 a vector a wheel. Then the driver's and co-driver's figures (`DRVR` / `NAVI`, 0x339AD0:
    960 bytes), the skid marks (`SMOK` 0x3E91E0, the last frame); the frame's gadgets (`GDGT` 0x3E5408) and particles (`PTCL`
    0x3C2990), the last frame.
  Here the pause's film carries the cameras (L1 + Cross's two), every car's parameters from its drawn state (the body, the
  wheels' places, spins and steering, the velocity, the rpm, the brake lamps at 18; the others 0 🔎), an empty `SMOK`; no
  `DRVR`, `GDGT`, `PTCL`. What it reads of them is kept and written again as it was.
- **Developing** (Photo Lab's Film folder, the third button "Develop the selected film", "Do you wish to develop the selected
  film?"): the film is loaded (`load`), `photo_labo`'s result is `develop`, and 0x1C7BF8 runs `RacePhotoDevelop` (0x335248 reads
  the film into a race of its course and cars, steps it until loaded, 0x3C8AA8 draws the photo at the film's settings): black
  while it is drawn, then `photo_save` without an argument (the photo saved as a photo: START Save, Square Return), then the lab
  again on its Film folder. Here the race screen's sequence `photo_develop`: the course and the film's cars (paint by the
  header's byte), each frame's cars set from their parameters as a replay's two steps, the photo pause's photo at once with the
  last frame's camera (the first's when it differs: the camera between them, as L1 + Cross's), the film's settings and car frame.
  ✔ Both ways by picture: the original's film gives its photo; ours develops to the photo it was saved with (mean difference 0.65
  of 255).

## Checked against the original

The reference's Times Square (saved states, the saved photo, the pages' pictures): the camera's place,
heading and angle, the car's place and heading from the saved state to the last digit shown; the pages
and dialogs by eye; the photo against the original's: the same geometry, levels within 2 to 5 of 255
in every part (the corners' darkening included), the car lit the same. The car's reflections are weighted by the course's factors as in a race (Times Square's
0.85 and 0.85, nothing head-on; [model.md](model.md) "A car in a race"): mean levels of the car 39.4 against the original's
37.1, its bonnet 52.2 / 49.8, its roof 31.6 / 27.3 (`~/gt4re-out/looks/bin/cmp.py`, the pictures blurred).
🔎 Left: the darkest parts are 2 to 5 levels lighter than the original's (the night sky 31.8 / 26.8). The live
view in the finder is too, by as much (its top 37.4 / 31.2, against `times_square_photo_page.png`, the page's grey
the same), so it is the scene's drawing, not the photo's steps: the correction's black level is 0 (0x3EFC10, +0x0C). It is
the course's fog: Times Square's (Env0 35 / 0.80) lifts the dark distance by our curve (visibility b + (1 - b) min(1, A / z))
more than the original's. The darks of the photo (red, the 2, 10, 25, 50, 75, 90 % levels of the top 200 of 240 rows): ours
7 12 19 27 45 84, the original's 4 10 14 22 41 79, ours without the fog 0 1 7 22 44 85 (`~/gt4re-out/photo2/pct.py`). 🔎 The
photo's pictures take the race's own fog (0x33E368 -> 0x33EAB8 -> 0x397CE0 -> 0x397DA0 with k 1), whose curve is GT4's
(gt4look2); the near car is lighter by as much (+2.5 .. +5), so 🔎 the gap is the GS's arithmetic (the fog's 8-bit blend with
the color halved, the frame doubled at the end: each truncation a level or two) rather than the curve: an engine matter.
