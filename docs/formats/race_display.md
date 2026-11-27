# The race display

What the player sees over the race: position, lap count, lap times, the course map, the meters
(speed, gear, rev counter, boost), and the messages. Facts read from the program (`CORE.GT4`,
SCUS-97328 v1.01; addresses are the program's, for the analysis outputs) and from the disc's
files; nothing here is compared with the original running yet (🔎 where a reading is partial).

Code: `src/gt4r/race/race_display.*` (the places and tables read from the program, the time
format, the map's numbers), `src/gt4r/app/race_hud.*` (the drawing). Drawn so far: the single
race's position, lap count, three times, event line and middle message, the course map with
the cars, the chase views' speed plate, gear and rev bar, the driver's views' plate, two dials,
gear, digital speed and unit.

## Where the layout lives

**In the program, not in a data file.** The race display is not a menu project and has no
layout file: each element is an object of a program class (`RaceDisplay` owns them all as
members), its pictures are named by strings in the program, and its place comes from

1. **layout tables in the program's data** (below) for the elements around the screen, and
2. **constants in the panel classes' code** for the meters, which are placed relative to their
   panel's rectangle.

The only data files are the picture pack `race_display/gt4/<REGION>/display.gpb` (the program
builds the path `/race_display/%s/%s/display.gpb` from the constant `gt4` and the region
folder: `US`, `GB`, `JP`, ...), the fonts of `font/system.fnt`, the race's text table
`rtext/race<LANG>.rt2`, the course's map (`GTCM`, course slot 33), and the car's numbers from
the specification database.

## Screen units

- The display is laid out on a **640 x 480 canvas**, y down, drawn through an orthographic
  projection onto the 640 x 448 frame: a canvas unit is one frame pixel across and 448/480 of
  one down. A picture drawn "at its own size" covers its pixel size in canvas units (so it is
  14/15 as tall on the frame as its pixels); most elements give their pictures an explicit
  size.
- **Wide screen** (the option's flag, program word 0x6184F0): the canvas is 853.33 x 480 with x
  from -106.667 to 746.667 (the same frame, which the television stretches), so everything keeps
  its shape and the middle of the 4:3 layout stays in the middle. Elements that belong to a
  screen side move out by **96 units** (below). Text is drawn with a base x scale of 0.75 on the
  frame in this mode (1 otherwise), which is the same thing for glyphs.
- Text is positioned on the canvas and drawn in frame pixels (640 x 448): a pen (x, y) lands on
  frame pixel (x, y * 448/480) (4:3).

Checked: the projection is set from (left, right, bottom, top) = (0, 640, 480, 0), or
(-106.667, 746.667, 480, 0) with the wide flag (`RaceDisplay` setup, 0x39BA08); the text
renderer is given 640 x 448 and the 0.75 / 1 scale each frame (0x39F550).

## An element's rectangle

Every element (`RaceDisplayObjectBase`, 0x18 bytes before a class's own fields) has:

| Offset | Type | What |
|---|---|---|
| +0x00 | u8 | Shown (1) or not |
| +0x01 | u8 | Horizontal anchor (below); 2 when made |
| +0x02 | u8 | Vertical anchor; 2 when made |
| +0x04 | s16 x2 | x, y |
| +0x08 | s16 x2 | Width, height (0, 0: the picture's own size) |
| +0x0C | f32 | Opacity (1) |
| +0x10 | pointer | Its picture |
| +0x14 | pointer | The class's functions |

**Anchor**: bits 0-1 say which point of the rectangle (x, y) is: 0 the left (top) edge, 1 the
right (bottom) edge, 2 the middle. In the horizontal anchor, bit 2 (value 4) ties the element
to a screen side on a wide screen: x moves by -96, or by +96 when bit 3 (value 8) is set too.
So 4 = left edge, tied left; 6 = middle, tied left; 13 = right edge, tied right; 14 = middle,
tied right. (Functions at 0x3AFD18 / 0x3AFD78: left = x, x - w, or x - w/2.)

**Colors** are GS colors 0xAABBGGRR with 0x80 as full: 0x80C8C8C8 is the light gray of most text
(200, 200, 200), 0x80B3B3B3 the gray of labels' numbers (179), 0x80E3B896 the meters' pale
blue (R 150, G 184, B 227), 0x800000C8 / 0x800000F0 the reds (200 / 240, 0, 0), 0x800086E6
orange (230, 134, 0). An element's opacity multiplies the alpha.

## The layout tables

A table is a list of 0x24-byte rows ending with a zero first word:

```
+0x00 u32  the element: its offset in RaceDisplay + 1
+0x04 u32  0
+0x08 s32  x      +0x0C s32  y
+0x10 s32  width  +0x14 s32  height
+0x18 u8   horizontal anchor   +0x1C u8   vertical anchor
+0x20 u32  0
```

Applying a table sets each listed element's rectangle and shows it; elements in no applied
table stay hidden (everything is hidden first). Which tables apply (0x3A2360) depends on the
race's **kind** (set by the race class: 1 arcade race, GT mode race, championship, mission;
3 license and training; 5 machine test; 7 free run; 14 free practice; 17 / 18 LAN / online; 8 and
9 are the two-player halves) and on a **display mode** 0-4 that only the license, machine test
and network races set (0 otherwise):

| Case | Tables |
|---|---|
| Kind 8 (two players, one above the other) | 0x6A0DE0, panel layout 1 |
| Kind 9 (two players side by side) | 0x6A0FD8, panel layout 3 |
| Mode 0 (the single race) | 0x6A09A8, then 0x6A0838 (or 0x6A08F0 when the kind is 2 or the display's "total time" flag is set), then the panel layout. The flag (the display's byte +0x38, `RaceDisplay::vf9` 0x39B5F0 .. 0x39B638): the race's laps (the conductor's virtual +0x128) 10 or more, or a time limit (0x34C140 not 0). Here `race::total_time_display`, `read_total_race_layout` (Japan's table 0x696DD0) |
| Mode 1 | 0x6A0BA0, then the panel layout |
| Modes 2, 3 | As mode 0; the panel layout only while a replay-side counter is set |
| Mode 4 | 0x6A0D50 only |

### The single race: tables 0x6A09A8 + 0x6A0838

Read from `~/gt4re-out/program/core.bin`. "Member" is the element's offset in `RaceDisplay`.

| Member | Class | What | x, y | w, h | Anchors |
|---|---|---|---|---|---|
| 0x1668 | `RaceTexturePanel` | The word "Position" (`JP/position`, 74 x 20) | 56, 21 | own | 6, 0 |
| 0x1398 | `RaceRankDisplay` | The place: a digit on `position_base` | 56, 86 | 42, 45 | 6, 1 |
| 0x1684 | `RaceTexturePanel` | The word "Lap" (`JP/lap`, 42 x 20) | 135, 21 | own | 6, 0 |
| 0x1300 | `RaceLapDisplay` | The lap count "1/3" | 135, 74 | 100, 0 | 6, 1 |
| 0x1108 | `RaceMiniMap` | The course map | 24, 96 | own | 4, 0 |
| 0x15F8 | `RaceTimeDisplay` | The current lap's time, with the stopwatch picture | 335, 107 | 0, 0 | 2, 1 |
| 0x16A0 | `RaceTexturePanel` | "Best Lap" (`JP/best_time`, 80 x 20; "Total Time" with table 0x6A08F0) | 550, 21 | own | 14, 0 |
| 0x14A8 | `RaceTimeDisplay` | The best lap's time (0x1438 with 0x6A08F0: the total) | 550, 58 | 82, 15 | 14, 0 |
| 0x16BC | `RaceTexturePanel` | "Last Lap" (`JP/last_time`, 80 x 20; "Best Lap" with 0x6A08F0) | 550, 74 | own | 14, 0 |
| 0x1588 | `RaceTimeDisplay` | The last lap's time (0x14A8 with 0x6A08F0: the best) | 550, 110 | 82, 15 | 14, 0 |
| 0x17BC | `RaceEventDisplay` | Timed lines: the lap time at the line, gaps, information | 320, 139 | 580, 0 | 2, 2 |
| 0x252C | `RaceMessageDisplay` | The big middle message (countdown, FINISH...) | 320, 224 | 0, 0 | 2, 2 |
| 0x267C, 0x27CC | `RaceMessageDisplay` | The A-spec points' two lines (`RaceDisplay::vf43` 0x39C570: `AspecRaceFormat1` in `hb_24#1`, `AspecRaceFormat2` in `hb_24#1*130,130`, 0x800086E6). Rows of their own only in Japan's single-race tables (its members 4 lower: 0x2678, 0x27C8; its vf42 0x396020); the US and European tables have none, and the lines stand at the same places | 320, 264; 320, 304 | 0, 0 | 2, 2 |
| 0x2A6C | `RaceMessageDisplay` | Replays: the "REPLAY" picture (`JP/replay`) | 32, 34 | 400, 107 | 4, 0 |
| 0x2BBC | `RaceReplayModeDisplay` | Replays: the mode plate (`replay_plate`) and its text | 20, 22 | 132, 20 | 4, 0 |
| 0x23DC, 0x291C | `RaceMessageDisplay` | A line of text, `hn_20R#1*90,90`, color 0x80C8C8C8: 0x23DC a name from the viewed car's entry (🔎 the car's name), 5 s with 0.25 s fades when the display is set up for a car; 0x291C a line the race sets. The panel layout moves 0x23DC (below) | 40, 441 | 350, 21 | 4, 1 |
| 0x2D78 | `RaceRefuelMeter` | The pit stop's refuel bar | 320, 58 | own | 2, 2 |

The other tables, for reference (same columns: member x, y, w, h, anchors):

- **0x6A0BA0** (mode 1): 0x1398 56,86 42x45 (6,1); 0x1300 135,74 100x0 (6,1); 0x1108 24,96
  (4,0); 0x15F8 335,107 (2,1); 0x1438 550,58 82x15 (14,0); 0x16A0 550,21 (14,0) with
  `JP/total_time`; 0x1684 135,21 (6,0); 0x1668 56,21 (6,0); 0x17BC 320,139 580x0 (2,2);
  0x252C 320,240 (2,2); 0x2D78 320,58 (2,2).
- **0x6A0DE0** (two players, a half of 640 x 240): 0x1398 45,8 42x45 (6,0); 0x1108 24,52 (4,0);
  0x15F8 612,170 (1,1); 0x14A8 550,42 82x15 (14,0); 0x1588 550,90 82x15 (14,0); 0x16A0 550,8
  (14,0); 0x16BC 550,55 (14,0); 0x17BC 320,68 500x0 (2,1); 0x252C 320,120 (2,2); 0x2A6C 32,16
  400x107 (4,0); 0x2BBC 20,16 132x20 (4,0); 0x23DC 40,220 350x21 (4,1); 0x2D78 320,45 (2,2).
- **0x6A0FD8** (two players side by side): 0x1398 46,86 42x45 (6,1); 0x1108 0,110 (4,0);
  0x15F8 218,130 (6,1); 0x14A8 360,58 82x15 (6,0); 0x1588 360,110 82x15 (6,0); 0x1668 46,21
  (6,0); 0x16A0 360,21 (6,0); 0x16BC 360,74 (6,0); 0x17BC 203,160 390x0 (6,2); 0x252C 203,220
  (6,2); 0x2A6C 15,34 400x107 (4,0); 0x2BBC 15,22 132x20 (4,0); 0x23DC 24,441 350x21 (4,1);
  0x2D78 203,45 (6,2).
- **0x6A0D50** (mode 4): 0x23DC and 0x291C 40,441 350x21 (4,1); 0x2BBC 20,22 132x20 (4,0).

## Text

The program draws all the race display's text with the fonts of **`font/system.fnt`**
(`hn_20R`, `FC_26`, `FC_48`, `hb_24`, `hm_16`, `hc_20`, `GT4meter`, `GT4oddmeter`), named as
the menus name them: `name#palette*x%,y%` (`hn_20R#1*90,90`, `FC_26*80,80`; palette 0 is the
outlined set, 1 the plain one).

**`font/race.fnt` is not loaded by this program**: its name is nowhere in `CORE.GT4`, and the
only font files the program opens are `system.fnt` (and `tw_system.fnt` / `kr_system.fnt` by
language) and `menu.fnt`. The file reads with the `FT01` reader (layout 1, 8 fonts: `DC_12`
0x30-0x74, `DC_24_5` 0x30-0x4A, `FC_16` 0x27-0xB7, `FC_20` 0x30-0x53, `FC_26` 0x22-0x5F,
`FC_48` 0x30-0xD3, `GT4meter` 0x30-0x52, `hn_20R` 0x20-0x17F; each with a `Tex1` set) and is a
leftover: the `DC_*`, `FC_16` and `FC_20` names are not in the program either.

| Font (`system.fnt`) | Codes | Line height, ascent | Used for |
|---|---|---|---|
| `hn_20R` | 0x20-0x16F | 22, 15 | Times, event lines, small messages |
| `FC_26` | 0x22-0x5F | 35, 27 | The place digit, the lap count (at 80 %), the finish place (at 125 %) |
| `FC_48` | 0x30-0xD3 | 68, 65 | The start countdown (`FC_48*75,75` when the screen is split) |
| `hb_24` | 0x20-0x17F | 30, 19 | A-spec points lines (`hb_24#1`, `hb_24#1*130,130`) |
| `hm_16` | 0x20-0x17F | 25, 15 | Race information (`hm_16#1`), the replay plate (`hm_16*80,80#1`) |
| `GT4meter` | 0x30-0x52 | 18, 13 | Digital speed, gear (`GT4meter#1`) |
| `GT4oddmeter` | 0x30-0x39 | 10, 8 | The trip meter (`GT4oddmeter#1`) |
| `hc_20` | 0x20-0x17F | 28, 19 | Fuel figures of the hybrid panel (`hc_20*108,108`) |

The text renderer's pen flags (the word at the renderer + 4, used by the formatted draw; the
plain draw uses 0):

- bits 0-1: 0 the text starts at the pen; 2 it ends at the pen; 3 it is centered on the pen.
- bits 2-3: 0 the pen is on the baseline; 4 the pen is the top of the line; 12 the pen is the
  middle of the line (top = y - line height / 2).

So 2 = right-aligned on the baseline, 15 = centered both ways. The pen's place: an element hands its place (x, y, z) to
0x44DCC0 (through 0x44DD98 / 0x44DDF8 / 0x44DE40), which puts it through VU0's matrix vf24 .. vf27 (the display's: x / 320,
-y / 240, -z and the element's own translation, e.g. (-0.64375, 0.225) for a display at (114, 186); the matrix ops each
chopped) and divides by w, then on the FPU x = (ndc x + 1) x W x 0.5 and y = (1 - ndc y) x H x 0.5 (W, H the renderer's
+0x30 / +0x34, ❓ 640 x 448 not read from a state); 0x44E1E0 then takes trunc of it for every alignment: the box [0, trunc x] right-aligned, the pen
at trunc x left-aligned, trunc x - 320 centered, the line at trunc y. So, as in Prologue (prologue.md part 11), a place can
land a hair under its whole pixel and draw a pixel left or up, but by other arithmetic (no fixed point with 4 fraction
bits). The matrix is the renderer's current one: 0x44DCC0 starts with 0x49E3E0, which (through 0x49E480 when the state
changed) leaves modelview x projection in VU0's vf24 .. vf27 and the state's +0xFA0 .. +0xFD0 in vf20 .. vf23 (vf22 the
clamp of the `vmini`); so a display's translation is the 2D modelview its draw sets before its elements, and the
elements' places (RaceTimeDisplay::vf5: the element's +4 / +6 through 0x3AFD18's side push) are taken from it. With the
translation -1 (no display offset) 374 of the 641 places 0 .. 640 come out a hair under their whole pixel and land one to
the left (`~/gt4re-out/daylight/tools/gt4proj.py`). 🔎 Ours draws GT4's elements at their layout places as they are: the
per-display translations are not read (they are set in each display's draw, and a saved state holds only the last).
Text colors are the GS colors
above, used as the shared engine's race display uses them: the RGB as it is, alpha 0x80
opaque. Setting a color also sets the outlined fonts' outline: black, with alpha
(A / 128 - 0.25) x 4/3 kept in 0 .. 1.

## The elements of the single race

### Position (`RaceRankDisplay`, member 0x1398)

- The picture `position_base` (42 x 42) stretched over the element's rectangle (42 x 45 at
  left 35, top 41: x 35-77, y 41-86 on a 4:3 screen), neutral color, at the element's opacity.
