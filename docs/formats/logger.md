# The Analyzer (data logger)

The Analyzer icon on the time trial's, free run's and license test's pages (`COMMAND_LOGGER`) opens the project `logger`:
the page "Data Logger" with the graphs of two laps, Track A and Track B, against the distance run. GT4 US 1.01 addresses.

## The sequence's side (0x10FEE8)

Called by the time trial (0x112ACC), the free run (0x117394) and the license sequence (0x11CEE4) for `COMMAND_LOGGER`:

- `CanLogger` (quick work +0x4B0) is kept; the logger's global 0x618710 is the sequence (0x1234F0), 0x618718 = 1.
- The project `logger` runs. When it leaves with `load_track` (global 0x618714, the control's `load_track`) 0 or 1, the
  track is kept in the sequence (+0xF0FC), labomode runs on its load page (`MODE_LOAD_BESTLAP_REPLAY`), `CanLogger` is 1 when
  a file was loaded, and the project `logger` runs again; with -1 the loop ends.
- After: 0x618710 cleared, `CanLogger` back to its value before. When the file loaded put another car in the race's entry 0
  (+0xF104, 0x3EB0D0), the entry is put back and the race's recorders are emptied (their times 359,999,999), and
  `canSaveGhost`, `CanLogger` and `canReplay` (+0x474, +0x4B0, +0x46C) are 0.

## The tracks

Track A is the race's recorder `vf72(0x100)` (RaceSolitaire 0x3EA8C8: +0xE48C, RaceFreeRun +0xE84C), the best lap
(RaceFreeRun::vf67 0x3BA0E8 copies a faster lap there); Track B is `vf72(1)` (0x3EA908: +0xE66C / +0xEA2C), a lap loaded
from the card. A recorder's +0x1D0 not 0: empty. `getCarName(i)` / `getPower(i)`: the entry's car (0x445450; the power by
its showroom car 0x364E78 in the option's unit, `"%d%s"` (0x4729A0, 0x4726B8), `"----"` when the car hides its figures or
has none); an empty track: "".

## MLoggerControl (0x11F1E0)

| Member | Routine | Does |
|---|---|---|
| `analyze_start` | 0x123368 | the analysis: +0x2A4 1 (running), +0x2A8 (done) and the progress 0; its thread (0x123568 → 0x122B28) runs 0x122FA8 |
| `analyze_stop` | 0x1233B0 | stops a running analysis |
| `getCarName(i)`, `getPower(i)` | 0x11EF50, 0x11F068 | above |
| `setTotalTime(i, face)`, `setSpeedAverage(i, face)`, `setSpeedCurrent(i, face)` | 0x11EDB8, 0x11EE40, 0x11EEC8 | the text faces the control writes each picture (+0x8C, +0x94, +0x9C by track) |
| `setCourseMap(face)` | 0x11F180 → 0x11FF98 | the course map; its marks `point_target` (A) and `point_rival` (B) |
| `mode`, `load_track` | | plain values (`load_track` -1 at first) |
| `course_label` | 0x120020 | the race's course (0x4472A0 of the race's +0x70) |
| `race_mode_label`, `race_mode_subnumber` | 0x120100, 0x1201F0 | the sequence's vf at +0x308: its label and number (-1: none) |
| `display_speed`, `display_accel`, `display_brake`, `display_TrackA`, `display_TrackB` | 0x123720 .., 0x1236C0 .., 0x1236F0 .., 0x123750 .., 0x123780 .. | the game's bytes +0x3A35A, +0x3A35B, +0x3A35C, +0x3A358, +0x3A359 (the save's block 0x39F1C: TrackA, TrackB, speed, accel, brake; 1 each in a new game) |

The constructor (0x11EA90): the unit flag +0x2B8 (1 = metric, the option at 0x6244D8 being 0), the grid's step +0x2BC
25 m (imperial 25.146 m, 1/64 mile) and the speed lines' factor +0x2C0 1 (1.609); the speed graph's top +0x308 / +0x30C 800,
the scales +0x2E4 .. +0x2EC 1; a block of 0x4000C bytes (two tracks of 0x4000 samples of 8 bytes, the two counts, the
sample spacing).

## The analysis (0x122FA8)

