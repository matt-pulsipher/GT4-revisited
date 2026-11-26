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

The start type is the race information's byte +0x40, its seconds byte +0x41: the event's `RACE` row's
bytes +0x40 and +0x41 (every sequence hands the race's label to 0x3BB440, and 0x4470E8 copies the row into the
information; the start's reset 0x3C0190 reads both). The hold by type (0x3C0208, its table at 0x6A29C0): 1 sets the
seconds to 8, 2 to 10, 3 and -1 to 0; 0 (and a type past 3) keeps the row's seconds; so only type 0 reads them. ✔ On the
disc: type 2 on 585 events (the races, `sample_entry_1` among them), 1 on 60 (the license tests that stand), 3
on 60 (the tests and missions that start moving), 0 with 7 s on 4 (the machine tests). The base conductor
(vf10 0x34C1B0) keeps the row's type unless its configuration has a speed for car 0 (CFG +0x4E); `RaceBase::vf38` makes it -1 when the conductor has a rolling start with a
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
`sim::Entry::start_speed` and `start_distance` by `sim::rules::rolling_start_distance`) or the row's type 2, a standing start of 10 s with the
opening shots (the original's Grand Canyon start). In a rolling start the computer drivers drive the player's car too
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
is 3 s at the start's speed short of the line; a car already past the line (a section's start) has no shots. ✔ Mission 21's end
between 3.68 and 4.18 s in the original's picture, 3.85 s here; mission 1 has none. ✔ Against the original's Tsukuba start (the field at 50 mph,
the clock starting during the last shot, the player's view about 1.5 s later, the line 4.2 s after the clock's
start at 56 mph): here (the Ford Ka '01, 6 cars) the view at 1.62 s, the line at 4.47 s and 55 mph.
**The held starts** (missions): K +0xCBFC + 4 i, car i's start count,
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

- Per car and tick (`fn_00358608`): a forward wrap past the line adds a crossing (car +0x5B0, +1 a
  lap). **Where it starts** (the set-up 0x363B10, 0x364300 .. 0x36444C): the car's sector (F +0x4AE) is the
  first of the road's split points past its distance (the conductor's virtual +0x150 the sectors, one more
  than the points; +0x158 a point), else the last; the crossings are **1 unless the car is in the last
  sector**; there still 1 short of the section's goal (0x34C470, virtual +0x138) or in the lap's first half,
  else 0 (behind the line: the first crossing is then the start line). A place of more than a lap keeps its own
  count (register s5 there, ❓ its making). Fuji's and Suzuka's standing grids are 166 to 258 m past the line: 1.
  Here `race::start_crossings` (a section test has no split points; Prologue's 0x3C9790 counts by its road's split points alone).
  Every later crossing completes a lap. A backward wrap takes one back. The crossing's time = the race clock
  (K +0xF898, **3000 units a second, 50 a tick**) + trunc(the tick's share before the line x 50);
  milliseconds = units / 3. A lap's time runs from the last completed lap's crossing; the first
  lap from the start signal (0).
- **The finish** (`DynamicsConductor::vf21` 0x357B98), when a car completes a lap: it finishes
  if the leader (rank slot 0, K +0xF8A4 by 0x3658D8) has finished, or the race has laps and the car has driven them, or
  the time limit is over and it leads. **The time limit** (0x34C140): the conductor's u16 +0xF850 (the rule block's +0x38,
  copied by 0x35E108 from the `RACE` row's +0x18; cleared by 0x34D418 when the race's +0xEC or +0xDC is set) x 60,000 ms;
  0x34C158 asks whether the crossing's moment (ms, the value then kept in F +0x614) is at or past it (none when 0). So in a
  timed race the first car to cross the line once the time is over finishes there if it leads, and every other car at its
  next crossing. ✔ On the US disc eight events have a limit, all with 0 laps (`en_nur4h_0001` and `en_roadster4h_0001` 240
  minutes, `en_infinion_alms_0001` 165, `en_motegi8h_0001` 480, `en_tsukuba9h_0001` 540, `en_nur24h_0001`,
  `en_sarthe24h_0001`, `en_sarthe_old24h_0001` 1440): the time alone ends them (`test_endurance.cpp`). The pit stops' last
  lap reads the same limit (0x343C20). Here: `FlowSetup::limit_ms` (`race::row_time_limit_ms`), the race's laps 0 then
  (`screen_race.cpp` `start`); `GT4R_RACE="...,limit=<seconds>"` sets a limit for checks. Event 5 for the car; the player's finish shows the
  middle message `RaceFinish` (+0x130) for 3.7 s with a 0.3 s fade (`RaceDisplay::vf36`
  0x3A0228, vf41 0x3A1568) and the finish view (camera value 11, ❓ what it shows).
- Order (0x365BC0 with its comparison 0x3659E8, `RaceFlow::sort_order`): more crossings first; two finished cars by their
  finish, two running ones by their distance; of one finished and one running on equal crossings the running one, but the
  finished one in a test that ends at its goal and on a course without a lap.
- The cars race on after the player's finish; each finishes at its next crossing.

## The results

`ResultArcade` (0x3DEEA0..; base `RaceResultBase`): a row per place, filled as each car finishes
(`ResultArcade::vf6` 0x3DFCC8). A row's time by the result's kind +0x50 (ResultArcade::vf9 0x3DFEB0, its table 0x6A3790): kind 0
(0x3E0030) the winner's whole time, the others on its laps their gap (`+M'SS.mmm`), the others `+N` and the word for laps short;
kind 1, a race with a time limit (set by vf2 0x3DF0A4 when 0x34C140 is not 0; 0x3E00B0), the winner's laps (`" %d"` and
`WordOfLap` / `WordOfLaps`, 0x3E0348), the others on its laps their gap, the rest their own laps the same way. A car's laps
(0x343B50): its crossings F +0x4AC less one once it has finished. The page (`menu/common/result.strb`, a converted Flash movie)
goes up at the player's finish: the race's tick (`RaceBasic::vf74` 0x38AB98) runs the result (race +0xE41C, set by
0x38B8D8; ResultArcade at RaceArcade +0x22738) through its vf4: `RaceResultBase::vf4` (0x3E05A0) fills a row for each event 5
of the step (vf6) and, when the car is the player's (vf8) and the page is not up, calls vf7 (0x3DFE20: +0 = 1, +0x4FC = 1,
the movie to its frame 0 by 0x4751D0). (✔ the championship's saved state at FINISH, `26-endurance/tcgp_race_30`: the page is up,
+0 = 1, its movie made.) Nothing shows at first: the movie's root waits at frame 0 for its sprite 1 (40 frames at the movie's
12 a second; Prologue's `menu/result.strb` 60), which plays it on, so the title and the rows come about 3.4 s after the line;
the race display goes on under the page all the while (FINISH, the A-spec points, the panels' fade: race_display.md "Finish").
(Race +0xE420 is the race display, `RaceBasicWithRaceDisplay`'s, not the result: `RaceBasic::vf19` 0x38AB40 tells the display
the race's mode.) Its callbacks `CB_STABLE`, `CB_PLAYERRANK`,
`CB_GETTIME`, `CB_GETNAME`, `CB_ISVISIBLE`, `CB_ENABLEEXIT`, `CB_RACEEXIT`, `PLAY_SOUND` name what
it shows; times are written `%d'%02d.%03d` (`fn_003E0198`). Leaving it (`CB_RACEEXIT`) ends the
run at once. The movie and its callbacks: [strobe.md](strobe.md). The pause menu is a movie too
(`menu/common/pause.strb`, same file).