- One digit over it, font `FC_26`, color 0x80C8C8C8, pen flags 15, at x = left + w/2 - 1 (+4
  more when the digit is 1), y = top + h - 19: **the place alone, "1" to "6"**, not "1/6" (the
  character is '0' + place).
- When the place changes the digit rolls like a counter: the shown value v moves toward the
  place by 0.2 of the difference each frame (at least 0.01), the digit int(v) is drawn
  (v - int(v)) line heights (35) higher and the next digit one line height below it, both cut
  to the rows from top + 8 to top + h - 3.
- The word above it is the picture `JP/position` ("Position"), its top middle at (56, 21).

### Lap count (`RaceLapDisplay`, a `RaceRichCountDisplay`, member 0x1300)

- Text `"%d/%d"`: the lap being driven (at most 999) and the race's laps; `"%d"` alone when the
  race has no lap count.
- Not drawn (the byte +0x3A: 0x39B874) on a road section (0x34C470: its start 1 m .. the lap less 1 m along), on a course without
  a lap (COURSE +0x12) and in a license test that counts cones (0x3BCE70).
- Font `FC_26*80,80`, color 0x80C8C8C8, plain draw: left edge from the anchor (middle at
  x 135, tied left) using the text's width, baseline at y (74: the vertical anchor 1 puts the
  line's bottom, top + line height, at y). When the text is wider than the element's width
  (100) it is squeezed in x to fit.