The race is put in its replay (0x387EF0 mode 2, or 3 with +0xCC4 1) and run again picture by picture (the sequence's vcall
+0x38) until the longer of the two recorders has played all its frames (0x346510: its length; +0x40 the frame: the
progress, +0x2AC / +0x2B0), the cars of entries 0 (A) and 1 (B; with B empty, entry 0 again) sampled after each picture
(0x11E450). The sample spacing: the course's length (0x395400) / 16,383, at least 0.5 m.

A sampler (0x11E400, 0x11E450): the car's distance = its distance in the lap (+0x5A8) + (laps − 1) × the course's
length (0x343B48); a sample every spacing metres, the speed (+0x6C8, m/s), throttle (0x344698) and brake (0x3446A0)
averaged over the sample's metres (0x11E348: the values between the two pictures taken at the metres they cover), and two
values 0x344798 / 0x344828 at the sample's point (0x11E310). A sample: u16 = speed × 40 (bits 3 .. 15) | gear (+0x624 & 7),
u8 throttle × 255, u8 brake × 255, s16, s16. At most 0x4000 samples a track; the fastest speed kept (+0x2C). (A [1 2 1] / 4
filter of the samples follows the sampler at 0x11E8F0; nothing calls it.)

Then: each track's count the smaller of the two (A's and B's the same), +0x2F8 the smaller start distance, +0x2FC
+0x2F8 + the shorter length ((count − 1) × spacing, 0x11DBB0); the speed graph's top +0x30C = min(800, the fastest of
both in km/h + 100), the speed scale +0x2E8 = max(1, (int)(fastest km/h / 100)); 0x122D38: the length +0x300, the
middle +0x2CC = length / 2, the metres' scale +0x2E4 = clamp(face width / length, 0.4, 10), the tracks' times +0x290
(the recorders' +0x10, ms). Done: +0x2A8 1, the face's event `onLoggerAnalyzeFinish`.

## The face (mLoggerFace, 0x1BBB68)

Properties `control` (+0xA4, and the control told its face), `progress` (+0xA8, an MProgress), `grab` (+0xAC). With `grab`:
Triangle (`cancel`, 0xFF1B) `onLoggerMenu`, Cross or Start (0xFF0D, 0xFF8D) `onLoggerPopup` (vf72 0x1BC2C0). Each
picture (vf70 0x1BC138): while the analysis runs the progress is its frame / its length (0x122CC0), else the pad
(0x1206A0):

- the left stick's x zooms the metres (+0x2E4 += x / 32 × (log10 +0x2E4 + 1.2), 0.4 .. 10), its y the speed
  (+0x2E8 += y / 32 × (log10 +0x2E8 + 1.2), 0.5 .. 4);
- the right stick's x moves the middle (+0x2CC += 2 × |x|^1.5 / +0x2E4, signed); past either end it stops there, and
  pushed past again after the stick was let go it goes round to the other end (+0x2C4, +0x2C8);
- the right stick's y scrolls (+0x2D0 −= 2 × |y|^1.5, signed, 0 .. the graph's height − the face's, 0x1233E0: the pedal
  bands 50 each + 4 + 10 + +0x308 / +0x2E8 + 75);
- each track's time face ("m'ss.sss" of +0x290, 0x42E478) and speed face (`"%3d%s"`: the speed at the middle, 0x11DBE8,
  in the option's unit, 0x472940 / 0x472680); the course map's main point at the middle + +0x2F8 (its vcall +0x308) and its
  span the metres shown (+0x310: max(left, 0), min(right, +0x2FC − +0x2F8), 0x120518 / 0x120528).

The picture (vf71 0x1BC1E0 → 0x120CF0), y up from the face's bottom (an orthographic view: x the metres
left = middle − width / 2 / +0x2E4 .. right, y +0x2D0 .. +0x2D0 + height), the face's width and height at +0x2EC, +0x2F0:

- a track is shown when it is not empty and its `display_Track` is set; the pedal bands when one is and `display_accel` or
  `display_brake`: B's band at y 4 .. 54 then A's above it (each 50 high, its letter `A` / `B` (0x68DDD0) in `hb_24` at scale 2,
  the track's speed colour at alpha 0.4, at x 64 and 12 above the band's base (0x122A50), and a grid 0x121C00 of 100 in
  steps of 25);
