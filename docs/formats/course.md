# Courses

Code: `src/gt4r/formats/course/` (the file and the road), `src/gt4r/race/course_scene.*` (the
course as the race view draws it), `gt4tool race <disc> sections | models <course> [slot]`. `crs/<name>` (packed, 4-12 MB) is one course layout; the US
disc has 110, with a `crs/<name>.ad` file each for the computer drivers (below) and the license tests'
driving-line models in `crs/bestline/`.
All 1,145 model sets of the 238 files under `crs/` decode.

## Container

Gran Turismo 3's table of section offsets
([engine/gt3r/docs/formats/course.md](../../engine/gt3r/docs/formats/course.md)) grown from 48 to
64 u32 slots, with the same roles in the slots both have. Slot 0 is 0 and slot 1 starts at
0x100. A section runs to the next section's offset.

| Slot | Tag | Contents |
|---|---|---|
| 1 | `MDLS` | The scenery of the race view |
| 2, 3, 4 | `Vls0` | Its visibility lists: the race view, the mirror, a two-player half |
| 5 | `MDLS` | The scenery drawn into the cars' reflection picture (6: its lists) |
| 7 | `Vls0` | Slot 1's lists for the mirror of a two-player half |
| 8 | `Vls0` | Slot 1's lists for the race view's second mode (63 courses; "Visibility lists" below) |
| 9 / 13 | `MDLS` | The scenery mirrored below the road, and the road mask of that reflection (10 / 14: lists; "Road reflection" below) |
| 17 | `MDLS` | Drawn after the cars (`ss_shibuya`, `ss_snow`, `ss_tsumago` only; slots 18-20 would hold its lists, no course has them) |
| 21 / 22 | `MDLS` | Sky dome / distant backdrop |
| 23 / 24 | `MDLS` | The reflection picture's sky / the mirror's sky |
| 26 | `Tex1` | Tire smoke and dust: one 128x64 T8 picture (0x2480 bytes on every course) |
| 27 | `MDLS` | The driving line of the license tests (9 courses) |
| 29 | `GTFX` | Lamp glows |
| 30 | `BLBd` | Billboards |
| 32 | `Env0` | Environment: far clip, fog, lights, sun |
| 33 | `GTCM` | The course map of the race display |
| 35 | `CRSS` | Ambience emitters |
| 37 | | The glow meshes of the lamps (the layout of Gran Turismo 3's `race/flare.bin`; "Lamp glows" below) |
| 38 | `Tex1` | The sun glare picture, 512x512 T8 (93 courses; the same bytes as the disc's `race/glare.img`) |
| 39 | `Tex1` | Particle pictures: one 16x16 and eight 32x32 (`race/particle.img`) |
| 40 | `Tex1` | The lamp streak of the road reflection, 32x64 T4 (`race/reflection.img`) |
| 42 | `Tex1` | One 256x128 T8 picture on ten loose-surface courses (assisi, capri, grandcanyon, icerace, nursnow, paris_rally, smtsouth, tahiti_maze, yosemite_dirt and its short layout) ❓ |
| 43 | `MDLS` | `ss_snow` only ❓ |
| 44-46, 48 | `REP4` | Replay cameras (44); two shots round the car the program does not use (45, 46); the scenic shots behind the race's menus (48) |
| 47 | `GDGT` | Cones and boards |
| 49 | `Tex1` | A picture of the race display (256x64 or 256x128 T8; 102 courses) ❓ |
| 50 | `MDLS` | The sky of the road reflection (35 courses) |
| 51 | `CRPM` | The 15 `ss_` photo locations: where the camera and the car may stand, the map ([photo.md](photo.md)) |
| 52, 53 | `Mot1` | Camera motions: the race's start shots (52), a replay's (53) |
| 55 | `RNW4` | The road: surface, walls, checkpoints, start grid (below) |

Checked on all 110 courses: every one has slots 1-7, 21-24, 26, 29, 30, 32, 33, 35, 37, 39, 40,
44-48, 52, 53 and 55; slot 9 is on 85, 10 on 64, 13 on 99, 14 on 95. The program gets the
effect pictures and meshes from slots 37-40; it never opens `race/flare.bin`, `glare.img`,
`particle.img` or `reflection.img`.

`Vls0`, `CRSS`, `GTFX` and `BLBd` read with the shared engine's parsers as they are.

## Scenery

The model sets are `MDLS` ([model.md](model.md)); positions are packed at a scale of 0.002 m.
Most shapes are pre-lit (vertex colors). The stored colors are darker than Gran Turismo 3's
(mean about 0.48 of full scale on Tsukuba, 0.38 on Grand Valley, 0.57 on the Nürburgring): the
game draws the whole 3D picture at half scale and multiplies the finished picture by `Env0`
+0x38, which is 2.0 on all 110 courses ("The picture's scale" below). Scaling the stored
colors by +0x38 gives the same picture.

Animated objects (flags, people) are driven by each set's script through registers ("Scripts"
below); a still scene reads every register as 0.

The programs' render state and the shapes' kinds are the shared engine's
([its course.md](../../engine/gt3r/docs/formats/course.md), "Render state", "Lighting"), and
the scenery is decoded with the same options as its own courses: cut-outs by the programs' alpha
test, the blended decals without depth writes, and the second pass of kind 4 over the road, the
curbs and the fences (blend 0x58: added where the frame's alpha, written by the pass under it,
lets it). Left as an ordinary pass that second pass covers what is under it: Tsukuba's noise
barriers come out black. 🔎 The kind 4 pass is taken to be the shared engine's road sheen (the
first scene light's specular term); its arithmetic has not been read in this program.

## Env0 (slot 32)

0x1C00 bytes on every course. It is Gran Turismo 3's environment block
([engine/gt3r/docs/formats/course.md](../../engine/gt3r/docs/formats/course.md), "Env0") with
the same header (+0x04 u32 0x26E2C0, +0x0C u32 0x2D0) and **every field Gran Turismo 3 has at
the same offset**, grown from 0xF40 to 0x17CC bytes of fields. Bytes the game never reads hold
leftovers (pointers, pieces of other blocks), so a field map cannot be guessed from the data
alone: the table says how each row is known. *read*: the program reads the field at this
offset for this purpose (the routine was located through the course file's slot 32 pointer and
through the function map from Gran Turismo 3's program). *data*: no reader located; the values
on the 110 courses fit Gran Turismo 3's field. Values f32 unless noted.

| Off | Meaning | Known from |
|---|---|---|
| +0x10 | u32 flags (below) | read |
| +0x20 | far clip (m): 5000 on 65 courses, 600 to 3330 on the others. A two-player half uses half of it. The sun's glare reads it too (the shared engine puts the sun at far / 2) | read |
| +0x24, +0x2C, +0x40, +0x44 | ❓ no reader found (+0x24: 1.0 on 55 courses, 0.34 to 1.4; +0x44: 0 or 1). Gran Turismo 3's files have them too | |
| +0x28 / +0x48 | yaw / pitch (degrees) of the reflection picture's sky (slot 23) | read |
| +0x30, +0x34 | the reflection factors of the scenery's environment-mapped shapes: at the start of a model set's draw the two color factors of those passes are C0 = +0x34 and C1 = +0x34 x +0x30 (color = vertex color x (C0 + C1 min(cos, 0)), the shared engine's kinds 2 and 3, where cos is negative on a face turned to the camera). +0x30 is 0.8 on 87 courses and +0x34 1.0 on 87: such a shape reflects in full at a grazing angle and 20% head-on. Draw commands 0x56 (these two again) and 0x57 (C0 = 1, C1 = 0) switch within a program. Times Square (1.0, 0.85: nothing head-on) bears it out: its glass fronts show no reflection facing the camera in the original's photo. No course's program sets its own factors (0x32, 0x33, 0x36, 0x37, 0x56, 0x57 are on none) | read |
| +0x38 | the picture's scale, 2.0 on all 110 (below) | read |
| +0x3C | a second factor on the scenery alone (below); 1.0 on all 110 | read |
| +0x4C | brake disc glow gain (the car's wheel code reads it with +0x38) | read |
| +0x50 | texture-list threshold of the scenery ("The race view's frame"); 0 on all 110 | read |
| +0x54 | 220 or 200 (one 440): the car LOD size threshold in the shared engine's layout | data |
| +0x58 | headlamp pool strength (non-zero on 22 courses) | read |
| +0x60..+0x68 | fog RGB (0..1), in the finished picture's units | read |
| +0x70 / +0x74 | fog distance A / minimum visibility b, the shared engine's formula (per vertex from the view depth W: F = trunc(255 b + 0.1 + (255 - 255 b) min(W, A) / W)) | read |
| +0x80..+0xA4 | ❓ ten values shaped like a second sun (RGB, 1, elevation, azimuth, ...). No reader found; the same in Gran Turismo 3's files | |
| +0xB0..+0xC8 | heat haze: u32 lens count (400 everywhere), lens size, box half-width, vertical cap, distance ahead, drift ranges | read (the routine is unchanged) |
| +0xD0 + 0x80 i | car light i (4): direction towards the light, +0x10 ambient, +0x20 diffuse, +0x30 specular (RGB, w 1), then the rest of the shared engine's record (+0x40..+0x7C) | read |
| +0x4D0 + 0x44 i | six tire smoke / dust kinds (RGB, opacity, size, growth, ...) | data |
| +0x914 / +0x91C | u32 smoke picture divisors (1; 2 on two wet courses) | data |
| +0x930..+0x938 | the depth of field: +0x938 the nearest focus (m); +0x930 an f-number and +0x934 a focal length (mm) with no reader: the lens is the camera's ("The passes of a replay") | read (+0x938) |
| +0x950..+0x974 | vignette: outer / inner radius, outer RGBA, inner RGBA | read |
| +0x980..+0x994 | color grade: a gamma per channel, black, white, saturation (the screen settings' correction with at most 235 a level, strobe.md "The pass") | read |
| +0x9C0..+0x9E8 | sun: RGB, occlusion weight, glare size, disc size, disc gain, elevation, azimuth (degrees), i32 style (0 on 96 courses, 2 on 12, 1 on 2), occlusion box | read |
| +0x9F0..+0xA1C | the cars' lamp glow type: RGB, intensity, glow size, halo size, halo gain, attenuation c / l / q, max distance, +0xA1C; with the new +0x17A0 | read |
| +0xA60..+0xA78 | car shadows: RGB, alpha scale, direction elevation / azimuth (degrees), length | read (+0xA70..+0xA78), data |
| +0xA90..+0xAA4 | the mirror's fog: RGB, +0xAA0 A, +0xAA4 b | read |
| +0xAB0..+0xACC | the cars' shade factors (below) | read |
| +0xAD0 + 0x50 i | scene light i (4): direction, +0x10 ambient, +0x20 diffuse, +0x30 specular | read |
| +0xC10..+0xC1C | **new**: the road reflection's color RGB and gain ("Road reflection") | read |
| +0xC20..+0xC2C | road reflection: u32 blend mode of the composite (0x58 on 104 courses, 0x54 on 6), buffer width / height (64 x 64 on most; 32 to 256), blur passes | read |
| +0xC40..+0xC54 | the reflection buffer's fog: RGB, +0xC50 A, +0xC54 b | read |
| +0xC60 + 0x40 i | lamp type i (8): RGB, intensity, glow size, halo size, halo gain, exponent, attenuation c / l / q, max distance, occlusion box, blink period | read |
| +0xE60..+0xE7C | lamp streaks of the road reflection: RGB, gain, widths, cap, length | read |
| +0xEA0 / +0xEA4 | wheel blur factors. The game overwrites +0xEA0 with 1.0 when it loads the course (the file has 0.8 or 1.0) | read |
| +0xEC0 / +0xEC4 | the two factors of the panoramic reflection mapping (4 / 0.25 or 15 / 0.3 on most courses) | read |
| +0xEF0..+0xEF8 | global ambient of the cars (0 by day; 0.02 to 0.3 on 13 night courses) | data |
| +0xF30 / +0xF34 / +0xF38 | u32 draw flags of the one-player view, the two-player halves and the mirror: bit 0 draws the billboards. 0xFFFFFFFE in the halves and the mirror on 38 courses | read |
| +0xF50 + 0x40 i | ❓ 32 records {RGB, f32, f32, f32, u32 1} with the shape of the color grade's values. No reader found | |
| +0x1750..+0x1764 | **new**: a glow filter on the finished picture, on when u32 +0x1750 bit 0 is set (`ss_gion`, `ss_sagano`, `ss_sanmarco`, `ss_shibuya`): a threshold a (+0x1754), b and c (+0x1758, +0x175C), a radius r (+0x1760 x the picture's diagonal / 781.22, that of 640 x 448) and a count n (+0x1764). 0x33F1C8 -> 0x3BADF8: for each of n x n offsets (i r / n, j r / n) the picture is sampled into one of (W / r) x (H / r) from that offset, 255 a taken off it (ALPHA 0x62 with a white box), and added into a picture of W / 2 x H / 4 at the offset at the GS weight min(trunc(128 c / ((1 - a) n^2)), 255) / 128 (held to 255 a level); that picture is then added over the frame at min(trunc(128 (1 - a) b), 255) / 128 (0x4A2808's fixed weights). Nothing for a < 0, r <= 0 or n <= 0. Values: `ss_gion` 0.8 / 2 / 20 / 24.9 / 3, `ss_sagano` 0.85 / 2 / 2 / 8.1 / 3, `ss_sanmarco` 0.7 / 1 / 0.2 / 20 / 4, `ss_shibuya` 0.73 / 1.9 / 1.9 / 30 / 5 (bit 1 of +0x1750 on the first two: ❓ no reader). Ours (`race/glow_filter.*`): the engine's glow pass, a box mean of the thresholded picture centered on each place (🔎 the program's copies sit up to half a box off), where the engine has it | read |
| +0x1794..+0x179C | **new**: the strengths of the cars' three lamp lists' glows (`RaceCarModel::vf36` 0x3911D0): the lamps lit with the lights, the brake lamps (times the brake's ramp), the tail lamps (non-zero on 25 courses, most of them the night ones: 1.0 0.6 0.6 on route5, 1 1 1 on `ss_shibuya`, 0.75 0.5 0.5 on the Nürburgring) | read |
| +0x17A0 | **new**: the factor on the power of a car lamp's directional factor (the lamp's +0x60, [model.md](model.md); 0x3B8530): 1.0 on 91 courses, 4.0 on 10, so their glows narrow. 0 reads as 1.0 (0x397C38) | read |
| +0x17B4..+0x17C8 | **new**: an RGBA color and two size factors of the cars' exhaust flame (`race/after_fire.mdl`) 🔎. When the RGBA is all 0 the six read as 1.0 | read |

### Flags (+0x10)

| Bit | Meaning | Known from |
|---|---|---|
| 0-3 | car lights 0-3 (bit 0 on all 110; 1-3 on night courses) | read |
| 5 | heat haze (83 courses) | read |
| 6 | vignette (all 110) | read |
| 7 | color grade (`ss_sagano` only) | read |
| 8 | set on all 110; no reader: the camera asks for the depth of field | |
| 10 | sun disc and glare (85 courses) | read |
| 11-14 | scene lights 0-3 (bit 11 on 100 courses; 12 on hongkong and seattle) | read |
| 15 | night: the cars get time zone 1 (their models' script variable `timezone`: lamps on) and their lamp glows. 24 courses: route5, hongkong, seattle, lasvegas, paris_rally, the three motegi layouts, sarthe, sarthe24, sarthe_old24, seven `ss_` | read |
| 16 / 17 | mirrored cars / lamp streaks in the road reflection (13 courses each) | read |
| 19 | evening: time zone 2; it wins over bit 15. No course sets it | read |
| 20 | in the several-view draw (below) with two players: the cars' reflection picture is not redrawn (seoul, `ss_gion`) 🔎 | read |
| 21 | the `GTFX` lamp glows (18 courses) | read |
| 22 | in the several-view draw: no lamp glows at all (midfield, `ss_gion`) | read |
| 23 | read by the cars' draw (laguna, `ss_rialt`) ❓ | read |
| 24 | in the several-view draw: runs the road reflection pass. No course sets it | read |
| 4, 9, 18 | ❓ (18, 20 and 49 courses) | |

The time zone is 2 with bit 19, else 1 with bit 15, else 0, as in the shared engine; Gran
Turismo 4 hands it to one car model instead of picking a model file.

### The picture's scale (+0x38, +0x3C)

The race view draws the sky, the backdrop, the scenery and the cars with colors at 1 / +0x38
of their final strength, and multiplies the finished 3D picture by +0x38 before the glow
filter, the smoke and glows, the color grade, the vignette and the race display: one full-screen pass that
multiplies what is in the frame (GS blend fixed factor min(255, 128 x +0x38): 255 / 128 =
1.992 for 2.0). What the game does to keep each part in step:

- **Stored colors** (the vertex colors of the sky, backdrop and scenery) get nothing done to
  them: they come out doubled, and the files hold them at about half the strength of Gran
  Turismo 3's (means of the scenery's pre-lit vertices: 0.49 of 0x80 on Tsukuba, 0.38 on Grand
  Valley, 0.52 on `ss_shibuya`, 0.57 on the Nürburgring). 🔎 Some skies have vertex colors of
  0x80 throughout (Nürburgring, Grand Valley): their pictures were not looked at.
- **Fog color**: the color given to the GS is the `Env0` color x 1 / (+0x38 x +0x3C) while the
  backdrop and scenery are drawn and x 1 / +0x38 while the cars are drawn (the sky has no fog).
- **The scenery** is multiplied once more, by +0x3C, after it is drawn and before the cars
  (the same kind of full-screen pass; 1.0 everywhere, so nothing happens on this disc).
- **Car lights** (+0xD0) and the scene lights as the cars' and lit objects' renderer gets them
  are multiplied by 1 / +0x38, after the shade factors: for the car's shade s (0..1) and a
  second factor t, ambient x ((1 - (1 - s)(1 - [+0xAC8])) (1 - [+0xACC]) t + [+0xACC]) / +0x38;
  diffuse the same with +0xAB4 and +0xABC, specular with +0xAC0 and +0xAC4. This is the shared
  engine's shade rule with the division added.
- **The light on a car** (0x38E7F8; `race/car_light.*`). On a course without lamps s and t above are the
  road's two values under it (+0x0E / 255 and +0x0F / 127); the cast shadow's factor is
  ((+0xAB0 - +0xAB8) s + +0xAB8) t (0x3983E0). Times Square's road has 0 and 0 everywhere: its cars get only the
  floors +0xACC, +0xABC, +0xAC4 of the lights. The global ambient +0xEF0 (over +0x38) is not shaded. On a course with lamps the car
  is lit by the strongest four of those its triangle names: weight 1 / (c0 + c1 d + c2 d^2) of the lamp's record,
  x cos^e inside its cone; each casts a shadow away from itself (+0xA78 long, the record's +0x60 color and alpha), and
  up to six are drawn into the cars' reflection picture (0x38F5D0; not here).
- The brake disc glow reads +0x38 with +0x4C.
- **The sun's glare, the lamp glows and the tire smoke are drawn after the multiply** in the
  one-view draw, so their `Env0` numbers mean what they mean in the shared engine. (The
  several-view draw puts them in before its multiply and gives the smoke 1 / +0x38 as its color
  factor; 🔎 nothing of the kind was found for its glows.)
- The sun's disc is drawn right after the sky, before the multiply, by the shared engine's routine (0x398650 -> 0x4986D8:
  the sun's RGB x gain / 2, added at As / 128, no division): the same numbers give twice the strength they give in the shared
  engine. Here its passes go in once per unit of +0x38 (`RaceWorld::collect`). 🔎 Not compared with the original running (no
  capture with the disc in sight).

An adapter that presents this block to the shared engine therefore keeps the fog colors, the
lights, the glare and the lamp types as stored, scales the stored colors of the model sets by
+0x38, and the sun disc's strength by +0x38.

### Gran Turismo 3 offset → Gran Turismo 4 offset

Every field the shared engine reads is at the same offset (checked field by field against the
program's readers above, and the values of five Gran Turismo 3 courses against these): the
flags and their bits, +0x20, +0x28 / +0x48, +0x4C, +0x50 / +0x54, +0x58, +0x60..+0x74, the haze
+0xB0..+0xC8, the car lights +0xD0, the smoke kinds +0x4D0 and +0x914 / +0x91C, +0x930..+0x938,
+0x950..+0x974, +0x980..+0x994, the sun +0x9C0..+0x9E8, the car lamp type +0x9F0..+0xA1C, the
shadows +0xA60..+0xA78, the mirror fog +0xA90..+0xAA4, the shade factors +0xAB0..+0xACC, the
scene lights +0xAD0, the road reflection +0xC20..+0xC54, the lamp types +0xC60, the streaks
+0xE60..+0xE7C, +0xEA0 / +0xEA4, +0xEC0 / +0xEC4, +0xEF0 and the view flags +0xF30..+0xF38. The
block can be handed over as it is; the differences are in values and rules:

| Field | Difference |
|---|---|
| stored colors, the sun's disc | the picture's scale +0x38 (above) |
| +0x30 / +0x34 | new use: the default reflection factors of environment-mapped scenery (the shared engine's default is C0 = 1, C1 = 0) |
| +0xEA0 | reads as 1.0 whatever the file holds |
| +0xC10..+0xC1C | new: the road reflection's color and gain; the pass runs on every course with slot 9 or 50, not only wet ones |
| +0x9F0 group | one more factor at +0x17A0 |
| flags 15 / 19 | the time zone goes to the car model's script, not to a model folder |
| +0x50 | 0 on every course: the full texture list always |
| far clip | 5000 on most courses against 1000 |

## The race view's frame

How the game draws a course, from its race class's draw routines (`RaceCourse` holds the
course file; the model sets are drawn through its `ModelSet2` callback). There are two paths:
the one-view draw of a normal race or replay, and a several-view draw (two players, linked
screens) that differs where noted.

1. Clear color and depth. Texture-list threshold, projection, then the environment:
   the scene lights (+0xAD0, flag bits 11-14, global ambient 0), the car shadows' direction,
   the fog (+0x60), the panoramic factors (+0xEC0), the reflection factors (+0x30 / +0x34).
2. **Sky** (slot 21): all its models, with the view's translation removed, no fog, and its own
   depth range (near 1, far 1000; 🔎 the near value). The dome needs that depth: its last shape
   is a flat top (Tsukuba: a square 40 m to each side at y = 32) whose corners lie behind the
   upper ring and are darker than it; drawn over the ring without a depth test, a dark corner
   shows in the sky. Then the sun's disc (flag bit 10), and the fog color is set for the
   scenery.
3. **Backdrop** (slot 22): the frame's alpha and depth are cleared; all its models are drawn
   with the scenery's fog at their true distance and the projection's depth term multiplied by
   100 (the depth of a point is that of a point 100 times nearer; Gran Turismo 3's 0.01
   modelview scale is gone). Depth is cleared again after it.
4. **Scenery** (slot 1), model by model from the `Vls0` list of the camera's place, in the list's
   order, then the billboards of the same models (view flag bit 0). The model's own bound (8
   box corners in the model record) culls it against the view. Texture list: **the first (full)
   list always in the one-view draw**; the several-view draw picks by the model's size on
   screen s against T = +0x50 (s > T: list 0, > T / 2: 1, > T / 4: 2, else 3, limited to the
   lists the set has; T is 0 everywhere, so list 0) and forces the last list with two players;
   the mirror forces list 1.
5. A model set the course object loads apart from the course file, when its switch is on
   (🔎 the driving line of `crs/bestline/`, below).
6. **Road reflection** (below), in the one-view draw.
7. The frame is multiplied by +0x3C.
8. The cones and boards (`GDGT`) lit by scene light 0 and the pit crews (🔎 both named from
   their neighbors in the program), the **cars** (fog color x 1 / +0x38), then **slot 17**,
   all its models (three photo locations have one: on `ss_shibuya` 16 models drawn with
   blending, within about 150 m of the origin).
9. The mirror's picture is drawn in the same way into its own buffer: its fog (+0xA90), its sky
   (slot 24), slot 1 by slot 3's list (slot 7's in a two-player half that has it) with view
   flags +0xF38 and texture list 1, the multiply by +0x3C, the cars.
10. The sun's glare, the cars' lamp glows (always called, with the night flag) and the `GTFX`
    lamp glows (flag bit 21) are collected: each is placed and its occlusion test is started;
    nothing of them is drawn yet.
11. The frame is multiplied by +0x38, then the glow filter (+0x1750) runs.
12. Heat haze (flag bit 5), the tire smoke, the collected glare and glows, the color grade
    (bit 7), the vignette (bit 6), then the race display.

### The passes of a replay

Code: `race/replay_effects.*` (when, and the numbers), drawn by the shared engine's passes (`View3D::DrawOptions`). They belong
to the race's play modes but 0 (race +0xCC8: 1 a B-spec race, 2 / 3 a replay, 4 the scene behind the menus), in the one-view draw:

| Pass | When | What |
|---|---|---|
| Depth of field (0x33E520 -> 0x399998; `RacePS2Base::vf140` 0x3431B8) | the play mode is not 0 and the camera's flags have 0x4000 (a replay record's +0x10 over 0); **no flag of `Env0`** | Gran Turismo 3's pass, step for step (rechecked: 0x399998 with the lens 0x3993D0 / 0x399490 / 0x3994C0: near steps 1, far 2, near blur up to 4, far 8, f/2 and 35 mm as made; the composite multiplied by `Env0` +0x38, the picture's scale): near blurs of 4, 2, 1 lines at the levels 2, 1, 0 of the pyramid, the sharp picture, far ones 2^(k/2) up to 8; the blur unit film / the picture's lines. The lens: 24 mm x the screen's aspect (`CameraManager::vf13` 0x371CA8: 1 in a race) on the film 1.333 times as wide as high, the camera's f-number (its +0x150 / 100), its focus no nearer than `Env0` +0x938 |
| Heat haze (0x496638, the lenses' step 0x4965D8 every picture) | `Env0` bit 5 and the camera's flag 0x2000 (a record's +0x0C); any play mode | Gran Turismo 3's lenses; the vertex order differs, the arithmetic does not |
| Grade | the screen settings of the replays (`replay_color_correction`) unless the camera manager has its own (Dive's, its +0xBFD) or `Env0` bit 7 | [race_display.md](race_display.md) |
| Vignette (0x3BBBC0) | the play mode is not 0, `Env0` bit 6 and the race's `vf24` 0 (all but the linked races) | a ring of 32 steps over the 640 x 448 frame |
| Dissolve, then trail (`RacePS2Base::vf106` 0x33F890) | a cut's dissolve seconds; the trail only in Dive ([camera.md](camera.md)) | the frozen picture at 1 - t / T; the last picture shown at min(weight, 0.8) |

Seen in the original's saved states: the only states with flag 0x4000 (Tsukuba's shot at (-173, 11, -47), f/2.8) have 4.36, 11.43
and 14.80 degrees and the focus at 75.7, 34.5 and 21.4 m; ours there 76.0, 34.1 and 21.4, the pictures blurred the same. Of the 6,386
records 2,109 ask for the depth of field and 227 for the haze (Trial Mountain 84 of 98, Grand Valley East 10 ...); of the 475 scenic
shots 18 and none.

The player's switches ([video] `depth_of_field`, `heat_haze`, `replay_vignette`, `replay_trail`): all as the game's but the haze, off
unless chosen (in a sharp picture its lenses read as jitter). What runs where: the rules and numbers are Gran Turismo 4's, the
drawing the shared engine's (above 448 lines its blur follows the level's spread; the haze shifts the picture's field-sized part).

The several-view draw differs: no slot 17, no road reflection (flag bit 24), the glows without
occlusion tests, and the smoke and glows drawn before the picture is multiplied by +0x38 as it
is copied to its place on the screen.

The reflection picture of the cars is drawn before all this as in the shared engine: its sky
(slot 23) turned by +0x28 about the vertical and then +0x48, depth cleared, slot 5 by slot 6's
list of the car's place. ✔ Its camera (`DynamicEnvmapCamera`, 0x44EEA8; the panels 0x44E660) has the shared engine's numbers: 0.8 m
above the car, tilted 15 degrees, three panels (79.975 degrees, 120 degrees apart) to 200 m from 0.005 m, warped at 0.9 into the
sphere map. The race's map is 128 x 256 pixels (race + 0xE330 = 256, halved across; 512 in the photo drive, 0x3338A8) of 32-bit color
(its frame's format 0): GT4 keeps no 5-bit steps in it. Here the engine's cube (128 pixels a face, `env_gt3_lookup`, 200 m), without
`quantize_rgb5`.

### Road reflection (slots 9, 13, 50)

Code: `CourseScene::reflection_course` hands slots 9, 10, 13 and 14 to the shared engine's pass
(`engine/gt3r/src/race/wet_road.*`) as a course file of its layout; slot 50's sky is drawn into
the buffer first. The buffer's items are drawn at the strength (+0xC10..+0xC18 averaged) x
+0xC1C. Not drawn: the mirrored cars and the lamp streaks.

The shared engine's wet-road pass, on every course that has slot 9 or slot 50 (no flag asks for
it; 85 courses have slot 9): dry roads get a faint mirror of the sky and the backdrop, wet ones
the full scenery.

- Slot 13 (the road, drawn by slot 14's list of the camera's place where there is one) writes
  the mask.
- A buffer of +0xC24 x +0xC28 pixels is cleared, its fog is +0xC40..+0xC54, and it gets slot 50
  (a sky, drawn like the sky dome) and slot 9 by slot 10's list: the scenery mirrored below
  the road. On dry Tsukuba slot 9 is three far models (the horizon, about 700 m below it) and
  the buffer 64 x 64; on wet Tsukuba it is 28 models, 256 x 128 with 2 blur passes; Grand
  Valley's is one model and a slot 50 sky.
- Mirrored cars (flag bit 16) and lamp streaks (bit 17, the picture of slot 40) go in, then the
  blur passes (+0xC2C). The race view mirrors each car (but the in-car view's own) in the plane
  through its tires' contacts (`CarVisual::add_mirror_items`, as the shared engine's).
- The buffer is put on the road with the blend mode +0xC20 and the color (+0xC10, +0xC14,
  +0xC18) x +0xC1C (gain 0.5 on 74 courses; dry Tsukuba 1 1 1 x 0.5, wet Tsukuba 0.9 0.9 0.85
  x 1.0 with mode 0x54).

🔎 The mask's and composite's GS states were not re-derived; the routine is the counterpart of
Gran Turismo 3's.

### Visibility lists

`Vls0` is unchanged and the lookup (grid cell under the camera, nearest point of the cell, its
list) is the same routine. Which lists belong to which view, from the draw routines:

| Slot | For | View |
|---|---|---|
| 2 | slot 1 | the race view (and the `GTFX` lamp lists) |
| 8 | slot 1 | the race view when the race object's list switch is off (🔎 who turns it off was not followed; it starts on). A course without slot 8 then draws every model, and the reflection's slots 9 and 13 are drawn whole too |
| 4 | slot 1 | a two-player half |
| 3 | slot 1 | the mirror |
| 7 | slot 1 | the mirror of a two-player half (slot 3 without it) |
| 6 | slot 5 | the cars' reflection picture |
| 10 / 14 | slot 9 / 13 | the road reflection |
| 18 | slot 17 | (absent on the disc: slot 17 is drawn whole) |

## Scripts of the model sets

Code: `src/gt4r/formats/model/model_script.*` (the machine), `src/gt4r/race/animated_models.*`
(a course's models that pick a key shape and a blend weight: decoded once per key shape, blended
each frame), `gt4tool race <disc> script <course> [slot] [ms]`. A model whose program moves or
turns it whole by registers (0x44 .. 0x4B before its shapes) is moved so each frame: the turning
sign of seoul (model 44) and of lasvegas (41). Seoul's program: push, translate to the sign's
place, push, run the script, 0x48 by `builtin_ra` / `rx` / `ry` / `rz` (cells 0 .. 3: the angle in
degrees, then the axis; 90, (0, 1, 0) at 3 s), push, translate and scale the shape, draw, pop x 3:
the turn about the sign's own vertical axis (the race view takes the decoded shapes back through
the literal matrix at the first move, turns them and puts them back). Icerace's walkers have a second
register switch (`cb`, the person's clip): each pair of cases is a key shape.

**The animated skies** (seoul, tahiti_maze, sarthe24 and their reversed courses; slot 21): aeroplanes crossing with their
trails, 2, 2 and 8 of them, thin strips on the dome's scale (14 vertices: the aeroplane, the trail). Their model's program has
per aeroplane 0x4C (its weight, `builtin_tw`) and 0x4D (its switch, `builtin_br`, 4 cases), each case a shape with two keys of
position and colour (the alpha fades it in and out); the draw script sets both from `time`: 25 s a case and 12.5 s on seoul and
tahiti_maze. `AnimatedModels::Layer` decodes each layer's cases apart.

**Checked**: `gt4r_diff <disc> course_script <course> <eeMemory.bin>...` takes a set's registers out of a saved state (each
model's as its draw script left them when it was last drawn) and finds the time at which ours are the same: all 43 of Ice
Arena's models that do not ask about the cars (24 walk) in four of five states (`35-ice-arena`), all 19 flags of Tsukuba in
three. (The script's arithmetic is the EE's here; the states' last bits, an emulator's, are not judged.)

The sky, backdrop and scenery sets carry a small program per animated model: flags, people who
walk or step back from the road, turning signs. Counts on the US disc: 3,347 scenery models
with a draw script on 66 courses, 785 with a per-tick script on 19, six animated skies (seoul,
tahiti_maze, sarthe24 and their variants). Checked on all 110 courses: every script of slots 1
and 21 decodes with the commands below.

### Tables in the `MDLS` header

```
0x20 u16 cells of variable segment 0 (the registers the draw commands read)
0x22 u16 cells of segment 1 (0 on courses; 🔎 by its place between the two)
0x24 u16 host variables              0x4C u32 -> {u32 name, u16 cells (1), u16 cell} each
0x28 u16 named registers             0x54 u32 -> {u32 name, u16 cells (1), u16 cell} each
0x5C u32 script code
0x7C u32 -> the set's instance: {u32 (the set, filled at load), u32 -> segment 0,
            u32 -> segment 1, u32 -> segment 2 (the host variables), u32 -> a cell per host
            variable for the routine that fills it, u32 callback (filled by the game),
            u16 material variation}
```

A cell is 4 bytes (an i32 or an f32); the file holds them as 0. A model record's three entry
points (+0x1C, +0x20, +0x24; -1: none) are offsets into the code: **+0x1C runs once when the
course is set up, +0x20 once a tick for every model of the set, +0x24 when the model is drawn**
(draw command 0x43, once per draw).

**Host variables**, bound by name. A course knows two (every scripted course set names exactly
these, in segment 2): `time` (cell 0), an i32 of milliseconds that grows by trunc(1000 x the
tick's step) each tick, and `deltaT` (cell 1), the step in seconds as an f32 (1 / 60; 0 while
the race is paused). The race sets both to 0 at the start. (A car model's host variables are
`brake`, `dirtiness`, `engineRPM`, `pedalLeft`, `pedalRight`, `steering`, `steering_rot`,
`taillamp`, `time`, `timezone` and `wing`.)

**Named registers** of the courses, by what the draw programs do with them: `tw` / `builtin_tw`
(f32 0..1, the morph weight between two key shapes: draw command 0x4C), `br` / `builtin_br`
(i32, the key shape: command 0x4D's switch), `cb` (i32, which clip of a person: another 0x4D),
`mvx` `mvy` `mvz` (a translation, command 0x45) and `roy` (a turn about the vertical, 0x4A) of
the walking people of icerace, `builtin_ra` `rx` `ry` `rz` (a turn about an axis, 0x48: one
model each on lasvegas and seoul), and the people's own state (`PSip`, `PScm`, `PSmpx/y/z`,
`PSmvx/y/z`, `PSpt`, `PSle`, `PScc`), which only their scripts read.

### The machine

A stack of 4-byte cells (256), a frame pointer, at most 10,000 commands a run. A variable
operand is a u16: bits 14-15 the segment (0, 1, 2), bits 0-13 the cell; a missing segment reads
0. Locals are cells of the frame (s16 index; 0 and 1 hold the caller's frame and return place,
so a routine's own start at 2). Operands follow the command byte, little endian, unaligned.

| Command | Operands | Does |
|---|---|---|
| 0x10 / 0x13 | s16 | push / pop a local |
| 0x11 | u32 | push the value (an i32 or the bits of an f32) |
| 0x12 / 0x14 | u16 | push / pop a variable |
| 0x20 | u8 n | reserve n cells |
| 0x21 | | start a call: push the frame pointer and leave a cell for the return place |
| 0x22 | u8 arguments, u16 place | call the code at the place (from the code's start) |
| 0x23 | u8 n | return (the top cell is the result; in the outermost frame the run ends) |
| 0x24 / 0x25 | u16 / s16 | jump to a place / by an offset from the next command |
| 0x26 / 0x27 | s16 | pop; jump by the offset when 0 / when not 0 |
| 0x28 | | drop the top |
| 0x30-0x34 | | i32 add, subtract, multiply, divide, remainder (second from top by top) |
| 0x35-0x3A | | i32 <, <=, >, >=, ==, != (result 1 or 0) |
| 0x3B / 0x3C | | i32 negate / i32 to f32 |
| 0x40-0x42, 0x44-0x46 | | and, or, xor; shift left by 1, shift right by 1, not |
| 0x80-0x84 | | f32 add, subtract, multiply, divide, fmod |
| 0x85-0x8A | | f32 <, <=, >, >=, ==, != |
| 0x8B / 0x8C | | f32 negate / f32 to i32 (toward zero) |
| 0xA0 | | random: top x a random fraction in 0..1 |
| 0xA1-0xA4 | | sin, cos, tan (radians), square root |
| 0xA5 | | 0.5 - 0.5 cos(top) |
| 0xB0 / 0xB1 | | pow(a, b) / atan2(a, b), a second from top |
| 0xB2 | | the fraction of a / b (a / b less its whole part) |
| 0xC0 / 0xC1 | u16 id, u16 variable | ask the set's callback: an i32 / an f32 result, given the id and the address of the variable (0 without a callback) |

The courses use 0x10-0x14, 0x20, 0x23, 0x25, 0x26, the i32 and f32 arithmetic and compares, 0xA5,
0xB2 and 0xC1.

**The course's callback** (`RaceCourse::Callback`, command 0xC1; the variable is the first of
three cells x, y, z: a person's place):

| Id | Result |
|---|---|
| 0 | the distance (m) to the nearest car |
| 1 | the time (s) until a car is level with the place: over the cars, d² / (v . d) of the smallest size, d from the car to the place and v the car's velocity (negative when the car is leaving) |
| 2 | the bearing (radians) of the nearest car as seen from the place: atan2(dx s - dz c, dx c + dz s), with (dx, dz) from the place to the car and (c, s) the x and z of the three cells after the place (the person's facing) |

Used by the people of 19 loose-surface and city courses (assisi, capri, grandcanyon, icerace,
nursnow, paris_rally, tahiti_maze, yosemite_dirt, lasvegas and their variants).

**Camera flashes.** On grandcanyon (and the other people's courses) a spectator's camera flash is
an animated model of its own: a 64 x 64 picture of a star on black on a quad, drawn with GS ALPHA
0x48 (Cs x As + Cd: added to what is behind), the person's script picking its key. Drawn as an
ordinary blend it shows as a black square with a star in it (the Drive screen's "black square
near the horizon"); the race view adds those models' blended passes
(`race/animated_models.cpp`; the shared engine's model decode adds ALPHA 0x48 for cars only).

### Examples

- **Tsukuba's 19 flags** (slot 1 models 347-365, a draw script each): t = fmod(`time` / 1000 +
  phase, 1) x 10; `br` = the whole part (10 key shapes), `tw` = the fraction. One wave a
  second, each flag at its own phase (0.5, 0.1, ...).
- **A spectator of yosemite_dirt** (set-up script: its place; tick script): when a car is less
  than 0.7 s away (id 1) or nearer than 7 m (id 0) the pose runs forward at one unit a second,
  beyond 30 m it runs back; the pose p stays in 0..0.85 and gives `br` = whole part of p /
  0.142667 (6 key shapes) and `tw` = the fraction.
- **A walker of icerace**: a 16 s loop of `time`; each stretch sets the clip `cb`, the place
  `mvx` `mvy` `mvz` as start + speed x ms, the heading `roy`, and the key shape and weight
  from the ms within the stretch (a step of 201 or 251 ms per key shape).

### Draw commands the scripts feed

The registers of commands 0x44-0x4D and 0x54 / 0x55 ([model.md](model.md)) are cells of segment 0
(u16 index). Command 0x4D branches by an i32 register's value v through its table when 0 <= v <
n and goes on after the table otherwise. Command 0x0A (u8 n, n points of 12 bytes, s16 offset)
tests a box given in the program against the view and skips ahead when it is outside (icerace's
walkers, the skies); 🔎 0x0B ends the tested part. No course program uses the callback commands
0x59 / 0x5A or the switch on a callback 0x06 / 0x07.

A course set is drawn with material variation 0. The texture sets of slots 1, 21, 22, 5, 9 and
the billboards are stepped once a tick too (🔎 their palette animations, as in the shared
engine).

## Billboards: `BLBd` (slot 30)

The shared engine's layout, unchanged (header counts at +0x10, lists per scenery model, sprite
types, the embedded `Tex1`), drawn by the same routines after the scenery's models. **Only the
four route5 files have sprites** (route5, route5_club and their reverse layouts: 289 sprites of
289 types in 56 or 52 lists, one texture, each sprite 7.2 to 10 m wide and high; read with the
shared engine's layout, every type index and texture index is in range); the other 106 courses
carry an empty 0x80-byte block. Trees and
crowds are models of the scenery set in this game. The two-player halves and the mirror leave
the billboards out on route5 (view flags 0xFFFFFFFE).

## Lamp glows: `GTFX` (slot 29), slot 37

`GTFX` is the shared engine's layout, unchanged: +0x10 u32 1, +0x14 the table (u32 list count,
list offsets, 0 = empty), a list u32 n and n 32-byte lamps {u32 type 0..7, f32 x3 position,
f32 x3 facing (unit length), f32 blink phase (0 on the whole disc)}. List i goes with model i
of slot 1 and is drawn when the camera's slot 2 list names that model; at most 100 lamps a
frame. Checked on all 110 courses: the types are 0..7 and the lists are fewer than the models.
On hongkong, `ss_tsukiji` and apricot every lamp lies inside its model's bound; on route5,
`ss_shibuya` and paris_rally many do not (a lamp is then shown when its list's model is, not
when a model near it is).

| Course | Lamps | Lists |
|---|---|---|
| paris_rally | 583 | 999 |
| ss_tsukiji | 216 | 4 |
| ss_shibuya | 138 | 21 |
| lasvegas | 118 | 19 |
| hongkong | 105 | 23 |
| route5, route5_club | 103 | 118, 110 |
| ss_sanmarco / ss_gion / ss_brooklyn / ss_fremont | 59 / 35 / 15 / 6 | |
| apricot | 9 (flag bit 21 is off: not drawn) | 70 |

seattle and `ss_times` set flag bit 21 with no lamps. The lamp types are `Env0` +0xC60 (the same
twelve values a type, read in the same order).

**The glow meshes come from slot 37**, not from a file: `u16 mesh count` at +0x0A, then at +0x10
per mesh {u32 offset, u16 quadwords, u16 0}; a mesh is a VIF packet of untextured strips
(positions V3-32 at 0x00, strip table at 0x40, RGBA V4-8 at 0x80), the layout of the shared
engine's `race/flare.bin` reader. Two versions exist on the disc: 14 of the 15 `ss_` photo
locations have three meshes, byte for byte the disc's `race/flare.bin` (64, 64 and 30
vertices: the last a ray shape 1.17 long); the other 96 courses, every one a race is run on,
have two (a disc of 62 vertices, 0.40 x 0.44, edge alpha 0, and a smaller one of 40 vertices,
0.16).

The draw rules are the shared engine's (read in this program, 0x3B88C8 with 0x498198 and 0x498398; the record of a car lamp
0x3B8530): the screen test, attenuation 1 / (c + l d + q d^2) at the lamp's depth d, the 100-lamp limit; a round lamp's glow
(|a - b| < 0.01) in its colour x attenuation x D x I (D the directional factor, I the list's strength); every lamp's halo,
when its gain and size are over 0 and it is further than 1 m, in its colour x attenuation x the halo gain x 0.5 (**not** D
or I), depth-tested; a bar lamp only the halo, drawn through the 2 x 2 matrix with columns H (x, y) and H b / |x| (-y, x) of
its projected axis x (scaled (a - b) + b / |x|), as Gran Turismo 3's. The glows are depth-tested at full visibility in the
several-view draw (race +0xA30) and weighed by the occlusion queries otherwise. ❓ The engine draws the bar halo as an ellipse
along the screen's axes (its own note), not turned with the lamp's axis. See "The picture's scale" for their strength.

## Cones and boards: `GDGT` (slot 47) and the road's gadget records

Code: `race/gadgets.*`, drawn by `course_scene.cpp`. The road file lists where they stand (RNW4
+0x44 u16 count, +0x70 -> 0x20 bytes each): `u32 kind, u32 0, f32 x, y, z, f32 a, b, c` (the
Euler angles of the shared engine's pylon records: Tsukuba's 18 cones turned by b only). The
`GDGT` block: `"GDGT"`, +0x08 u32 (0x102AD on tsukuba ❓), +0x10 u32 where the models
start (0x400 on Tsukuba, 0xC0 on the test courses' single kind), +0x1C u32 the kinds (6); from +0x20 a kind each 0xA0 bytes: u8 its number, u8 0, u8 flags (bit 0: a body
meets a car once, then no more: 0x3E3C48 sets its +0x45), u8 the hull's points (8 for the boards, 5 for the cone), f32 x3 inertia, the hull's points
from +0x10 (x, y, z, w = 1), +0x90 f32 radius, +0x94 f32 mass; then at +0x10's offset an `MDLS` of
2 x kinds models (🔎 model 2k a kind's shape, 2k + 1 its shadow, as the shared engine's pylon
sets), then its pictures. The cone (kind 5): radius 0.59 m, 15 kg. **A race on one of the road's sections** (a license
test's, a mission's: the course object's +0xB0) places the section's own records after the road's (0x395B60, from
RacePS2Base virtual 38: the section record's +0x0C u16 count, +0x1C -> the same 0x20-byte records): the gates and cones there
only for that test. Deep Forest's road has none of its own; B-3's section (2) has 25: a start gate (two posts of kind 2 and the
"Start" banner, kind 5, 4 m up), a finish gate at the goal (posts of kind 3, the "Finish" banner of kind 1) and a row of cones;
Tsukuba's sections add 13 or 14 to the road's 18. ✔ Ours places them (`Gadgets::parse` with the section; B-3's start and finish
gates by picture; the original's B-3 demonstration shows the same start gate, 34-b3-demo). The race view draws each
kind's shape where the road puts it (within 400 m). The cars knock them about (`race/gadget_bodies.*`, below). A record's
height is not where the body stands: the set-up drops it onto the road file's surface (below). Many records are placed
level above the road (Infineon's 26 at 25.83 or 20.42, over a road 1.2 to 29 m lower, Tsukuba's pit cones at 11.20 over 9.6 to
10.1, Laguna Seca's two boards at 0 over a road at -16.8 and -15.7); the program's bodies in the reference's saved
states (26-endurance `fc_pitwindow_00`, 24-arcade-grids `state_laguna_seca_ka_prerace`) sit on the road file's surface,
as ours do, within 5 mm on all 18 and 34. Their shadows: model 2k + 1 thrown along the light
onto the road's plane under the body (0x3E4968; on the level it stands on where that is above the road file's surface), the
alpha by the light on the road there (0x3983E0); the body's lights scaled by it too (0x398058).

### Cones and boards: the bodies

Gran Turismo 4's own routines (the shared engine's pylons are Gran Turismo 3's: the same impulses, another
turn and rest rule). A body (`0x3E2710` sets it from its record: kind, place, Euler angles): +0x04 the angles
a, b, c, +0x10 its place, +0x1C its velocity, +0x28 its turn rate in its own frame, +0x34 the road's plane
under it (n, d), +0x44 its kind, +0x45 a once-only kind's count after it met a car, +0x48 the road triangle,
+0x4C / +0x50 an added force and torque (none seen), +0x54 the step's impulses, +0x58 a count per hull point.
The turn matrix from the angles (0x3E24F0): rows (cb cc - sa sb sc, -ca sc, sb cc + sa sc cb), (cb sc + sa cc sb,
ca cc, sb sc - sa cc cb), (-ca sb, sa, ca cb), the body's axes in course space.

- **Set-up** (0x3E2710): at rest, hull points 0..2 counted 2 (resting), the road's plane found from 1 m above the
  record (0x3E29F8 → 0x3E5A30: the first road triangle straight down from the point, through the road's box to its
  bottom, however far; the step asks from the body's center);
  a kind with flag bit 1 stays as placed; else turned from up (0, 1, 0) onto the plane's normal (0x3E2268), the
  angles taken back from the matrix (0x3E2600), hull point 0 set on the plane.
- **Step** (0x3E2B28, every body each frame, 0x3E5040): a once-only kind that met a car counts steps and is
  removed when they pass 1.7 s; a kind with flag bit 1 never moves. A body with three hull points whose count is
  exactly 2 rests (not moved; the road and walls not met). Otherwise: the angles stepped by the turn rate
  (b' = (wx sc - wy cc) / ca, a' = -(wx cc + wy sc), c' = -(b' sa + wz)) and the turn rate by Euler's
  equations (I w' = (Iy - Iz) wy wz .. plus the torque), in stages: angles x 0.6756036 dt, then three times
  rates x 1.351207 / -1.702414 / 1.351207 dt (0x6A3960) and angles x -0.1756036 / -0.1756036 / 0.6756036 dt
  (0x8456B0, set on first use); the angles kept in 0 .. 2 pi; the place moved by the velocity, then gravity
  9.81 m/s². ✔ No damping and no cap but 0x3E3218: a turn rate over 20 rad/s about any axis is cut to a fifth
  (after a road or wall contact).
- **Walls** (0x395A38 over the box from the old to the new place → 0x3E26D0 → 0x3E44E8, the first wall met):
  a wall segment (RNW4 wall vertices, the next by +0x0C; +0x0E bit 1: skipped). Overlapping (the center within
  the kind's radius +0x90 of the segment): put at the radius on the side (ez, -ex). Else crossed this step from
  its negative side: back along the motion onto the line. Then (0x3E4708) the road impulse along the wall's
  level normal at a hull point (❓ its search starts from 0x6A396C, a NaN, so it is always point 0), and when
  the body's energy (sum I w² + m v²) rose, its velocity and turn stopped.
- **Road** (0x3E29F8 → 0x3E5A30 the triangle under it: its plane; 0x3E3748): each hull point below the plane:
  the deepest so far counts a step more, one above it counts 0; the body is lifted by the deepest depth
  (measured straight down) and gets the **impulse** 0x3E3840 there: K = 1/m + n . ((r x n_b / I) x r),
  restitution 0.25 (0 when the point moves slower than 0.9 m/s), j = -(1 + e) v_n / K (none when j < 0),
  friction 0.5 j / m against the point's sliding speed, at most all of it. ✔ The count rule seldom finds three
  counts of exactly 2 again (only the deepest point counts up), so a knocked body stays awake, all but still.
- **Cars** (0x3E3C48, not once a once-only kind met one): each car's five planes (0x3E5108 into 0x621D28,
  0x50 bytes a car: normals (0, 0.15, -1), (-1, 0.15, 0), (0, 0.15, 1), (1, 0.15, 0), (0, 1, 0) (0x6A3978) turned
  by the car's heading, through its box's corners: half width +0x494, rear +0x48C, front +0x490 of the car
  record, the top 1.5 m above its center); a car whose planes are all within the radius of the body's center,
  then the first hull point inside all five: pushed out through the nearest face (0x3E43B8), an impulse along
  that face's normal (0x3E3ED0) against the car's point velocity (its velocity and yaw rate), restitution 0.05
  for a once-only kind (flag bit 0) else 0.01, the moments of inertia x 1.5; the car feels nothing; the counts
  zeroed (awake); a contact of kind 2 (9) with the impulse reported to the car (0x350530); one car a step.
  Here the box's extents are the car's collision body's (❓ the car record's not read).

## Road: `RNW4` (slot 55)

Code: `runway.*`. All 110 roads read (`gt4tool runway-survey`). Offsets from the magic.

```
0x0C u32 flags          0x10 u32 version (4; 0 on some courses)
0x14 f32 lap length (m) 0x18 f32 start, 0x1C f32 goal: distances along the course
0x20 f32 x3 bounds min  0x2C f32 x3 bounds max
0x38 u16 checkpoint lists   0x58 u32 -> the lists
0x3A u16 grid places        0x5C u32 -> {f32 x, y, z, heading (radians)}
0x3C u16 checkpoints        0x60 u32 -> 0x28 bytes: left, middle, right (f32 x3 each), f32 distance
                                         (a point's y is not the road's height: Tsukuba's are 0 to 4.2
                                         where its road lies at 9.2 to 15.5)
0x3E u16 checkpoint lookups 0x64 u32 -> u16[]
0x40 u16 lamps              0x68 u32 -> 0x20 bytes each (below)
0x42 u16 lamp numbers       0x6C u32 -> s16[]; 0x50 u16 the numbers in a list
0x44 u16 gadgets            0x70 u32 -> 0x20 bytes each
0x46 u16 vertices           0x74 u32 -> 0x10 bytes: f32 x, y, z, s16, u8, u8
0x48 u16 triangles          0x78 u32 -> 0x0C bytes: u16 x3 vertices, u8, u8 kind, u32 flags
0x4A u16 clusters           0x7C u32 -> 0x10 bytes: u16 triangles, u16 lookup count, u16 lookup
                                         first, u16 0, u32 -> u16 triangle numbers[]
0x4C u16 tree depth         0x80 u32 -> the tree
0x4E u16 wall vertices      0x84 u32 -> 0x10 bytes: f32 x, y, z, u32 tag
0x88 u32 wall lookups       0x8C u32 -> s16[]
0x52 u16 pit stops          0x90 u32 -> as grid places (the pit's stands: the air hoses hang there, pit_scene.md)
0x54 u16 sections           0x9C u32 -> the sections, 0x20 bytes each: f32 start, f32 goal (distances along
                                         the course), then u16 ❓, u16 (a count of 1 on these) and three
                                         pointers: +0x18 -> the section's own start places, as a grid place
                                         {f32 x, y, z, heading}
```

- **A triangle's surface**: its byte +0x06, bits 0-4, is the material (US disc, all courses:
  0 on 57% of the triangles, the tarmac; 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12 the rest; 2, 3, 4
  and 6 are the soft ones, sand and grass, by the program's table). Its byte +0x07 is a set of
  flags: bit 0 the pit lane, bit 1 wet (the wheel is then on the material's wet variant:
  [specs/physics.md](../specs/physics.md) "The ground"); ❓ bits 2-5. Triangles are wound so
  that (b - a) x (c - a) points up.
- **A vertex's bytes** +0x0E / 255 and +0x0F (bits 0-6) / 127 are **the light on the road**, the sun's and the sky's: the
  road query interpolates them across the triangle (1 and 1 in the open, 0 under a roof). They scale the lights of a car on
  it ("The light on a car" above) and the shadow the light throws. 0x38E7F8; `Runway::light_at`, `race/car_light.*`. (Here: at
  the car's place; the program's value for a car with a driver's record ❓ a stored one.)
- **The lamps of the night's courses** (lasvegas, paris_rally, route5, route5_club and their reverses, ss_fremont,
  ss_shibuya: +0x40 u16 lamps, +0x68 -> 0x20 bytes each: u32 kind (a car-light record of Env0, 0xD0 + 0x80 x kind), f32 x3 its
  axis, f32 x3 its place, f32 1 (0: the place is a direction)). A vertex's +0x0C (s16, >= 0) is where its list begins in the
  numbers at +0x6C (+0x42 of them), +0x50 (6) to a list. A car takes the list of its triangle's first vertex: each lamp's
  strength 1 / (c0 + c1 r + c2 r²), times cos^exponent inside its cone, 0 outside; the four strongest light it (ambient,
  diffuse and specular times the strength), the Env0's global ambient with them. 0x38E7F8, 0x3E6C60. ✔ By eye: Special Stage
  Route 5's tunnel (`route5_night_chase`). Each throws a shadow (0x38F168). ❓ Not built: their highlights (0x38F5D0, +0x140).
- **The tree** finds the triangles under a point. It is a full binary tree of the given depth
  stored one byte per node, depth first with the left subtree before the right: bits 0-5 a
  place p, bits 6-7 the axis (0 x, 1 y, 2 z). A node splits its box (the root's is the bounds)
  at min + (max - min)(p + 0.5) / 64; the leaves, left to right, are the clusters, each a list
  of triangles. Checked on every course: a triangle's centre leads to a cluster that lists it.
- **Walls** are closed chains of the wall vertices in order. A chain's first vertex has its
  length in the tag's low 16 bits, and its last vertex minus (length - 1) there: the link to
  the chain's next vertex, as in Gran Turismo 3's wall points, whose collision step reads these
  records as they are (the tag's high half its flags: bit 0 a corner vertex, bit 1 a sensor
  edge). ❓ the wall lookups.
- **Checkpoints** are gates across the whole course, wider than the road (37 m on Tsukuba, up
  to 140 m on the Circuit de la Sarthe, with the pit lane and whatever lies beside the road
  inside), so their middles are not the road's middle. Their heights are not the road's
  (Tsukuba's are 0 .. 4 where the road is at 9 .. 15). The list starts mid-lap: the distances
  rise along it and fall back once, at the lap's end. Left and right are as a driver sees
  them: up x (right - left) points along the course.
- **The start grid**: most courses have 6 places that stand on the road; some list places
  that do not (❓ the sections name the places a layout uses). A place's heading h gives the
  direction (sin h, cos h) in (x, z). Its height is only rough on some courses (Laguna Seca's
  places are at 0 where the road is at -21): the road under it is found from above.

## The drivers' file: `crs/<name>.ad`

Code: `ai_line.*`; what the race does with it: [specs/ai.md](../specs/ai.md). One per course (110
on the US disc, `gt4tool ai <disc> survey`). A table of 32 u32 section offsets (slot 0 is 0, slot
1 starts right after the table at 0x80), slots 1 to 5 used:

- **Slot 1, the lines.** Usually "ADLN": +0x0C u32 the size, +0x14 u32 the line count (8), +0x18
  u32 the offset of the table of line offsets (from the magic; 0: no line). A line is a 0x40-byte
  header (u32 the point count, the rest 0) and its points, 0x40 bytes each:

  ```
  +0x00 u8 kind          0 a straight to the next point, 1 an arc
  +0x01 s8               the braking distance's adjustment, percent
  +0x02 s8               the arc's speed adjustment, km/h; -128 on 247 points: passed over
  +0x03 u8               0
  +0x04 f32 x, +0x08 f32 y   the place (the simulation's frame turned half round: (-x, -y) there)
  +0x0C f32              0 (the race writes the road's height)
  +0x10 u16              0xFFFF on all but 4 points (the race writes the surface at +0x12)
  +0x14 f32              0 (the race writes the distance along the course)
  +0x18 f32 radius       the arc's signed radius, metres (> 0 turns left); 0 on a straight
  +0x1C .. +0x23         0 (the race writes the slope and the bank)
  +0x24 f32 heading      radians, the direction (-sin h, cos h)
  +0x28 .. +0x3F         0 (the race's run-time words)
  ```

  17 files have the older layout of Gran Turismo 3's line section instead (+0x04 u32 8, then
  eight line offsets at +0x08, lines of a u32 count and 0x28-byte points: the kind and the two
  adjustments in the first word, x and y in 1/4096 m, the segment, the radius in 1/4096 m, the
  heading in the low half of the last word in 1/4096 of a turn). ✔ 14 of them (the special
  stages `ss_*` and `testcourse_100m`) carry the same line section, which does not lie on their
  roads; the other three are the gymkhanas' spirals'.
- **Slots 2 to 5, the driver sets**, 0x80 bytes each (only the first 0x30 used), one per
  driveline class. ✔ All 110 files have all four; the bytes are in [specs/ai.md](../specs/ai.md)
  §3.1.

Line roles and counts: [specs/ai.md](../specs/ai.md) §2.1. Every course has line 6, the main line.

## The license tests' lines: `crs/bestline/`

128 model sets (`MDLS`), one per license test, named as the test's event (`jpl0a0001` ..; the
regions jp, kr, uk, us x the lessons l0a, l0b x 16): the driving line drawn on the road in the
license tests (the race view's step 5 above). Not used by the drivers.