- The word above it is the picture `JP/lap` ("Lap"), top middle at (135, 21).

### Times (`RaceTimeDisplay`, a `RaceValueDisplayBase`)

- Font `hn_20R`, color 0x80C8C8C8 (the element's +0x18) times the opacity and its fade.
- The horizontal anchor places the text by its width; **y is the baseline** (the vertical
  anchor is not used).
- **Lap time format** (value in milliseconds): `M'SS.mmm`, minutes without leading zeros (two
  digits from 10 minutes, three from 100), clamped at 999'59.999. No time (a value above
  359,999,998, so -1 too): `-'--.---`, where each dash is character 0x7F of the font.
- **Total time format** (members with +0x68 set: 0x1438): `H:MM'SS.mmm` (two hour digits from
  10 hours), clamped at 99:59'59.999; no time: `-:--'--.---`.
- An element can carry a picture (+0x20) drawn at its own size with its right bottom corner at
  (text left + w + dx, y + h + dy), dx / dy signed bytes at +0x44 / +0x45: the **current lap
  time** (0x15F8, middle at x 335, baseline 107) has the stopwatch `GT4_meter_texture_laptime`
  (24 x 24) with (-8, 7): it ends 8 units left of the text and 7 below the baseline.
- Best lap: text centered on x 550 (tied right), baseline 58, under the picture `JP/best_time`
  (top middle at (550, 21)). Last lap: baseline 110 under `JP/last_time` at (550, 74). With
  the total-time table: total at 58 under `JP/total_time`, best lap at 110 under
  `JP/best_time`.

`RaceLapTimesDisplay` (member 0x13F0, 3 lines, line spacing 0.9) lists the laps driven with
their numbers; it is in no table of the single race (it shows in the free run and practice
layouts, ❓ not read).

### Course map (`RaceMiniMap`, member 0x1108)

See [The course map](#the-course-map-gtcm).

## The meters

The meters are a **panel** (`RacePanel`) with its parts as members. `RaceDisplay` has several
and shows one, chosen from a **panel layout** number (0x3A18A8, 0x3A1970):

| Layout | When | Panel | Its rectangle (x, y, w, h, anchors) | Also placed |
|---|---|---|---|---|
| 0 | The driver's views (the camera's view number 0 or 1 🔎) | `RaceOnboardPanel` (member 0x108) | 320, 452, 640, 0 (2, 1) | Status message (0x228C) 320, 348 (2, 1); text line (0x23DC) 24, 328, w 280 (4, 1) |
| 1 | The other views (the chase views); every two-player half above / below; replays that show meters | `RaceSimplePanel` (member 0xD58) | 320, 464, 640, 0 (2, 1) | Status message 320, 430, w 240 (2, 1); text line 320, 430, w 240 (2, 1); `RaceSteeringDisplay` (0x10E4) 320, 450, 85 x 85 (2, 2) |
| 2 | Display mode 1 | `RaceSimplePanel`, reduced | 320, 463, 640, 0 (2, 1) | Status message 320, 400 (2, 1); tool tip bar (0x2DA0) 0, 480, 640 x 42 (0, 1) |
| 3 | Two players side by side | `RaceSimplePanel`, split form | 213, 464, 426, 0 (6, 1) | Status message 203, 380 (6, 1); text line 203, 410, w 240 (6, 1) |
| 4 | A car the program's hybrid test accepts (🔎 the Prius), outside replays | `RacePriusPanel` (member 0x39FC; `meter_prius_base_*`, 406 x 42) | 617, 459, 406 x 42 (1, 1) | Text line 24, 360, w 280 (4, 1) |

The layout number: 1 for kind 8, 3 for kind 9, 2 for display mode 1; otherwise the view's (0 or
1), 1 in replays that show the meters, 4 for the hybrid car, and 1 again for a car with a flag
of its own (0x3F9018, 🔎 not identified).

A panel places its parts itself when it is given its rectangle (constants in the panels' code,
0x3A9008 and 0x3A7AB8). Below, L / R / C are the left edge, right edge and middle of the
panel's rectangle and B its bottom, after the anchors: for the single race L = 0, R = 640,
C = 320; B = 452 (driver's views) or 464 (chase views).

### Driver's views: `RaceOnboardPanel`

| Part (member of the panel) | Class | Picture | x, y | w, h | Anchors |
|---|---|---|---|---|---|
| Back plate (0x50) | `RaceTexturePanel` | `panel_base` | C, B | 336, 102 | 2, 1 |
| Speedometer (0x6C) | `RaceOnboardSpeedmeter` | `speed_*` | C - 168, B - 100 | 130, 100 | 0, 0 |
| Rev counter (0xC8) | `RaceOnboardTachometer` | `rev_*` | C + 168, B - 100 | 130, 100 | 1, 0 |
| Boost gauge (0x964) | `RaceBoostmeter` | `boost` | C + 175 (+8 with a flag ❓), B - 21 | 54, 54 | 0, 1 |
| Trip meter (0x9C0) | `RaceOdometer` | | C - 103, B - 45 | text | 2, 1 |
| Digital speed (0x9E8) | `RaceDigitalSpeedmeter` | | C - 2, B - 41 | text | 2, 1 |
| Unit (0xC08) | `RaceTexturePanel` | `kmph` or `mph` | C, B - 24 | 28, 9 | 2, 1 |
| Gear (0xA20) | `RaceShiftPositionDisplay` | `gear_base` | C, B - 64 | 24, 26 | 2, 1 |
| Suggested gear (0xA78) | `RaceSuggestedGearDisplay` | | C, B - 111 | 44, 34 | 2, 2 |
| Shift lamp (0xAD0) | `RaceShiftTimingLampDisplay` | `timing_lamp` | C + 103, B - 62 | 8, 8 | 2, 2 |
| Tire wear (0xB0C) | `RaceTireWearDisplay` | `tire_wear` | C - 294, B + 13 | 40, 68 | 0, 1 |
| Signs (0xB3C, 0xB80, 0xBC4) | `RaceIndicator` | `sign_asm`, `sign_brake`, `sign_oil` | C - 199, C - 166, C + 166; B | 24, 24 | (1, 2), (1, 2), (0, 2) |
| G meter (0xC24) | `RaceSideGravityMeter` | `g_meter_base`, `g_meter_point` | C, B | 214, 24 | 2, 2 |

So on a 4:3 screen the plate covers x 152-488, y 350-452; the speedometer's face x 152-282,
y 352-452 (left); the rev counter's x 358-488 (right); the boost gauge x 495-549, y 377-431.
The panel also draws its bars with its own code (0x3A8968), from the speedometer's corner (x, y) =
(C - 168, B - 100): the brake (panel +0x28, red 0x800000F0) at x + 121 and the throttle (+0x24, light blue
0x80E3B896) at x + 195, `pedal_bar` 20 x 58 lit from y + 79.5 up by the pedal (0 .. 1), the throttle's picture
mirrored (its quad's u from the picture's width to 0, 0x3A8C04: the brake's arc bows toward the speedometer, the
throttle's toward the rev counter; ✔ reference `03-race/incar_frames`); with the panel's
flag +0x1B the fuel gauge `fuel_a0` 38 x 64 at (x - 22, y + 19) and over it `fuel_a1` lit from the bottom in
ten steps n = round(fuel / tank x 10) (+0x2C / +0x30): its top at 60 - 5.2 n (4 higher from the sixth),
red 0x800000C8 under three steps, else light blue; with +0x1C the nitrous gauge (`nos_a0`, `nos_a1`, +0x34 /
+0x38, red 0x800000F0) the same at the rev counter's (x - 16, y + 19). ✔ Against the original's in-car
view (03-race incar frames): the fuel arc left of the speedometer, the throttle's blue arc right of the
digits. The flag +0x1B is the followed car's tank (F +0x5D0) more than 0 (`RaceDisplay::vf2` 0x39E598 through 0x354E38;
the chase panel's fuel meter reads the same flag), and the tank is 0 when the race counts no fuel (0x354AF8: the car's tank
zeroed by the race's flag): no arc at the Ice Arena (✔ the original's `35-ice-arena`). Drawn here (`RaceHud::Impl::draw_bars`,
with `fuel_on`; the nitrous gauge not drawn: no nitrous part yet). Sign
colors: `sign_asm` 0x80E3B896, the other two 0x800000C8. Draw order: plate, the two dials'
shadows, trip meter, shift lamp, the two dials, boost gauge, gear, suggested gear, digital
speed, tire wear, signs, unit, G meter, bars.

The race display draws the signs, the shift lamp, the G meter and the trip meter at these places
(`RaceHud::Impl::draw_small_parts`); ❓ judged: the signs stay unlit (a third of their color: the
simulation does not say when an aid works), the lamp lights past 97 % of the red line, the trip meter counts whole
miles or kilometres of the race. Not drawn: the boost gauge, the nitrous gauge.

**The G meter** (`RaceSideGravityMeter`, vf5 0x3AB240): `RaceDisplay::vf2` gives the panel (vf27 0x3A84A4, the meter's
+0x1C) the car's follower of its lateral acceleration (F +0x560, 0x35DE20: a spring after F +0x554) x -1/9.81 (0x39E1E0),
so the point shows the push the driver feels: to the left in a right-hand corner. The value within ±2 (+0x20) as a share
of 2 moves the point's middle that share of 0.9439 (+0x24) of the base's half width from its middle, 8 below the
base's top. ✔ Tsukuba's first corner (a right-hander): the original's point left of G (reference `20-race-chase`
`chase_t30s.png`), ours the same.

**The suggested gear** (`RaceSuggestedGearDisplay`, the panels' member 0xA78 / 0x17C; shown with the display's
flag bit 8, panel vf23): its value (the display's +0x54, set by the panel's vf24 from `RaceDisplay::vf2`
0x39E14C) is **the computer driver's** byte +0x40 of the shown car's driver object (car +0x14, which every car
has, the player's too), worked out each step in the input phase (0x349FD0 → 0x469E58): -1 (none) unless the
car's speed (F +0x53C) is at or over the driver's target speed (+0x28; with the driver's mode +0x1E 4 the
lower of +0x28 and +0x24; mode 3 and 4 also set +0x41), then 0x36BF68 at that target speed: the lowest gear
from first whose up-shift engine speed (0x36BEE0) less 500 rpm (2,000 rpm in first) x 2 pi / 60 over the
gear's speed factor (P +0x2A0 + 4 g) reaches it (a car whose gearbox 0x368398 / 0x3F1F78 answers: 1). So it
shows ahead of a corner the gear the driver's line would brake to. Here the player's car keeps a planning
driver (`Field::set_shadow`, its suggestion `Field::suggestion`) and the gear is `Session::gear_for_speed`
(`RaceWorld::suggested_gear`); drawn as a red digit in the display's box (❓ the display's own drawing, vf5
0x3A7108, not read; colour and size from the original's in-car view, where a red 3 shows above the gear).
✔ Seen: the Ka '01 at Tsukuba, a red 3 from 74 km/h before the first corner.

### Chase views: `RaceSimplePanel`

The single race's form (the panel's split flag and its reduced flag both clear):

| Part (member) | Class | Picture | x, y | w, h | Anchors |
|---|---|---|---|---|---|
| Speed plate (0x2C) | `RaceTexturePanel` | `remote_speed_kmph` or `remote_speed_mph` | R - 26, B - 44 | 82, 26 | 13, 0 |
| Digital speed (0x48) | `RaceDigitalSpeedmeter` | | R - 59, B - 26 | text | 13, 1 |
| Rev bar (0x100) | `RaceBattleTachometer` | `rev_bar` | R - 26, B | 160, 10 | 13, 1 |
| Gear (0x124) | `RaceShiftPositionDisplay` | `gear_base` | R - 114, B - 44 | 24, 26 | 13, 0 |
| Suggested gear (0x17C) | `RaceSuggestedGearDisplay` | | R - 143, B - 45 | 44, 32 | 13, 0 |
| Shift lamp (0x1D4) | `RaceShiftTimingLampDisplay` | `timing_lamp` | R - 142, B - 28 | 8, 8 | 13, 0 |
| Pedals (0x210) | `RaceABMonitor` | `pedals` | L + 26, B | 22, 66 | 4, 1 |
| Tire wear (0xD0) | `RaceTireWearDisplay` | `tire_wear` | L + 58, B | 40, 68 | 4, 1 |
| Fuel (0x80) | `RaceFuelMeter` | `fuel_b0`, `fuel_b1` | L + 107, B - 30 | 86, 11 | 4, 1 |
| Second gauge (0xA8, nitrous 🔎) | `RaceFuelMeter` | | L + 107, B - 42 | 86, 11 | 4, 1 |
| Signs (0x2BC, 0x300, 0x344) | `RaceIndicator` | `sign_asm`, `sign_brake`, `sign_oil` | L + 119, L + 147, L + 175; B | 24, 24 | 6, 1 |
| G meter (0x290) | `RaceSideGravityMeter` | | C, B | 214, 24 | 2, 1 |

So at the bottom right of a 4:3 screen: the speed plate x 532-614, y 420-446 with the digits
ending at x 581 on baseline 438; the gear box x 502-526, y 420-446; the rev bar x 454-614,
y 454-464. These parts are tied to the right side on a wide screen (+96), the left group to
the left (-96).

The split form (two players side by side) has the same right-hand parts with anchors (1, x)
and the left group at L + 0 (pedals), L + 32 (tire wear), L + 80 (fuel), L + 92 / 120 / 148
(signs), no G meter; the reduced form (display mode 1) has the plate 82 x 28 at (R - 26, B)
(13, 1), the speed at (R - 59, B - 8), fuel at L + 74 and tire wear at L + 26.

❓ Which of the optional parts (pedals, tire wear, fuel, signs, G meter, suggested gear, the
steering picture `replay_sign_gtforce`) are shown in a given race: the race's rules and the
options `race_display_meter`, `race_display_steering`, `race_display_g_meter`,
`race_display_suggested_gear`, `race_display_map`, `race_display_time`, `race_display_lap`,
`race_display_position` (the display's flag words at +0x5C / +0x60) decide; not read.

### Digital speed (`RaceDigitalSpeedmeter`)

Font `GT4meter#1`, color 0x80E3B896, text `"%3d"`, pen flags 2: the text **ends** at
x = left + width of `"000"`, where left comes from the anchor with that width, and y is the
baseline. The number is the car's speed in the unit of the options (km/h or mph), rounded to
nearest; it is sampled every second frame and the larger of the last two samples is shown.

