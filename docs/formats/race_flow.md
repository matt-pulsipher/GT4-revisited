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
other than 0 gives a rolling start**: the conductor takes the distance, speed and a third value
from the course object's block for its surface (K +0xCBD8 -> +0xA0 + 4 x surface), else the
program's table at 0x623DC0 (a row for tarmac, one for dirt and snow), bytes +0x23 (x -10 m),
+0x24 (km/h), +0x25; the zeros there give the defaults **-400 m, 80 km/h** (the original's 50
mph field) and 20. ❓ The other digits are not read here; ❓ who reads the course object's block.

The race's menus here take the course's start: rolling (every car at 80 km/h in single file, the
last place 400 m before the line and 20 m between places: `sim::Entry::start_speed` and
`start_distance` by `sim::rules::rolling_start_distance`) or ❓ a standing start of 10 s with the
opening shots (the race information's own type byte is not traced to its source; the original's
Grand Canyon start fits it). In a rolling start the computer drivers drive the player's car too
until it takes the line. The conductor holds the race clock while its flag K +0xCBE4 is set
(`DynamicsConductor::vf15` 0x357E80 adds a step to K +0xF898 only when it is clear);
`DynamicsConductorSinglePlayer::vf27` (0x3477C8) sets it for a rolling start with a spacing, and
vf20 (0x347928) clears it with the start signal (event 4), handing a car whose state byte F +0x467
is 1 back to its driver (0x345228: the byte cleared, the driver's line set). The car set-up
(0x3452F0) gives a rolling car state 1 and the driver line 6 (the start's). ❓ What holds the
field's speed through the run-up is not read: here every car is held at the start's speed until
the release, when the player's car is 80 m from the line (the race clock starts there), and the
course's TV cameras show the field until 50 m (judged from the original's Tsukuba start: the field
at 50 mph through some 12 s of shots, the clock starting in the last one, the player's view about
1.5 s later, the line 4.2 s after the clock's start at 56 mph; here 58 mph at 4.5 s with the Ford
Ka '01).

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

**The license tests' failing rules** (🔎 found, not built: the test is graded by its time alone
here). `DynamicsConductorLicense::vf15` calls the judge 0x3F8D70 each step for the player's car:
with F +0x4B2 clear, F +0x4B3 bit 2 fails the test with kind 3, bit 4 kind 5, bit 1 kind 4
(0x3F8980: the run's end 0x343D78 with the kind, event 10). F +0x4B3 is written by
`DynamicsConductor::vf49` (0x35FA98: a count at F +0x4B6 of steps on a place 0x359538 names,
then 2 or 4) and by the license's vf50 → 0x35FE80 (a hit, its strength against the race's bytes
CFG +0x48 / +0x49 by the hit's kind: bit 1). The test's kind is CFG +0x02 (K +0xF81A): 1 a stop
in an area (0x3F91F0), 3 and 6 end at the lap's line (vf21 → 0x3F8AB0), 2 at vf23. The display's
words: `ExamFail` (word 6, "Lesson Failed..."), `ExamTimeOver` (7, "Time up"),
`Disqualification` (0x45, "Disqualified!"), the words' table at 0x621540 ({index, name} pairs).
❓ What 0x359538's places are, the hit kinds, the stop area.

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
