# Gran Turismo 4 Prologue

Gran Turismo 4 Prologue (Japan, SCPS-15055 v1.01) is a build between Gran Turismo 3 and Gran
Turismo 4: Gran Turismo 4's file names, database files and scripts next to Gran Turismo 3's
cars, roads, drivers' lines and physics tables. This page says what each format is, against the
shared engine's (Gran Turismo 3's) and this repository's (Gran Turismo 4's), and how Prologue's
are read. Code: `src/gt4r/formats/db/specdb_prologue.*`, `src/gt4r/formats/course/runway_gtrw.*`,
`ai_line_course.*`, `src/gt4r/sim/program_prologue.*`, `src/gt4r/race/car_visual_prologue.cpp`,
`src/gt4r/menu/prologue_natives.cpp`; `gt4tool prologue <disc> program | survey | drivers`;
tests `tests/test_prologue.cpp`.

## The disc

One layer, one `GT4.VOL` read from its front table (2,592 files; [../FORMATS.md](../FORMATS.md)).
By folder: `sound/` 1,121, `car/` 416, `projects/` 335, `specdb/` 192, `replay/` 122, `bgm/` 70,
`crs/` 62, `narration/` 59, `script/` 46, `tire/` 42, `mpeg/` 31, `rtext/` 24, `config/` 23,
`menu/` 10, `race/` 9; `character/` holds three `Rdl0` driver files.

