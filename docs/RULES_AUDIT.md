# Game rules: Gran Turismo 4's against ours

A table of the game's rules, mode by mode, read from the program (`CORE.GT4`, SCUS-97328 v1.01; addresses are the
program's), from the disc's scripts and from the save's format, with where each runs here and how far it follows the
program. It is the list of what a player meets that is not yet the game's, ranked by how likely a player meets it.
State of master 83c5587 (2026-11-26).

Status words: **ported ✔** (the program's routine, checked against the program or a capture of the original),
**ported** (the program's routine, not compared with a capture), **script** (the rule is the disc's own script, run on
our natives), **judged** (a stand-in), **ours** (by design, no counterpart in the game), **missing**, **wrong** (differs
from the program). ❓ not read to the end, 🔎 partly. Likely: how likely a player meets the gap (high, medium, low).

Captures named are under the reference's folders (`INDEX.md`).

## The gaps, most likely met first

| # | Gap | Where in the program | Ours | Likely |
|---|---|---|---|---|
| 1 | **Course rankings are never written.** The program inserts a run's best itself (time, date, car with its power and weight, pass code): the time trial (0x112620 → 0x113320 when the run's best lap 0x35E3F0 beats the session's), the free run and Gran Turismo mode's course practice (0x116E20 → 0x117898), the machine tests (0x115A90 → 0x1161A0 → 0x116268, 0x116440, 0x1165B8), all through the course unit's insert 0x433260 / 0x433348 (the natives `insertEntry` 0x15B7A0 and `insertEntryMaxSpeed` 0x15B948 are the same routines). No script inserts them. | 0x113320, 0x117898, 0x1161A0 | `menu/game_race.cpp` `race_finished` writes only the license tests' rankings; the Rankings pages of Arcade's Time Trial, of every course's Practice and of Power & Speed stay empty | high |
| 2 | **No best-lap ghost.** The option `automatic_ghost` (MOption +0x10CC, 1 in a new game): the best lap drives as a ghost from the second lap (the time trial keeps it for the next run, 0x3C08C0 / 0x3C0B30; Serialize::GhostData 0x3DEC50), Up shows or hides it (`BUTTON_GHOST`). ✔ 25-sound-cars (Arcade's Time Trial at Tsukuba: "a silent ghost appears after a first lap"); Prologue's lessons too (37-prologue-medal: a second run of a lesson shows the ghost car). ❓ the ghost's maker not read. | 0x3C08C0, 0x3C0B30 | none: the button is read and nothing uses it (input.md) | high |
| 3 | **Save Best Lap Replay does nothing** (`COMMAND_SAVE_GHOST` on the time trial's, free run's, license's and machine test's pages: labomode `MODE_SAVE_BESTLAP_REPLAY`; `canSaveGhost` +0x474 and `CanLogger` +0x4B0 set to 1 after a run with a recording, 0x1128E0). So the license page's icons after a run differ from the original's (18-round4-gt: after a B-1 run Start Test, View a demo run, Replay, Rankings, Save Best Lap Replay, Load Best Lap Replay, Analyzer, Settings, Options, Exit, no plain Save Replay). | 0x112B50, 0x11CF78 | `screen_race.cpp` `update_quick`: GT4's falls to "the menus again"; `canSaveGhost` set for Prologue alone | high |
| 4 | **The options pages' Cancel keeps the changes.** `MGame.getOption` (0x161B88) gives the scripts a copy of the options, `setOption` (0x161C38) writes it back and applies it (0x43A970, 0x4365B8); option2 and option3 (Screen, Race) preview on the copy, commit with `setOption`, and Cancel reads `getOption` again. | 0x161B88, 0x161C38 | `menu/game_natives.cpp`: `getOption` is the live options, `setOption` does nothing: every change is kept at once, Cancel shows the changed values | high |
| 5 | **A course visit's diary day is put per run.** The free run, practice, machine test and photo drive put one day when the sequence ends, whatever the runs (0x42F448: place 15, note 14, no time; the free run's 0x1179F8 with the session's best and note 13 when it beat the course's record). | 0x115E90, 0x1184DC, 0x11B0D8, 0x1179F8 | `game_race.cpp` `race_finished` (Course): a day per run with its time, none for a visit without a run; the calendar (and the used cars' weeks) runs on with every run | high |
| 6 | **Championship Free Qualify does nothing.** The pre-race page's timing-board icon (offered with A-spec): a RaceFreePractice of the field (0x3B92D0) driven in mode 0, then every car's best lap sorts the grid (0x3EF498, 0x3EE808; start slots 0x3EE7C0), the race back behind the menus (mode 4) and the page filled again (0x119918). ✔ the icon: 30-championship `race1_prerace.png`. | 0x119310 .. 0x119420 | **ported**: `screen_race.cpp` `free_qualify` / `end_qualify`, `race::qualify_places` (0x3EE808); the field's rolling starts (kind 5) and the player's pit start (kind 6: his grid place, held 2 s, an out lap) 0x3F4F10 / 0x36437C; places by best laps (vf3); the practice (0x1181C8) the same; ✔ scripted through the menus (race_flow.md); ❓ no capture | high |
| 7 | **B-spec cannot be sped up.** The Race Monitor shows the race's speed `"x %d"` (0x3DBBD0, the monitor's +0x74); ❓ its writers (0x3D9FDC, 0x3DA02C, 0x3DAC20) and the keys not read. | 0x3DBBD0 | **ported ✔**: L1 + Left / Right x1 .. x3 while the monitor is up, the race's steps a frame times it (0x3D8B00, RaceBase::vf74), x1 at each opening, the text blinking while L1 is held | high (B-spec, endurance) |
| 8 | **The Analyzer does nothing** (`COMMAND_LOGGER` on the time trial's, free run's and license's pages: project `logger`, MLoggerControl 0x11F1E0 and its face, the course map's display members; save block at 0x39F1C). ✔ the icon: 11-license-tt `tt_prerace_menu.png`, `b1_page.png`. | 0x10FEE8, 0x11F1E0 | falls to "the menus again"; 19 of the logger's members missing | medium |
| 9 | **The time-limited endurance races end on laps.** Eight events give minutes in the `RACE` row's u16 +0x18 (`getLimitTime`; cond + 0xF850 x 60,000 ms, 0x34C140): the two 24 hours of Le Mans and the Nürburgring 24 hours 1440, Tsukuba 9 hours 540, Motegi 8 hours 480, the two 4 hours 240, Infineon 165. The finish (vf21 0x357B98) ends the race when the time is over and the car leads; the others at their next crossing. ❓ which of laps and time ends such a race first. | 0x357B98, 0x34C140 | **ported**: `race_flow.*` `FlowSetup::limit_ms` (0x34C158 in vf21), laps 0, the results by laps (kind 1, 0x3E00B0), the pit stops' last lap and the fuel count; checked with `limit=30` | medium |
| 10 | **A license run without a grade is written into the save.** 0x1105A8 keeps only a gold, silver or bronze (results 9, 10, 11) in the test's unit and ranking; a failed run is kept nowhere. | 0x1105A8 | `game_race.cpp` License: `unit_result(1, 13 or 15)` for every run (a tried test's unit gets a result the original never writes) | medium |
| 11 | **The license page's Replay stays after a demonstration.** DEMONSTRATION plays the demo (mode 2) and clears `canReplay` (✔ 34-b3-demo: after the demo the page's row has no Replay). A run under 2 s on the clock (0x358440 < 2001 ms) gives no Replay either and restores the settings (0x1100D0, 0x110230). | 0x11CEA0 .. 0x11CED8, 0x11CE3C | `run_over` → `enter_quick(1)`: Replay after the demo and after any run | medium |
| 12 | **Cars never get dirty.** The step adds dirt from the wheels' sliding (x 4 on a paved course, capped 0.8, 0x367148 → 0x3438C8). | 0x3438C8 | the step computes it (`sim/rules_effects.cpp`), but the race's result carries no condition back to the garage: Car Wash has nothing to wash | medium |
| 13 | **Photo Mode from a replay.** Select in a Replay Theater replay opens the photo pause (PhotoPause 0x3C3C20) instead of the simple pause; Gran Turismo mode's Photo Drive (sequence `photo_drive` 0x11ABE0, Cr 3) drives with it (32 pictures, `racephoto.strb`, films), and Photo Lab develops the films (0x1C7BF8, RacePhotoDevelop). | 0x3C3C20, 0x11ABE0, 0x1C7BF8 | Select holds the picture; the photo drive is a plain free run; no films (photo.md "read, not built") | medium |
| 14 | **The title's demonstrations.** On the title's time-out (`timeout_count_to_autodemo`, 20 s) the sequence `autodemo` (0x10BE20) counts its turns (Status +0x3B018) and with a draw (0x579358) plays the opening movie, one of the disc's recorded races (0x10BBB0) or an arcade demo race (0x10BA28, RaceArcadeDemo); `MDemonstration` (0x15DB40, 0x15E488, 0x15E4C0) keeps the counts. ✔ the first time-out's movie: 01-boot `title_idle_until_timeout.mp4`. | 0x10BE20 | `screen_menu_viewer.cpp`: the movie every time; `MDemonstration`'s methods do nothing | medium |
| 15 | **Power & Speed (the machine tests).** DynamicsConductorMachineTest (vtable 0x685BF0, 0x3F6958 ..) ends the 0-400 m, 0-1000 m and top-speed tests; the sequence keeps `BestMaxSpeed` (the car's top speed +0x6CC, km/h) for the top-speed test and the ranking (gap 1). `getMTUnit` picks the unit by the tests' name table 0x846320. | 0x3F6958, 0x115A90 | **ported**: the test's kind from the row's laps, the goal 400 / 1000 / 6845 m and its split points (0x622490), the run ended at the goal, the car coasting down; `BestMaxSpeed` (km/h x 100) and the top speed's ranking (unit 2 by speed); ✔ by picture (400 m: FINISH, 0'11.156, Session Record; top speed: Session Record 199.01 mph); ❓ the display's top speed line, the race display of the test (RaceMachineTest's words `MaxSpeed`, `RecordSpeedLabel` ..) not read; `getMTUnit`'s name table still by text | medium |
| 16 | **Load Best Lap Replay** (`COMMAND_LOAD_GHOST`, `MODE_LOAD_BESTLAP_REPLAY`): a saved best lap raced against as a ghost. | 0x112AE0, 0x11CEF8 | labomode's load page with an empty list; nothing raced against | medium |
| 17 | **The endurance display's lines**: "Final Lap" (`EnduranceFinal`), "%d laps left" (`EnduranceLaps`) and the time left. | 0x3AE9A0 (table 0x621548) | **ported**: the words `EnduranceFinal` / `EnduranceLaps` are never read by the program; what a timed race shows is the total time's layout (0x6A08F0, flag 0x39B5F0), drawn for 10 laps or more or a limit | medium |
| 18 | **The Race Monitor's pit panel and pace-up marks** (the next three stops' laps and tires, 0x3FA508), the view's speed (L1 + Left / Right), Show Map. | 0x3DBF90, 0x3FA508 | the pit panel's tire diagram and fuel gauge (`TIRE_PANEL`, 0x3D7EAC) **ported ✔**; the view's speed (gap 7) done; the pace-up marks **ported** (vf18 0x3578B0's lists by round, 0x3FA508: a car closing on the one ahead between its last two section ends; ❓ the mark's colours: ours magenta, the original's red to yellow); Show Map (Cross, +0x70) **ported**: the map hidden, the rows' L (0x343B50), P (0x3FA498), Best and its new-best mark (0x3DA7C8); ❓ the `sector` bars (0x3D7930) not drawn | medium |
| 19 | **Arcade's penalty "Passing pit lane"**: the mark is set (rule +0x4A 0xFF → F +0x62E); what it asks of the driver (a drive through the pit lane) and how it clears are not read. | 0x34CA20 | **ported**: the mark (F +0x62E) is the pit pass's pad visit (`Session::pit_mark`, `PitHooks::mark`): a drive-through, cleared at the lane's end; ✔ in a race (`GT4R_RACE="course=tsukuba,race=sample_entry_6,cars=2,laps=3,penalty=2,drive=pad,auto_after=1500"`, full throttle into turn 1's wall): the mark 1 at the hit (236.9 m, 10.5 s), 2 at 1362.9 m, the car down the lane (stage 1 at 1738 m, out at stage 5) without a stop, the mark 0 at 147.5 m after the lane's end, the crossing counted (lap 2, 1'26.186); ❓ the display's line while marked | low |
| 20 | **`setReturnToLastMenuProject`** (0x166398: the next project the last one, 0x42FC50), how the option, memory card, logger, network and slide projects leave when `major_menu_project` is empty. | 0x166398 | does nothing; ❓ whether our fallback lands on the same page is not checked | low |

