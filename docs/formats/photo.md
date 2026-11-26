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
| Not built | developing a film, the photo drive (`PhotoPause`, `racephoto.strb`) | |

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
  text[72] car name, ..., +0xD9 0xFF, u32 portrait at +0xDC}, scrambled with 0.314159 and 0.468643 and its
  bytes past the first four shuffled (films the same with 0.654321 / 0.345678);
- `icon.sys`, and `icon0.ico`: the disc's `icon/GT4_photo_4_to_3_.ico` (`3_to_4_` upright) with a
  64 x 64 of the photo in its texture's top left, packed again when it fits (0x1D3E20).

Its size on the card counts each file up to a KB, half a KB an entry, and one more (0x1CC9A8). Checked
with `gt4tool photo <disc> check <card>` on the reference card: the record, both files scrambled again
and the icon come out the same bytes; a photo taken here lists, shows and checks the same.

The app's save files are no card: the photo pages' card manager always answers a card, formatted, with room (`free` 8,000), in
either slot the one folder; the page that asks where to save and as what (`SaveDialog`) is answered without
showing it (the photo, the save files); a write that fails says so once. The other questions of the scripts about
a card are answered in the menus.

The **slide show's list** is the folder `BASCUS-97328LSTSLIDE` (76 KB by the program's count): 400
places of u64 (the folder's number, its letter in the top byte), u32 count at 0xC80, padded to 0xCC0
bytes and not scrambled (0x1D0F38). The slide show face shows the list's photos, or the disc's
(`photo/%03d.JPG`) when the card has none or the face's `demo` is set (0x1C6BE8).

## Checked against the original

The reference's Times Square (saved states, the saved photo, the pages' pictures): the camera's place,
heading and angle, the car's place and heading from the saved state to the last digit shown; the pages
and dialogs by eye; the photo against the original's: the same geometry, levels within 2 to 5 of 255
in every part (the corners' darkening included), the car lit the same. The car's reflections are weighted by the course's factors as in a race (Times Square's
0.85 and 0.85, nothing head-on; [model.md](model.md) "A car in a race"): mean levels of the car 39.4 against the original's
37.1, its bonnet 52.2 / 49.8, its roof 31.6 / 27.3 (`~/gt4re-out/looks/bin/cmp.py`, the pictures blurred).
🔎 Left: the darkest parts are 2 to 5 levels lighter than the original's (the night sky 31.8 / 26.8). The live
view in the finder is too, by as much (its top 37.4 / 31.2, against `times_square_photo_page.png`, the page's grey
the same), so it is the scene's drawing, not the photo's steps: the correction's black level is 0 (0x3EFC10, +0x0C).