| What | Prologue | Gran Turismo 3 | Gran Turismo 4 |
|---|---|---|---|
| Car database | `specdb/GT4_PROLOGUE1000/`: 42 `GTDB` tables stored plain, `GTID` labels, `GTST` strings | `paramdb` (`GTAR` of `GTDT`) | 79 `GTDB` tables, packed |
| Courses | `crs/<name>`, 62 files: 46 in the 64-slot table (45 with `MDLS` scenery, `L_newyork4` with `GTM1`), 16 in Gran Turismo 3's 48-slot table with `GTM1` scenery | 48 slots, `GTM1` | 64 slots, `MDLS` |
| Road | `GTRW` (version 9) in slot 28, all 62 | `GTRW` slot 28 | `RNW4` slot 55 |
| Drivers' lines | the course's slot 31 (Gran Turismo 3's line section, version 8): all 62 files | slot 31 | `crs/<name>.ad` |
| Driver sets | the course's slots 37 .. 40, 0x40 bytes each: 57 files (not `mini`, `nurburgring`, the three `ss_` photo stages) | slots 37 .. 40 | `.ad` slots 2 .. 5 |
| Replay cameras | `REP4` slots 44 .. 46, 48 (46 files); `GTRC` slots 34, 41, 42 (62) | `GTRC` slot 34 | `REP4` |
| Cars | `car/lod/<model>`, `car/menu/<model>`: 204 each in Gran Turismo 3's car file (`GTCI`, `GTM1` body, `GTTR` tire picture, `GTTW` wheel); one (`pdi_0001`) `CAR4` with a `.pat`; no `wheel/` folder | the same layout | `CAR4` + `Pat0` |
| Tires | `tire/` 42 `GTTR` | | |
| Engine sounds | `ENGN` in `sound/engine` (307), `normal0..3` (211, 55, 57, 80), `turbo0..3` (108, 86, 89, 120) | `ENGN` | `ENGN` |
| Banks, effects | `INST` (4), `GTSE` (2), `music/jp/bgmmid.{inf,ins,sqt}` ([sound.md](sound.md)) | | |
| Menus | `ADCH` scripts, text layouts, `gpb0` pictures, `RT02` text, Gran Turismo 3's `FT01` fonts ([menu.md](menu.md)) | | the same, newer versions |

`gt4tool prologue <disc> survey` prints these counts from the disc.

## The car database ✅

`specdb/GT4_PROLOGUE1000/` has Gran Turismo 4's files: a `.dbt` table, an `.idi` label list per
table, `japanese_StrDB.sdb`, `american_StrDB.sdb`, `UnistrDB.sdb`, and `RACES/<16 hex digits>`
(the events' opponents). The program's loader is 0x4070E0 (the folder name and `SpecDB_Version`).

**Tables stored plain.** `"GTDB"`, u32 flags (0x80000; 0x80002 on the name and paint tables: bit 0
clear, where Gran Turismo 4's packed tables have it set), u32 rows, u32 row size, then
`{u32 id, u32 offset}[rows]` by id and the rows, each at its offset from the end of that list
(the offsets step by the row size rounded up past the next multiple of 8). Every row of the 42
tables reads (3,857).

**Table ids** are Gran Turismo 4's up to 26 (`REARTIRE`); Prologue has no `NOS`, `SUPERCHARGER`,
`WHEEL` or `WING`, so its 27 .. 38 are Gran Turismo 4's 31 .. 42 (`TIRESIZE` .. `VARIATION`). The
ids in the label lists, in the rows' `{row, table}` pairs and in the opponents' files are
Prologue's; an opponents' file is named by table 31 (`RACES/0000001F<row>`).

**One paint table for every language**: `VARIATION` (1,054 rows x 39), labelled `<model>_<n>`.
`CAR_VARIATION_<language>` has a row per paint of a car, each `{VARIATION row, table 38}`.

**The columns.** They are read where the program's spec builder (0x404620, a helper per part
in the order brake, brake controller, steer, chassis, engine, drivetrain, gear, suspension,
differential, front and rear tire, lightweight, racing modify, the engine parts, ASCC, TCSC)
reads them, and matched with Gran Turismo 4's on the 60 cars both discs have. Gran Turismo 4's
column ← Prologue's:

| Table (Prologue / GT4 bytes) | Columns |
|---|---|
| `BRAKE` (8 / 6), `BRAKECONTROLLER` (13 / 11), `ASCC` (27 / 25), `TCSC` (12 / 10), `CLUTCH` (10 / 8), `LSD` (29 / 27) | every column two bytes on (Prologue's rows start with five bytes the builder does not read, Gran Turismo 4's with three) |
| `STEER` (42 / 38) | four bytes on: the curves at +0x05, +0x11, +0x1D, the lock at +0x29 |
| `CHASSIS` (23 / 37) | +0x00 .. +0x11 the same; GT4 +0x1A .. +0x1E ← +0x12 .. +0x16 (front weight +0x14, yaw byte +0x15, center of gravity +0x16); GT4 +0x12 .. +0x19 (the widths across the tires, the wheel tops) are not in Prologue's table: made from the car's tread (`RACINGMODIFY`) and tire (width across = tread + tire width, wheel top = tire diameter) ❓ judged; GT4 +0x1F .. +0x24 none |
| `ENGINE` (90 / 113) | +0x00 .. +0x17 the texts; GT4 +0x18 .. +0x1D (sound, PS, torque) ← +0x1C; a 16-point curve (GT4: 24) ← +0x22; GT4 +0x4E .. +0x54 (limits, idle, clutch, points) ← +0x42; the curve's speeds ← +0x49 (16); GT4 +0x6F the output factor ← +0x59; GT4 +0x6D, +0x6E none |
| `DRIVETRAIN` (23 / 21) | GT4 +0x00 .. +0x0B ← +0x04; GT4 +0x0E .. +0x14 (drive, four wheel drive, torque split) ← +0x10 |
| `GEAR` (33 / 45) | seven gears ← +0x04 (GT4: eleven); reverse and the final drive's range ← +0x12; GT4 +0x22 .. +0x28 (kind, count, slider) ← +0x1A; no second reduction |
| `SUSPENSION` (112 / 111) | the ride heights ← +0x04; GT4 +0x0C .. +0x6D ← +0x0E; GT4 +0x6E none |
| `RACINGMODIFY` (52 / 29) | GT4 +0x00 .. +0x17 ← +0x1C (Prologue's first 0x1C bytes are not read by its builder); no part that changes with speed |
| `TURBINEKIT` (18), `TIRESIZE` (4), `TIRECOMPOUND` (90), `DEFAULT_PARTS` (208), `DEFAULT_PARAM` (37), `ENEMY_CARS` (24) | the same columns (`TIRESIZE` +0x02: the rim in inches, which Gran Turismo 4's readers take below 61) |
| `MUFFLER` (9 / 7) | GT4 +0x00 .. +0x03 ← +0x04; the sound's variant +0x06 ← +0x08 |
| `FRONTTIRE` / `REARTIRE` (61 / 59, 57) | the seven parts as they are; GT4 front +0x38 (price, u16) ← +0x38, the kind ← +0x3C |
| `TIREFORCEVOL` (13 / 18) | surfaces 0 .. 9 the same; the wet variants of 0, 1, 9 (Gran Turismo 3's surfaces 12, 13, 14) at GT4's +0x0A, +0x0C, +0x0B; none of 10 .. 14 (the builder's default) |
| `GENERIC_CAR` (13 / 24) | the car's parts as sold (+0x00); nothing of its price, year, makers and kinds |
| `CAR_NAME_<language>` (8 / 16) | the name (GT4 +0x00, +0x04, +0x08), the short name (+0x0C ← +0x04) |
| `VARIATION` (39 / 52) | +0x00 .. +0x26: the texts, the body's box, the color |
| `COURSE` (9 / 22) | +0x00 .. +0x07 the texts; the surface GT4 +0x10 ← +0x08 |
| `RACE` (98 / 151) | the course {row, table}; the grid GT4 +0x3E ← +0x22 and the computer drivers' columns GT4 +0x4B .. +0x73 ← +0x31 .. +0x57, +0x5A, +0x5B, where the race's copy of a row reads them (GT4 0x35E108, Prologue 0x3C5978); the laps GT4 +0x43 ← +0x25 ❓ (by the data: 2 on every championship race, 1 or 3 on the tests); ❓ the rest |

Values that both discs have but changed between them (Prologue's are kept): brake torques, the
yaw byte (Prologue's is 1.4 to 1.7 times Gran Turismo 4's), the suspension's travel, bump stops
and unsprung mass, some gear ratios.

**What it gives.** 83 cars, every one named (Japanese and American), with its model on the disc
and a car spec that builds; every engine's sound file is on the disc; 64 courses; 105 events
(46 license tests `l0p0001` .. `l0p0046`, the `gt4p_*` races) with their opponents.

## Courses ✅

The 64-slot files have Gran Turismo 4's slot roles where they have the sections (scenery `MDLS`,
`Vls0`, `GTFX`, `BLBd`, `Env0`, `GTCM`, `CRSS`, `REP4`, `GDGT` in slot 47); the 48-slot files are
Gran Turismo 3's courses. On every Prologue course slot 28 is the road (`GTRW`), 31 the drivers'
lines, 37 .. 40 the driver sets, as on Gran Turismo 3's: Gran Turismo 4's lamp glow meshes and
effect pictures (its slots 37 .. 40) are not there.

**The road** reads with the shared engine's `GTRW` reader and is presented as a `Runway`
(`runway_from_gtrw`): the segments' vertices and triangles in one list (at most 60,135
vertices and 55,147 triangles, Laguna Seca and the Nürburgring), the material as the
triangle's material and the flags (0x01 pit lane, 0x02 wet) as its flags byte, as in `RNW4`;
the vertices' two light values (u16 / 65535) as its bytes / 255 and / 127; the checkpoint lines
with left and right as a driver sees them (right less left is (dir z, -dir x) when the travel is
-dir), at the road's height; the start grid with the heading as Gran Turismo 4's (Gran Turismo 3's
yaw: the direction (sin, cos) of it); the wall points as they are (the last point's tag is
-count, which the wall reader closes back to the first). A lookup tree is made over the
triangles: x and z split by turns at the median of the triangles' centres, a leaf listing every
triangle whose box meets it, about 32 a leaf.

Checked (`tests/test_prologue.cpp`, `gt4tool road`): all 62 roads read; all 287 grid places
are over the road; every triangle is found in its cell; the first place faces along the
checkpoints on 55 courses (the three gymkhanas' and Laguna Seca's face against their order, Las
Vegas's lies before the first checkpoint, two photo stages have 3 and 8 checkpoints).

## The drivers ✅

No `.ad` files: the computer drivers read the course file's slot 31 (Gran Turismo 3's line
section, the older layout Gran Turismo 4's reader already takes from 17 of its `.ad` files) and
slots 37 .. 40 (0x40 bytes; Gran Turismo 4's sets begin with the same 27 bytes, its later bytes
are 0 here). All 62 files have lines, 57 the four sets (`gt4tool prologue <disc> drivers`); Laguna Seca, the photo
stages and `test2` carry another road's lines (the same counts as other files), as some of the
Concept discs' development courses do.

## Cars ✅ (race view)

A Gran Turismo 3 car file is decoded by the shared engine's car reader and cut into the race
view's pieces: the body (lamps unlit and lit), and each wheel with its tire moved back into the
wheel's own frame (units of the rim's radius, a left wheel with its face at x = 0), so they turn
and steer as Gran Turismo 4's. The paint is the body's paint variant (`VARIATION` +0x04 less 1).
One car (`toyt0030`, the `mtr_gmv_04`) names its info block `CIF0` for `GTCI`; the reader reads
nothing else of the block, so it is taken under the other name. ❓ Not done: the shadows, the
lamps' glows and the brake parts of these cars; the cars' camera mounts.

## The program ✅ recognized, 🔎 tables

`CORE.GT4`: load 0x100000, entry 0x106A98, 4,901,192 bytes; 13,831 functions, 510 classes with
run-time type names (`analyze.py --text-end 0x4FC12C`). Gran Turismo 4's `sim::Program` does not
take it; the build is recognized by the hashes of five routines (`PrologueProgram`): the spec
builder 0x404620 and two of its helpers, the database's loader 0x4070E0, a part's row by id
0x407520. Its car spec is Gran Turismo 3's layout: the builder starts the engine factors at +0xA4
(1000) and +0xA6 (100); the chassis helper writes the length +0x1A, wheelbase +0x24, weight
+0x2A, front weight +0x2C, yaw +0x2E, center of gravity +0x31, grips +0x36 / +0x37 (+124).

The shared engine's locator finds none of Gran Turismo 3's routines in it (another compiler).
Its own tables, at fixed places in the recognized build, each addressed by the routine named:

| Table (Gran Turismo 3's) | Prologue | Addressed by | Bytes equal to GT3's |
|---|---|---|---|
| the static physics block (0x120) | 0x503208 | 26 routines (0x3764E0, 0x3B3748, ...) | 221 of 288 |
| the AI driver parameter sets (0x60) | 0x5065B0 | 0x427424 | 93 of 96 |
| the AI line loading order | 0x5066E8 | 0x429340 | 8 of 8 |
| the spec's lock curve default | 0x571C78 | 0x3B3CA0 | 16 of 16 |
| the spec's tire curve B default | 0x503E18 | 0x3B3E9C | 14 of 14 |
| the spec's surface classes | 0x571C88 | 0x3B3DDC | 16 of 16 |
| the differential type letters | 0x504AD0 | 0x3DF0E0 | 8 of 8 |
| the machine test sectors | 0x503E68 | 0x3B511C | 16 of 16 |
| the tire effect parameters | 0x503EA0 | 0x3B98A8 | 87 of 88 |

They replace Gran Turismo 3's in the shared engine's race core when a session runs on
Prologue's disc (`exact_bridge`). ❓ Not located: the controller settings, the surface classes,
the soft materials, the gear kinds, the stick curve, the tire effect kinds and scales (the
engine's values serve), the race display's places, the race sound's gains and microphones.
`gt4tool prologue <disc> program` prints the build's routines, these tables and, for each table
not located, the place in the data most like Gran Turismo 3's values.

**What runs.** A session on Prologue's disc runs Gran Turismo 4's spec builder on the database
in Gran Turismo 4's layout and the shared engine's per-step rules (`known()` is false, so not
Gran Turismo 4's own), with Prologue's tables above. A field of computer drivers laps Tsukuba
(Subaru Impreza rally car 1'14 .. 1'16, Civic Type R 1'14 .. 1'18 on two-car runs); ❓ Prologue's
own per-step rules are not compared with the program (the harness runs Gran Turismo 4's
routines only).

## The menus ✅ boot to a race

Power on: `memcard` (the card's warning: with no saved game, "No" goes on), the opening movie
(`prologue_opening`; Cancel skips it), the title page `prologue` (`SchoolRoot`: the School
Mode's signs), a license test's entry and the race. What Prologue's scripts ask of the program
that Gran Turismo 4's do not:

- A project's first page is its `getStartPage(arg)` (the argument the project was started with,
  `main::game.next_menu_arg`): `memcard` chooses Warning, GameSave, GameLoad, AutoSave or the file
  list; `prologue` the school, a present's page or the status.
- `MScriptWatcher(context, module, "name")`: the function by its module and name.
- Arrays' `bsearch(value)`: the index in the sorted array, -1 when absent.
- `main::game.license`: `getUnit(number | race code)` a unit per license test (the code by the
  scripts' own `main::prologue::getLicenseID`; the main script writes each unit's `category`),
  with `isPassed`, `getBestTime`, `getRankNum`, `getPassCode`; `isPassedCategory`,
  `isCompletePassedCategory`, `getDetailCount`, `getTotalPoint`, `forcePassedAll`. The results
  are a new game's (nothing passed). ❓ A test's result kept and saved.
- A license test's car is the first of its event's opponents (`demio_sport_03` for `l0p0001` on
  `L_20r60r2`), its grid the event's (1).

Pictures (under `~/gt4re-out/prologue/shots/`, not kept): the card's dialog, the opening movie,
the title page, the license test on the Driving Park's beginner course, races on Tsukuba and
Trial Mountain. ❓ Not done: the dialogs' Japanese text (the pages' message text is not drawn),
the race display (a plain read-out: Prologue's race display tables are not located), the
arcade mode's flow, the results and the presents after a test.