Lower (items 21 on, in rank order; done ones say so): Arcade's event per car (`MCarData.GetRaceCode`, `IsTireNormal`,
`IsDirtRunnable` 0x12E150 / 0x12E388 / 0x12E498 through 0x448AD0) **ported ✔**, with the cars' dirt and snow versions
(`ShiftCarCode` 0x12F3E0: every Arcade entry on a dirt or snow course raced as its ARCADEINFO row's ENEMY_CARS row, menu.md
"Arcade's dirt and snow versions"); a new game's screen shape (`MSystem.GetStaticWideMode` 0x1ACA90: the console's screen
setting, wide for "full" or 16:9 through 0x437928; `MUnit.SetUnitSystemByLanguage` 0x1AEDB8) **not met**: only the show and
event configs call them (config-august, -e3, -june, -lemans, -mazda, -subaru, -wed), never the retail config-main (ours answers
0, the setting a computer lacks); the slide show's and Photo Lab's music **ported**: `MSound.isPlayingStream` (0x23EA18) and
`sound.stream.isPlaying` (0x2C3700) answer whether the stream plays (Runtime::stream_playing, the app's music stream), so
their music player goes on to the next track (it held on the first while they answered 0; `disc_menu_stream_playing`); the
pause's idle time-out (`timeout_count_to_topmenu`, the option's +0x1154, 600 from the options' maker 0x436488) **not met**:
nothing in the program reads it but the scripts' attribute (0x186530) and the race's copy of the options, which keeps its own
(0x3B5070), and no retail script reads it (Prologue's title counts its own `timeout_count`); the car's distance **ported**:
added each step by the step (0x35DD00 in 0x35DE20, `sim/rules_speeds.cpp`) and the block carried back to the garage car
(`RaceResult::condition`), `add_distance` only for a run without that step; the oil's and the body's life rates **ported**
(`game_car.cpp`: getOilLifeRate 0x3437F0, oil 0x343698, replaceOil 0x343928 on the engine condition's limits 0x350678;
getBodyLifeRate 0x343A80 -> 0x366300, bodyLife 0x343978 on the body's table 0x366100 from 0x620AF4 / 0x620AF6; dirtiness at
most 0x3F4CCCCC, 0x3438A0; the US disc's values as the constants were, the body's setter now without a least and the rates in
the EE's arithmetic; `disc_menu_car_condition_natives`); the Replay Theater's play list (empty) and the replays' SHUFFLE play
(`replay_play_mode`, read, unused); the time trial's `ranking` project after a run with a time (0x108354: the online
leaderboard); the event Preview (`race_view` 0x10BB80 → 0x10BA28, a RaceArcadeDemo: ours the event's field driven by the
computer in the TV view, ❓ the demo's length and camera not read); a free run's pause Exit after a lap (RaceFreeRun::vf77
0x3BA260: the car to its driver first, ❓).

## The race sequences

The sequences by name (the names at 0x6A53D8, run by 0x108040 through its table 0x68CC00); each has its own table of the
race's menus' commands. Here one handler takes every sequence's commands (`screen_race.cpp` `update_quick`); a command the
handler does not do goes back to the menus. The quick projects offer: quick-arcade START SPECTATOR REPLAY SAVE_REPLAY OPTION
EXIT; quick-tt START SPECTATOR REPLAY SAVE_REPLAY LOAD_GHOST SAVE_GHOST LOGGER OPTION EXIT; quick-freerun the same and
SETTINGS; quick-license START DEMONSTRATION REPLAY SAVE_REPLAY LOAD_GHOST SAVE_GHOST LOGGER NEXT_RACE OPTION EXIT; quick-mt
START REPLAY SAVE_REPLAY LOAD_GHOST SAVE_GHOST SETTINGS OPTION EXIT; quick-championship START SPECTATOR FREE_QUALIFY SETTINGS
RETIRE_CHAMPIONSHIP NEXT_RACE REPLAY SAVE_REPLAY OPTION EXIT; quick-event START SPECTATOR SETTINGS REPLAY SAVE_REPLAY OPTION
EXIT; quick-practice START REPLAY SAVE_REPLAY SETTINGS OPTION EXIT; quick-mission START REPLAY SAVE_REPLAY OPTION EXIT.

| Sequence | Routine | Its rules | Ours | Status |
|---|---|---|---|---|
| `boot` | 0x108160 | the project `boot` | the disc's script | script |
| `menu` | 0x108220 | the menus' project | the disc's script | script |
| `single_race` | 0x111498 | START mode 0 with the quick tune (0x111DC0), the replay at once after 2 crossings or more, `canReplay` 1; SPECTATOR mode 1, no replay, `canReplay` 2; REPLAY mode 2 / 3; SAVE_REPLAY labomode; the A-spec best into the course record (0x111E98); no diary day | `update_quick`, `RaceWorld::end_run`, `game_race.cpp` | ported ✔ (race_flow.md) |
| `time_trial` | 0x112620 | as the single race with the free run's conductor; after a run the ghost kept, `CanLogger` and `canSaveGhost` 1, the ranking insert and `BestTime` (0x113320); no auto replay; no day; at the end the project `ranking` when a time was made | `BestTime` ✔; no ghost, no ranking, Save Best Lap Replay and Analyzer missing | gaps 1, 2, 3, 8 |
| `freerun` | 0x116E20 | as the time trial, and SETTINGS; the B-spec book (0x468AF0) and the A / B meters (0x10FA58); the ranking insert (0x117898); one day at the end (0x1179F8) | the book ✔, the meters ✔, `BestTime` ✔; a day per run | gaps 1, 2, 5 |
| `practice` | 0x1181C8 | RaceFreePractice; START mode 0, the meters, no book, no best time; REPLAY, SAVE_REPLAY, SETTINGS, OPTION; one day at the end (0x1184DC) | the meters ✔, no book ✔; a day per run | gap 5 |
| `machinetest` | 0x115A90 | its own conductor; `BestMaxSpeed` or `BestTime`; the machine test's ranking (0x1161A0); one day at Exit (0x115E90); its table has no case for LOAD_GHOST / SAVE_GHOST (❓ whether the page shows them) | the conductor's goal and end, `BestMaxSpeed` / `BestTime`, the rankings; a day per run | gaps 5, 15 (the display) |
| `solo_event` | 0x113B48 | START: a finished session's rows refilled (0x114250) without a run, else mode 0, the result (0x114498: prize, place counts, record, score, a day per run), the book, the meters, the replay at once after 2 crossings, `IsSessionFinished` 1; SPECTATOR the same without the replay; SETTINGS the project `setting` | `update_quick`, `race_finished` Event | ported ✔ |
| `championship` | 0x118EF0 | the race's result (0x119CB8: points 10 6 4 3 2 1, prize, record), the last race's (0x119F28: grand prize, `SeriesRank`); NEXT_RACE (0x119428: the series closed 0x119BD8 or the next race, its grid 0x3EE9F8); RETIRE_CHAMPIONSHIP; Exit keeps the series; FREE_QUALIFY | `race_finished` Championship, `next_race`, `suspend_series` (✔ 30-championship) | ported ✔ but gap 6 |
| `license` | 0x10FBD8 → 0x114E38 (tests 5, 10, 15), 0x11CB38 | the test's car (0x10F880); a graded run kept (0x1105A8); the session's own Best (+0x498); DEMONSTRATION clears Replay; NEXT_RACE the next test (0x10FCD0); the day when the sequence ends (0x110800); Retry by end code 4 | `next_license_test`, `license_day`, the Best (✔ 17-export-card) | ported ✔ but gaps 2, 3, 8, 10, 11 |
| `mission` | 0x11BB60 | finished only by the car's +0x5B6: 0x11C328 (prize whatever the place, record kind 3, score, day), else 0x11C2E0 (the day alone) and the page back with Replay | `race_finished` Mission (✔ 21-gtmode-deep) | ported ✔ |
| `photo_drive` | 0x11ABE0 | a drive with the photo pause (0x3C3C20), the book, the meters; one day at Exit (0x11B0D8) | a free run, a day per run | gaps 5, 13 |
| `split_battle` | 0x10C0E8 | race_flow.md "2 Player Battle" | `split_battle.*`, `screen_split.cpp` | ported (❓ no capture of the original's race) |
| `lan_race` | 0x10D080 | RaceLanBattle | `screen_online.cpp`, net.md | ported, on our transport |
| `online_p2p_race`, `online_rally_race`, `online_bspec_race` | 0x10D2E0, 0x10ED60, 0x10D520 | the official service's races; only the project `online` names them and nothing on the US disc starts that project | not offered (our online races are the LAN race over a relay) | ours by design |
| `autodemo` | 0x10BE20 | gap 14 | the movie | judged |
| `load_replay`, `demonstration` | 0x10CA50 (`MODE_LOAD_REPLAY`, `MODE_DEMONSTRATION`) | the Replay Theater's replay (RaceReplayInformation 0x3DD838) | `replay_file.*`, the card's replay files, `load_recorded_race` | ported; our own file by design |
| `race_view` | 0x10BB80 → 0x10BA28 | an event's Preview: a RaceArcadeDemo of the event | the field driven by the computer, the TV view | judged |
| `photo_shoot`, `photo_labo` | 0x1C79A8 (a day, kind 5), 0x1C7A88 | Photo Travel, Photo Lab | `screen_photo.cpp`, `game_photos.cpp` | ported ✔ (photo.md); films gap 13 |

Every race: the pause's rows by the race's class (strobe.md), Retry (end code 4) running the race again at once in the license
tests, missions and machine tests (0x11CD10, 0x114F70, 0x11BC68, 0x115C00): ported. The finish, the order, the A-spec points,
the license tests' judge and ends, the starts: [formats/race_flow.md](formats/race_flow.md), ported ✔ but the time limit (gap 9).

## The scripts' natives

The US disc has 493 scripts (`projects/` 41 of its 671 files, `config/` 46, `script/` 6, `usedcar/` 400), 488 of the
interpreter's version, all read; Prologue's 39, 34 of them. The program registers 199 classes for them. A static walk of every
script's calls into the program's classes (US): **1,194 members used, 22,133 uses**.

| Status | Members | |
|---|---|---|
| implemented | 985 | most are the game's state as plain values (the rules live in the scripts and run as written); 177 cite the program's routine beside them, the rest are not compared with the program |
| implemented, compared here | 7 | `MRaceRecord.isLicensePassed`, `isLicenseAllSilver`, `isLicensePerfect` (0x1A63A0, 0x1A6120, 0x1A5FE0 → 0x4312B0, 0x431288, 0x431260: tests 1 .. 16 through the region's label then "jp" 0x430E58; per test 0x430C50, 0x430BC0, 0x430B58), and four with nothing in them |
| partial | 2 | `MGame.setOption` (gap 4), `MCourseRecord.getMTUnit` (gap 15); `MCarData.GetRaceCode` / `IsTireNormal` / `IsDirtRunnable` now compared (0x12E150, 0x12E388, 0x12E498), and `ShiftCarCode` / `GetDirtCarCode` / `GetSnowCarCode` (0x12F3E0, 0x12F8B8, 0x12FBB0) the program's (they answered the car itself) |
| stub | 23 | by design: the card's failure and swap states (`isAutoSaveDisabled`, `isChanged`), the printer, the 480p / 1080i modes, the official service; rule-bearing: `setReturnToLastMenuProject` (gap 20), `MDemonstration`'s counts (gap 14); `GetStaticWideMode`, `SetUnitSystemByLanguage` only in the show configs; `isPlayingStream` / `isPlaying` now the stream's state |
| missing | 168 | the official online service 129 (MNetwork's account, lobby, buddies and news, MHttp, MDnas, MNetConf, MComm, MXml, the stats), the Analyzer 19 (gap 8), EyeToy 13, and 7 single uses (`MDatabase.getAllTunerList`, `MGame.checkExistValidGameData`, `MManager.loadProject` / `unloadProject`, `MStorage.getDir`, `MRunViewer.setRaceDebugFlag`, `MSystem.SetLocale`) |

Prologue: 294 members, 3,942 uses; the same gaps where shared, and its own `MGame::setLoadingMode` (the prologue, opening and
quick projects; ours is `MSystem`'s alone), `MMemorycardProgress.setProgress`, `MMCFileActor`.

Race commands the scripts offer and the race does not do: SAVE_GHOST, LOAD_GHOST, LOGGER, FREE_QUALIFY (gaps 3, 16, 8, 6).

## Gran Turismo mode's own systems

Most of the mode's page rules are the disc's scripts (`projects/gtmode` and its `share/*.ad`: the regulations, the events, the
dealers, the presents): they run as written on our natives. The program's side:

| System | Rule | Program | Ours | Status |
|---|---|---|---|---|
| B-spec | five pace levels and their factors; the commands (pace, passing, pit, not before the first crossing nor on the last lap); experience during the race; skill from the book; the book written back, a won race's change doubled; the Status page's figures | 0x3F1790, 0x3F19E0, 0x4684A8, 0x468C50, 0x468860, 0x468A60, 0x468B58 | `sim/ai`, `race_world.cpp`, `game_bspec.*` | ported ✔ (29-bspec-record) |
| B-spec | the race's speed | 0x3DBBD0 | gap 7 | missing |
| B-spec | the A-spec / B-spec distance meters at the sequence's end (not in licenses, missions, Arcade) | 0x10FA58, 0x114058 | `game_race.cpp` | ported; ❓ a run counted as one driver's |
| B-spec | fatigue, age, hiring, several drivers | none: no strings, natives or save fields | | the game has none |
| Endurance | the time limit, the display's lines | 0x357B98, 0x3AE9A0 | gaps 9, 17 | missing |
| Endurance | a save in the middle of a race | none (the save's `suspended_*` are a championship left between races, 0x119BD8) | `GameState::suspend_series` | the game has none |
| Endurance | fuel (tank, burn P +0x104C, 0.74 kg/l, out of fuel), tire wear and the event's curve (only on a course counting fuel), pit stops (request, lane, crew, refuel, the PIT STOP menu) | 0x354B78, 0x352EA0, 0x34CCD4, 0x35B060, 0x3DCC50 | `sim/rules_fuel.*`, `sim/rules_wear.*`, `pit_flow.*`, `update_pit_menu` | ported ✔ (26-endurance `fc_pitwindow_*`) |
| Car condition | the block (odometer, engine distance, oil, dirt, body); the engine's factor on torque by its distance and the oil's | 0x43DC20, 0x350868 | `CarRecord::condition`, `sim::engine_condition` | ported ✔ (05-gtmode: the Panda 40 → 44 HP after an oil change) |
| Car condition | distance added each step to the odometer, engine, oil (its run-in wrap), body | 0x35DD00, 0x35DE20 | `sim/rules_speeds.cpp`, the block back by `RaceResult::condition`; `add_distance` once only for a run without the step | ported |
| Car condition | dirt from the race | 0x3438C8 | gap 12 | missing |
| Car condition | oil change, car wash, body refresh, the life rates | 0x14EB38, 0x14EB80, 0x14EDD8, 0x3437F0, 0x343A80 | `game_car.cpp` on the program's limits (0x350678, 0x366100) | ported |
| Used cars | makeUsed, the price by odometer | 0x440FB8, 0x441128, 0x4411F0 | `game_car.cpp` | ported ✔ |
| Used cars, legend cars, presents | the week's line-up (`usedcar/US_used%04d`, 400 lists), the sold marks, the legend dealer, presents by race and by date | scripts, MUsedCar 0x1B21A0, MPresent 0x19FE18 | natives in `game_records.cpp` | script ✔ (21-gtmode-deep: two weeks); the legend dealer not compared |
| Calendar | a day per diary event (kept 2,922); the weeks from 2005-04-02 | 0x42F0E0, 0x42F530 | `GameState::put_day`, `game_records.cpp` | ported ✔ |
| Calendar | who puts a day: a car got or sold, a race (event 0x114498, championship 0x119CB8 / 0x119F28), a license visit (0x110800), a mission (0x11C2E0 / 0x11C328), wheels, a wing, a course visit, a photo shoot | 0x42F198 .. 0x42F448 | every caller but the course visit's moment (gap 5); Arcade none ✔ | ported ✔ but gap 5 |
| Prizes, records | an event's prize by place, place counts, total, best score, the record when the place beats the sequence's best; a championship's points, grand prize, rank, a series left kept; the prize cars (scripts over the units); the A-spec points offered and taken; Complete and the halls' locks (`canEntryEndurance` 25 %, GT World, Extreme) | 0x114498, 0x119CB8, 0x119F28, 0x3C0FF8, 0x3C1220 | `game_race.cpp`, `race/aspec_points.*`, `game_progress.*` | ported ✔ (30-championship; save.md "A game with progress") |
| Prizes, records | the course and machine test rankings | 0x433260 | gap 1 | missing |
| Missions | counted only when finished; no off-course or pit fail; a hit's penalty (CFG +0x4A); held starts | DynamicsConductorMission, 0x11C328 | `license_rules.*`, `Field::drive` | ported ✔; the held start judged |
| Licenses | the tests' rules, judge, ends, staff, results; coffee breaks after test 8; the license needed for A-spec (all 16 bronze or better); the grades and all-gold presents (scripts) | race_flow.md, 0x110498, 0x10FCD0 | `license_rules.*`, `screen_race.cpp`, race_natives | ported ✔ but gaps 2, 3, 8, 10, 11 |
| Special condition events | dirt and snow tires forced by the course's kind; wheels by category at the Wheel Shop | RaceArcade::vf92, 0x330650 | ported | ported |
| Regulations | license, drivetrain, aspiration, car type, tires, power, weight, length, country, years, allowed cars; the car's current power with its condition | `share/regulation.ad`, MRaceData 0x1A0F80 .., 0x146648 | natives | script on ported natives ✔ |

## Arcade and the other modes

| Mode | Rule | Program | Ours | Status |
|---|---|---|---|---|
| Arcade | no unlocks of its own: cars and courses made available by Gran Turismo mode's and the boot's scripts (`setCarAvailable`, `setCourseAvailable`) | scripts; save block 0x361C0 | `game_natives.cpp`, `save/game_data.cpp` | script |
| Arcade | difficulty (the car's ARCADEINFO entry), `enemy_level` (the opponent pick's offset), the opponent picker, paints | 0x12E150, 0x1308C8, 0x579330 | `game_opponents.*` | ported ✔ (the reference's grids); the tire and dirt checks judged |
| Arcade | favorites (30, in the order added), the best A-spec score (at most 255) | 0x4303B0, 0x111E98 | `game_records.cpp`, `game_race.cpp` | ported |
| Arcade | the penalty option (none; speed limit 10 s past a wall's 8 / a car's 40; passing the pit lane) | 0x34CA20, 0x3BFB10 | `race/start_rule.*` `arcade_rules` | ported but gap 19 |
| Arcade | tire damage (two wear curves), the time limit option (`arcade_time_limit`: no race routine reads it) | 0x354F30 | race rules | ported |
| Arcade | Quick Tune | 0x111B50, 0x111DC0, 0x112FE8, 0x113248 | `sim/quick_tune.*` | ported ✔ |
| Arcade | the time trial's Session Record (no time at a new sequence) | 0x112E78 | `game_race.cpp` | ported ✔ (11-license-tt `tt_prerace_menu.png`) |
| 2 Player Battle | the sequence, the options' rules, handicap, boost, finish, gaps, results, pause, views, picture, sound | race_flow.md | `split_battle.*`, `screen_split.cpp` | ported (❓ no capture) |
| LAN | the lobby, the barrier, the settings, RaceLanBattle | 0x1E4B20 .., 0x161F28 | `menu/game_network.*`, `net/*` | ported on our transport |
| Photo Travel, Photo Lab | the places, camera, lens, picture, card files, the slide show | photo.md | `photo/`, `screen_photo.cpp` | ported ✔; films gap 13; the slide show's music gap (lower list) |
| Replay Theater | lists, play, rename, delete, the Demo tab (13 recordings) | 0x10CA50, 0x42FC98 | `game_replays.cpp`, `load_recorded_race` | ported ✔; the play list empty |
| Saved replays | a full replay where the page offers SAVE_REPLAY | race_flow.md "Saved replays" | our own `.gt4rrep` for every run; the program's stream for Arcade's single race and the license tests | ours by design |
| Options | laps, tire damage, penalty, the gearbox, aids, views, the display's parts, replay mode, the 2P options, vibration, force, steering assist | 0x3BFB10, 0x10F770, 0x10F848, 0x10F880 | `game_settings.cpp` into the race | ported; Cancel gap 4 |
| Boot, title | the check, the name entry, the new game, the logo, the movie, the title | `boot` scripts | `no_card.cpp` | script |
| Prologue | School Mode: the ring, Status, a lesson's page, the grade and targets (0x3C1050), medals, records, presents | prologue.md | `prologue_license.*`, `screen_race.cpp` | ported ✔ (37-prologue-medal); the ghost gap 2 |

## Gran Turismo 3's code and the app's defaults in a Gran Turismo 4 run

The values still Gran Turismo 3's (sound, what players see, Prologue's driving) are `nogt3`'s list
(`~/gt4re-out/lead/briefs/round9/_gt3list.md`) and not repeated here. Rules that still run on the shared engine or on GT3
Revisited's app defaults:

| Path | Where | Gran Turismo 4 | Status |
|---|---|---|---|
| Prologue's step: its own routines with the engine's GT3 ones and GT3's tables where Prologue's do not replace them; an unknown Prologue build falls to GT3's tables | `sim/*prologue*` | Prologue's program | nogt3's list B (`prostep`) |
| The rumble's default, Modern | `input/race_feedback.*` (`gt3r::race::RumbleModel`) | the pad block (0x36CEE8), our "original" choice | GT3 Revisited's default; GT4's is the other choice |
| The music's and effects' 0.8 levels over the game's options | `app.h` Sound | the options alone (115 / 128) | GT3 Revisited's default (`gt4sound`) |
| Looking back from a car the pad does not drive | `screen_race.cpp` (`gt3r::race::look_back_held`) | the port's word 0x43CF90 for the pad's car | engine, watched cars only |
| Prologue's race display rows for modes Prologue lacks | `race_hud_prologue.h` (`gt3r::race::HudRun`) | Prologue's own modes | nogt3's list D |
| The scenery's 0.6 s fade, the frame pacer, the pause when the window loses focus | `gt3r::race::SceneryFade`, `FramePacer`, `focus_loss` | none | ours |