- the speed graph 10 above the bands (or at 0 without them): its grid of 875 (0x121C00: vertical lines every grid step
  except every fourth, grey 0.25; across every 25 units × +0x2C0, white at 0, grey 0.5 every 100, 0.25 else, units of
  km/h / +0x2E8 high);
- the distance lines every 4 grid steps (100 m), grey 0.5 from the bottom to the top, their numbers in `hc_20` x 0.5 under
  the speed graph's base: `"%dm"`, imperial every 16th `"%dml"` of the metres / 1600 (0x121340);
- the band's base line (white) and `"0%"` / `"100%"` in `hc_20` x 0.4 at x 16 (0x121688);
- each track's throttle (green 0.25 1 0.15) and brake (red 1 0.25 0.15) as filled areas from the bands' boundary, A up,
  B down, 50 high at 1, coloured from the colour at the base to twice it at the top, half opaque (0x121590, 0x1218C0);
- the speed labels `"%dkm/h"` / `"%dmph"` every 100 to 800 in `hc_20` x 0.7, right-aligned at the face's right less
  16 (0x122830);
- each track's speed line (A 0.809 0.29 0.295, B 0.318 0.489 0.739), B first, drawn five times at offsets (0,0) (1,0)
  (0,1) (1,1) (0.5,0.5) of a pixel; at most 1,000 points across the face (0x1226C8, 0x121DD0);
- the readout (0x121FF0): an orange line (0.9 0.5 0.1) at the end of the data; the distance at the middle in `hc_20` x 0.7
  right of the middle (`"%.1tm"` of the metres × 10; imperial `"%.1tml"` of metres × 0.00062137 × 1000); each shown
  track's speed at the middle with a leader line (15 across and 30 up or down, then 30 across) to its label `"%.1tkm/h"`
  (`"%.1tmph"`) in `hc_20` x 0.8 in the track's colour: down to the right; with both, the faster's label up (A's to the
  left), and up when the point is within 30 of the bottom; the middle's red line 2 pixels wide and a red mark pointing down onto the graph's base (0x120540: base 11 above, point 1 above).

Texts written at the pen (0x490890) stand on their baseline there; those in a box (0x490700) hang from its top. ✔ PCSX2 (the
logger's pages from the US disc with a saved B-1 best lap loaded into Track A, `~/gt4re-out/analyzer/orig/`): the page,
the menu and the options as drawn here; the letter `A` standing 16 above the band's base; the readout's distance just above
the base and the distance's number just under it; the mark pointing down. The emulator's analysis gives no usable laps
(time 0'00.000, 458 mph: it cannot run recordings again), so the graph's lines are not compared.

The face's picture is drawn into a buffer and put on the screen three times, the second and third one line up and down at
1/3 and 1/4 (0x120CF0's end).

## The pages (projects/logger, the disc's scripts)

LoggerRoot: with `CanLogger` 0 the menu at once with Return to Logger and Load Track B greyed (only Load Track A and Exit);
else the progress dialog "analyzing" (Cancel stops it: the menu) and then the graph. LoggerMenu (Triangle): Return to
Logger (a stopped analysis runs again), Load Track A / Load Track B (`load_track` 0 / 1, the project leaves), Exit.
LoggerPopup (Cross): Display Speed / Accel / Brake Yes or No, Display Track A, B or A & B.

## Ours

`menu/logger.*` (the control, the analysis, the samples, the readouts), `app/logger_face.*` (the picture),
`screen_race.cpp` `open_logger` / `logger_back` (`COMMAND_LOGGER`, the loads; after a file loaded into Track A the run's
laps are dropped and Replay, Save Best Lap Replay and the Analyzer's run with them, 0x3EB040 / 0x3EB0D0: ✔ the B-1 page
then has eight icons), `RaceHud::draw_map_on_screen` (the course map face: the course's map from the face's top left at
its picture's size, ✔ the original's place in PCSX2, with point_target / point_rival at the tracks' places at the middle
once `display_main_point` is set; ❓ the face's own drawing is not read, its span (`display_span`) is not shown). The laps run again are the race's kept laps' controller
records (the ghost's, `Race::ghost_best`) on a car of their own (`sim::Session::solo`), not the race's replay; ❓ the two
values at +4 / +6 of a sample are not read by any picture and are not kept.