### Gear (`RaceShiftPositionDisplay`)

- `gear_base` (24 x 26, a pale box) over the rectangle, color 0x80FFFFFF.
- One character in `GT4meter#1`, pen flags 15, at (left + w/2, bottom - 12). The font's
  letters are not their codes: **`R` draws "R", `K` draws "N", `C` draws "D"**; the digits are
  themselves. Gear 0 is `R`; a car whose transmission has no steps (a flag from the car 🔎)
  shows `C`; otherwise the digit of the gear.
- Color: 0x80E3B896 with its RGB times 1.5 times a flash value, so **the character is black**
  on the pale box and flashes pale blue for 0.15 s after each gear change (the flash value
  goes from 1 to 0 in 0.15 s). Its alpha is halved while a flag from the car is clear (🔎
  the clutch engaged / drive available).

### Round meters (`RaceRoundMeterBase`)

Speedometer, rev counter and boost gauge share one drawing (0x3BE318, 0x3BE3C8, 0x3BE5F8):

1. A shadow pass: a second picture (+0x24) over the rectangle in color 0x80000000. None of
   these meters sets one (the field is cleared when the face is chosen), so nothing shows.
2. The pivot is the rectangle's middle plus an offset (ox * sx, oy * sy), where sx, sy = the
   rectangle's size over the picture's (1 at the sizes above). A frame is set there, scaled by
   the radius r (times an aspect, 1) and sx, sy.