## The single race's sequence (`fn_00111498`)

1. The race's menus first (`fn_001119A0`): the race in mode 4 (the simulation held: the cars
   stand on the grid, only the camera runs, in mode 5: the scenic shots), the project
   `quick-arcade` run to its end, then the race reset.
2. The command the scripts chose (MQuickWork `selectedCommand`):
   - START: the quick tune applied (below), a driven run (mode 0); then a fade, and **the replay at once
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

**What a license test's page shows behind it.** The test's page (project `quick-license`, 0x11D1B8) runs the race in
mode 2, the replay of the last run: the replay's start shots round the car, then the course's replay cameras, with no race
display; before the first run there is no run to play and the page is black. The license's camera (RaceLicense::vf40
0x3BC350) is put in mode 5 (the scenic shots) only while the demonstration's flag (+0x24EE0, set by 0x3BC5A8 from
0x11D270's `quick` page, which runs the race in mode 4) is up. ✔ The original's B-1 page before any run (black,
05-gtmode `36_license_test_b1_page.png`) and after one (05-gtmode `license_b1_test_run_fail.mp4` from 124 s: close-ups
of the car on its start, then the shot from the stopping box up the straight; 11-license-tt `b1_page_after_pass.png`).
The course's scenic shots would not do there: Test Course 100 m's four stand 1.5 to 2.0 km from the test's start, where
that course has no ground. 🔎 Whether the replay's sound plays under the page is not read (ours is quiet); ours plays the
replay again from its start when it ends.

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

**Where a test's cars start** (the conductors' virtual 30, 0x3F6278: the license's, the pace car tests' (RaceTraining)
and the missions'; code `race/start_rule.*` `RowStart`, `car_start`, `race/race_start.*`). Two numbers per car, a distance
(virtual 35, s16 metres) and a speed (virtual 36, km/h), kept in CFG from the event's row (0x35E108): place 0 CFG +0x54 ←
row i32 +0x14 and CFG +0x4E ← row +0x42; places 1 .. 5 CFG +0x56 + 2 k ← row s16 +0x2E + 2 k (0x4477D0) and CFG +0x4F + k ←
row +0x80 + k (0x447778). The license's conductor reads place 0's for every car (0x3F6070 / 0x3F6080); the pace car tests'
and the missions' the car's own, a speed of 0 being place 0's (0x3F7090 / 0x3F70A8). Then:
- a distance below 0 with a speed: start kind 2; any other distance but 0: kind 1; none: the grid (kind 0);
- **the speed holds for every kind** (0x34D0F8 sets it whatever the distance; the set-up gives it at 0x3646A8 to 0x363868 when it is not
  0): a place of 0 m with a speed, the rivals of missions 2 .. 5 and 8, stands on the section's grid place and moves from it, its own
  driver driving from the first step (no driver kind 1, no pace); the start count of such a car is 0 (0x34D8F4);
- the distance counts from the section's start when the race runs on a road section (CFG +0x21) whose start lies 1 m .. the
  lap less 1 m along (0x34C3E0, truncated), else from the line; the course's own start code (a rolling start) is not read;
- the set-up (0x363B10) takes whole laps off it and puts the car on the course's driving line there (0x34C5E0: no more than
  the lap less 20 m before the line; on a course without a lap, COURSE +0x12 = 1, no further than 20 m short of the last
  split point), facing the line's place 10 m on, on the road's height (0x46ADE8, `sim::ai::CourseLines::start_place`), at its
  speed (0x363868); without the line it stands on the grid. A car of kind 2 is driven by its driver (F +0x467 = 1, 0x3452F0)
  until its start is counted. On the grid it stands on the section's own place (0x3952C0 reads the section record's +0x18
  when the course object has a section; the count is +0x0A: `RoadSection::grid`), else the road's;
- **the mark F +0x65B**: on a section, for a place past the line: 1 for kind 2 or a place before the section's start, 2 past
  it; 0 for a car on the grid or without a section;
- **the crossings F +0x4AC**: on a driving line 0 before the line, one more than the laps taken off past it; on the grid by
  the sector (below).
On the US disc 40 section tests and 9 Super License laps start moving (8.3 to 55.6 m/s), IB-12 stands on the driving line
(kind 1 without a speed), the rest stand on the grid. A race with a speed for car 0 has no start's hold (virtual 10, 0x3F6090).

**Where a test's clock starts.** The clock runs from the first step after the hold (the step whose count, the race
information's +0xBC, the frame takes to 0 before it: 8 s hold 479 steps). When a car with the mark 1 passes the section's
start, the lap step (0x358608) calls virtual 22; the license's (0x3F6760) clears the mark and calls its virtual 20
(0x3F64D0), as the line's first crossing does on a whole lap: with CFG +0x4E set it keeps the moment (ms) in F +0x614, lets
the car go from its rolling start (0x345228) and, for car 0, sends event 4 (the start signal); the mark is 2 after. A
mission's (0x3F6E50) puts F +0x614 back to 0. The run's time is the moment of its end less F +0x614; the display shows none
before (0x357830: no crossing yet, or the mark 1); a mission and a race show the race's clock less the base all the time (0x357800).

**The lap step** (0x358608; `RaceFlow::cross`): with the last step's distance p and this one's d (F +0x4A4), lap length L:
- p <= d, and p + L / 2 < d: backwards over the line, F +0x4B0 counted up. Else, with no such count, a crossing counted and
  the run not over: the section's start (above), and the goal (0x34C470: 1 m .. the lap less 1 m along) passed calls virtual
  23: the license's ends a test of kind 2 (0x3F67E8 → 0x3F8AB0).
