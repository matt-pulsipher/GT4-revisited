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
| `RACE` (98 / 151) | the course {row, table}; the grid GT4 +0x3E ← +0x22 and the computer drivers' columns GT4 +0x4B .. +0x73 ← +0x31 .. +0x57, +0x5A, +0x5B, where the race's copy of a row reads them (GT4 0x35E108, Prologue 0x3C5978); the laps GT4 +0x43 ← +0x29; a race's and test's rules GT4 +0x44 (the kind) ← +0x2A, +0x75 (cones) ← +0x5B, +0x86 ← +0x12, +0x87 (time limit) ← +0x14, +0x88 (a hit's penalty in seconds) ← +0x1A, +0x8A (wall limit) ← +0x16, +0x8B (car limit) ← +0x18 (u16, their low bytes); the gold, silver and bronze times GT4 +0x76 .. +0x7B ← +0x5C .. +0x61 (minutes, seconds) and GT4 +0x1A .. +0x1F ← +0x0C .. +0x11 (u16 ms), as 0x409B70 / 0x409BB0 / 0x409BF0 read them (all 0: none; on the license tests +0x5A is the test's number); ❓ the rest |

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

**The spec and the converter.** The spec builder (0x3B3998 over 0x404620, Gran Turismo 3's builder and layout, 0x3E2 bytes)
and the converter (0x3C74B8) are ported whole (`convert_prologue.*`); `GT4R_CONVERT=1 gt4r_diff <disc> prologue` runs the
program's builder and converter in the interpreter on all 83 cars of its database and ours on the same: every spec byte and
every word of the parameter block equal (test `disc_prologue_converter_equal`).

- *The spec* (`prologue::core_spec`): ours is Gran Turismo 4's builder on the database in Gran Turismo 4's layout, read into
  the core's layout (`core_spec`), with the inner builder's own bytes where they differ: no byte +0x0F, the pitch and roll
  inertia bytes +0x2F / +0x30 as the chassis row has them, the gearbox's kind +0xE1 (our spec's +0x158), no controller bytes
  +0x112 / +0x113, each tire block's surfaces 10, 11 and 15 empty and its kind bytes +0x6C .. +0x6E. Then the wrapper's
  rules: +0x10 = 2, +0x2D = 0; the special cars by their labels (the database's label index, its tables 0, 0x1E, 0x1F, 0x22
  listed at 0x573D00: `sl500_02` / `sl55_amg_02` kind 7, `pod_tmv_01` 1, `r35_gtr_tmv_01` 2, `mtr_gmv_04` 8 with lock byte
  0x26 and, on a dirt course, its ride heights + 100 and its springs and stops); the aerodynamic devices (+0x0A .. +0x0E:
  kind, two speeds, a rate, a downforce) of `r35_gtr_tmv_01`, `crossfire_dmv_02` / `crossfire_04`, `cien_dmv_02`,
  `w12_tmv_01`, `hsr2_concept_89` (none of them in Prologue's database); the overhangs +0x1C / +0x1E half the length when 0;
  the pitch inertia times (100 + the static block's signed byte +9) / 100 (+35 on the disc); the second and third steering
  curves' defaults (0x571C78 / 0x571C80) when empty; +0x112 / +0x113 100 / 50 when 0; +0x110 0 when +0x117 is 100;
  +0x12 = 1; the surface classes from 0x571C88; the front block's surfaces 12, 13, 14 from 0, 1, 9 when empty; each block's
  curve range 45 degrees and curve B (0x503E18 / 0x503E20) when empty and its empty surfaces from the front block's.
- *The converter*: Gran Turismo 3's (0x1DBFD8) in its structure with the later builds' constants (folded toward zero) and
  soft doubles (rounding toward zero) and these of its own: the block's +0x30 the wheel layout's row of the special kind
  (spec +0x11: 4 → 1, 5 → 2, 6 → 3), +0x32 the kind, +0x33 the special table's row (spec +0x2D, or 1 when 0 and the front
  tire's grip byte is 180 or more); a special car of kind 3 or 4 its wheelbase, center of gravity, tire sizes and steer
  coupling from the program's table 0x5040F8 (0x78 bytes a row); each tire record's kind bytes (+0x1B4 .. +0x1B6) and, for one
  read at its sideways speed, the slip angle of curve A's peak the axis point of its greatest value (0x3BC218); with the
  static block's +0xDA set (it is) each tire record's base grip times its load curve at the axle's static load per wheel,
  after the mass (Gran Turismo 4's rule); the sprung mass less each axle's unsprung mass times its wheels in the layout; the
  aerodynamic device's words +0xE84 .. +0xE97; the driveline's layouts 9 (four wheel drive system 6) and 10 (system 8),
  system 3's gain and capacity from 0x504AD8 / 0x504AD9, the differentials opened by the wheel layout's byte +8 + w, the
  gearbox kinds of +0xE1 (1: stepless, the launch at idle; 3: one gear, the top speed 50 m/s), the first shift's speed
  halfway to the launch speed with the static block's +0xDE, the up-shift speed's class (+0x13: 3 from 5,000 rpm, 2 from
  4,500, else 1, 0x3CBA70); the axles' spring places by their wheels' count; the braking constant by the front wheels'
  count. The race's start's values it reads (the air's density 0x504A74, the rolling curve 0x504428 and its paved factor
  0x504438) are made from the static block as 0x3DBA58 / 0x3C5BC0 make them (equal to the program's).
- *The car's set-up* (0x3C93A0): an entry with the stability control off (+0x3FB8) clears the spec's switches +0x109 but
  for bits 6 and 7 while 0x504B04 is set, one with the traction control off (+0x3FBC) its +0x110 (`prologue::build_car`).

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
| 5 | 0x3DEDE0 | Prologue's (`prologue::driver_input_pad`): the input phase's steering prefilter (row 0 of the controller settings' +0x70 rows; kind 6 rows 1 / 2), the pad path 0x3DED50 with 0x3DE270: the engine's pedals, reverse, shift and clutch with Prologue's eased pedals, and its steering 0x3DDC98 (`prologue::pad_steering`): the lock curve by speed (a pad: curve B, C on a loose surface: the steered wheels' least class of 0x504AC0, 2 counted as 0; a wheel: A, C on a dirt course), the steering wheel's least lock, the steered wheels' mean slip direction's extension, the full lock; the follower 0x3DDA10 (Gran Turismo 4's 0x3485D0) with its own gains and limits by rate class and mode, a pull toward the mean slip direction, limits moving with the speed; the HUD percentage. Its settings are the controls' set-up 0x3DD068 (Gran Turismo 4's 0x347A00, `prologue::controls`) on the controller settings' bytes. 1,200 of 1,200 calls equal (Civic, Impreza) |
| 7 | 0x3B5A20 | the engine's angles; with the steering mode (0x714880: 1 in a race, built by 0x3B57D0 from the static block's +0xB9 .. +0xC0, Gran Turismo 4's rule) Gran Turismo 4's steering offset (0x3F7EA8's) but a wheel without the slip's fade aims at 0; no follower, no compliance steer (`prologue::wheel_steer`) |
| 8 | 0x3BA450 with 0x3BA070 | Gran Turismo 4's slip ratio but the direct one: (hub less rim speed) / the static block's +0xE6 (driving) or +0xE5 (braking) km/h, each falling back to the other, then 50 (`prologue::slip_ratios`) |
| 10 | 0x3BBFE0 | the engine's order and routines (brakes and clutch, tire curves, the class grip, longitudinal force, wheel torques, vectoring, axles, wheel speeds, engine speed, gearbox, the hold); Gran Turismo 4's tire slip force (0x3BB138: its direct lateral speed's scale pi / 2 over the static byte +0xE4 km/h; the mean slip angle into F + 0x420 with the switch 0x506612, which the race's set-up leaves 1) and forward step (0x3BAC98); the lateral forces and yaw (0x3BB5D0) Gran Turismo 4's but the lateral acceleration the sum times P + 0x4C (1 / mass), no yaw follower, no computer driver's grip (`prologue::dynamics`); the rolling resistance and the brake heat (phase 11) the engine's within a unit in the last place (another order of operations) |
| 19 | 0x3B29D0 | Prologue's ground contact 0x3754D8 (`prologue::ground_contact`: the query points along the body's up axis by the hub's rest drop, Gran Turismo 3's road query 0x3882D0 = 0x18FB58 on the course's GTRW road (the engine's `CourseGround`, bit-exact), its own bump tables), terrain 0x375D00 (Gran Turismo 4's dot product) with its places 0x375A38 (one terrain factor for every wheel, the parameter block's +0x28C tire drop), the suspension (0x375F58) the engine's, its attitude 0x3765F8 (Gran Turismo 4's 0x36B0C8 but every damper counted, the sums times P +0x50, each wheel's whole load onto the ground's up axis, no speed part's lift; its pitch terms 0x376440 at the tire's radius by the static byte +0xD9; the start's hold 0x376330 the engine's with Gran Turismo 4's axes), its bump nudge 0x376EC8 (Gran Turismo 4's 0x36BA88 on these places), the ground frame (0x3C4D80) the engine's pitch, roll and height with Gran Turismo 4's axes; the hub offsets, speed ramp, speeds and wheel spin the engine's. The whole phase as the session runs it equal at every call but the effects' fields (below) |