3. The rev counter draws its red zone in that frame (below).
4. The face picture over the rectangle with color (1.2, 1.2, 1.2) (🔎 1 being neutral).
5. The needle, in the same frame.

**Angle** of a value v: a = a0 + (a1 - a0) (v - min) / (max - min), kept within a0 .. a1 + 2,
in degrees **plus 90**, measured clockwise on the screen from the +x axis (so a0 = 84 starts
just below the 9 o'clock direction and the needle sweeps over the top).

**Needle**: a strip from n0 * r to n1 * r along the angle, 6 points given for a unit length
(x from 0 at the pivot end to 1 at the tip): half-width 0.02256 at the base and 0.01288 at the
tip (table 0x6A28B0), colored 0x800000C8 along the middle and 0x80000040 at both edges (table
0x6A2900). The boost gauge's needle is 0.044 / 0.02752 wide (0x6A42F0). It is drawn N times at
alpha opacity / 6 each, the copies spread evenly from the previous frame's angle to this
frame's (a trail when it moves; at rest they add up), 🔎 each copy with a sub-pixel shift.

| Meter | Offset ox, oy | r | a0 .. a1 | n0 .. n1 | N | Range |
|---|---|---|---|---|---|---|
| Speedometer | 1, 15 | 60 | 56 .. 276 | -0.3097 .. 0.9613 | 5 | 0 .. the face's top value |
| Rev counter | -1, 15 | 60 | 84 .. 304 | -0.3097 .. 0.9613 | 5 | The face's (below) |
| Boost gauge | 0, 0 | 27 | 0 .. 216 | -0.5 .. 0.85 | 5 | 0 .. 2.4 |

(The base class's own values, used by no meter of this display: offset 0.5, 0.5, r 150,
65 .. 295, -0.1 .. 0.95, N 8.)

**Rev counter face and range** (table 0x6A3D38; rows of 16 bytes: `f32` limit, `f32` range
maximum, `f32` range minimum, `u32 ->` the number in the picture's name, "8000" for `rev_8000`;
the last row's limit is the word 0x7FFFFFFF): the first row whose limit is at least the car's
dial maximum:

| Dial maximum up to | Picture | Range (rpm) |
|---|---|---|
| 4000 | `rev_4000` | 0 - 4000 |
| 5000 | `rev_5000` | 0 - 5000 |
| 6000 | `rev_6000` | 1000 - 6000 |
| 7000 | `rev_7000` | 0 - 7000 |
| 8000 | `rev_8000` | 1000 - 8000 |
| 9000 | `rev_9000` | 1000 - 9000 |
| 10000 | `rev_10000` | 0 - 10000 |
| 12000 | `rev_12000` | 2000 - 12000 |
| any (the limit is 0x7FFFFFFF, which this processor compares as the largest number) | `rev_18000` | 2000 - 18000 |

**Red zone**: a ring sector behind the face from the red line's angle to a1, between radii
0.6833 r and 0.6833 r + 0.3109 r, color 0x80000099, in steps of about 10 degrees (at least 2),
drawn twice at alpha opacity / 3. No sector when the red line is at or past the dial's end.

**Speedometer face** (0x3EB860; tables 0x6A3C88 for km/h, four rows, and 0x6A3C50 for mph,
three; rows of 12 bytes: `f32` limit, `f32` range maximum, `u32 ->` the picture's name): from the dial's top speed S, the first of `speed_80km`
(S <= 80, range 0-80), `speed_160mph` (<= 160, 0-160), `speed_240km` (<= 240, 0-240),
`speed_320km` (0-320; in mph there is no fourth row, 240 is the last). The pictures are number
rings only (the "km" / "mph" in their names is not a unit); the unit is the separate `kmph` /
`mph` picture.

**The per-car numbers** (0x36CDF8, 0x36CD98, 0x441C58):

- Red line (rpm) = `ENGINE` row +0x6D (u8) x 100. Every one of the 723 rows on the US disc
  has it (it equals +0x50, the engine's red line byte, in 697 of them). If it were 0, the
  program would use the car's own red line value / 100.
- Dial maximum (rpm) = `ENGINE` row +0x6E (u8) x 1000 when not 0; **it is 0 in every row of
  the US disc**, and then the maximum is ((the car's rev limit in rpm + 900) / 1000, whole)
  x 1000. 🔎 The rev limit there is a u16 of the car's run-time state (+0x200), most likely
  `ENGINE` +0x51 x 100 after the tuning parts: with that, the faces come out as 8000 for 282
  engines, 7000 for 210, 9000 for 149, 10000 for 33, 6000 for 26.
- Dial top speed: N x 80 km/h, or N x 50 mph, with N = int(top speed x 1.1 / 22.2222) + 1
  kept within 2 .. 4, where the top speed (m/s) is a float of the car's run-time state
  (+0xFD8, 🔎 the estimate the program makes from the gearing). So the faces used are 160,
  240 or 320 in km/h, and 160 (for 100 and 150) or 240 (for 200) in mph.
- The values come to the panel when the display is set up for a car (0x39D218); each frame the
  panel gets the engine speed (rpm, an integer), the speed, the boost, the gear, the
  suggested gear and the lamp state.

### Rev bar (`RaceBattleTachometer`, a `RaceBarMeter`)

- `rev_bar` (160 x 10: 40 segments, the first 28 pale blue, the last 12 red) over the
  rectangle in black at half the opacity (the unlit bar).
- Over it the lit part: the left fraction f of the picture (same texture fraction), color
  0x80FFFFFF, with **f = floor(40 x min(0.7 x rpm / red line, 1)) / 40**: the red line is where
  the picture's red segments begin, and the bar is full at red line / 0.7. (The bar's maximum is
  set to the dial maximum and then to the red line, 0x3A8318 / 0x3A8338.)
- A plain line of color 0x70000099 above the red part: x from left + 0.70625 w to
  left + w - 1, y from top - 3 to top - 1.

### Boost gauge

`boost` (54 x 54) with the round meter's needle; value 0 .. 2.4 from the car each frame (🔎
its unit not read). Shown when the car has a turbo (a flag from the car set at set-up).

### Other parts, in brief

- `RaceShiftTimingLampDisplay`: `timing_lamp` (8 x 8, a red dot) at its own size, blinking
  while the car's shift-up flag is set. `RaceSuggestedGearDisplay`: a `GT4meter#1` character
  (`"%c"`) that blinks in before corners. `RaceIndicator`: its sign picture tinted with its
  color, blinking. `RaceOdometer`: `GT4oddmeter#1`, 6 digits (`"%06d"`), color 0x80E3B896.
  ❓ Their timing constants are in the classes' set-up code (0x3A6B70-0x3A73F4), not listed.

## The course map (`GTCM`)

Course slot 33. The layout is the shared engine's
([engine/gt3r/docs/formats/course.md](../../engine/gt3r/docs/formats/course.md)) with a table
of the road's edges added. Read on the 110 course files of the US disc.

```
0x00 "GTCM"
0x10 u32 width (180)    0x14 u32 height (180)
0x18 f32 world x of the map's middle   0x1C f32 world z of the map's middle
0x20 f32 scale: map pixels per meter   0x24 f32 rotation, degrees
0x28 u32 -> a Tex1 set with one picture (0x80)
0x2C u32 points (255; 50 or 500 on a few courses)   0x30 u32 -> the points (0x41E0)
0x34 u16, 0x36 u16 ❓ (83, 54 on Grand Valley; 0, 0 on Tsukuba), then zeros to 0x80
points: {f32 x, z of one road edge; f32 x, z of the other} at even steps round the lap, the last
        equal to the first (NaN on the gymkhana and slalom courses)
```

- **The picture**: 4-bit, 16 grays with alpha (the line is light, its soft edge dark and
  faint), uploaded as it is stored (a 184 x 180 4-bit buffer, the texture a 180 x 180 region
  of a 256 x 256 one, 16 colors at 0x4120 of the set). The program takes the picture's size
  from the texture's region, not from +0x10 / +0x14.
- **World to map** (0x3BEBA0): with dx = x - middle x, dz = z - middle z and the rotation r,
  `mx = scale (dx cos r - dz sin r)`, `my = scale (dx sin r + dz cos r)`, from the picture's
  middle, x right and y down. **The shared engine's transform holds.** Checked by putting the
  middle of every checkpoint of each course's road (`RNW4`) on its picture: 95 courses, 12,040
  checkpoints, 97.3 % on the line (within a pixel); with the rotation's sign turned 83.6 %.
  The misses are the art's: Seoul's picture is a few pixels off its own numbers (6 of 30 on),
  the 500 m / 1000 m test courses half off, the Motegi and gymkhana ones a little.
- 14 files (`ss_*`, the photo locations) have the older layout: 160 x 120, the picture at
  0x30, no points. `ss_times` has an empty header (no map).
- 🔎 The points table is not used by the race display's map (the menus' `RaceCourseMapFace`
  is the likely reader).

**Drawing** (`RaceMiniMap`, 0x3BEE78): the map does **not rotate or scroll**. Its rectangle is
the picture's size times a scale (1; 0.75 for two players), placed by the anchors: in the
single race left 24, top 96, so x 24-204, y 96-276 on a 4:3 screen (tied left on a wide one).
In order:

1. The player's marker in black (color 0x80000000), under the map.
2. The map picture, color 0x80FFFFFF, at the element's opacity.
3. Every other car: `point_rival` (10 x 10, a blue dot), color 0x80FFFFFF, centered on its
   map position, at its own size (times the map's scale).
4. The player: `point_target` (22 x 22, a red dot on a dark disc), color 0x80FFFFFF, with a
   render setting changed around it (🔎 not identified) and alpha = opacity x (0.3 + 0.7 b), where b pulses:
   period 0.5 s, rising for the first half at 1 / 0.1 s and falling at 1 / 0.3 s, kept in
   0 .. 1.

A car in state 2 or 3 of the race's car-state query (🔎 out of the race / in the pits) is
drawn at half alpha. In the network races the markers are the letters `carptr_0` .. `carptr_5`
("A" to "F", 18 x 22) per entry and the player's is tinted 0x805EA8F2.

## Messages

`RaceMessageDisplay` is a line of text or, when the text starts with `@`, the picture named
after it. It has a font, a color, fade-in and fade-out times and a blinker; showing it for a
duration fades it in, holds, and fades it out. The text is centered by the horizontal anchor
with **y as the baseline**; a picture uses both anchors. `RaceEventDisplay` (member 0x17BC) is
eight such lines used as a queue at one place: a new line waits for the one before it. The
words are the page `RaceDisplay` of `rtext/race<LANG>.rt2` (102 keys the program looks up by
name).

| Message | Where | What |
|---|---|---|
| Start countdown | 0x252C: 320, 224 | The whole seconds left of the race's count (🔎 the count's source is the race object, not read), `"%d"`, once a second: font `FC_48` (`FC_48*75,75` for two players), color 0x80C8C8C8; from 3 down each shows 0.8 s and fades out in 0.15 s. The display has **no lights**: numbers only. `FC_48` holds the digits and A C D E F G H I L M N O P R S T U V W Z É Ó |
| Start | same | Key `RaceStart` ("START"), 1 s, fade out 0.15 s |
| Finish | same | Key `RaceFinish` ("FINISH"), 3.7 s, fade out 0.3 s (`RaceDisplay::vf41` 0x3A1568); with it the display's fader (+0xBC) keeps the rest full 1 s and takes it out over 0.25 s; FINISH, the lap's line and the A-spec points stay on their own times (✔ the original's chase race), under the results page A license test's display (RaceLicenseDisplay) has no FINISH (its virtual 41 does nothing) and goes out over 0.25 s from the test's end (0x3BCFF0, as the result word is set, and its virtual 36: the fader +0xBC), under the page's title (✔ the original's B-1 pass in 0.1 s frames: the display gone 0.3 s after the stop; ours by picture). A run that ends with a time posts it to the lap line (0x3F8AB0 → RaceDisplayGeneralTimeEvent 0x35E550, not after a rule failure): the time alone for 3 s, under the page's title (✔ the original's "0'11.166"; ours by picture) |
| Lap time at the line | Event line: 320, 139, w 580 | `RaceDisplay::vf47` (0x3A0A78), told by the lap's events (0x39F200: a `RaceDisplayLapTimeEvent` and a `RaceDisplayDiffEvent`, posted at each lap's line and at the finish by 0x35E550 from `DynamicsConductor::vf21`) for the display's car: the time alone, `"%s%d/%d   %s"` (key `WordOfLapTime` "L", the lap, the laps, the time; `"%s%d   %s"` without a lap count) only with the display's flags +0x58 bit 10 (the option `race_display_config`, off in a new game) or on a two-player half. Font `hn_20R`, color 0x80C8C8C8, 3 s, fades 0.15 s. ✔ the original's chase race: "1'47.118" with "+0'14.284" at lap 1, "1'42.771" with "+0'25.374" (the winner's gap) at the finish; in B-spec the finish shows none (03-race B-spec, 30-championship; ❓ why), the other laps do |
| Section time | Event line | `"%s%d   %s"` with key `WordOfSectionTime` ("T"): "T1   0'35.123", as above. The split points are the road's (RNW4) first checkpoint list (`race/sections.*`; ✔ Tsukuba 505.9 and 1072.2 m, where the original's chase race shows T1 and T2); the lap step 0x358608 compares each car's distance with its next point (F +0x4B4) and pushes event 15 with the time (0x359C10). Here the gap under it is to the first car through that split on that lap, in red (the single race's; who is told which gap: race_flow.md "2 Player Battle"; the original's: "T1 0'27.247 / +0'05.800" with a car's picture); the car's picture is not drawn |
| Gap to another car 🔎 | Event line, offset (32, 25) | At a lap's line the gap to the first car through that line on that lap (here; ❓ the DiffEvent's own partner not read), red; the gap, `hn_20R`, 3 s, with a car marker beside it (`RaceCarIconDisplay`, member 0x2D14, placed 82 or 89 left of and 22 below the line, 2.85 s) |
| Information | Event line | `hn_20R#1`, color 0x80C8C8C8, 3 s: "Final Lap" (key `EnduranceFinal`), "%d laps left", "%s is making a pit stop", ... |
| Wrong way | Status message 0x228C (320, 430 in the chase views; 320, 348 in the driver's) | The picture `GT4_meter_texture_wrongway` (32 x 32, a no-entry sign), color 0x80FFFFFF, blinking: period 0.2 s, on for 0.3 of it, rise 0.05 s, fall 0.1 s; kept up while the car goes the wrong way |
| Other status lines | 0x228C | Text in `hn_20R`, color 0x80C8C8C8 ("AUTO DRIVE", pit messages, ...) |
| A penalty | 0x228C | The word `Penalty` with the seconds left (0x35FC80), color 0x800000C8 (RaceDisplay::vf27); before the pit messages |
| Replays | 0x2A6C; 0x1170 | `JP/replay` (144 x 32) at 32, 34; the logo `JP/gt_logo_mini` (188 x 50) with its right bottom corner at (604, 441), tied right |

The lap-time and best-time elements blink / fade through the same helpers when a lap is
completed (🔎 0x3A0D10, 0x3A0D78: fades of 0.15 s).

### The two timing helpers

- **Fade** (0x3A9758-0x3A99B8): a value 0 .. 1 with a delay, a fade-in time, a hold time (or
  forever) and a fade-out time, stepped by 1/60 s a frame.
- **Blink** (0x3A9608-0x3A9758): a period and the fraction of it that is "on"; the value rises
  at 1 / rise-time while on and falls at 1 / fall-time while off, within 0 .. 1; a count of
  periods or forever.

## The picture pack (`display.gpb`)

`gpb2`, 72 pictures, the same names in the `US`, `GB` and `JP` packs (the words differ by
region; the `JP/` folder name is the same in all). Sizes in pixels:

`00no_texture` 36 x 50, `GT4_meter_texture_laptime` 24 x 24, `GT4_meter_texture_point` 9 x 9,
`GT4_meter_texture_wrongway` 32 x 32, `JP/best_time` 80 x 20, `JP/gt_logo_mini` 188 x 50,
`JP/lap` 42 x 20, `JP/last_time` 80 x 20, `JP/position` 74 x 20, `JP/replay` 144 x 32,
`JP/total_time` 94 x 20, `boost` 54 x 54, `carptr_0` .. `carptr_5` 18 x 22, `chip_metalic`
12 x 42, `chip_solid` 12 x 42, `fuel_a0`, `fuel_a1` 38 x 64, `fuel_b0`, `fuel_b1` 86 x 11,
`g_meter_base` 214 x 24, `g_meter_point` 7 x 7, `gear_base` 24 x 26, `kmph` 28 x 8,
`mark_1st`, `mark_2nd`, `mark_3rd` 40 x 30, `meter_prius_base_kmph`, `meter_prius_base_mph`
406 x 42, `meter_prius_brake` 46 x 42, `meter_prius_vsc` 260 x 42, `mph` 28 x 8, `nos_a0`,
`nos_a1` 38 x 64, `panel_base` 336 x 102, `pedal_bar` 20 x 58, `pedals` 22 x 66, `point_rival`
10 x 10, `point_target` 22 x 22, `position_base` 42 x 42, `prius_monitor` 172 x 80, `pylon`
60 x 70, `refuel_bar` 8 x 26, `refuel_bg` 256 x 26, `refuel_scale` 157 x 12,
`remote_speed_kmph`, `remote_speed_mph` 82 x 26, `replay_plate` 132 x 20,
`replay_sign_gtforce` 170 x 170, `rev_4000`, `rev_5000`, `rev_6000`, `rev_7000`, `rev_8000`,
`rev_9000`, `rev_10000`, `rev_12000`, `rev_18000` 130 x 100, `rev_bar` 160 x 10, `sign_asm`,
`sign_brake`, `sign_oil` 24 x 24, `speed_80km`, `speed_160mph`, `speed_240km`, `speed_320km`
130 x 100, `timing_lamp` 8 x 8, `tire_wear` 40 x 68.

A picture is looked up as `image/<name>.png`; a name not in the pack gives
`image/00no_texture.png`. For pictures a GS color is a tint with 0x80 neutral (0xFF nearly
doubles and saturates), 🔎 by the GS's rule, not compared with the original running.

## How this was read

- Layout tables, the tach / speed tables and the needle shapes: bytes of
  `~/gt4re-out/program/core.bin` at the addresses given.
- Positions inside the panels, fonts, colors, formats and timings: constants in the program's
  functions for the classes named (`RaceDisplay` 0x39A908-0x3A2718, the element classes
  0x3A3900-0x3AF300, the meters 0x3BE1D8-0x3BF670 and 0x3EB7A0-0x3ECAD0, 0x407568).
- The pack, the fonts, the text table, the `ENGINE` rows and the course maps: the disc's files.
- ❓ Not read: the lap-time list, the
  tool tips, the two-player and license displays' own classes (`RaceSplitDisplay`,
  `RaceLicenseDisplay`), the hybrid panel, the replay displays, the pause menu.


## A replay's opening card (`RaceDisplay::vf45`, 0x39C838)

Two lines in the middle of the picture, color 0x80C8C8C8: the car's name (the entry's +0x348C) in
`hb_24#1` at (320, 220), and below it in `hm_16#1` at (320, 260) the race word `RaceInfoFormat`
("%s  %s  %s") filled with the car's power as built (`"%d%s"` in the unit the options name; `"--%s"`
when its GENERIC_CAR row hides it), its tires (`"%s/%s"`: the words `TireShort_NN` of the front and
rear tires, "SM/SM") and the race's name (a word the organization gives, `"%s%d"` with a number:
`ARCADE_SINGLE_RACE` "Single Race"); the times it shows and fades are its caller's (❓ not read).
In the original's replay the picture under it is darkened and the course's logo shows bottom right;
here the card shows the first 3.5 s with fades of 0.5 s over a darkening of 75 % (judged), and the
course logo is not drawn (❓ its picture was not found).