- p > d, and d + L / 2 < p: forwards over the line: F +0x4B0 counted down if it is not 0, and that is all; else the crossings
  go up: the first calls virtual 20, the others virtual 21 (the license's, 0x3F6588, ends kinds 3 and 6 when the laps are done).
- A moment is the clock (50 a step) at the step's start + trunc(the share of the step x 50), over 3 in ms; the clock steps after
  (virtual 15), so a rolling start's clock takes its first step in the step the line is taken.

**The judge** (`DynamicsConductorLicense::vf15` → 0x3F8D70, each step for the player's car while its run
has not ended, F +0x4B2 clear) reads the rule bits F +0x4B3 and fails the test with kind 3 for bit 2, 5 for
bit 4, 4 for bit 1 (0x3F8980: event 10, the run's time none). Then by the test's kind: 1, the stop in the
box (0x3F91F0: all four wheels on triangles flagged bit 2 and the speed at most 0.2778 m/s, 1 km/h, timed the
speed's share of that of a step into it, → 0x3F8AB0); 6, the car's place byte (car +0x5B3, 1 the leader) at 1 fails with
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
- **A mission** (DynamicsConductorMission over the training's conductor): its virtuals 46 and 47 answer 0 (wheels off the
  course and on the pit lane count nothing) and its 50 is a race's (0x35FCC8: a hit past the row's limits gives the penalty's
  seconds, CFG +0x4A), so nothing but its end stops it; the judge's other ends are the test's. ✔ Mission 1's wall hit goes on
  under "5 sec. penalty" as `21-gtmode-deep`.
- **Lap and section times with other cars** (0x3F6588, 0x3F63A0: CFG +0x40, the cars less one, not 0 and the kind not 6): not kept
  and not shown; the lap still counts.
- **The places** (0x365BC0, the comparison 0x3659E8): more crossings first; two finished cars by the moment (F +0x614), two
  running ones by the distance; of one finished and one running on equal crossings the running one, but the finished one in a
  test of kind 2 (the goal) or on a course without a lap (K +0xCBDE = 1).
- **The cones' reports** (0x3E3C48, each step per gadget, skipped once the body's +0x45 is set): each car's
  five planes (0x621D28, 0x50 bytes a car) against the body's middle with its kind's radius, then its hull
  points; a hull point inside all five is a contact: the impulse (0x3E3ED0) added to the body's +0x54, the
  body woken (0x3E29C8), kind 2 reported to the car (other 9), and a kind with flags bit 0 (GDGT kind +0x02)
  sets the body's +0x45: it meets no car again.
  The gadgets step (0x3E5040 → 0x3E2B28) after the conductor's whole step (RacePS2Base::vf74: RaceBase::vf74, whose vf75 runs
  0x34DB30 with the license's judge in it, then 0x3E5040), so a cone met in a step is judged in the next.

Here the hits come from the simulation's contact (`CarState::contact`: a wall's |j| / 100000, a car's |j| /
15000) and the cone bodies' contacts per car (`GadgetBodies::contacts`, with the kind's flag latch); the
mass is the car's parameter block's P +0x48 as 0x35FE80 reads it; the time limit is not counted (no test has one; F
+0x4AF's meaning ❓). Seen here: B-1 run at full throttle past the box ends in FAIL on the row of cones at
the box's far end (`lic3`), as the original's; braking into the box passes (9.849).

**B-1's grades** (gold 0'10.150, silver 0'10.400, bronze 0'11.300 from its row, as the original's page shows them; a grade
is its time or less, 0x3F8AB0's loop over 0x3C0B88): the program's step in the interpreter, on the demonstration's
inputs to step 900, then full throttle and full brake from step S. The hold ends at step 480, so the clock reads
(S - 480) / 60 s when the brake goes on. Results:
- S 967 or earlier: the car stops short of the box, with no time.
- S 968 .. 973 (0.1 s): gold, 10.033 .. 10.133. The console's demonstration brakes at 969 .. 972: 10.083.
- S 974 .. 986: silver.
- S 987 to about 1020: bronze (S 1020: 11.065).
- Each step later costs 17 or 33 ms.

So the time is lowest when the car stops just inside the box's near edge. A part brake can recover a brake begun too
early: S 964 with the brake at 2700 of 4096 from step 1000 gives 10.148.

Every sequence's commands go through the one handler of the race's menus (`screen_race.cpp` `update_quick`); each
sequence's own rules (what it records, its diary day, the commands it does not do yet) are listed by sequence in
[RULES_AUDIT.md](../RULES_AUDIT.md).

## An arcade single race's options

The conductor's set-up (virtual 26, 0x34CA20, for a RaceArcadeSingle: 0x3BFD00) takes two of the options over the event's row
(`race::arcade_rules`):
- `penalty_type` (the race information's +0x04; 0 in a run alone): 0 no penalty (rule block +0x4A 0, the limits +0x48 / +0x49 255);
  1 the speed limit, 10 s (+0x4A), past a wall's 8 m/s (+0x49) or a car's 40 (+0x48); 2 the pit lane: the same limits, the mark (+0x4A
  0xFF; none on a course without pits). The mark is F +0x62E = 1 (0x35FE18, virtual 50 0x35FCC8, while it is 0), the same byte the
  pit pass (0x35B060) reads as a pad car's own visit: at the lane's entry the car is taken in (2 on the lane, no service asked), and
  out of the lane's end the byte is 0 again (0x35B824), so a new penalty can be given; leaving the lane short of its end puts it
  back to 1. A drive-through. Here the session's mark (`Session::pit_mark`) is the pit flow's `pad_state`, read before the pass and
  written after it (`PitHooks::mark`, `set_mark`). ❓ What the display shows while the mark is set is not read.
- `tire_damage` (+0x08; virtual 11, 0x354F30): 0 no wear; 1 and 2 the wear's curve (+0x2C .. +0x34): 8, 40, 12, 1, 5, 4, 0, 5, 5 and 6, 40, 12,
  1, 5, 3, 0, 4, 5.
Any race's wear runs only on a course that counts fuel (COURSE +0x14, not loose: 0x34CCD4).

## A car's finish

At its finish (virtual 21, 0x357B98, the license's 0x3F8AB0) a car is marked (F +0x4B2) and its driver takes over (0x343D78, mode 3: kind 5):
it cruises on at the race's limit or 80 km/h, the player's car too (`Field::finish_run`, `RaceWorld::finish_car`). A test ended by the player
sends the computer's cars off the same way (0x3F88E8). A car on the pit lane (kinds 2 .. 4) keeps its driver.

**A license test's end** (0x3F8AB0 for a test timed or ended at its goal, 0x3F8980 for one failed; `race::license_end_mode`,
`license_fail_mode`, `screen_race.cpp` `end_test`): the player's car is given to its driver in the mode 0x3F8818 picks (1 it coasts down
and stops, kind 6; 2 it brakes to a stop, kind 7; 3 it drives on, kind 5): the rules' +0x41 1, 2, 3 give 3, 1, 2; else by the test's kind,
1 (the stop in the box) 2, 3, 4 and 6 3, any other 1; then 2 when the test counts cones (+0x46) and 3 when other cars are in it (+0x40).
A failure by leaving the course, a hit or kind 6's lead asks the same with kind 1 giving 1, and alone a 3 becomes 1; any other failure 2.
So B-1's car stays in the box when it has stopped there, and coasts to a stop past it when it runs into the cones. ✔ The original's
B-1 pass (11-license-tt `license_b1_pass_bronze.mp4`: the car stays in the box under the result scene) and overshoot
(18-round4-gt `license_b1_whole_test_overshoot_fail.mp4`: stopped past the box). The judge's ends (the stop, a failure, the cones
counted) come through here as well as the line's; before, the player's car was left to the stand-in follow-the-line driver and drove
on.

