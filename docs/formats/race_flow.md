# A race's flow: from its menus to its results

Code: `src/gt4r/race/race_flow.*` (the start's hold, laps, times, order, the finish rule, the
result rows), `src/gt4r/race/start_camera.*` (the start's shots, [camera.md](camera.md)),
`src/gt4r/menu/race_natives.*` (MQuickWork), `src/gt4r/app/screen_race.cpp` (the sequence).
Facts from the program (`CORE.GT4`, SCUS-97328 v1.01; addresses are the program's). ❓ not
read to the end, 🔎 partly. "race" is the race object (RaceArcadeSingle for a single race),
"org" its RaceOrganization (race +0x6C), "K" the DynamicsConductor (race +0x70).

## The run

`RacePS2Base::vf101(mode)` (0x33BBF8) sets the race's mode (0x387EF0) and runs a frame at a time
(update, draw) until the run is over (race +0xD18). Modes: 0 the player drives, 1 B-spec (the
same race, his car driven for him), 2 / 3 a replay of a driven / a B-spec run, 4 the scene behind
the race's menus. There is no numbered state machine: a run is the organization's start hold,
then the race until its end timer fires.

**The end** (`vf87(code, seconds, x)`, 0x38AAB0 -> 0x389ED8): the end code goes to race +0xCE0;
with no time the run ends at once, else the organization's end timer runs (org +0xD4) and at 0
pushes event 1, the run over. Codes: 1 Start in mode 4; 2 Start in a replay (0.5 s), or the
pause's "exit"; 3 a set length ran out (a demo, a replay's end) or the pause's idle time-out; 4
the pause's "retry"; 5 a restart inside the run (free run only); 6 the time limit.

## The start (`fn_003C0208`, every frame; reset by `fn_003C0190`)

The start type is the race information's byte +0x40, its seconds byte +0x41 (❓ their writers:
the event's record); `RaceBase::vf38` makes it -1 when the conductor has a rolling start with a
speed (K +0xF884 distance < 0 and K +0xF886 km/h != 0; `DynamicsConductorSinglePlayer::vf10`
0x3475B8).

| Type | Hold | Camera |
|---|---|---|
| -1 rolling | none: the cars run up to the line, the player's car handed over there | the race's view at once |
| 0 | the race's own seconds | as it was |
| 1 | 8 s | the opening shots (camera mode 10 -> the start motion) |
| 2 | 10 s | the opening shots |
| 3 | none | as it was |

- The hold counts org +0xBC down a frame at a time; at 0, **event 4: the start signal**. The race
  clock and the cars stand meanwhile (the engines answer the throttle).
- **The count**: a beep (sound effect 0x27) when the player's hold is 180, 120 and 60 frames
  (`fn_0039E9B0`, 0x39DA80), the start's sound (0x28) at the signal; and the digits
  (`fn_0039D390`, display kinds 0 and 1): when the hold h has h % 60 == 1, the whole seconds
  n = h / 60 (n > 0) are written `"%d"` as the middle message (color 0x80C8C8C8), shown 0.8 s
  with a 0.15 s fade for n < 4; n of 4 and more only while the display's own opacity (D +0xBC)
  is 1, with no fade. So under the opening shots only 3, 2, 1 show (the original's Grand Canyon
  start: the camera swinging round the player's car while 3, 2, 1 appear).
- At the signal the middle message `RaceStart` (the race text table's word +0x12C) in font
  `FC_48`, shown 1.0 s with a 0.15 s fade (`RaceDisplay::vf40` 0x3A1488).

### Which start a course has (`race/start_rule.*`)

The conductor's reset (`DynamicsConductor::vf28` 0x34D490 -> 0x34C7E0) reads the course's row
of `COURSE` (0x447F48): the bytes +0x10 (the surface) .. +0x14, and **the start code**: the
`UnistrDB` string the row's +0x08 names, a short text of hex digits ("1ff0" for tsukuba, route5,
seattle, motegi ...; "0ff0" for suzuka, fisco, trialmt, grandvalley ...; "0000" for grandcanyon
and the other loose and special courses), each digit into K +0xCBE6.. (0..15). **A first digit
other than 0 gives a rolling start**: the conductor takes the distance, speed and spacing from the course's
drivers' file (`crs/<course>.ad`): the course object's +0xA0 (0x466B90, the file's slot 2, its first
driver set; RaceBase::vf38 → 0x395078 keeps the file's slots 1 to 5 at the course object's +0x9C ..
+0xAC), bytes +0x23 (x -10 m), +0x24 (km/h), +0x25 (m); without that set the program's table at 0x623DC0
(a row for the tarmac courses, one for the loose, all zeros). A zero takes the default **-400 m, 80
km/h, 20 m** (vf28 0x34D490); the event's own per-car start values (CFG) or vf6 turn the speed to 0. ✔ On
the disc (`gt4tool race <disc> starts all`): most courses the defaults; Autumn Ring Mini (r_mini) 50
km/h with 15 m (the original's Sunday Cup field at 31 mph), Laguna Seca, Hong Kong, Monte Carlo, the
Nürburgring 60 km/h, Motegi 70, Le Mans 750 m, Route 5 500 m, Capri 180 m, Kashiwa and 20R60R 10 m. ❓ The other digits are not read here; ❓ who reads the course object's block.

The race's menus here take the course's start: rolling (every car at the course's speed in single
file, the last place its distance before the line and its spacing between places:
`sim::Entry::start_speed` and `start_distance` by `sim::rules::rolling_start_distance`) or ❓ a standing start of 10 s with the
opening shots (the race information's own type byte is not traced to its source; the original's
Grand Canyon start fits it). In a rolling start the computer drivers drive the player's car too
until it takes the line. The conductor holds the race clock while its flag K +0xCBE4 is set
(`DynamicsConductor::vf15` 0x357E80 adds a step to K +0xF898 only when it is clear);
`DynamicsConductorSinglePlayer::vf27` (0x3477C8) sets it for a rolling start with a spacing. The car set-up
(0x3452F0) gives a rolling car the driver kind 1 (F +0x467, the car's +0x56B) and its driver the line 6 (the
start's). **The pace** (0x3F7880, `sim::rules::start_pace`): a car of driver kind 1 that has not yet taken the
line (F +0x4AC, its crossings, 0) runs capped at the start's speed (CFG +0x6E, K +0xF886; else per place) while
it is more than 3 s at that speed short of the line (the lap length less F +0x4A4 over 3 x the speed / 3.6);
from there on it runs free. **The crossing** (the lap routine 0x358608): a car's first crossing of the line
calls the conductor's virtual 20 instead of the lap's virtual 21; the single-player conductor's (0x347928) hands
a car of driver kind 1 back to its driver (0x345228: the kind cleared, the start's driver line dropped; 0x3F37B8
clears the car's +0x694), sends the start signal (event 4) when the car is the player's (index 0), and clears the
clock flag whichever car it is: **the clock starts when the first car takes the line**; a car's first lap is
timed from the clock's start (F +0x614 is left at 0; the lap virtual 21 sets it from the second crossing on).
**The opening shots**: the camera manager's state (0x3FD620, from its routines 0x374E90, 0x3751F0, 0x3754F0,
0x376AF0) is 4, the course's TV shots, while its car's pace answers 1, so the shots end when the followed car
is 3 s at the start's speed short of the line. ✔ Against the original's Tsukuba start (the field at 50 mph,
the clock starting during the last shot, the player's view about 1.5 s later, the line 4.2 s after the clock's
start at 56 mph): here (the Ford Ka '01, 6 cars) the view at 1.62 s, the line at 4.47 s and 55 mph.
(`RaceFlow`'s clock starts at the step after the first crossing: ❓ the order of the clock's step and the
crossings within a step is not read.) **The held starts** (missions): K +0xCBFC + 4 i, car i's start count,
is set by vf28 to 60 x the event's seconds for the place (CFG +0x60 + 2 i, K +0xF878; 0 in a rolling start);
the conductor's step (0x34DB30) holds every car while the start's count runs (0x34C210: F +0x4BC = 1, the
start's hold), then counts each car's down a step at a time, the car held (F +0x4BC = 1) until it reaches 0,
when the car gets its start signal (event 4 with F +0x440, its index). Here `sim::ai::Field` holds such a car
on its brake for the same seconds (the session has no per-car start hold yet).

## The grid

`RaceSinglePlayerInformation::vf14` (0x3D5C10): **the player takes the last place**; the other
entries take the places before it in entry order. Each car stands on the course's start place
of its number (`DynamicsConductor::vf28` 0x34D490: position and heading from the road's grid).

## Laps, times, order, the finish

- Per car and tick (`fn_00358608`): a forward wrap past the line adds a crossing (car +0x5B0: 0
  behind the line at the start, 1 past it, +1 a lap); the first crossing is the start line, every
  later one completes a lap. A backward wrap takes one back. The crossing's time = the race clock
  (K +0xF898, **3000 units a second, 50 a tick**) + trunc(the tick's share before the line x 50);
  milliseconds = units / 3. A lap's time runs from the last completed lap's crossing; the first
  lap from the start signal (0).
- **The finish** (`DynamicsConductor::vf21` 0x357B98), when a car completes a lap: it finishes
  if the leader (rank slot 0) has finished, or the race has laps and the car has driven them (or
  the time limit is over and it leads). Event 5 for the car; the player's finish shows the
  middle message `RaceFinish` (+0x130) for 3.7 s with a 0.3 s fade (`RaceDisplay::vf36`
  0x3A0228, vf41 0x3A1568) and the finish view (camera value 11, ❓ what it shows).
- Order: ❓ the comparison itself; taken here as crossings first, a car still running ahead of
  a finished one on equal crossings, finished cars by their finish, running ones by distance.
- The cars race on after the player's finish; each finishes at its next crossing.

## The results

`ResultArcade` (0x3DEEA0..; base `RaceResultBase`): a row per place, filled as each car finishes
(`ResultArcade::vf6` 0x3DFCC8). The page (`menu/common/result.strb`, a converted Flash movie)
goes up at the player's finish (vf7 0x3DFE20); its callbacks `CB_STABLE`, `CB_PLAYERRANK`,
`CB_GETTIME`, `CB_GETNAME`, `CB_ISVISIBLE`, `CB_ENABLEEXIT`, `CB_RACEEXIT`, `PLAY_SOUND` name what
it shows; times are written `%d'%02d.%03d` (`fn_003E0198`). Leaving it (`CB_RACEEXIT`) ends the
run at once. The movie and its callbacks: [strobe.md](strobe.md). The pause menu is a movie too
(`menu/common/pause.strb`, same file).

## The single race's sequence (`fn_00111498`)

1. The race's menus first (`fn_001119A0`): the race in mode 4 (the simulation held: the cars
   stand on the grid, only the camera runs, in mode 5: the scenic shots), the project
   `quick-arcade` run to its end, then the race reset.
2. The command the scripts chose (MQuickWork `selectedCommand`):
   - START: the quick tune applied, a driven run (mode 0); then a fade, and **the replay at once
     when the player's crossings are 2 or more**; `canReplay` = 1; the menus again.
   - SPECTATOR: the same in mode 1, no replay after; `canReplay` = 2.
   - REPLAY: mode 2 after a driven run, else 3. OPTION: the project `option`, then the menus.
   - SAVE_REPLAY: the project `labomode` (❓ not done here: the menus again).
   - EXIT and any other: the sequence ends; the menus go on with `last_sequence` = the race's and
     `next_sequence` = menu, at the project the scripts named (`next_menu_project`).

**NEXT_RACE** (command 18) has a case only in the championship's sequence (0x118EF0, its command
table at 0x68D910) and the license tests' two sequences (0x114E38 for tests 5, 10 and 15 of B, A,
IB and IA, 0x11CB38 for the others; the choice in 0x10FBD8 by 0x38A3D0's class: B 4, A 5, IB 6, IA
7, S 8, coffee break 9):
- A championship (0x119428): when 0x3EF330 says the race run was the series' last, the series is
  closed (0x119BD8) and the sequence ends; else the next race (0x3EF088), the race rebuilt on its
  course with the series' entrants (0x3EE9F8), the scene behind the menus (mode 4), MQuickWork
  filled again (0x119918) with `canReplay` 0 and `IsSessionFinished` 0, and the menus again.
- A license test: the sequence ends returning 1, and 0x10FCD0 names the next test from the code
  (`menu::next_license_test`): test 8 → the license's coffee break test (`l0c000k`, k = class - 3),
  a coffee break test k → `l0b0009`, `l0a0009`, `lib0009`, `lia0009`, `l0s0009`, else the next
  number up to 16; the game's race code takes it (0x43A220) and the sequence runs again.
Here the Drive screen rebuilds the race on the next race's course (a test with its own car through
the menus' request completion) and opens its menus.

### The license tests' rules

Code: `race/license_rules.*` (the rules and the judge), `screen_race.cpp` `judge_license`. The race copies
the event's `RACE` row into its configuration ("CFG", K +0xF818; 0x35E108): among others CFG +0x01 the
laps (+0x43), **+0x02 the test's kind (+0x44)**, +0x38 the row's u16 +0x18, +0x40 the other cars
(+0x3E less one), +0x41 (+0x86), **+0x44 a time limit in seconds (+0x87)**, +0x46 **the cones to knock
down (+0x75)**, **+0x48 the car-hit limit (+0x8B)**, **+0x49 the wall-hit limit (+0x8A)**, +0x4A (+0x88),
+0x00 (+0x85), +0x4E (+0x42). `gt4tool race <disc> license-rules [prefix]` lists them. On the disc (US):
B-1, B-2, B-6, B-7 kind 1 (a stop in the box) with a wall limit of 0; tests 5, 10 and 15 of each license
kind 6 with one other car (the event's pool: the test's car and an R34 GT-R pace car; the drivers slow
the cars ahead by 50 % from 10 to 50 m); the other tests kind 2, Super License and some IA tests kind 3;
the coffee breaks kind 1 or 3 with 100 or 50 cones; the missions kind 2 or 3 with 1 to 5 other cars and
CFG +0x4A 5. Every test: wall limit 8 (B-1/2/6/7: 0), car limit 40. Races: kind 0.

**The start of a test on a road section** (B-3: CFG +0x21 = 2, vf26 0x34CA20): a plain grid start (start kind 0,
so the driving-line rule 0x34C5E0 for kinds 1 .. 5 does not apply): the car stands on the place 0x3952C0 reads for
the section, from the road's section record (+0x9C + 0x20 x section, +0x18) instead of the road's grid (+0x5C):
Deep Forest's section 2 gives (-8.891, 7.280, -256.900) heading -1.563 rad in the course frame, and the car then
stands at 3505.52 m along the course (checked against the program's own set-up run in the interpreter, `floats`).
Its goal is the section's, reached at 0x34C470 when it lies 1 m .. the lap less 1 m along. Here `RoadSection::grid`,
`WorldEntry::place` and `sim::Entry::place` (`screen_race.cpp` `set_license_rules`); the stand-in of earlier rounds
(the section's start on the driving line) was wrong. B-3's demonstration recording, played through it, takes the corner
and returns to the test's page (the car's whole path bit-identical to the program's from this pose, `floats`).

**The judge** (`DynamicsConductorLicense::vf15` → 0x3F8D70, each step for the player's car while its run
has not ended, F +0x4B2 clear) reads the rule bits F +0x4B3 and fails the test with kind 3 for bit 2, 5 for
bit 4, 4 for bit 1 (0x3F8980: event 10, the run's time none). Then by the test's kind: 1, the stop in the
box (0x3F91F0: all four wheels on triangles flagged bit 2 and the squared speed under 0.2778, timed that
share of a step into it, → 0x3F8AB0); 6, the car's place byte (car +0x5B3, 1 the leader) at 1 fails with
kind 6 ❓ (read, not seen: a lap behind the pace car that may not be passed); when the test counts cones
(CFG +0x46 > 0) and the count K +0x1003C reaches it, the run ends timed (0x3F8AB0); with a time limit the
count K +0x1003E runs a frame a step while the car's byte F +0x4AF is 1 and starts again otherwise
(0x3FA248 / 0x3FA238), the seconds left (0x3FA2B0, -1 without a count; the license display shows them)
reaching 0 end the run timed. Kinds 3 and 6 end at the lap's line when the laps are done (vf21 → 0x3F8AB0),
2 at vf23 (❓ its moment: 0x3F8AB0 at once for the car it names).

**The rule bits**:
- **The wheels** (0x369888 each step → `vf49` 0x35FA98, while F +0x4B3 is clear): it counts the wheels on
  the ground (wheel record +0x30 clear) whose road flags bit 0 is set (the pit lane), and those on a
  material the program's table 0x622430 marks (0x3F4EA0, the material +0x31). When every wheel of the
  layout (0x359538 +0x01) is on the pit lane, the step's value is vf46 (the license's: 1); when every wheel
  is off the course, vf47 (the license's: 1 unless the course's kind, COURSE +0x10 → K +0xCBDD via 0x34C968,
  is 2 or 3: 0x34C998). A value of 1 counts F +0x4B6 up, 2 sets it to -1, 0 clears it; any count sets
  F +0x4B3 to 4 (the pit lane) or 2 (off the course).
- **Hits** (0x350530(car, kind, other, value) → the conductor's virtual +0x190 → the license's vf50 →
  0x35FE80, while the car is not finished, rolling or out): kind 0 (the walls, 0x45E288 / 0x45E768: the
  contact's impulse |j|) as |j| over the car's mass P +0x48 against CFG +0x49; kind 1 (a car, 0x45F340:
  |j| / 15000) x 100 against CFG +0x48 when 0x35FBC8(other) is under 3; kind 2 (a cone or board, 0x3E3C48)
  fails when the test counts no cones, else counts one (0x35DF28, up to the count). A limit (0x35FC10):
  0 any hit, 255 none, else the truncated value at or over it. A hit sets F +0x4B3 bit 1.
- **The cones' reports** (0x3E3C48, each step per gadget, skipped once the body's +0x45 is set): each car's
  five planes (0x621D28, 0x50 bytes a car) against the body's middle with its kind's radius, then its hull
  points; a hull point inside all five is a contact: the impulse (0x3E3ED0) added to the body's +0x54, the
  body woken (0x3E29C8), kind 2 reported to the car (other 9), and a kind with flags bit 0 (GDGT kind +0x02)
  sets the body's +0x45: it meets no car again.

Here the hits come from the simulation's contact (`CarState::contact`: a wall's |j| / 100000, a car's |j| /
15000) and the cone bodies' contacts per car (`GadgetBodies::contacts`, with the kind's flag latch); the
mass is the car's parameter block's P +0x48 as 0x35FE80 reads it; the time limit is not counted (no test has one; F
+0x4AF's meaning ❓). Seen here: B-1 run at full throttle past the box ends in FAIL on the row of cones at
the box's far end (`lic3`), as the original's; braking into the box passes (9.849).

The other sequences (time trial, events, championships, licenses, missions, machine test, free
run, practice) and their projects are listed in `race_natives.cpp` (`quick_project`); only the
single race's commands are run here.

## MQuickWork (`race_natives.*`)

One object of 0x4F8 bytes (`fn_00127E60` its defaults) current at 0x6187A8; every script object
`main::menu::MQuickWork()` reads and writes it. Its integer attributes (canReplay, canLoadGhost,
canSaveGhost, numberOfEntries, cursorPosition, IsSessionFinished, SessionNumber, IsQuickTuned,
IsDryCourse, IsASpec, IsBSpec, BestTime, BestMaxSpeed, playerGridNumber, prize, GrandPrize,
SeriesRank, CanLogger, DisableLogger, IsFinalSession, PsValue, TorqueValue, driveTrainType,
carCategory, carYear, CourseLength, Laps and the QuickTune* values), `selectedCommand` (a
command's name, stored as its number: COMMAND_NOTHING 0, START, EXIT, REPLAY, DEMONSTRATION,
RECORDS, LOAD_GAME, SAVE_GAME, LOAD_REPLAY, SAVE_REPLAY, LOAD_GHOST, SAVE_GHOST, CANCEL,
CONTINUE, SPECTATOR, SETTINGS, QUICK_TUNE, RETURN_MENU, NEXT_RACE, RETIRE_CHAMPIONSHIP,
FREE_QUALIFY, LOGGER, OPTION 22), `raceLabel`, `courseLabel`, `goldTime` / `silverTime` /
`bronzeTime` (text; no time 0x157529FF is the font's dashes), and per grid row
`getGridCarName`, `getColorChipInfo` (the paint's chip words), `getPower`, `getWeight`,
`getTireType`, `getGridTime`, and `getCatPs` / `getCatTq` of the player's car.

The single race fills it (`fn_00111A40`, `fn_00111B50`): each car's name and paint by its grid
place, `numberOfEntries`, `playerGridNumber` (the player's place), `courseLabel`, `IsBSpec`,
`IsDryCourse`, the race's tire for every row, the quick tune from the player's settings
(transmission, assists), `canReplay` 0. ❓ Here IsBSpec and IsDryCourse answer 1 (the course's
condition and record byte are not read), and only the first chip word (the paint's colour) is
filled.

## Saved replays

The program's Save Replay runs the project `labomode` with `next_menu_arg` =
`MODE_SAVE_FULL_REPLAY` and keeps the drivers' controls on the memory card
(`Serialize::ReplayData`, `GT4MC::FileGT4Replay*`); a replay is the race run again from them. The
rebuild keeps the cars' states instead, step by step, in a file of its own under the data folder's
`replays/` (`<course>-<date>-<time>.gt4rrep`; `race/replay_file.*`): `"GT4RRPL1"`, u32 the body's
size, u32 its deflated size, then the zlib-deflated body: the course, the sequence and the race's
labels, the laps, the cars (label, paint), the steps (every car's state). The Drive screen plays
one with `GT4R_RACE="replay_file=<path>"` through the course's replay cameras. ❓ The menus'
Replay Theater does not list these files yet.

## What runs where (the race screen)

Every piece of the race as the player sees it, by whose code it runs: **GT4** (ported from the program,
its address), **judged** (a stand-in fitted to the reference's captures), or **engine** (the shared
engine's Gran Turismo 3 code). The player notices them roughly in this order.

| Piece | Where | Runs | The GT4 routine that should replace a stand-in |
|---|---|---|---|
| The rolling start's run-up: the field's pace until the line | `sim::Session` (`sim-step`), `RaceWorld::release_start` | GT4 (0x3452F0 at set-up: F +0x467 = 1, driver kind 1; the aids' start pace 0x3F7880 / 0x3F76E8; each car released at its crossing, vf20 0x347928 → 0x345228 / 0x3F37B8). Tsukuba: the player's crossing 4.37 s on the clock (the original's 4.2); the stand-in simulation keeps the judged cap | |
| The rolling start's distance, speed, spacing | `race/start_rule.*` `apply_drivers_file` | GT4 (vf28 0x34D490, 0x466B90) | |
| The race clock's start in a rolling start, the opening shots' end | `race_flow.*` `clock_running`, `screen_race.cpp` `opening_tv` | GT4 (vf20 0x347928: the first car's crossing; the camera state 0x3FD620 by the pace 0x3F7880) | |
| The held starts (missions' places with a start delay) | `sim/ai/drivers.cpp` `Field::drive` | the seconds GT4 (CFG +0x60, the count K +0xCBFC, 0x34DB30); the hold **judged** (the brake on, the engine blipped) | the start's hold F +0x4BC per car (`sim-step`) |
| A standing start's hold, count, START | `race_flow.*`; the hold in `sim::Session` (`hold_once`: the countdown's flag a step at a time, the race information's +0xBC, 0x34C210 / the car's start hold) | GT4 (0x3C0208, 0x39D390, 0x39E9B0); the hold's length for a license test (8 s) from its demonstration, for other races 10 s ❓ | the race information's start type byte +0x40 (its writer) |
| The opening shots (start motion, replay start) | `start_camera.*` | GT4 (CameraPathanim 0x37D338, 0x37D620) | |
| The replay cameras (TV shots), their choice by distance | `replay_script.*` | GT4 (0x37C138, 0x3FD980 → 0x3DDFF0); the dissolve's drawing judged | |
| The scenic shots behind the menus | `replay_script.*` `ScenicCamera` | GT4 (mode 5, 0x375980) | |
| The driving views | `drive_camera.*` | the car's camera places (CAR4); the on-board lens as the original shows it (61.2 degrees: its K and its chase pictures, camera.md); their springs ❓ | how the program's 90 degrees becomes it (the screen's scale) |
| Laps, crossings, times, finish | `race_flow.*` | GT4 (0x358608, vf21 0x357B98); the race order's comparison judged | the order routine (❓ not found) |
| License tests' rules | `license_rules.*`, `judge_license` | GT4 (0x3F8D70, 0x369888 → vf49, 0x35FE80); the hits' strengths from the simulation's contact (its sum per step, not the program's per hit), the mass P +0x48 | the hit records 0x350530 (`sim-step`) |
| Cones and boards knocked about | `gadget_bodies.*` | GT4 (0x3E2B28 with 0x3E3748 / 0x3E3840, 0x3E44E8 / 0x3E4708, 0x3E3C48 / 0x3E3ED0 / 0x3E43B8, 0x3E5108, set-up 0x3E2710; course.md "Cones and boards: the bodies") in host floats; the road query and the car boxes' extents judged | the road query 0x3E5A30; the car record's box extents (+0x48C .. +0x494) |
| The demonstrations | `screen_race.cpp` `begin_demo`, `sim/ai/demo_driver.*` | GT4 (the recordings, `ai-drivers`; a section test's start at the section's grid place); the card's tire word is the race's, not the car's ❓ | |
| The pad's controls | `screen_race.cpp` (`gt3r::race::ControlMapper`) | **engine** (GT3's mapping) | the input phase 0x349FD0 (`sim-step`) |
| Looking back | `gt3r::race::look_back_held` | **engine** | ❓ not read |
| The race display: places, numbers, meters | `race_display.*`, `race_hud.*` | GT4 tables (RaceDisplay); the speedometer's face by the car's top speed estimate (P +0xFD8); how some parts move their pictures judged | the meters' drawing routines |
| The driver's view's fuel arc and pedal bars | `race_hud.cpp` `draw_bars` | GT4 (0x3A8968: places, steps, colours); the fuel gauge's flag +0x1B judged (always) | |
| The suggested gear | `race_hud.cpp` `draw_suggested_gear`, `RaceWorld::suggested_gear` | GT4 (the driver's suggestion 0x469E58, `ai-drivers`; the gear 0x36BF68, the session's); its drawing judged | the display's vf5 0x3A7108 |
| The license display's trophy times | `race_hud.cpp` | places judged | RaceLicenseDisplay::vf9 |
| B-spec's display and commands | `race_hud.cpp`, `screen_race.cpp` | display mode 1 GT4; the meters the followed car's; the pace bar, speed plate and help strip places judged; Select Car's order ❓ | the B-spec display's routines |
| B-spec's driver record and skill | `race_world.cpp` | GT4 (the book's record 0x468710, `gt-mode-2`; the skill 0x468C50 and the experience, `ai-drivers`); the skill's course flag (cond + 0xCBE7) taken as the course's marks being known ❓ | the flag's writer |
| The replay's card, logo, caption timings | `screen_race.cpp` `fill` | **judged** (3 s, 4.5–14 s) | their caller's times |
| The results and pause pages | `strobe*`, `screen_race.cpp` | GT4 movies and callbacks; a passed license test's page also the trophy (0x3E18D0 / 0x3E1A80, race/license_trophy.*), drawn once a frame (no averaged passes), the program's camera for its live picture judged (the course's TV cameras), the trophy's kind by the grade judged | ResultLicense's result kind (+0x88) and its camera |
| The tire diagram of the race display | `race_hud.cpp` `draw_tires`, `race_display.cpp` `tire_color` | GT4 (0x3A5FD8's picture, boxes, 0x3677F8 colors from the wheel's wear level +0xB1; drawn when the race wears tires) | the panel's flag (+0x18, 0x3451D8) and the other drivetrain kinds' mapping (0x359538) not read |
| Sun, glare, lamp glows, billboards, smoke, wet road, the scenery's fade | `race_world.cpp`, `course_scene.cpp` | **engine** routines on GT4's layouts (`gt3r::race::SunGlare`, `SunDisc`, `LampGlows`, `Billboards`, `CarLampGlows`, `TyreSmoke`, `WetRoad`, `SceneryFade`) | GT4's own effect routines (not read) |
| The car icons on the map | `race_world.cpp` | angles and size judged (CarIconMaker 0x393B70) | |
| The section times | `race/sections.*`, `screen_race.cpp` `step_splits` | the points GT4 (the road's list); the gap's partner judged | event 15 (0x359C10) and its display |
| B-spec's Race Monitor | `screen_race.cpp` `fill_monitor`, `fill_gap_strip` | the movie GT4; the rows (names fitted with "..." 0x3DA668, the followed car lit 0x3DAF60), the gap strip GT4 (0x3DBF90: places 0x3DBE98, gaps 0x3DBDF0, their text 0x3DBEE8, the unit); the gap column judged; the map's letters drawn by us; the strip's car pictures, its pace-up marks (0x3FA508) and the pit panel not filled | 0x3D8DB8, 0x3DB7B8 (pit panel), 0x3FA508 |
| B-spec's pit command | `RaceWorld::pit`, `sim/ai/pit_stops.*`; the box (P lit, "Pit Stop") | GT4 (`ai-drivers`' port of 0x35B060); the command box (P and words `key_pitin`) | the PIT STOP Menu (pitmenu.strb), SPEED LIMIT |
| The cars' levels of detail | `car_visual.cpp` `CarVisual::level` | GT4 (0x07's size and centre, the car's rule 0x4539A8, T = 200); K, the view's factor (0x456918), from the chase view's measured 721.2 by the lens's angle (judged: the lens) | the race's lens (camera.md) |
| The far level's wheels | `car_visual.cpp` | GT4 (the body's own shapes with the wheel set's last texture set, callback 46) | |
| The brake discs' glow | `car_visual.cpp`, `race_world.cpp` | GT4 (heat² x `Env0` +0x4C / +0x38, the sponsor's vf24 0x3D75F0; heat from the simulation); the blend as the renderer's added pass with its alpha 🔎 | |
| Fitted wings | `car_visual.cpp`, `race_world.cpp` (the entry's WING part) | GT4 (0x4533A8's place and scale, callback 47 = 1); "spoiler taken off" (2) not reached: no part names it ❓ | |
| Moving models | `race/animated_models.cpp` | GT4 (the models' scripts and their programs' register moves 0x44 .. 0x4B: seoul's and lasvegas's turning signs; programs with two switches of their own registers, Ice Arena's people: each pair of cases is a key) | the animated skies (two morph weights per model), which models the race draws at once |
| The driver figure | `race/driver_figure.*`, `car_visual.cpp` (`GT4R_RACE` `driver=1`, off by default) | GT4 (the set: the car's own +0x34 or a character file by the cockpit kind 0x3B5438; its parts at the skeleton's nodes 0x3B1070; PELVIS at the seat, parts block +0x34, as a saved state shows); the nodes the **stored pose** (judged: the posture not ported) | the posture 0x3B2708 and its solver 0x416FD8 (model.md "The driver in the car") |
| Not built | | animated skies, cone shadows | |