## The small gauges

A picture "in its tint" below: the pack's picture is white where drawn and the original shows the tint itself, not the tint over 0x80.

- **Fuel** (`RaceFuelMeter::vf6` 0x3A57A8; on the chase views and B-spec only when the race counts fuel): `fuel_b0`, over it `fuel_b1`
  in the tint 0x80E3B896 as far as n = int(share x 10 + 0.5) steps: 5.2 n + 15 units, 4 more from the sixth step (the gap).
- **Tire diagram** (`RaceTireWearDisplay::vf6` 0x3A5FD8): `tire_wear` in the tint 0x80877B80, the wheels' boxes 6 x 11 over it.
- **Pedals** (`RaceABMonitor` 0x3A54F8): `pedals`, the brake's bar 4 x 56 at (x + 1, y + 4) in 0x800000F0, the throttle's at
  (x + w - 5, y + 4) in 0x80E3B896, from the bottom (`RaceSimpleBarMeter` 0x3AB570).
- **Signs** (`RaceIndicator::vf5` 0x3A6C28): a tint (0x80E3B896 the aids', 0x800000C8 the brake's and the oil's) scaled by a level, the
  greater of this frame's and the last's: state 0 black, 1 half, 2 full, 3 blinking (0.2 s: 0.1 s on, then out at 1 / 0.07 a second).
  The states (RaceDisplay::vf2, 0x39E280): the greater of F +0x5F8 and +0x5F9 (ASCC's and TCS's status) through the table 0x6A0690
  (on 1, acting 3); the handbrake F +0x490 over 0.25 → 2; the oil's distance at its limit (0x3437B0) → 2.
- **Refuel bar** (`RaceRefuelMeter::vf6` 0x3A59D8, member 0x2D78, shown while 0x35BEB0): `refuel_bg` 256 x 26 centered at (320, 58), from
  x + 15 `refuel_bar` 155 x the share wide in 0x805EA8F2, `refuel_scale` 157 x 12 at (x + 14, y + 7), "%d/%d" (liters, rounded) in
  `hn_20R` ending at x + 241, its baseline at y + 18.
- **Pit messages** (RaceDisplay::vf28 0x39D7C8, by F +0x5FA, on the status line 0x228C): 4 `AutoDriveFullThrottle`, 5 `AutoDriveReady`,
  6 `AutoDriveGo`, 7 `PitTooFast`, 8 `PitLastLap`, 9 `PitNoTireWear`, 10 `PitSpeedLimit`, 11 `PitWork`, 12 `PitGetOut`; 1 .. 3 the
  wrong way. (7 .. 9, 11 and 12 without the letters' extra 1.2.)

## B-spec

The original's B-spec race (reference `03-race/bspec_display_a.png`) shows display mode 1's places
(table 0x6A0BA0: the place, laps, map and lap time as the single race's, "Total Time"
(`JP/total_time`) with the race's time (member 0x1438, written H:MM'SS.mmm) in place of the best and
last laps, the middle message at 320, 240) and panel layout 2 (the small speed plate, the help strip
0x2DA0 along the bottom: the race words' `Tool Tip1`, "%BF Pace %Bc Overtake %Bd Pit %Be Change
View %BG Select Car %Bh Race Monitor %Bj Display", the pad's buttons from the `Gaiji` font). Over
them the commands' panel: the Race Monitor's movie at its root's second frame (0x3D92B0), whose
`PACEPANEL` has the five paces as keys, the chosen one's digit orange, an overtake key and a pit
key "P", and the word of what is asked under them (`key_pace1` .. `key_pace5`: Slow Down, Cruise,
Steady, Fast, Push; `key_overtake` Overtake; `key_pitin` Pit Stop); it is down while the car is in
the pits (strobe.md). **The strip** (RaceTooltipDisplay::vf5 0x3AE138): a black plate 640 x 42 at
the bottom, the line centered, its baseline 22 down, squeezed sideways into 15/16 of the width.
❓ Which part of the program sets display mode 1 for B-spec, and the plate's place (its bottom at 431
as measured), are not read. The commands: Left / Right the pace, Triangle overtake,
Cross the view, R2 the display; the race opens in the low view from the car's nose.

**The display's car (Select Car).** The display shows one car, its member +0x2E; +0x2F is its own (the
player's). Each step the race gives it the camera's target (RaceSinglePlayer::vf74 0x3D5FF8 and the network
races' vf74: the camera manager's current entry, +0x9BC x 0x19C, its car +0x1C, to RaceDisplay::vf11 0x39CDF0).
When the car changes, vf11 marks the display to be set up again (+0x31, +0x32) and the next frame (0x39EC40) sets
the panel up for it (0x39D218), clears the event lines and the gap's car picture (vf54) and shows the car's name
(0x39CFD8: its entry's name on the strip's timed line 0x2F2C, 5 s with fades of 0.2 s, and on 0x23DC). Everything the
frame reads per car is the target's (RaceDisplay::vf2 0x39DBB8): the place, the laps, the lap's clock (0x3445F0), the
best and last laps, the lap and section lines with their gaps, the meters; the race's total time stays the race's
(0x358440). The tire diagram, the fuel gauge and the panel flag +0x1C only for a car a player drives (0x3451D8: the
control source F +0x466 is 0, 1 or 4 and F +0x441 clear); for any other car they are hidden (0x39E6A0). The commands'
panel (`PACEPANEL`) shows only while the followed car is the player's and his car is out of the pits (0x3D8C18: the
monitor's +0x64 = +0x60, +0x40 = 0). ✔ The original's B-spec race following other cars (reference
`36-driver-sky/gtwc_race1_bspec_chaparral_pescarolo.mp4`): their place, lap clock, section time, the map's red dot on
the followed car, no pace panel, no tire diagram, the name on the strip for about 5 s. Here: `screen_race.cpp`
`display_car()` (B-spec live: the followed car; else the player's, a replay keeping its own), `fill`, `update_panel`;
the name over the help line, the help line faded under it (❓ the two lines' blend judged).

**The gap's car picture** (RaceCarIconDisplay 0x2D14, set by 0x3A1098 for a time gap, 0x3A0E40 for a lap gap): the other car's
icon (RaceDisplay::vf51 0x3A1030: in the race kinds 1, 2, 17, 18 the car in place 1 when the gap is behind, in place 2 when it
is a lead; shown when that car's state is 3), its 64 x 32 picture's left 89 units (a lap gap 82) left of the event line and its
middle 22 below it, 2.85 s with fades of 0.15 s. ✔ The original's section time following the Peugeot (the same reference). Here
(`screen_race.cpp`, after the display): beside a section time's or a lap line's gap, the car first through that split or line
on that lap, with the gap's own fades and, like the line's words, not under the display's fade at the finish (✔ the original's
chase race: the Beetle's picture by "+ 0'14.384" at lap 1 and by "+ 0'25.374" over FINISH); ❓ the car's state test is not made.

## The two-player halves

`RaceSplitDisplayBase` (vf9 0x3A29A0), one for each half; kind 8 above / below, kind 9 side by side (with the wide-screen
option).

- **Canvas**: kind 8's is 640 wide, the first player's half shown as y -10 .. 230 of it and the second's as 0 .. 240; kind 9's is x
  -106.667 .. 320, 480 high. The elements' places: the tables 0x6A0DE0 and 0x6A0FD8 above (`race::read_split_layout`).
- **The panel** is the simple one whatever the view: layout 1 drawn 240 units higher in kind 8 (the display's +0x6C), layout 3 in
  kind 9. The map is x 0.75 (0x39B714).
- **The middle message** in `FC_48*75,75` (0x3A1460).
- **The lap's line** names the lap with the laps in every race of it (0x3A0AD8), the gap under it blue (0x80E3B896) for a lead and
  red (0x800000C8) behind.
- **The display's opacity**: hidden until 6.1 s into the 10 s hold, then in over 0.5 s (0x39BEC8). At its own car's finish full
  for 1 s, then out over 0.25 s; the FINISH message 3.7 s with a fade of 0.3 s (0x3A1568). The other half goes on.
- Which gap each display is told: [race_flow.md](race_flow.md) "2 Player Battle".

Here `RaceHud::draw` with `HudState::split` 1 (stacked) or 2 (side by side). On a picture narrower than 16:9 the halves are stacked
whatever the option says; on a wider one a half's display keeps to the screen's sides as the single race's does (stacked), or to its half's left (side by side) (ours).

## License tests

The original's B-1 (reference `05-gtmode/37_license_test_b1_driving.png`): the laps "1/1", the map,
the lap's time and the best lap as the single race's, no place, and in place of the last lap the
test's three target times (its RACE row's gold, silver and bronze) each by its trophy (`mark_1st`,
`mark_2nd`, `mark_3rd`; RaceLicenseDisplay::vf9 names them and `pylon`). ❓ Drawn here at the
picture's places: trophies centred at x 485, y 100 / 128 / 156, the times ending at x 608 on
baselines 8 below; the pylon count is not drawn.

### The endurance words

The display's word table (102 names at 0x621540, loaded by 0x3AE9A0 into the table at 0x621870) names `EnduranceFinal` (14)
and `EnduranceLaps` (15) beside `TimeLimit` (0) and `TimeLimitCount` (83); ✔ nothing in the US program reads entries 14 and
15 (no load of the table's +0x38 or +0x3C, and no reckoned index reaches them). A timed endurance race shows what its display's
flag gives: the lap count without a number of laps (`"%d"`), "Total Time" with the race's time and "Best Lap" with the best
(table 0x6A08F0). `TimeLimit` and `TimeLimitCount` are the arcade's own time limit's count (0x39D500), not the endurance races'.

