# Gran Turismo 4 Prologue

Gran Turismo 4 Prologue (Japan, SCPS-15055 v1.01) is a build between Gran Turismo 3 and Gran
Turismo 4: Gran Turismo 4's file names, database files and scripts next to Gran Turismo 3's
cars, roads, drivers' lines and physics tables. This page says what each format is, against the
shared engine's (Gran Turismo 3's) and this repository's (Gran Turismo 4's), and how Prologue's
are read. Code: `src/gt4r/formats/db/specdb_prologue.*`, `src/gt4r/formats/course/runway_gtrw.*`,
`ai_line_course.*`, `src/gt4r/sim/program_prologue.*`, `src/gt4r/race/car_visual_prologue.cpp`,
`src/gt4r/menu/prologue_natives.cpp`, `src/gt4r/app/race_hud_prologue.*`; `gt4tool prologue <disc>
program | survey | drivers | hud | strobe <path>`; tests `tests/test_prologue.cpp`, `tests/test_prologue_menus.cpp`.

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
nothing else of the block, so it is taken under the other name.

**Shadows and lamps.** The body model's extra nodes are the shadows, as on Gran Turismo 3's cars:
node 1 the body's shape thrown along the light, node 6 the shadow under the car, node 5 the
headlamps' light on the road (the shared engine's race takes the same three). They are drawn by
the race view's shadow pass with the course's look; their y = 0 is the road (Gran Turismo 4's
models' is the ride height), so the thrown shape is sheared from the road up. The lamps are the
info block's three lists (`gt3r::race::CarLampSet`). Pictures: `~/gt4re-out/prologue/r4/v1.png`.
❓ Not done: the brake parts (Gran Turismo 3's cars draw them inside the wheel models), the
cars' camera mounts. The menus' 3D car (car faces) takes these files too.

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
Prologue's disc (`exact_bridge`). The controller settings are at 0x504950 (Gran Turismo 4's 0x622110 layout ❓ checked only for the row; the input
phase 0x3DEDE0 reads the steering prefilter row at +0x70); ❓ not located: the surface classes, the soft materials, the gear
kinds, the stick curve, the tire effect kinds and scales (the engine's values serve), the race sound's gains and
microphones.
`gt4tool prologue <disc> program` prints the build's routines, these tables and, for each table
not located, the place in the data most like Gran Turismo 3's values.

## The race step ✔

Prologue's step (0x3B7020, the conductor's car i at conductor + 0x77A8 + 0xC64 i, its parameter block at + 0xC8 + 0xEDC i,
the count at + 0x107E4) has Gran Turismo 4's phases in Gran Turismo 4's order (the same three phases Gran Turismo 3 has not,
0x3BDEC8, 0x3C3C90, 0x3C4400). Its routines are a mix: some are Gran Turismo 3's, some Gran Turismo 4's, some its own.

**The record.** The car record is Gran Turismo 3's without the parameter block: an offset of Gran Turismo 3's record from
+0xEC0 is Prologue's + 0xEAC (the state block F at car + 0xD8, the wheels at car + 0x138 + 0xA4 w, the drive blocks at
car + 0x3C8 + 0x24 w), and from Gran Turismo 3's F + 0x39C on + 0xEA8 (4 bytes inserted after the rolling resistance; the
ground records at car + 0x614 + 0x34 w); then 0x430 bytes more to 0xC64. car + 0x10 points at the parameter block (0xEDC
bytes): Gran Turismo 3's (0xEB0) with each tire record 4 bytes longer (0x1B8; its +0x1B4 .. +0x1B6 the record's kind bytes,
Gran Turismo 4's direct slip ratio, direct lateral speed and x1da), what follows the six records 24 bytes on, and 20 more
bytes (0 in every race seen) before the wear's load terms (Gran Turismo 3's P + 0xE6C, Prologue's + 0xE98). The drive
block's +0x18 / +0x1C hold the steering offset's rate and the offset (Gran Turismo 3's: the steer angle's sine and cosine).