| 9 | 0x3E0838 | Prologue's (`aids_prologue.*`): the engine's indicators (0x3E02A0 = the engine's 0x1E3708), its own traction control 0x3E1960 (no clutch test, class factor, speed raise or follower; the target from curve D's rising side with the static block's +0xE3), brakes 0x3E1540 (the ABS target blended toward curve D's falling side at the wheel's curve A byte by the static block's +0xE1 + axle, 100 % on the rear on the disc; the dirt cut by its +0xB5 .. +0xB7) and stability control 0x3E11F8 (Gran Turismo 4's 0x3F94C0 without the throttle cut, its multiplier's operands in another order: 1 x scale and scale x 1 differ on the PlayStation 2), Gran Turismo 4's brake shares 0x3E1008 / 0x3E1078, the speed caps 0x3E0670. 1,200 of 1,200 calls equal with the entry's aids on (the engine's, Gran Turismo 3's 0x1E3B18: 1,197) |
| 13 | 0x3B6FB8 with 0x41CA90 | Gran Turismo 3's collision step (0x1FA8F8 in another layout): the engine's port (`collision_exact`) on the GTRW road's walls, grid and links (`CourseCollision`), `collide_prologue.*`. The test drive's wall touch at step 862 equal to the program's |
| 8, 11 | 0x3B3810, 0x3BBF18 | the engine's brake heat and rolling resistance on Prologue's own race-start constants (0x3B3740's brake heat constants, 0x3C5BC0's rolling curve: the later builds' roundings, 0x46191FFF, 0x3DCCCCCC, 0x3E8E38E3, 0x3A83126E): equal at every call |
| 20, 21 | 0x3BBBB8, 0x3BBD80 | the engine's tire wear and wear flags: equal at every call |
| 14 | 0x3DC1C8 | the cars' pairs (slipstream): the engine's (Gran Turismo 3's) on Prologue's tables; `gt4r_diff <disc> prologue-near <course> <cars> <steps>` and the test `disc_prologue_pairs_pass_equal`: 6,000 of 6,000 car records equal but the drivers' zone cells and contact, which only a computer driver reads |
| 19 | 0x3C5740 | the speeds' bookkeeping (`prologue::speeds`, `feel_prologue.*`): the speeds along the ground's frame and the hubs' velocities by the wheel layout (Gran Turismo 4's 0x35D920 / 0x35D6B0 with Prologue's lean and roll switches), the odometer, the engine's condition by the step's distance, the top speed, the speedometer at car + 0xC60 (111.1 m/s a second), the free wheels. 1,200 of 1,200 equal |
| 19 | 0x3B4238, 0x3B9020, 0x3CB1E0, 0x3CE1A8 | the view's follower (its own gains 0x571C98 / 0x571CA0; its block in the conductor + 0xDB48), the engine sound's levels (scales of the effects block 0x504B08 + 0x1E0), the tire effects (Gran Turismo 4's 0x367148 on Prologue's surfaces and its effects block) and the pad's vibration (its block in the conductor + 0xDAC8): `prologue::view_follow`, `engine_sound`, `tire_effects`, `pad_rumble`, each 1,200 of 1,200 equal |

Prologue's tables these read, at fixed places (`PrologueProgram`): the static block 0x503208 (Gran Turismo 3's layout: the
slip bytes +0x84, the direct speeds +0xE4 .. +0xE6, the steering offset's +0xB9 .. +0xC0), the camber force's reference and
gain 0x504294 / 0x504298, the wheel layouts 0x503FA8 (Gran Turismo 4's 0x54-byte rows), the surfaces whose offset aims at 0
0x504AC0, the controller settings 0x504950 (Gran Turismo 4's 0x622110; read by the input phase).

What a test drive gives (the program's / ours, km/h): 9.3 / 9.6 at 1 s of throttle,
46.3 / 47.7 at 3.5 s in first gear (the engine's whole step before: 45.1 in second; it shifted at 33 km/h with its wheels
spinning), 73.7 / 75.6 at 7.5 s; Impreza WRC 146.7 / 147.3 at 8.5 s.

### What runs where

A session on Prologue's disc (`sim::Session` with `prologue::Tables`, `rules_prologue.*`): the car's spec Prologue's
(`prologue::core_spec` on Gran Turismo 4's builder), the parameter block Prologue's converter (`convert_prologue.*`), the step
as the table above, the collision step Gran Turismo 3's on the GTRW road (`collide_prologue.*`), the aids Prologue's
(`aids_prologue.*`). `gt4r_diff <disc> prologue` with `GT4R_WHOLESTEP=1` loads the program's record into the session every
step and compares the step's end (`=front`: phases 1 .. 11 alone; `GT4R_VS19`, `GT4R_AFTER19`, `GT4R_GROUND_CHECK`,
`GT4R_SHOW_COLLIDE`): on the test drive (Civic Type R, Tsukuba, 1,200 steps, aids on) every physics field equal on every
step of 2,400 (two touches of a wall in them; Tsukuba with the Civic, New York with the Impreza WRC; the test
`disc_prologue_whole_step_equal`). (The octagon of the collision step cuts its corners by 0x3E4CCCCC of the width, 0x41C8E0: the
engine's builder has it as its later builds' constant.) `GT4R_START=1` with it compares the car's set-up: on `L_20r60r2` every
field of the pose equal, on Tsukuba within a unit in the last place of the road's height (the set-up's single query 0x3881F8;
❓ not followed) and what follows from it. A plain run from the start follows the program's to the unit of rpm through step 840.

- **The car's set-up** (0x3C93A0, `Session::place`): the place's heading turned by 0x40490FDA (0x388138), the body's front on the
  place, the pose 0x3C89D8 (the height under the place, Gran Turismo 4's axes, the wheels' points on the GTRW road, their loads at
  rest, Prologue's ground frame, the pitch of the ride heights), the view's lags at rest (0x3B41E8), the standing start 0x3C8E10
  (the engine's but F + 0x420 and the steering force's byte at 0). ❓ A rolling start's set-up is the engine's
  moving start on that pose (the wheels' and engine's speeds for the road speed).
- **The flaps** (0x3C26F0, phase 22): the arithmetic and constants are the engine's Gran Turismo 3 ones (compared routine by
  routine); they differ in the whole step by the state of the shared random stream, which other parts of the program draw from.

Still not run:

- Phase 15 (0x3B1C58, Gran Turismo 4's 0x3F28B8): the aerodynamic devices that move with speed (the parameter block's kind
  +0xE84 1, 2 or 4; states F + 0x514 / 0x518, flags F + 0x51C / 0x51D, speeds P + 0xE88 / 0xE8C, rate P + 0xE90): no car of the
  disc has one.
- The steering force 0x3BC7F0 (Gran Turismo 4's 0x353EE8 "larger change"; a wheel's force feedback): Gran Turismo 3's runs.
- Phases 2, 3 and 6 (0x3BDEC8, 0x3C3C90, 0x3C4400: a special car's part 7 and the special car kind 1's moods): not run
  (the special kinds 1 and 2 are `pod_tmv_01` and `r35_gtr_tmv_01`, not in Prologue's database).
- The computer drivers' fields of the player's car (`ai.*`, the gear hint, laps): the program runs its line planner for every
  car; ours not for a player's.

## The license tests

- **Where a test ends**: on a road whose goal distance (the GTRW header +0x1C) lies inside the lap and that has no
  stopping box, where the lap's progress crosses it after the line (`judge_goal`, screen_race.cpp; Gran Turismo 3's
  section test): test 2 on `L_fisco6` ends 203.1 m from the line (its gold 0'12.180).
- **Failing** (`judge_license`): Prologue's judge is Gran Turismo 4's (DynamicsConductorLicense, 0x3C1318 = 0x3F8D70) on its own
  rows: a wall hit whose change of speed is past the row's +0x16 (10 m/s on most tests, 20 on two), a car hit past +0x18, a cone
  (counted where the test has cones, +0x5B), the time limit, and every wheel off the course where the course counts it (not on a
  dirt or snow course): the program's table 0x503E28, materials 2, 3, 4 and 6. ✔ The original's lesson 1 off the course: fails at
  once, "失格です", no time, retry / EXIT. ❓ The pit lane's rule (only inside the course's pit zone, 0x42A670) is not applied. The
  clock runs from the start signal whether the car moves or not (the original's too).
- **A race's rules** (the race's copy of its row 0x3C5978 into the conductor's rule block + 0x10780; `sim/race_rules.*`): every
  `gt4p_*` race gives a **penalty** for a hit at the car's front (DynamicsConductor's virtual 45, 0x3C6078): a wall hit past 20,
  10 or 5 m/s (the row as it is, `_h`, `_p`) or a car hit past strength 1 caps the car at 50 km/h (the block's +0x4B, a constant
  of 0x3C5978) for 10 seconds (the counter F + 0x51E, counted down by the aids' phase 0x3C5FE8); the display counts it (0x38A7B8,
  the words `RaceDisplay::Penalty`). Test `disc_prologue_race_rules`. ❓ Our collision step keeps a step's summed impulse, not
  each point's, so two points hitting in one step are judged together. Gran Turismo 4's rows have the same rule (5 seconds in the
  rally events and missions), run by its own judge in the session.
- **A test's own changes**: 0x3C5978 hands the test's number (+0x5A) to 0x3E1C20, a switch (0x572CC0) that for test 1 writes the
  cars' speed cap +0x42 = 50 km/h (✔ the original's lesson 1 tops at 50), for tests 26 .. 35 the other cars' starts (+0x4F .. the
  speeds, +0x56 .. the distances, below), for test 36 +0x45 = 100 ❓. `sim::race_rules` reads the cases' constants from the code.
- **The test's car** is entry 0 of its event's pool with that entry's parts, settings and paint (`lcns_pt_*`, `lcns_pr_*`: lesson 2's
  Fit pulls to 52 km/h in first, the car as sold to 50; ✔ its speeds against the original's lesson 2 within 1 km/h over 4.5 s).
  Gran Turismo 4's tests the same: the files of its demonstrations hold entry 0's equipment for their car (test
  `disc_license_car_is_the_pools`: 47 of 53 equal in every part and setting; the rest list the pace car first, or another region's
  car).
- **Rolling starts**: a test with a start speed (RACE +0x27, km/h) and a start distance (+0x1E, metres, below 0 behind the line; the
  rule block's +0x4E and +0x54) sets its cars moving at that speed and distance, each driven by the computer until it takes the line
  (the start kind 2, the driver kind F + 0x538 = 1 from 0x3B3FD0; 0x3B4C88 gives the car back). 25 tests (8 .. 12, 14, 15, 17, 18,
  21, 22, 24, 26 .. 35, 41 .. 43; test `disc_prologue_test_starts`). Tests 26 .. 35 have one to five other cars, the pool's entries
  from 1 in order, placed by the switch: behind the line, or ahead of it (up to 1,950 m; those cars go at once, their crossings' count
  1). The clock starts as the player's car takes the line (0x3B4C88 writes the race's clock into F + 0x534, and the test's time is
  counted from it). ❓ The pace the computer keeps until the line is taken as the start's speed.
- **The cones** (the lessons' gadgets): the road's records (GTRW +0x50 / +0x54, the records of Gran Turismo 4's RNW4) with the kinds of
  `GDGT` (slot 47): `L_fisco6` has 64 of three kinds. The bodies step as Gran Turismo 4's (0x39B368, 0x39B608 .. 0x39D758) but no
  kind stays fixed (flag bit 1) and a wall's impulse has no check of the energy (0x39CFB8). A cone touched ends a test that counts
  none: ✔ the original's lesson 2 driven straight on ends as the car passes the blue cones after 0'04.516, ours at 0'04.583.
- **After the test** the camera circles the car (CameraSys::CameraFinishType1, 0x3D4018: half a degree a step, 4 m away, 30 degrees
  up, a lens of 65 degrees: 0x504674 .. 0x50467C) and the car brakes to a stand (✔ the original's; ❓ its routine not read). The race
  display fades out; with a medal its three target times stay. ✔ (`reference/37-prologue-medal`)
- **The medal**: gold and silver by their times, bronze by its time or, where the test has none, for any finish (✔ the original's
  lesson 1: 1'49.301 past the silver 1'48.000 is bronze). The emblem `race/trophy.mdl` (a `GTM1` with its own reflection picture, the
  gold, silver and bronze sets by 0x572B68) turns over the whole picture where the movie places its `BRONZ` (0x3D99C0; its motion
  0x3DA130 / 0x3DA310 is Gran Turismo 4's). ❓ The movie's words are under it here where they cross it.
- **The laps**: the RACE row's +0x29 (the race's getter 0x37CFD0 reads it through the race information's row pointer +0x28):
  2 on lesson 1 (`l0p0001`, "Lap 1/2" in the original), 1 on lesson 2, 2, 3 or 5 on the championship races by their
  variant; it is Gran Turismo 4's column +0x43 in our database (the byte +0x25 read before is not the laps). A test whose
  road's goal lies inside the lap ends there on its last lap. 
- **Where the crossings start** (0x3C9790 .. 0x3C9860): the sector of the car's start place among the road's split points (GTRW
  +0x22 / +0x24: 8 bytes each, u16 type 0 the start, 1 the goal, 2 a split point, u16 its list, f32 the distance; 0x415FE0,
  0x416028): the last sector 0, else 1, as Gran Turismo 4's; so a grid past the line (Fuji) runs its laps and no more.
- **The results movie** (`menu/result_license.strb`, ResultLicense 0x3D95D8): GetTime the time as 0x409D88 writes it
  (M'SS.mmm), PrizeLevel (the result kind 1, 2 → 0, 3 → 1, 4, 5 → 2, else 3), GetRank the run's place in the test's
  ranking (0x3FE3B0: the first of ten entries that is empty or slower; -1: no name entry), GetDefaultName the user
  profile's name (0x402428, +0x1133C; empty before any entry: the page then takes one letter at a time, each from A; else the three
  letters stand with the cursor on the last), NameEntry the name into the run's record (0x37B378) and the profile (0x402438). Its
  script keeps the 41 letters in an array, hides the letters not reached (`_visible`) and slides the cursor (`_x`); Down is the next
  letter, accept the next box, cancel one back; the third letter enters the name and the page goes back to the lesson's. ✔ The ranking keeps each run with its name at its place.
- **The record** (`menu/prologue_license.*`): the 48 units in the program's layout (a unit of 0x2E8 bytes, 0x3FDFC0: +0x00 the
  category, an index into the names at 0x504FD0 (none, B, A, IB, IA, S, D, GREEN, ORANGE, BLUE, RED, PURPLE, COFFEE); +0x04 the
  medal, an index into 0x505008's (none, bronze, silver, gold); +0x08 the most points; from +0x10 ten entries of 0x48 bytes in
  time order; +0x2E0 the unit's number, not saved). An entry (0x3FDF80 empties one): +0x00 the name (16 bytes), +0x10 the time
  (359,999,999 none), +0x18 the date (u64 seconds from Julian day 0 at noon: 0x4762A8 adds 2,451,544.5 days of seconds to the
  clock's seconds from 2000-01-01), +0x20 the run's points, +0x24 the pass code (32 bytes, "--------" none). The license race's
  end (0x19ED58): result kinds 2, 3, 4 keep medal 3, 2, 1 (0x3FE1A0, the best); a run with a time goes to 0x3FE430: the most
  points kept, the entry at the time's place (0x3FE3B0), the slower moved down (0x3FE250). The unit's natives (0x1582C0):
  `isPassed` the medal not 0 (0x153640), `isCompletePassed` gold, `getBestTime(k)` / `getDate(k)` / `getEntryName(k)` /
  `getPoint(k)` entry k's (text by 0x409D88 / 0x3FDED8), `getMaxPoint` the unit's +8, `getPassCode(k[, part])` the code or its
  eight characters from part x 8 (0x3FE710), `category` / `detail` by the names; `forcePassedAll` does nothing (0x3FEF38).
  The saved game takes the units' first 0x2E0 bytes (`prologue_license_units` / `prologue_license_load`; the file is gt-mode's,
  `save/prologue_data.*`).

## The pass codes

A ranking entry's 30 digits (`menu/prologue_passcode.*`), made by 0x19EC08 over 0x1A31A0 from ten fields: the record's kind (5
bits, 11 for a license run), the car (9: the place of the run's car among table 0's row ids, 0x40A270 with the search 0x406AE8),
the course (8: its place among table 30's, `COURSE`, 0x40A2A0), a flag (1), the test's number (12: the race code's digits from
the fourth, 0x19F238), two words (12, 3), the time (22, ms), two more (4, 4); the license run's unnamed ones 0. 0x1A4660 packs
them low bit first into 80 bits. 0x1A2FA8: the 10 bytes after two zero bytes (12 bytes); bits 0 .. 3 a nibble of a 32-bit hash
(x ^= x >> 16, >> 8, >> 4) of the program's word 0x5057C0 (0 in every state seen; ❓ what writes it); bits 4 .. 15 the CRC-32
(0x498530, the standard one) of the 12 bytes folded (c ^ c >> 12 ^ c >> 24, 12 bits); then SAFER SK-128 (the exponent 45 modulo
257 tables, 0x1A3340; the strengthened key schedule 0x1A3618) of 13 rounds under the program's key halves 0x55CAD0 / 0x55CAD8,
enciphering the 8 bytes at each offset 0 .. 4 in turn in place (0x1A3E70). The hash (0x1A2EA8; 0x1A2C00 .. 0x1A2E58) keeps 16
bytes h from zero and steps on 8-byte blocks m (the word's 4 bytes and 4 zeros): t = E[h1 ‖ m](h0) with a 10-round schedule,
h0 ^= t, t = E[m ‖ h0 (before)](~h1), h1 ^= t; the 32 bits are h's bytes folded 4 apart. 0x1A48B0 writes the 12 bytes, a
little-endian number, as decimal digits least first, padded with '0' to 30 and turned round; the first digit is then the check:
from 0xFFFFFFFF, x 3 plus each later digit's character, modulo 10. `gt4r_diff <disc> prologue-passcode [n]` runs the program's
0x1A31A0 on made-up fields against ours (test `disc_prologue_pass_codes_equal`: equal on every set). The arcade's course records
(the `course` object's units, `quick-arcade`'s PasscodePopup) carry codes too: ❓ their maker 0x1A0498 not followed.
## The race display ✅

Prologue's display is its own `RaceDisplay` (0x388FC8; `app/race_hud_prologue.*`): an object a part (0x38E7A8 the base: +0x01 /
+0x02 the alignments: 0 left or top, 1 right or bottom, 2 centered, + 4 the wide picture's edge; +0x08 the opacity, +0x0C / +0x0E
the place, +0x10 / +0x12 the size) in a frame of 640 x 448, its colors constants of its code. The parts a mode shows and their
places are Gran Turismo 3's tables `database/racemode_e3.db` and `racedetail_e3.db` (16 modes; 122 rows differ from Gran Turismo
3's; `gt4tool prologue <disc> hud` lists them), applied by 0x38D020 (the part's number) and 0x38D548 (a time line's):

| Part | Object | Drawn |
|---|---|---|
| 1 rank | RaceRankDisplay 0x38ECB8 | the place in `FC_48` (orange 0x5EA8F2) rolling up or down to a new one, its "st" in gray 0.9 of a digit on |
| 2 laps | RaceLapDisplay 0x394448 | "Lap" in `DC_12`, the lap in `FC_26`, the slash and the laps at 0.77 |
| 3 panel | 0x38D730, two lists of places in its code | view 0 (the car's own views): the tachometer and speedometer (155 x 122 at x 345 and ending at 295), the gear's box, the lamps; view 1: the small panel 0x3928A0 |
| 8 targets | RaceLicenseDisplay 0x386340 | a test's three times, each with its cup (`mark_1st` ..) |
| 9 map | RaceMiniMap 0x398958 | the course's map, the other cars' marks at 0.66, the player's pulsing |
| 11 times | RaceTimeDisplay 0x38FC40 | `FC_16` at 0.9 ending at the place, "Best" / "Last" in `DC_12`, the lap's with the stopwatch |
| 6, 7 | messages | the penalty's line (red), the pop-ups |
| 26 | 0x38A558, 0x38CDC8, 0x38CE68 | the count (`FC_48`, 0.8 s a second), START (1 s), FINISH (3 s) |

A dial (0x397268 .. 0x397518) is its black base (`sprv_base`), the red zone's ring from the up-shift revolutions (P + 0x226), the
face and a needle drawn eight times at a sixth, led by its motion. The tachometer's face is the first of 0x5711F8's whose limit
holds the limiter's revolutions (P + 0x1C4) + 1,500 to the thousand under (6,000 .. 18,000); the speedometer's 160, 240 or 320
km/h by 1.1 x the top speed (0x3CDFF8, 0x5713D0). The needles turn from 174 to 394 and from 146 to 366 degrees round a middle
16.5 under the face's. The in-car panel's places are in a frame of 480 lines (0x38BEEC).

The race's opening (0x389D30): the course's logo (the course file's slot 49) at the bottom right from half a second for five, the
display fading in over half a second from 4.5 s before the start. A replay shows only `gt_logo_mini` until its display is asked
for (a minute's slow blink). ✔ Against the original at Tsukuba and lesson 1 (`reference/14-prologue`): the rank, laps, times, map,
dials, gear, small panel, cups, logo and count in the same places and colors. ❓ Not drawn: the boost gauge, the Prius panel, the
steering and pedal monitors of replays, the car's name and view mode of a replay, the tests' cones count, the wrong way sign; the
lamps of fuel and oil never light; the small tachometer's bar is cut by its width where the program tests its alpha ramp.

The
pause and results are the race movies `menu/pause.strb`, `menu/result.strb`,
`menu/result_license.strb` (Prologue's: TIME, the medal, a three-letter name entry for a new
record; its callbacks `GetTime`, `GetRank`, `GetDefaultName`, `PrizeLevel`, `EXIT_MENU`).
Its texts are drawn in `font/race.fnt` (FC_16 .. FC_48, DC_12) and the movies' own fonts (`font/menu.fnt`), and its mask
track is a box the texts are clipped to, as the program draws it ([strobe.md](strobe.md), the track's clip depth): the white
box the page drew was the mask's bar. What a passing run shows: "The license tests" above.

## The saved game ✔ written and read (`src/gt4r/save/prologue_data.*`)

Prologue's game data (`GT4MC::FileGT4GameData`, "GAMEDATA", the card's `BISCPS-15055` folder) is its
`GranTurismo4::Status` (ctor 0x401E90; size 0x402150, save 0x4021F0, check 0x4021A8, load 0x402288) written as raw
copies, each part padded to 16 bytes (0x401D20), in this order:

| Payload | Size | What | From |
|---|---|---|---|
| 0x00 | 0x10 | u32 0xFF60BAD0 (Gran Turismo 4's is 0xFF60BAD3), i32 minus the body's length, u32 the body's CRC-32 (0x498530, the table at 0x51F488: the same CRC as Gran Turismo 4's), 0 | 0x4021F0 |
| 0x10 | 0x10 | u32 the garage's car count (its list at UserProfile +0x11330), padded | UserProfile::vf5 0x402500 |
| 0x20 | 0x148 a car | the cars: nothing written (CarGarage::vf5 0x4045F0 only moves on): ❓ Prologue keeps no garage | |
| 0x20 | 0x10 | the profile's name char[0x10] (UserProfile +0x1133C; 0x402428) | |
| then | 48 x 0x2E0 | the license units, the first 0x2E0 bytes of each 0x2E8-byte unit from the license object's +8 (UserProfile +0x11350; 0x3FF190, 0x3FE998) | |
| then | 64 x 0x550 | the course units from the course object's +8 (UserProfile +0x19ED8; 0x3FD438, 0x3FCE30) | |
| then | 0x6C0 | the options: 0x6B8 bytes from Option +8 (Status +0x2F510; 0x3FFEF8) | |
| then | 0xB0 | two controller settings of 0x54 (from Option +0x6C0 +4, 0xAC apart; 0x404458, 0x4041F0) | |
| then | 0xE0 | the LAN battle's settings (Option +0x81C +4; 0x400ED8) | |
| then | 0x130 | the network settings (Option +0x900 +4; 0x4017B0 -> 0x48B308) | |

Measured on the program's own save of a constructed Status (`gt4r_diff <prologue disc> prologue-save`): 0x1E7A0
bytes (its size routine says the same, and its check accepts them); the name at 0x20, license unit k at 0x30 +
0x2E0 k, course unit k at 0x8A30 + 0x550 k, the options at 0x1DE20: the options begin 16 bytes before the course units
end (the profile's size, 0x402460, counts the name but not the cars' count block's own padding the same way), so they
are written over the last course unit's last 16 bytes; the load (0x402288) reads them in the same order.

**A new game** (the constructors' values; ours, `new_prologue_payload`, is the program's byte for byte, CRC included,
`gt4r_diff <prologue disc> prologue-save`): every ranking entry empty (a license unit's ten of 0x48 from +0x10, a course
unit's ten of 0x88 from +0x00: the time 359,999,999 at the entry's +0x10, the pass code "--------" at +0x24 / +0x68);
the options' block: its size 0x6B8 first, then (offsets from 0x1DE20) words 1 at +0x10, +0x14, +0x18, +0x20, +0x24,
+0x2C, +0x38 .. +0x48, +0x60, +0x70; 0.9 at +0x28; 2 at +0x34 and +0x54; 1,200 at +0x74; bytes 1 at +0xAE, +0xAF;
0xC63 at +0xB0; 0x100 at +0xC0 + 0x10 k for 64 rows; the two controller settings at +0x6C0 and +0x714 (the same 0x54
bytes, 0x4041F0); the LAN settings at +0x770 ("machine01", 1 at +0x7F4 .. +0x7FC, 2 at +0x840, 6 at +0x84C); the network
settings at +0x850 ("default setting", 1 at +0x870 and +0x8D4, the addresses 192.168.0.2, 255.255.255.0,
192.168.0.1, 192.168.0.100, 192.168.0.101, 192.168.0.200 at +0x874, +0x894, +0x8B4, +0x8D8, +0x8F8, +0x91C). ❓ What
the option words mean is not followed: a loaded game's options are kept as they were read and written back.

**The file**: the card's folder `BISCPS-15055GAMEDATA` (the product, 0x19BCB0, and "GAMEDATA", 0x19D250) holds the game
data file of the folder's name (the file path as Gran Turismo 4's, File::vf15), `icon0.ico` (the disc's
`icon/gamedata.ico`) and `icon.sys` (the disc's, with the title). The game data file is the payload as it is: the read (0x19C9D0) makes a buffer of the file's size
rounded up to 64 (0x4CC708, a vector's resize, the same routine as Gran Turismo 4's 0x5D0F50), reads the file into it and
nothing else, and the load (0x19D2F8) hands that buffer to the Status's check and load, where Gran Turismo 4's read
(0x1CDAC8) calls its decoder (0x1D4E60) in between. ❓ The write side (0x19C0F0) is not followed past its virtual calls,
nor the file's padded size on the card;
there is no garage file (the count is 0). ❓ The title's text in
icon.sys (the program builds it at +0xC0 in 0x19C488 with a "[%d]" number): here the title the page gives.
Not checked against a card the original wrote (no Prologue save from the console here).

**Saving and loading** (`menu/game_natives.cpp`, the card manager as Gran Turismo 4's): before a save the license
record's units (`prologue_license_units`, `menu/prologue_license.h`) and the license's default name (the profile's name)
go into the state; after a load they go back (`prologue_license_load`, `setDefaultName`). The title's Save and Load
Game start `memcard` with `save_game` / `load_game` (its GameSave and GameLoad pages); power on with a saved game loads
it (AutoLoad). Test: `disc_prologue_game_saved_and_loaded` (a gold run and its name saved through GameSave, back
through GameLoad on the next start). ❓ The course units (Prologue's course records) are kept as loaded: nothing of
ours fills them yet.

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
- `main::game.license` (the program's license record: 48 units of 0x2E8 bytes from the object's +8, its natives
  0x1582C0 and 0x15B4D0): `getUnit(number | race code)` a unit per license test (the code by the scripts' own
  `main::prologue::getLicenseID`; the main script writes each unit's `category`), with `isPassed`, `isCompletePassed`,
  `getBestTime`, `getRankNum` (the entries in its ranking of ten, 0x3FE548), `getPoint`, `getPassCode`;
  `isPassedCategory` / `isCompletePassedCategory` (every unit of the category with a medal / gold: 0x3FF000 / 0x3FF080),
  `getDetailCount(name)` (the units whose medal is that of the names at 0x505008: none, bronze, silver, gold;
  0x3FEF78), `getTotalPoint` (their points summed, 0x3FEFA8), `forcePassedAll`. A unit keeps its medal (+4: 3 gold,
  2 silver, 1 bronze; the best, 0x3FE1A0), its points (+8: the most, 0x3FE430) and ten times in order (from +0x20,
  0x48 bytes each). The license race's end (0x19ED58) records a run: its medal, its time and its points (0x19F410: by
  the test's RACE +0x22 and +0x5B and whether it has a bronze time, 0x19F508: gold 100 x (silver - time) /
  (silver - gold), silver 50 x (bronze - time) / (bronze - silver), bronze 10; 0x19F620: 100 / 50 / 10; 0x19F670: gold as
  0x19F508, silver 50, bronze 10; integer arithmetic, ms). `recordResult` from the race's end keeps
  them with the name entered on the results page (the record and its save: above and "The saved game").
- A license race's gold, silver and bronze times go into the race menus' work (`MQuickWork` goldTime ..) from its
  RACE row as the program's race set-up does (0x1A1C10 with 0x409B70 ..): the race display's three medal lines show
  them (`~/gt4re-out/prologue/r5/lic3.png`: test 1 1'47.600 / 1'48.000 / none; `lic4.png`: test 2 0'12.180 / 0'12.200 / 0'13.000).
- A license test's car is the first of its event's pool (`demio_sport_03` for `l0p0001` on `L_20r60r2`), its grid the event's
  (1; 2 to 6 on tests 26 .. 35: "The license tests").

**The older layouts.** Prologue's `.mproject` files give a face's states as `cond_active`,
`cond_focus`, `cond_prelight`, `cond_insensitive` (0 either, 2 the bit off, 3 on), read as the
visible condition and mask over bits 1, 2, 4, 8 (a license sign's grey face shows until the test
can be tried). They have no `clip`: a scroll box's select bar shows only what is inside it (the
title's one item of eight, the arcade's Start, the five course pictures); ❓ judged from the
original's pages. Their scripts call `getDeepCopy` (a widget's copy), read a text face's `value`
where it has only its `key` (the car page's formats "%Cfff%d %C222%s/..."), and tint picture faces
with `setColor(color)` (the paint balls).
**Its pictures' alpha.** A picture face draws in a vertex alpha of 63 (mImageFace::vf63 0x2B4BA8 halves the color's alpha, 0x1D7240
halves what goes to the GS), so its pictures keep alpha as 0 .. 255, where Gran Turismo 4's have 128 for whole: the board's
background (130 .. 250) lets the film behind it show. A frame's parts (0x2A4E80) are drawn without that. `Project::set_face_alpha`.

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
set. With the option page's opponents on ("筑波、富士、NYに敵車を出現させる") the car's own event (`car.race_code`,
`_h`, `_p` by the difficulty): six cars from its pool, the grid page listing them (`grid1CarName` ..). The option page's
`getDeviceConfig("CHEETAH")` counts from 1 (❓ the device's default).

Pictures (under `~/gt4re-out/prologue/r4/`, not kept): the title ring (`d1.png`), the arcade
pages (`f2`, `f3`, `h4`, `h5`, `i6`), the pre-race page (`m1`), the race with its display
(`m3`, `n1`, `k3`), the pause (`p1`), School Mode's ring (`q1`), the license race (`s3`, `t2`) and
its results (`u1`). Against the reference: the title, arcade, car, paint, settings and pre-race
pages match the original's layout; the pre-race page runs the race behind it as a replay shows one (below); Japanese words are drawn by
`menu-draw` (the settings' labels and START are blank here). ❓ Not done: the license tests'
target times (the `RACE` rows hold none; not located), the dialogs' Japanese text; the license results (see "The license tests").

## The cameras of a race ✅

- **The opening** (CameraSys::CameraStarting, 0x3D3A90; `race/start_camera_prologue.*`): the last record of the course's REP4 in
  slot 45 (a race) or 46 (a replay), its eye (+0x34) and look-at (+0x40) curves in the followed car's frame (x to the left, z
  ahead), at t = 1 - n / 1,200 with n the start's count less 60 steps (a race) or plus 60 (a replay); below n = 59 the view stays.
  A race's ends 1 s before the start, with dissolves of half a second 2, 1 and 0 s before its end; a replay's 1 s after the start.
  Tsukuba's race: the approach, the tail, the nose; its replay: the nose rising, then one shot a second (tail, side, above, nose).
  Test `disc_prologue_start_camera`.
- **The lens**: a camera's angle A (degrees, field +0x0C) shows a half width of 10 / 7 tan(A / 2) at 4:3 (4/3 x 480/448: the angle up
  the 448 lines), measured as 1.43 on three shots of the original (the opening, the grid page, a trackside shot).
- **The pre-race page** runs the race behind it: the computer drives the player's car too, the replay's opening shots for ten
  seconds and one, then the course's replay cameras, no car heard (✔ the original's Tsukuba page, 45 s).