## A-spec points

Offered by a race (0x3C0FF8) with other cars: 250 in a mission (the race's virtual 95 is 10); else from the cars' ratings (the
parameter blocks' +0xFE0): the others' mean + 0.03 (0.07 on a course of kind 2, COURSE +0x10, for a car whose DRIVETRAIN +0x10 is not
2, four wheel drive) less the car's own; below 0 that x 10 + 1 and at least 0.1, else x 200 and at least 1; x 10, at most 200,
rounded. Taken at the end (0x3C1220, the race's +0x96C, what the result pages read): the same when the car's place is 1 and the
B-spec driver drove none of it (car +0x7B0 0), else 0. The display's two lines (RaceDisplay::vf43 0x39C570, vf44 0x39C6D0; its
virtual 42 asks the race's kind: the single race, missions, Gran Turismo mode's events): `AspecRaceFormat1` / `AspecRaceFormat2` as the
race begins (0x39C33C a moving start: 0.2 s in, 2 s, fades 0.1 / 0.3 s; 0x39C18C a hold: 0.5 s in, the hold less 3.5 s within
2 .. 5 s, fades 0.5 / 1 s), `AspecPointGet` / `AspecPointFormat` at the finish (1 s in, 2.7 s); `hb_24#1` at (320, 264) and, a 130 % and
orange, (320, 304). ✔ "250 pt. Race" at 0'00.7 of missions 1 and 21. ✔ The points of the original's two arcade races: the Ka '01 at Tsukuba
(Beetle 1100, Z ACT, N360, Midget II D, S500) 9, at Grand Canyon (the Beetle 1100) 24, the same here.

## Quick Tune (Arcade's race and time trial; `sim/quick_tune.*`)

The page (`QuickTunePopup` of quick-arcade and quick-tt, its script ../quick-share/quick_tune_popup.ad) offers Power (a slider
-20 .. +20 %), Weight (-10 .. +10 %), Top speed (the gearbox's slider x 10 km/h, insensitive when its least and greatest are both 0),
Driving aids, Transmission, Tire type with Front and Rear (the tire lists only when `IsDryCourse`), Cancel and OK. OK writes the
work's QuickTune* values from the page and sets `IsQuickTuned` 1; the Info lines are the player's grid row's power and weight
(`getPower`, `getWeight`) x (100 + level) / 100.

- **The fill** (once per sequence, before the first menus: 0x111B50 for the race, 0x112FE8 for the time trial): every entry's tire
  kinds +0x160 / +0x161 the race's tire (12 on snow) with the flag +0x165; the work's power and weight levels (+0x3B0, +0x3AC) 0,
  the front and rear tire (+0x3B8, +0x3BC) the race's tire, the transmission (+0x3C0) and aids (+0x3C4) from the options, and the
  gearbox (0x443ED0 reads the player's `GEAR` row): with +0x25 set, +0x3B4 the equipment's slider (+0x12A), +0x3B5 / +0x3B6 the
  row's least and greatest (+0x26, +0x27); else all three 0. `IsQuickTuned` (+0x488) 0, `IsDryCourse` (+0x48C) the course not
  kind 2 or 3. After a run the work is not filled again: the page shows the choice made.
- **The apply** (0x111DC0 / 0x113248, as START or SPECTATOR runs the race; after the transmission and aids): only when `IsQuickTuned`,
  on the player's car alone (entry 0's equipment): +0x163 (the power percentage) = power level + 100, +0x162 (the weight
  percentage) = weight level + 100, each a byte (a level below 0 wraps: -10 is 90); +0x160 / +0x161 the front and rear kinds;
  a slider other than 0 into +0x12A and the gears made from it (0x443240); +0x165 = 1; `IsQuickTuned` 0. The race's reset
  (RaceBase::vf38 → RaceArcade::vf92) fits the tires: dirt's 11 and snow's 12 on those courses whatever the page said, else the
  kinds asked when they are not the car's own. The car keeps the change for the rest of the sequence (the race object lives
  through it); the opponents are not touched. ✔ Here: the Ka '01 at Tsukuba, Power +20 %, Weight -10 % (the page's Info 72 HP,
  868 kg; the original's page reads 60 HP, 965 kg untouched): at 8.900 s on the race clock 67 mph against 63 mph untuned; a
  Front tire of SS gives kinds 5 / 4; the Falcon XR8 race car's Top speed 340 → 390 km gives slider 39; after a run the page
  shows +20 % / -10 % again (`test_quick_tune`: 61.6 → 67.7 PS, 965 → 868 kg, 59.4 → 65.8 km/h after 8 s from the grid).

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

A license test's **BestTime** (+0x498, the page's "Best") is the session's own: the test's sequence (0x11CB38; the
short tests' 0x114E38) keeps a best of its own from no time (0x157529FF) and writes it to +0x498 after each run
(0x11CDA0 / 0x114FFC) through the run's routine 0x1105A8; nothing reads it from the save. So the page shows the new
object's 0 ("0'00.000") on every entry to a test, after a load and on any revisit too, "-'--.---" after a run without a
graded time (0x1105A8 takes the time only for a result word of 1 .. 3: not after a failed run, nor a slow one, nor one left by
the pause's Exit), and the best graded time after one, while the test's Rankings keep the saved times. ✔ The original with its
own card after a fresh boot (reference 17-export-card `10_roundthree_card_b1_page.png`: "Best: 0'00.000" while
`10_roundthree_card_b1_rankings.png` holds 0'10.832; 18-round4-gt `b1_page_after_bronze_run.png`); ours the same (the dashes
after an untimed run by picture: licend's run/shots/o2).

The single race fills it (`fn_00111A40`, `fn_00111B50`): each car's name and paint by its grid
place, `numberOfEntries`, `playerGridNumber` (the player's place), `courseLabel`, `IsBSpec`,
`IsDryCourse`, the race's tire for every row, the quick tune from the player's settings
(transmission, assists), `canReplay` 0. `IsBSpec` (0x110CB8; the time trial's and the free run's
too): a paved course (its COURSE row's +0x10 0) whose byte +0x14 is not 0; `IsDryCourse`: not
kind 2 or 3; the tire on kind 3 is 12 (`start_quick_work`; the race side still writes 1 for both
on the single race). ❓ Only the first chip word (the paint's colour) is filled.

## Saved replays

The program's Save Replay runs the project `labomode` with `next_menu_arg` =
`MODE_SAVE_FULL_REPLAY` and keeps the drivers' controls on the memory card
(`Serialize::ReplayData`, `GT4MC::FileGT4Replay*`); a replay is the race run again from them. The
rebuild keeps the cars' states instead, step by step, in a file of its own under the data folder's
`replays/` (`<course>-<date>-<time>.gt4rrep`; `race/replay_file.*`): `"GT4RRPL1"`, u32 the body's
size, u32 its deflated size, then the zlib-deflated body: the course, the sequence and the race's
labels, the laps, the cars (label, paint), the steps (every car's state), then tagged sections until the body ends (u32 the
tag's four letters, u32 the bytes' size, the bytes; a section a reader does not know is kept): `INFO` the title, the race's
mode by the program's name, u32 its number (-1 none), u32 the time in ms, u8 the B-spec driver's run; `PICT` 64 x 64 RGBA8
pixels; `DETL` each state's brakes' heat and clutch; `RPLD` an arcade race's or a license test's run as the program keeps it
(replay.md "A replay of ours"). `save=<n>` saves a run at its step n. The Drive screen (a card's own replay file too) plays one with `GT4R_RACE="replay_file=<path>"` through the course's replay cameras; the Replay
Theater lists them.

Save Replay in the race's menus (COMMAND_SAVE_REPLAY) opens `labomode` on its save page: the run is the list's new file
(`GameState::pending_replay`; its title by the mode's name and number, as the page's script makes it), "Create new file" or a
replay there to write over, the keyboard for the title, then the menus again. The picture is the race as it stands when the
menus come up after the run (❓ where the program takes its own is not read; its captures show the road from the driver's
view). ❓ The number is given for license tests and missions alone.

## A free run's start, and the arcade's start against the original

- **A free run** (DynamicsConductorFreeRun, the arcade's time trial, the free run and Gran Turismo mode's practice): its
  virtual 30 (0x3F5670) asks 0x34D288 first: the driver set's bytes +0x19 (x 10 m behind the line) and +0x1A (x 5 km/h), kind 3
  (Tsukuba 640 m, 50 km/h; Nürburgring 30 km/h); without a speed the grid. vf28 clears the rolling start's pace (K +0xF886) for a run
  alone, so the driver drives at its own pace from the first step; the car is handed over 180 steps from the set-up (F +0x590, counted
  by vf14 through 0x3F37C0: the line's return 0x345228 and event 4, "START"). No opening shots, "FULL THROTTLE" until then. The race
  has no number of laps (virtual 37: 0); a lap's time counts from its crossing (vf20 writes F +0x614). ✔ the order and the times of
  the original's (`11-license-tt/arcade_time_trial_tsukuba_ka_start.mp4`).
- ✔ **The arcade's rolling start** against saved states of the original's (`03-race`, the Ka at Tsukuba, its six cars' equipment read
  from the state and raced with `GT4R_RACE` `entries=<file>`): 393 steps from the set-up all six cars' distances are equal to the
  millimetre and their speeds to 0.0003 km/h; 298 steps after the clock's start the four cars behind the leader are still within 1 mm
  and 0.0001 km/h (the leader, whose pace follows the player's place, 6 mm off: the player's pad differs).

## 2 Player Battle

Code: `src/gt4r/race/split_battle.*` (its rules, starts, halves, gaps), `src/gt4r/app/screen_split.cpp` (its screen),
`RaceWorld` with `WorldSetup::players` 2. Classes: the sequence `split_battle` (0x10C0E8), the race `RaceSplitBattle`, the
conductor `DynamicsConductorBattle2P`, the display `RaceSplitDisplayBase` ([race_display.md](race_display.md) "The two-player
halves"), the results page `ResultArcade`.

**The sequence** (no quick menus): the race in mode 0; an end code 4 (the pause's Retry) runs it again; any other end is
followed by the replay (mode 2), whatever the run came to and however short it was; code 4 in the replay races again, any other
end of it ends the sequence (the menus come back to Arcade's project). Two entries, car i at grid place i (0x3C0608), its
control source 0x100 + i (0x3B4F18: the pad of its port); the laps are the option `split_race_laps`; the car page gives each
player's gearbox, TCS and ASM and the tire (0x3D6350; request +0x60, chosen when >= 0), the options his `steering_assist_1p` /
`_2p` and `active_steering_1p` / `_2p`. The event is `sample_entry_1`: a standing start of 10 s.

**The rules** (the block 0x3BFB10 copies from the options; a new game's values 0x4363A0):

| Offset | Option | New game | What the conductor does with it |
|---|---|---|---|
| +0x0C | `split_race_laps` | 2 | the laps |
| +0x14 | `split_tire_damage` | 0 | 0 none, 1, 2: the wear's curve, the same bytes as Arcade's `tire_damage` (virtual 11, 0x354F30); only on a paved course whose `COURSE` +0x14 is 1 (0x34CCD4) |
| +0x18 | `split_handicap` | 0 | meters (the page's slider x 5): virtual 30, below |
| +0x1C | `split_boost_level` | 1 | the rubber band: virtual 12, below |

- **Handicap** (virtual 30, 0x347108): 0 both on the grid. Above 0 both stand on the driving line (start kind 1): car 0 10 m
  before the line and car 1 the handicap, at least 20 m, before it. Below 0 the other way round.
- **Boost** (virtual 12, 0x354EB8): the car behind is hurried (the rubber band, [ai.md](../specs/ai.md)): mild up to 7 % from 20 m
  back, strong up to 20 % from 10 m back, in full at 200 m; 0 no band (CFG +0x22 0xFF). The leader is not slowed. The band's
  reference is the first car of the order whose control source is not the computer's (0x34E418), so the leader of the two; a
  finished car's factor is the chain's (0x365790).
- **Tires** (0x330650): 11 on a course of kind 2, 12 on kind 3, else the chosen ones.
- **The finish** (virtual 21, 0x347230): as a single race's, without a time limit: at the line with the laps run, or with the
  other already finished; the car goes to its driver (0x343D78, mode 3).
- **Hits** are the generic ones (virtual 50), no penalty for leaving the course (virtual 49 is empty).

**The gaps** (0x3578B0, told by 0x359238). Each point of the lap (the split points and the line) keeps the cars' arrivals on the
highest lap any car has reached it on. Each new arrival after the first is told its time behind the first arrival unless its
place in the race is already better than its place among the arrivals; when the second arrival is the race's second, the first
is told the same gap as a lead (negative). A car that reaches the point a lap or more behind is told the number of laps.

**The results** page goes up at the first finish of either car (`ResultArcade` vf8, 0x3DF1B8: its +0x64 is 1) while the other
drives on; its Exit works at once (+0x4FC) by the first player's controller, and ends the run for both.

**The pause** is the first player's alone (the race's virtual 70, 0x32F910, answers his controller; 0x38B0E0): Continue, Retry
and Exit, no screen settings and no question. It opens in the replay too (virtual 23, 0x5F2BD0, answers 0): Retry races again, Exit
ends it. The player's own pit stops are not in this race (no pit menu object).

**The views**: each player changes his own; at the end both go back into the options `view_type` and `view_type_2p` (0x32F838).

**The picture** (draw 0x32FCC8): two halves with a black line 2 of 448 lines (units) wide between them. Without the wide-screen
option (`wide_mode`, 0x6184F0) one above the other, the lens x 1.2; with it side by side, the lens as it is. In a replay the
halves stay only with the option `replay_2p_split` (+0x1144), else the first player's replay fills the screen. The mirror of the
driver's view: stacked 240 x 40 at (200, 16), in the lower half (200, 8), of the half's 640 x 224, its lens' zoom 2.0; side by
side 140 x 46 at (90, 41) of 320 x 448, zoom 1.3 (the single race's). What a half leaves out of the scenery:
[course.md](course.md) "The race view's frame" (the several-view draw 0x33FD88: visibility list slot 4, the mirror's 7 or 3, the far clip x 0.5, the view flags `Env0` +0xF34 and the mirror's +0xF38, no slot 17, the mirror's fog and far plane at 60 m).

**The sound**: one mix for both, the ambience combined over both cameras without panning (0x398B70), each car's engine heard by its
own half's camera (0x3B5E80), the count's beep once a frame for both displays.

**The cameras**: each half has its own screen of the camera manager. At the start both show the same long clip (it stands at the
course's start place, camera.md "The start's shots"), then each his own car.

❓ Not seen in the original (no capture of the race): every figure above is the program's, the pictures are not compared. Judged
here: the lowest texture list the several-view draw forces on the scenery is not used (the full one is); a replay's dissolves
between cameras are cuts in the halves; the camera after a car's finish (mode value 11) keeps the view driven.

Here without a controller of his own the second player drives with his keys of the keyboard (the input layer's second set);
a controller that goes away in the race opens the pause.

## A championship's Free Qualify

The championship's command FREE_QUALIFY (its sequence 0x118EF0, the case at 0x119310 .. 0x119420): the championship race is
put away (its vf at +0x8 with 2), a `RaceFreePractice` (0x3B92D0) is made on the same race information (the whole field) and
driven in mode 0 (vf101), the options applied as for a race (0x10F948, 0x10F770, 0x10F848), the A / B meters counted
(0x10FA58); the player's best lap (0x35E3F0) read; the practice ended; the championship race made again (0x3EF498); then,
unless the player made no time (0x157529FF), the grid by every entry's best lap (0x3EE808: an entry's place is the number of
entries with a strictly better time, ties to the earlier entry, each taking the first place free from there; written to the
series' slots +0x2FC + 0x188 k and the entries' +0x3444 by 0x3EE7C0); the race behind the menus (mode 4, 0x387EF0) and the
page filled again (0x119918). The practice's conductor (DynamicsConductorFreePractice): no hold (vf10: start type 3), no laps
(vf37), no finish at the line (vf21 0x3F5150: the lap's time events only), the starts (vf30 0x3F4F10) the player's car kind 6
(❓ the pit's, 0x36437C), every other car kind 5 at 120 km/h (300 + 24 x its number) m behind the line; the pause's Exit ends
it. Here (`screen_race.cpp` `free_qualify`, `end_qualify`, `race::qualify_places`): the field rolling in as the program puts
it, the player's car on its grid place, standing (judged for kind 6); the grid by the bests after the pause's Exit, the race
rebuilt and the menus again; no result is reported. `GT4R_RACE="...,qualify=1"` runs the practice for checks. ❓ Not compared
with a capture of the original's Free Qualify (none in the reference).

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
| A standing start's hold, count, START | `race_flow.*` (`row_start`), `screen_race.cpp` `start`; the hold in `sim::Session` (`hold_once`: the countdown's flag a step at a time, the race information's +0xBC, 0x34C210 / the car's start hold) | GT4 (0x3C0208 with its table 0x6A29C0, 0x3C0190, 0x39D390, 0x39E9B0): the event's row's type and seconds (the machine tests' 7 s), a test's by its conductor (load_test); a run without a row (Gran Turismo mode's free run) 10 s ❓ | |
| Arcade's Quick Tune | `sim/quick_tune.*`, `screen_race.cpp` `fill_quick_tune`, `apply_quick_tune` | GT4 (0x111B50 / 0x112FE8 the fill, 0x111DC0 / 0x113248 the apply, RaceArcade::vf92 the tires, 0x443240 the gears) | |
| The opening shots (start motion, replay start) | `start_camera.*` | GT4 (CameraPathanim 0x37D338, 0x37D620); none in a free run (its pace 0x3F7880 is 0) | |
| The replay cameras (TV shots), their choice by distance, their lens | `replay_script.*`, `drive_camera.h` `shown_angle` | GT4 (0x37C138, 0x3FD980 → 0x3DDFF0); ✔ the lens and the picture against 47 of the original's replay states (camera.md); the dissolve's drawing judged | |
| The scenic shots behind the menus | `replay_script.*` `ScenicCamera` | GT4 (mode 5, 0x375980) | |
| A license test's page's scene | `screen_race.cpp` `stand`, `start_replay(page)` | GT4 (mode 2 from 0x11D1B8: the last run's replay; black before a run) | the replay's sound under the page; what the replay does at its end (ours: again) |
| The driving views | `drive_camera.*` | the car's camera places (CAR4); the on-board lens as the original shows it (61.2 degrees: its K and its chase pictures, camera.md), the roof view's by its mount as the program's angles show (`shown_angle`); their springs ❓ | the on-board camera's own lens |
| Laps, crossings, times, finish, the order | `race_flow.*` | GT4 (0x358608, vf21 0x357B98 with the time limit 0x34C158, the order 0x365BC0 / 0x3659E8; the results' rows by kind 0 and 1, 0x3E0030 / 0x3E00B0) | |
| License tests' rules | `license_rules.*` (`stopped_in_box`, `license_step`), `judge_license` | GT4 (0x3F8D70, 0x369888 → vf49, 0x35FE80; the stop in the box at or under 1 km/h, the share of the step before it; cones judged a step after they moved); the hits' strengths from the simulation's contact (its sum per step, not the program's per hit), the mass P +0x48 | the hit records 0x350530 |
| Cones and boards knocked about | `gadget_bodies.*` | GT4 (0x3E2B28 with 0x3E3748 / 0x3E3840, 0x3E44E8 / 0x3E4708, 0x3E3C48 / 0x3E3ED0 / 0x3E43B8, 0x3E5108, set-up 0x3E2710; course.md "Cones and boards: the bodies") in host floats; the road query and the car boxes' extents judged | the road query 0x3E5A30; the car record's box extents (+0x48C .. +0x494) |
| The demonstrations | `screen_race.cpp` `begin_demo`, `sim/ai/demo_driver.*`; headless `race/demonstration_run.*` (`gt4tool race <disc> demos us`) | GT4 (the recordings on our own race: starts, hold, run-up by the driver, rules, cones). ✔ 69 of the 70 US tests with a recorded time end on it to the millisecond (B-6 is 34 ms early, as in the program's own run of the file). B-8's car is not in the US database; 14 files record no time | |
| The disc's recorded races (the Replay Theater's Demo tab) | `screen_race.cpp` `load_recorded_race`, `play_demo`; headless `race/recorded_race.*` | GT4: the race set up from the file (replay.md): start, cars, drivers, pit stops; a pad run by its records, a B-spec run by its driver and the recorded commands, a free run from the recorder's moment. ✔ All thirteen of the US disc's end on their recorded times and best laps, in the app too | the replay's own play list (+0x148) |
| A run of ours kept as the program's, a card's replay file played | `race/race_recording.*`, `formats/replay/demonstration.*` (`encode`), `screen_race.cpp` `recording`, `begin_behind`; `gt4tool race <disc> recording` | GT4: the recorder's stream, the race's header, the cipher and the card's scrambling (replay.md "A replay of ours"); arcade single races and license tests. Ours: the saved replay file round it (states, details) |
| Free runs: time trial, free run, Gran Turismo mode's practice | `screen_race.cpp` `free_run`, `apply_course_start`, `Session::count_rolling` | GT4 (DynamicsConductorFreeRun: vf30 0x3F5670 the run-up, vf14 the hand-over, vf37 no laps) | the best lap's ghost |
| The pad's controls | `input/race_input.*`, `sim/rules_input.*` | GT4 (the port's report 0x43CF90, the frame's use; [input.md](../specs/input.md)) | |
| Looking back | `gt3r::race::look_back_held` | **engine** | ❓ not read |
| The race display: places, numbers, meters | `race_display.*`, `race_hud.*` | GT4 tables (RaceDisplay); the speedometer's face by the car's top speed estimate (P +0xFD8); how some parts move their pictures judged | the meters' drawing routines |
| The driver's view's fuel arc and pedal bars | `race_hud.cpp` `draw_bars` | GT4 (0x3A8968: places, steps, colours); the fuel gauge's flag +0x1B judged (always) | |
| The suggested gear | `race_hud.cpp` `draw_suggested_gear`, `RaceWorld::suggested_gear` | GT4 (the driver's suggestion 0x469E58, `ai-drivers`; the gear 0x36BF68, the session's); its drawing judged | the display's vf5 0x3A7108 |
| The license display's trophy times | `race_hud.cpp` | places judged | RaceLicenseDisplay::vf9 |
| B-spec's display and commands | `race_hud.cpp`, `screen_race.cpp` `update_panel` | display mode 1 GT4; the commands' panel the Race Monitor's movie at its second frame (0x3D92B0, `PACEPANEL` 0x3DB520), down while the car is in the pits (0x3D9998, 0x35BBD8); the help strip GT4 (RaceTooltipDisplay 0x3AE138); the speed plate's place judged; Select Car: the display shows the followed car (RaceDisplay +0x2E, race_display.md "B-spec") GT4, its order ❓ | the plate's place; the gap's car picture |
| B-spec's driver record and skill | `race_world.cpp` | GT4 (the book's record 0x468710, `gt-mode-2`; the skill 0x468C50 and the experience, `ai-drivers`); the skill's course flag (cond + 0xCBE7) taken as the course's marks being known ❓ | the flag's writer |
| Watching a replay: the keys, the car followed, the car's cameras, the zoom, the mode's plate, the pause | `replay_view.*`, `screen_race.cpp` `step_view`, `race_hud.cpp` | GT4 (camera.md "Watching a replay"); the display's numbers in a replay the meters alone, of the car followed | the photo drive, the x 4 sound |
| Dive: the cuts to the music, the cameras, the colors, the trail | `dive_camera.*` | GT4 (camera.md "Dive"), its random numbers ours | |
| The depth of field, haze and vignette of the views not driven in | `replay_effects.*` | GT4's rules and lens on the shared engine's passes (course.md "The passes of a replay") | |
| The replay's card and logo | `screen_race.cpp` `fill` | GT4's times (0x39BEC8) | the black under it, the caption's and the small logo's times measured |
| B-spec's views | `screen_race.cpp` `next_view` | GT4: the course's cameras, Cross the car's nose | the Race Monitor's own view keys |
| The results and pause pages | `strobe*`, `screen_race.cpp` | GT4 movies and callbacks, the player's focus and `inactivateButton` (strobe.md; ✔ a pressed row lights alone, beside the original's mission pause); ours: a tap or a click on a row of the pause, the PIT STOP menu or the split-screen pause focuses it, a second chooses it (`RaceHud::point_movie`), as GT3R's pause; a passed license test's page also the trophy (0x3E18D0 / 0x3E1A80, race/license_trophy.*), drawn once a frame (no averaged passes), the program's camera for its live picture judged (the course's TV cameras), the trophy's kind by the grade judged | ResultLicense's result kind (+0x88) and its camera |
| When the results page goes up | `screen_race.cpp` `results_shown`, `open_results`; `race_hud.cpp` `draw_words` | GT4: up at the player's finish (event 5, RaceResultBase::vf4 0x3E05A0), its movie held at frame 0 for its sprite 1 (3.3 s), the display under it fading at 1 s (0x3A1568) but for FINISH, the lap's line and the A-spec points; ✔ against the original's chase race (FINISH, panels out by 1.25 s, rows from ~3.5 s) and the championship's saved state (page up under FINISH) | the license test's page still covers the display at once |
| The race display's small parts | `race_hud.cpp` `draw_small_parts`, `draw_tires`, `draw_fuel` | GT4: the tire diagram (0x3A5FD8's picture, boxes, 0x3677F8 colors from the wheel's wear level +0xB1; drawn when the race wears tires), the fuel gauge (0x3A57A8; drawn when fuel is counted), the pedals (0x3A54F8, 0x3AB570), the signs (0x3A6C28, lit as 0x39E280: the aids' status, the handbrake, the oil), the gear digit (0x3A6DE0, 0x3A6D60) | the other drivetrain kinds' tire mapping (0x359538) not read |
| Sun, glare, lamp glows, billboards, smoke, wet road, the scenery's fade | `race_world.cpp`, `course_scene.cpp` | **engine** routines on GT4's layouts (`gt3r::race::SunGlare`, `SunDisc`, `LampGlows`, `Billboards`, `CarLampGlows`, `TyreSmoke`, `WetRoad`, `SceneryFade`). GT4's read: the sun disc's routine is the engine's, drawn at the picture's scale (course.md); the cars' lamps' colors, powers and strengths (model.md "Lamp"; Env0 +0x1794, +0x17A0); the heat haze's set-up, generator (seed 0x7F7F7F7F, x 0x5D588B65 + 1), drift and lens hexagon (0x4961B8: the same strip, clamps and 0.58 / 0.5 / 0.45 / 1.6 factors) and the vignette's ring (0x3BBBC0: 32 steps of 2 pi / 31, colors by 0x4AA0D0) are the engine's. **Ours, no counterpart in GT4**: the scenery's 0.6 s fade (`SceneryFade`; the program draws slot 1's models straight from the place's `Vls0` list, course.md "The race view's frame" step 4) and the frame pacing (`FramePacer`; the original draws one picture each 60 Hz field) | the glare's and the halos' own routines (not read); the backdrop's depth x 100 needs an engine item depth scale apart from its 0.01 modelview scale (fog and viewer at the true distance) |
| The car icons (results rows, Race Monitor, map) | `race_world.cpp` `draw_icons` | angles and size judged (CarIconMaker 0x393B70); the light the program's (0x394A10: one light, ambient 0.8, diffuse 1.5, specular 1.0, no course fog, car lights or reflection), the same at every course (✔ Fuji '90s against the reference's TCGP race 2, Tsukuba against 29-bspec-record, Clubman Route 5 at night the same as Tsukuba); drawn each frame where the program makes the pictures once at set-up through a ShowRoomCar (0x393D20, 0x364760; 128 x 64 and 64 x 32) | the light's direction in our view, the maker's reflection and darkened edge |
| The section times | `race/sections.*`, `screen_race.cpp` `step_splits` | the points GT4 (the road's list); the gap's partner: GT4's rule under "2 Player Battle", the split screen's; judged in the single race | event 15 (0x359C10) and its display |
| B-spec's Race Monitor | `screen_race.cpp` `fill_monitor`, `fill_gap_strip` | the movie GT4; the pit panel's tire diagram and fuel gauge on the movie's `TIRE_PANEL` GT4 (0x3D7EAC); the view's speed GT4 (L1 held with Left / Right, x1 .. x3, x1 at each opening; the race's steps a frame times it while the monitor is up: strobe.md "The race monitor"); the rows (names fitted with "..." 0x3DA668, the followed car lit 0x3DAF60), the gap strip GT4 (0x3DBF90: places 0x3DBE98, gaps 0x3DBDF0, their text 0x3DBEE8, the unit), its cars' pictures at the movie's `CARPOS_` externals, the rows' paint chips at `chip` (the paint's words; the place judged by 1 unit), the followed car's picture (CarIconMaker's kind 0: 30 units a metre, turned by the car's size, at most 158 degrees) without the lens's 18 degrees; the help line `RaceMonitorToolTip1`; the gap column judged; the map's letters drawn by us | the pace-up marks (0x3FA508), Show Map (Cross), the `sector` externals |
| The pit stops | `RaceWorld::pit_command` / `pit_offer` / `pit_stage`, `sim/ai/pit_stops.*`; `screen_race.cpp` `update_pit_menu`, `race_hud.cpp` (the messages, the refuel bar) | GT4: the pass (0x35B060), the command (0x3F19E0: not before the first crossing nor on the last lap, taken back by a second press), the PIT STOP menu (0x3DCC50, 0x3DCFB0, 0x3DD540, 0x3DD5D8; strobe.md), the messages (RaceDisplay::vf28), the refuel bar (0x3A59D8) and its stop (0x35BFC0), the driving mode's choice (0x3F19E0 command 10). ✔ The stages, messages, panel and menu beside the original's stop (`26-endurance/fc_pitwindow_*`) | |
| The cars' levels of detail | `car_visual.cpp` `CarVisual::level` | GT4 (0x07's size and centre, the car's rule 0x4539A8, T = 200); K, the view's factor (0x456918), from the chase view's measured 721.2 by the lens's angle (judged: the lens) | the race's lens (camera.md) |
| The far level's wheels | `car_visual.cpp` | GT4 (the body's own shapes with the wheel set's last texture set, callback 46) | |
| The brake discs' glow | `car_visual.cpp`, `race_world.cpp` | GT4 (heat² x `Env0` +0x4C / +0x38, the sponsor's vf24 0x3D75F0; heat from the simulation); the blend as the renderer's added pass with its alpha 🔎 | |
| Fitted wings | `car_visual.cpp`, `race_world.cpp` (the entry's WING part) | GT4 (0x4533A8's place and scale, callback 47 = 1); "spoiler taken off" (2) not reached: no part names it ❓ | |
| Moving models | `race/animated_models.cpp` | GT4 (the models' scripts and their programs' register moves 0x44 .. 0x4B: seoul's and lasvegas's turning signs; programs with two switches of their own registers, Ice Arena's people: each pair of cases is a key; 37 more of its models move, their motion not compared with the original's; the skies' aeroplanes, not compared with the original's either) | which models the race draws at once |
| The pit crew | `race/pit_scene.*`, `RaceWorld::step_pit_scene`, `pit_camera` | GT4: the men, the wheels, the hoses, the shadows, the sounds, the pit's cameras ([pit_scene.md](pit_scene.md)) | a replay's stops; the men's lower levels |
| The driver figure | `race/driver_figure.*`, `driver_posture.*`, `posture_*`, `car_visual.cpp`, `RaceWorld::add_drivers` (`GT4R_RACE` `driver=0` leaves them out) | GT4: the set (the car's own +0x34 or a character file by the cockpit kind 0x3B5438), its skeleton, the posture's solver bit for bit (`gt4r_diff posture`), the still posture of the closed cars, the driving posture of the single-seaters, their wheel, hands, elbows and body each step (model.md "The driver in the car") | the rest of the record's step 0x403260: the gaze, the gear change |
| Missions | `load_test`, `draw_grid` (the pool in its order), `judge_license`, `race_flow.*` | GT4. ✔ Mission 21 driven by the computers: 3'55.346, 3'57.123, 3'58.953, 4'00.795, 4'02.956 against the original's 3'55.340, 3'57.122, 3'58.953, 4'00.757, 4'02.952 (`31-progress6`, another player's car) | the win's page and missions 2 .. 5 and 8 (no original's picture) |
| 2 Player Battle: the sequence, the options' rules, the handicap's starts, the boost, the gaps, the results, the pause, the replay | `race/split_battle.*`, `screen_split.cpp`; the rubber band `sim/ai/drivers.cpp` | GT4 ("2 Player Battle") | the original's pictures; the camera after a finish |
| 2 Player Battle: the halves, their mirrors, the several-view draw | `screen_split.cpp`, `RaceWorld::render` (`half`), `course_scene.*` (`View::Half`, `View::HalfMirror`) | GT4's places, lenses, far planes, visibility lists and flags, on the engine's view; the texture list judged | the lower texture lists |
| 2 Player Battle: two controllers, their rumble and wheel forces | `screen_split.cpp` (`read_player`, `RaceFeedback` per port) | [input.md](../specs/input.md) | |
| 2 Player Battle: the sound | `audio/race_sound.*` `set_more_cameras` | GT4's rule (0x398B70, 0x3B5E80) in the engine's mixer | |
| A-spec points | `race/aspec_points.*`, `RaceWorld::aspec_points`, `fill` | GT4 (0x3C0FF8, 0x3C1220, 0x39C570, 0x39C6D0) | in a standing start the original hides the rest of the display and shows the course's logo until about 5 s in: here it shows |
| Not built | | cone shadows | |