**The spec and the converter.** The spec builder (0x3B3998 with 0x404620) makes Gran Turismo 3's spec layout (0x3E2 bytes):
on the Civic Type R, ours (Gran Turismo 4's spec from the database in Gran Turismo 4's layout, `core_spec`) differs in 36
bytes: the tire blocks' kind bytes +0x6C .. +0x6E (ours has them in Gran Turismo 4's spec), the pitch inertia byte, two aid
bytes, surface rows 10, 11 and 15 of the tire blocks. The converter (0x3C74B8) is Gran Turismo 3's (0x1DBFD8) in another
compiler's arithmetic (most words within a few units in the last place) with two of Gran Turismo 4's rules: each tire
record's base grip times its load curve at the axle's static load per wheel (0x361798's rule), and for a record read at its
sideways speed the slip angle of curve A's peak the axis point of its greatest value (0x353800's). With both
(`prologue::convert`), every word of the block equals the program's within 8 units in the last place but the driveline's
byte +0x13 (3; no reader found), a differential's decel ratio and the front droop.

**The routines** (`gt4r_diff <prologue disc> prologue [course] [car] [steps]` with `GT4R_TAPS=engine | prologue | input`;
the test `disc_prologue_step_routines_equal`): Prologue's step runs in the interpreter (its database's table objects
{image, vtable} at 0x505358 + 0x124 + 4 t, the course's container relocated by 0x3A8058, entries of 0x4000 bytes: +0x20 the
equipment, +0x3FA4 the controller (+0x184 set: a player's; its frame read car + 0x0C → +0xD0 → +0x28), +0x3FA8 the grid
place; the conductor 0x3B5468, its initialisation virtual 25 0x3B6BA0) and each of ours runs in place at every call of the
program's on the record as the program has it, every field compared. On a test drive (Civic Type R, Tsukuba, 1,200 steps):

| Phase | Prologue | Equal at every call |
|---|---|---|
| 1 | 0x3B2480 | the engine's (Gran Turismo 3's) |
| 4 | 0x3BF608 (with the peak force 0x3BF338, slip angles 0x3BF598, aerodynamics 0x3DBB70) | the engine's |
| 5 | 0x3DEDE0 | ❓ not ported: the pad's controls (0x3DE270, Gran Turismo 4's 0x349418 with small changes) ramp the throttle where the engine's take the analog value, and bound the steering otherwise |
| 7 | 0x3B5A20 | the engine's angles; with the steering mode (0x714880: 1 in a race, built by 0x3B57D0 from the static block's +0xB9 .. +0xC0, Gran Turismo 4's rule) Gran Turismo 4's steering offset (0x3F7EA8's) but a wheel without the slip's fade aims at 0; no follower, no compliance steer (`prologue::wheel_steer`) |
| 8 | 0x3BA450 with 0x3BA070 | Gran Turismo 4's slip ratio but the direct one: (hub less rim speed) / the static block's +0xE6 (driving) or +0xE5 (braking) km/h, each falling back to the other, then 50 (`prologue::slip_ratios`) |
| 10 | 0x3BBFE0 | the engine's order and routines (brakes and clutch, tire curves, the class grip, longitudinal force, wheel torques, vectoring, axles, wheel speeds, engine speed, gearbox, the hold); Gran Turismo 4's tire slip force (0x3BB138: its direct lateral speed's scale pi / 2 over the static byte +0xE4 km/h; the mean slip angle into F + 0x420 with the switch 0x506612, which the race's set-up leaves 1) and forward step (0x3BAC98); the lateral forces and yaw (0x3BB5D0) Gran Turismo 4's but the lateral acceleration the sum times P + 0x4C (1 / mass), no yaw follower, no computer driver's grip (`prologue::dynamics`); the rolling resistance and the brake heat (phase 11) the engine's within a unit in the last place (another order of operations) |
| 19 | 0x3B29D0 | the suspension (0x375F58), bump nudge (0x376EC8) the engine's; the ground frame (0x3C4D80) the engine's pitch, roll and height with Gran Turismo 4's axes (0x377310: the pitch before the roll); ❓ the attitude (0x3765F8) not ported (the engine's differs in heave, pitch rate and the velocity) |

Prologue's tables these read, at fixed places (`PrologueProgram`): the static block 0x503208 (Gran Turismo 3's layout: the
slip bytes +0x84, the direct speeds +0xE4 .. +0xE6, the steering offset's +0xB9 .. +0xC0), the camber force's reference and
gain 0x504294 / 0x504298, the wheel layouts 0x503FA8 (Gran Turismo 4's 0x54-byte rows), the surfaces whose offset aims at 0
0x504AC0, the controller settings 0x504950 (Gran Turismo 4's 0x622110; read by the input phase).

What a test drive gives (the program's / ours, km/h): 9.3 / 10.3 at 1 s of throttle (the program ramps the throttle in),
46.3 / 47.7 at 3.5 s in first gear (the engine's whole step before: 45.1 in second; it shifted at 33 km/h with its wheels
spinning), 73.7 / 75.6 at 7.5 s; Impreza WRC 146.7 / 147.3 at 8.5 s.

### What runs where

A session on Prologue's disc (`sim::Session` with `prologue::Tables`, `rules_prologue.*`): the car's spec is Gran Turismo
4's builder on the database in Gran Turismo 4's layout; the parameter block the engine's converter with
`prologue::convert`; the step as the table above. Still the engine's Gran Turismo 3 code where Prologue's differs or is not
compared:

- Phase 5, the driver's input: `ex::driver_input_pad` (Prologue's 0x3DEDE0 with 0x3DE270).
- Phase 19's attitude: `ex::attitude` (Prologue's 0x3765F8); the rest of phase 19 after the bump nudge (rumble, steering
  force, tire effects, engine sound, view follower, speeds, wheel spin): the engine's, not compared.
- Phases 2, 3 and 6 (0x3BDEC8, 0x3C3C90, 0x3C4400), the proximity and slipstream (0x3DC1C8), the speed-switched part
  (0x3B1C58), the wear (0x3BBBB8, 0x3BBD80) and flaps (0x3C26F0): the engine's or none, not compared.
- The start: the engine's pose and standing start, not Prologue's car set-up 0x3C93A0.

## The race display ✅ (Gran Turismo 3's)

Prologue's race display is Gran Turismo 3's, not Gran Turismo 4's (`RaceDisplay`'s layout tables
and `display.gpb` are not on the disc): the pictures `race/<region>/display/display.imgs` (43,
`JP`, `US`, `BR`; Gran Turismo 3's `rev_*` faces renamed `GT4_meter_texture_rev*`, the rank marks,
`meter_mini`, the Prius panel), the fonts of `font/`, and Gran Turismo 3's layout tables
`database/racemode_e3.db` and `racedetail_e3.db` (16 modes; 122 rows differ from Gran Turismo
3's). The race mode puts the place at (32, 69) at 100 %, the lap count at (32, 104), the map at
(16, 127), the best, last and lap times on the right (614, 50 / 78 / 187), the message at (320,
300), the title at (320, 224); the license mode adds the three target times (614, 80 / 110 /
140). `race_hud_prologue.*` draws it with the shared engine's `race::Hud` on these rows
(`gt4tool prologue <disc> hud` lists them beside Gran Turismo 3's). ❓ judged: the place shows
in a run without opponents (the original's solo arcade race shows "1st").

Checked against the original (`reference/14-prologue/20_race_nose_view_*.png`, `21_pause_menu.png`):
the place, lap count and the in-car view's two dials match in kind; the original's time is a
stopwatch line at the top right; the dials' faces differ (the original's show numbers, Gran
Turismo 3's drawing of `rev_*` does not find the renamed faces and draws its own band). The
pause and results are the race movies `menu/pause.strb`, `menu/result.strb`,
`menu/result_license.strb` (Prologue's: TIME, the medal, a three-letter name entry for a new
record; its callbacks `GetTime`, `GetRank`, `GetDefaultName`, `PrizeLevel`, `EXIT_MENU`).
❓ Its pictures 0 and 2 (1024 x 1, 1 x 512) are drawn as a white box; `GetRank` and the name
entry are not answered.

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

**The older layouts.** Prologue's `.mproject` files give a face's states as `cond_active`,
`cond_focus`, `cond_prelight`, `cond_insensitive` (0 either, 2 the bit off, 3 on), read as the
visible condition and mask over bits 1, 2, 4, 8 (a license sign's grey face shows until the test
can be tried). They have no `clip`: a scroll box's select bar shows only what is inside it (the
title's one item of eight, the arcade's Start, the five course pictures); ❓ judged from the
original's pages. Their scripts call `getDeepCopy` (a widget's copy), read a text face's `value`
where it has only its `key` (the car page's formats "%Cfff%d %C222%s/..."), and tint picture faces
with `setColor(color)` (the paint balls).

**MSwitchActor's colours** (the arcade's settings buttons, the license signs): the update
0x2EB848 colours the button's `base`, `text` and `ring` by its state each picture: active base
(0.902, 0.525, 0), idle (0.5, 0.5, 0.5), insensitive (0.451, 0.263, 0) and (0.25, 0.25, 0.25); text
black when active, white idle, grey insensitive; the ring blinking while focused. The colour
objects are made by 0x2EBC48 (at 0x70C218 + 0x10 n); `prologue_switch_colors` reads their four
floats by following that routine's constant loads in the disc's program (the values match the
original's memory on its pre-race page).

**The title** (`prologue`, `SchoolRoot`): the ring of license signs, the SCE line, one item of
the select bar at a time with its arrows (School Mode, Arcade Mode, Save, Load Game, Option,
Replay Theater, Credit). **School Mode**: the item moves the focus into the ring (Status, then
the tests); a test's sign opens its entry popup and asks for `license_race` with the test's
event (`l0p0001`: `demio_sport_03` on `L_20r60r2`, one car, one lap). A license test's race
menus are the project `quick` (`LicenseRoot`: Start, Replay, Guide, Records, Load and Save
Replay, Exit; `MQuickWork.dispGuidePage` 0 opens it there, 1 on the test's guide film).

**Arcade** (`prologue_arcade`): `TopRoot` (Start / Option / Exit over the film), the course page
(the course's film, logo, map and five pictures from the store house), the car page (the 3D car on
the cloud studio, maker and model logos, the spec pills in the region's units: Japan's PS and
kgf.m), the paint balls, the settings popup (Transmission, Driving assist, Tire grip, START), then
`single_race`. Its race is its event's: `gt4p_dirt` when not run against opponents (the option
`run_with_enemies` 0), `RACE` +0x3E the grid (1) and +0x43 the laps (2), which the scripts do not
set. ❓ The opponents' run (`car.race_code`, `_h`, `_p` by difficulty) is not followed.

Pictures (under `~/gt4re-out/prologue/r4/`, not kept): the title ring (`d1.png`), the arcade
pages (`f2`, `f3`, `h4`, `h5`, `i6`), the pre-race page (`m1`), the race with its display
(`m3`, `n1`, `k3`), the pause (`p1`), School Mode's ring (`q1`), the license race (`s3`, `t2`) and
its results (`u1`). Against the reference: the title, arcade, car, paint, settings and pre-race
pages match the original's layout; the pre-race page's camera here is the course's scenic shots
over the field, not the original's shots of the player's car; Japanese words are drawn by
`menu-draw` (the settings' labels and START are blank here). ❓ Not done: the license tests'
target times (the `RACE` rows hold none; not located), the results and presents after a test
(`MPrologueLicense` keeps no result), the dialogs' Japanese text.
