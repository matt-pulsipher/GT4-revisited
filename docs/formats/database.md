# The car database: `specdb/`

Code: `src/gt4r/formats/db/specdb.*`; `gt4tool db`. The US disc's set is
`specdb/GT4_US2560/`: 79 tables (102,767 rows, all of which unpack), a label list per table,
10 string pools, and `CARS/<16 hex digits>` (one small file per car ❓).

## Tables: `.dbt`, "GTDB"

```
0x00 "GTDB"   0x04 u32 flags (0x80001; 0x80003 on the name and paint tables)
0x08 u32 rows 0x0C u32 row size in bytes
0x10 {u32 row id, u32 offset}[rows], by id (the paint tables repeat an id: a car's paints)
then u32 dictionary size (from here), u32 codes
     {u8 byte, u8 bits}[256]  the byte a code of up to 8 bits stands for, indexed by the next
                              8 bits of the stream (bits 0: a longer code)
     {u8 bits, u8 byte, u16, u32 code}[codes]
then u32 size of the plain rows (from here), u32 plain rows (at most 64), the plain rows
then the packed rows
```

A packed row is at its offset from the packed rows' start: a byte, the number of codes that
follow, then the codes as a bit stream read from the low bit of each byte. A code is matched
on its low bits. The first code is a byte: bits 0-5 a plain row, bits 6-7 the kind:

- 0: the plain row as it is.
- 1: a code per byte of the row.
- 2: the plain row with changes: a code per 8 bytes of the row whose bit i says byte i
  changes, then a code per changed byte.

Checked on every row of the disc: the codes read are the number the row says, and the row
ends where the next one starts. The columns of a row are not described in the file.

## Labels: `.idi`, "GTID"

`"GTID", u32 count, u32 0, u32 table id`, then `{u32 label offset, u32 row id}[count]` in label
order; the labels follow that table, each `u16 length` and the bytes (the terminator counted).

## String pools: `.sdb`, "GTST"

`"GTST", u32 count, 8 bytes`, then `u32 offsets[count]`; the strings follow, each `u16 length`
and the bytes. `<language>_StrDB` (american, british, french, german, italian, spanish,
japanese, korean, big5) holds the names; `UnistrDB` the units and short specifications
("499.5cc", "OHV").

## What the tables hold

| Table | Rows x bytes | Contents |
|---|---|---|
| `GENERIC_CAR` | 722 x 24 | The cars; labels like `_117_coupe_68`. +0x00 the car's parts as sold {u32 row id, u32 table id 36}, +0x0C u16 model year, +0x11 u8 a kind the spec's byte +0x120 is made from, +0x13 u8 the car's category (2: a rally car, raised on loose courses; 2 .. 4 clear two spec bytes), +0x14 u8 copied into the spec ❓ the rest |
| `CAR_NAME_<language>` | 804 x 16 | Per car id: four string numbers, the first the car's name |
| `CAR_VARIATION_<language>` | 722 x 8 | Per car id: the id of its rows in the paint table |
| `VARIATION<language>` | 3,849 x 52 | A row per paint of a car, the rows of a car sharing an id; labelled `<model>_<n>`, where `<model>` names the car's files (`car/lod/<model>`, `car/menu/<model>`) |
| `COURSE` / `COURSE_NAME_<language>` | 23 x 4 | The courses; the label is the course file's name (`capri` is "Amalfi") |
| `CHASSIS` | 722 x 37 | The body and its weight; columns below |
| `TIRESIZE` | 286 x 4 | +0 u8 aspect / 5, +2 u8 rim inches x 10, +3 u8 width / 5 mm (205/60R15 is `0c 00 96 29`) |
| `FRONTTIRE`, `REARTIRE` | | Pairs of u32 row id, u32 table id; the first is the tire's `TIRESIZE` row |
| `ENGINE`, `GEAR`, `DRIVETRAIN`, `LSD`, `SUSPENSION`, `BRAKE`, `TIRECOMPOUND`, `TIREFORCEVOL`, `TURBINEKIT`, `MUFFLER`, ... | | The parts, a table per kind; columns below |
| `ENGINE` (sound) | 723 x 113 | +0x18 u16 the engine's sound number: the car plays `sound/engine/<number>` and `sound/<variant>/<number>` ([sound.md](sound.md) "A car's sounds") |
| `MUFFLER` (sound) | 1,844 x 7 | +0x06 u8 the exhaust sound's variant, 0..3 (a car's stock muffler is no row: 0) |
| `TURBINEKIT` (sound) | 1,798 x 18 | +0x09 u8 non-zero: a turbo (the exhaust sound is a `turbo` variant) |
| `DEFAULT_PARTS` | 2,990 x 208 | Sets of parts: 26 pairs of u32 row id, u32 table id, the part of table t at pair t - 1 (-1: none). A car has several: `df_pt_<car label>`, `df_pt_sim_<car label>` (the one its `GENERIC_CAR` row names), `df_pt_d_` / `df_pt_s_<car label>` (dirt, snow), and sets for license tests and events |
| `RACE` | 709 x 151 | The events ("Events" below) |
| `ENEMY_CARS` | 2,558 x 24 | The opponents: a car, its parts and its settings ("Events" below) |

Every one of the 722 cars' models is on the disc.

## The parts tables' columns

Code: `car_parts.*` (the rows the menus and tools read), `src/gt4r/sim/equipment.*` and
`car_spec.*` (every column the race reads). The columns are known from the program's spec
builder (0x4413C8: one helper per table, each copying or scaling row bytes into the car's
spec; [specs/physics.md](../specs/physics.md)) and its part setters (0x4449C0), and were
checked by running that builder from the user's disc against ours (every byte of every car's
spec is equal: [specs/physics.md](../specs/physics.md) §9) and on real cars: the Isuzu 117 Coupe '68 (1,050 kg, 120 PS at 6,500 rpm, 14.5 kgf m at
5,000, four gears 3.467 / 1.989 / 1.356 / 1.000), the Honda NSX Type R '02 (1,270 kg, six
gears 3.066 .. 0.717, final 4.235) and the Dodge Viper GTS '99 (six gears 2.660 .. 0.500,
final 3.070, 456 PS). Every one of the 722 cars builds a spec with a weight, power, gears
and tire sizes.

A row has no header: the columns start at byte 0. Rows begin with three bytes that the spec
builder does not read (❓ a stage and a price class). "Setting" columns come in triples the
Settings pages use: a number of steps (or a least value), a greatest value and a default; an
index setting i of n steps stands for min + (max - min)(i - 1) / (n - 1) in whole numbers.

A part is named {u32 row id, u32 table id} (the program keeps the pair as one 64-bit word, the
table in the high half). The table ids are the label lists': 0 `GENERIC_CAR`, 1 `BRAKE`,
2 `BRAKECONTROLLER`, 3 `SUSPENSION`, 4 `ASCC`, 5 `TCSC`, 6 `CHASSIS`, 7 `RACINGMODIFY`,
8 `LIGHTWEIGHT`, 9 `STEER`, 10 `DRIVETRAIN`, 11 `GEAR`, 12 `ENGINE`, 13 `NATUNE`,
14 `TURBINEKIT`, 15 `PORTPOLISH`, 16 `ENGINEBALANCE`, 17 `DISPLACEMENT`, 18 `COMPUTER`,
19 `INTERCOOLER`, 20 `MUFFLER`, 21 `CLUTCH`, 22 `FLYWHEEL`, 23 `PROPELLERSHAFT`, 24 `LSD`,
25 `FRONTTIRE`, 26 `REARTIRE`, 27 `NOS`, 28 `SUPERCHARGER`, 29 `WHEEL`, 30 `WING`,
31 `TIRESIZE`, 32 `TIRECOMPOUND`, 33 `TIREFORCEVOL`, 34 `COURSE`, 35 `RACE`,
36 `DEFAULT_PARTS`, 37 `DEFAULT_PARAM`, 38 `ENEMY_CARS`, 39 the car names, 40 the course
names, 41 `CAR_VARIATION`, 42 `VARIATION`.

| Table (bytes) | Columns |
|---|---|
| `CHASSIS` (37) | u16 mm: +0x00 length, +0x02 height, +0x04 wheelbase; +0x06 u16 weight, kg; +0x0E / +0x10 u16 front / rear axle grip, per mille (the spec keeps the low byte + 124: 930 reads as 0.930); +0x12 / +0x14 u16 the width across the front / rear tires, mm; +0x16 / +0x18 u16 how high the front / rear wheel's top may reach in the body, mm; +0x1C front weight, percent; +0x1D yaw inertia (x 100 kg m2); +0x1E the center of gravity's height term, mm; +0x20 / +0x21 two tire bytes used with tire kinds 6 .. 10 (a rim change and a width percentage); +0x22 / +0x23 two bytes the entry's grip byte is added to; ❓ +0x08 .. +0x0D (the length, height and weight again), +0x1F, +0x24 |
| `ENGINE` (113) | +0x00 six u32 string numbers (layout, valve gear, aspiration, ...); +0x1A u16 catalog power, PS; +0x1C u16 catalog torque, 0.01 kgf m; +0x1E u16 x 24 the full-throttle torque curve, 0.01 kgf m; +0x50 the shift limit and +0x51 the rev limit, x 100 rpm; +0x52 idle, x 10 rpm; +0x53 the engine speed the clutch takes up at, x 10 rpm; +0x54 the curve's points (6 .. 24; 52 engines have more than 16); +0x55 u8 x 24 the curve's engine speeds, x 100 rpm; +0x6D, +0x6E two bytes copied into the car's parameters (when 0 they are made from the rev limit and the weight) ❓; +0x6F the output factor, percent (100 on 633 engines, up to 118) |
| `DRIVETRAIN` (21) | u16: +0x00 engine inertia (x 0.001), +0x02 engine friction at the top of the curve (x 0.98 N m), +0x04 / +0x06 front / rear wheel inertia (x 0.01), +0x08 / +0x0A front / rear shaft inertia (x 0.01); +0x10 drive: 0 front engine rear drive, 1 front drive, 2 four wheel drive, 3 mid engine, 4 rear engine; +0x11 the four wheel drive system (0xFE none); +0x12 / +0x13 / +0x14 the front torque share's least, greatest and default, percent |
| `GEAR` (45) | u16 x 1000: +0x00 gears 1 .. 11, +0x16 reverse, +0x18 / +0x1A / +0x1C the final drive's least, greatest and default; +0x1E u16 a second reduction (x 1000) and +0x29 its kind (1: on every gear, 13 rows, the Honda S2000's 1.160; 2: in place of the final drive for reverse and the gears from the fifth on; ❓ 3, 4); +0x23 the gearbox's kind (0 on 2,255 rows; 1 and 2 without steps, e.g. the Honda Fit W's two "gears" 2.367 and 0.407 are its ratio range); +0x24 forward gears; +0x25 1: the ratios are made from the slider (1,672 rows: the gearboxes one can buy); +0x26 / +0x27 / +0x28 the slider's least, greatest and default (the design top speed / 10 km/h) |
| `SUSPENSION` (111) | u16 mm: +0x00 / +0x02 / +0x04 the front ride height's least, greatest, default, +0x06 .. the rear's; +0x0F / +0x10 / +0x11 front camber's least, greatest, default, +0x12 .. the rear's; +0x15 / +0x16 two bytes (the spec's +0x90 / +0x91); +0x17 .. +0x26 the camber curves: four angle codes and four gain codes for the front, then for the rear; +0x29 / +0x2C front / rear toe (128: none); +0x2D / +0x2E bump travel, +0x31 / +0x32 droop travel, mm; +0x2F / +0x30 the gap to the progressive rate, mm; +0x34 / +0x35 / +0x36 the front spring rate's least, greatest, default (0.1 kgf/mm), +0x37 .. the rear's; +0x3A / +0x3B the motion ratio, percent; +0x3C / +0x3D bump-stop rate; +0x3E / +0x3F bump damping; +0x40 .. +0x47 the damper speeds (front and rear in turn: bound 1, bound 2, rebound 1, rebound 2); +0x48 the front bound dampers' steps, then {least, greatest, default} for each of two stages (+0x49, +0x4C); +0x4F the same for the front rebound; +0x56 rear bound; +0x5D rear rebound; +0x64 / +0x65 unsprung mass (x 0.25 kg); +0x66 the front anti-roll bar's steps, least, greatest, default; +0x6A the rear's; +0x6E a byte copied into the spec |
| `BRAKE` (6) | +0x03 / +0x04 front / rear brake torque per wheel (x 98 N m), +0x05 the handbrake's |
| `BRAKECONTROLLER` (11) | +0x03 the front's steps, least, greatest, default; +0x07 the rear's |
| `STEER` (38) | three curves of six speeds and six limits: +0x01, +0x0D, +0x19; +0x25 the steering lock, degrees |
| `LSD` (27) | +0x03 / +0x0F the front / rear differential's type (a letter, as in Gran Turismo 3); {least, greatest, default} at +0x04 front initial torque, +0x08 front acceleration, +0x0C front deceleration, +0x10 / +0x14 / +0x18 the rear's |
| `ASCC` (25) | four channels at +0x03, +0x09, +0x0E, +0x14: a 2-bit mode, then steps, least, greatest, default of its gain (the first and third have a threshold byte after: +0x08, +0x13) |
| `TCSC` (10) | +0x03, +0x04 two terms; +0x05 the gain; +0x06 steps, +0x07 least, +0x08 greatest, +0x09 default of the level, percent |
| `RACINGMODIFY` (29) | +0x00 u16 weight, per mille; +0x02 / +0x04 u16 front / rear tread, mm; +0x0C drag coefficient x 100; +0x0D / +0x0E / +0x0F front downforce's least, greatest, default; +0x10 .. the rear's; +0x13 / +0x14 s8 front / rear wheel offset, mm (the track is the tread less twice it); +0x15, +0x16 two bytes (the spec's +0x98, +0x99); +0x17 percent of the yaw inertia; +0x18 .. +0x1C a part that changes with speed (❓ a moving wing): its kind, the two speeds it changes at (km/h), a rate and its force |
| `LIGHTWEIGHT` (6) | +0x00 u16 weight, per mille; +0x05 percent of the yaw inertia |
| `PORTPOLISH`, `DISPLACEMENT`, `COMPUTER`, `MUFFLER`, `INTERCOOLER`, `SUPERCHARGER` (7) | +0x00 u16 percent of the engine's high-speed factor, +0x02 u16 percent of its low-speed factor |
| `ENGINEBALANCE` (9) | the same two; +0x07 added to every engine speed of the curve, +0x08 to the rev limit (x 100 rpm) |
| `NATUNE` (11) | +0x04 / +0x06 u16 the two factors; +0x09 added to the curve's speeds, +0x0A to the rev limit |
| `TURBINEKIT` (18) | +0x04 / +0x06 u16 the two factors; +0x09 the boost cap (0: no turbo); +0x0A .. +0x0F the six boost settings; +0x10 added to the rev limit, +0x11 to the shift limit |
| `FLYWHEEL` (7), `CLUTCH` (8) | percentages: +0x03 of the engine inertia, +0x04 of the engine friction, +0x05 / +0x06 of the front / rear wheel inertia; `CLUTCH` +0x07 the clutch's torque byte |
| `PROPELLERSHAFT` (8) | percentages: +0x03 of the engine friction, +0x04 / +0x05 of the wheel inertias, +0x06 / +0x07 of the shaft inertias |
| `NOS` (8) | +0x00 u16 its capacity; +0x05 the default setting |
| `FRONTTIRE` (59), `REARTIRE` (57) | parts {row, table}: +0x00 the `TIRESIZE` row, +0x08 three `TIRECOMPOUND` rows and +0x20 three `TIREFORCEVOL` rows (for paved, loose and wet surfaces); the last byte (front +0x3A, rear +0x38) the tire's kind, 0 .. 12 |
| `TIRESIZE` (4) | +0x00 aspect / 5, +0x02 rim (tenths of an inch; below 61: inches), +0x03 width / 5 mm |
| `TIRECOMPOUND` (90) | +0x00 the wear rate, +0x01 grip x 100 (100 for the economy tire `gt4_ec` to 190 for `gt4_rq`), +0x02 the load curve (4 loads, 4 values), +0x0A the slip angle range of curve A, degrees, +0x0B curve A (8 + 8), +0x1B curve B (6 + 6), then for the slip ratio below zero +0x27 its axis, +0x2D curve C, +0x39 curve D, and above zero +0x3F, +0x45, +0x51 (6 each; the six bytes at +0x33 and +0x4B are not read); +0x57 three bytes |
| `TIREFORCEVOL` (18) | grip in percent of the compound's: +0x00 surfaces 0 .. 9, +0x0A / +0x0C / +0x0B surfaces 20 / 21 / 22 (the wet variants of 0, 1 and 9), +0x0D surfaces 10 .. 14 |

`VARIATION` rows (a car's paints) also hold the body's box and frontal area as floats:
+0x10 height, +0x14 width, +0x18 / +0x1C how far the body reaches ahead of and behind its
middle, mm; +0x20 the frontal area, m2. `CAR_VARIATION` gives a car's `VARIATION` id (its row is
{u32 row id, u32 table id 42}).

## What the menus read

Columns the menus' classes read (`menu::MCarData`, `MCourseData`, the car object; the
accessors' addresses are the US program's, v1.01). Code: `src/gt4r/menu/game_natives.cpp`.

| Table | Column | What |
|---|---|---|
| `GENERIC_CAR` (24) | +0x00 u32, +0x04 u32 | The car's row of `DEFAULT_PARTS` and that table's id (0x24): the label is not always `df_pt_<car label>` |
| | +0x08 u32 | Price in credits |
| | +0x0C s16 | Model year |
| | +0x0E, +0x0F, +0x10 u8 | Country, maker, tuner: numbers of the program's lists (below) |
| | +0x11 u8 | Category, modulo 100 (0 normal, 1 racing, 2 tuning, 3 concept); 100 and over ❓ |
| | +0x12 u8 | Flags: bit 0 not for sale, bit 2 not allowed in races, bit 5 a test car |
| | +0x14 u8 | The kind of open top |
| `ENGINE` (113) | +0x00 to +0x14, six u32 | Texts of `UnistrDB`: displacement, layout, valve gear, aspiration, the engine speeds of the power and of the torque |
| | +0x1A u16, +0x1C u16 | Power in PS; torque in kgf.m x 100 |
| `DRIVETRAIN` (21) | +0x10 u8 | 0 FR, 1 FF, 2 4WD, 3 MR, 4 RR |
| `VARIATION<language>` (52) | +0x0C u32 | The paint's name, a text of `<language>_StrDB` |
| | +0x24 u8 x 3 | The paint's color, red, green, blue |
| `COURSE` (110 x 22) | +0x10 u8 | The surface: 0 dry, 1 wet, 2 dirt, 3 snow (0x1B2F10) |
| | +0x11 u8 | The most cars on it (0x1B3030) |
| `ARCADEINFO_NORMAL` (721 x 65) | 8 x {u32 row, u32 table}, 1 byte | Entries 0 .. 5: the car's Arcade events, rows of `RACE`: entry [difficulty 0 to 2] on a dry course, [3 + difficulty] otherwise (`GetRaceCode` 0x12E150; the level 2 when 3 or more). Entries 6, 7: the car's dirt and snow versions, `ENEMY_CARS` rows (`GetDirtCarCode` 0x12F8B8, `GetSnowCarCode` 0x12FBB0, `ShiftCarCode` 0x12F3E0 by the course's surface; `IsDirtRunnable` 0x12E498: entry 6 not all 0xFF). +0x40: `IsTireNormal` (0x12E388). The natives read the row through 0x448AD0 (the label as a GENERIC_CAR or ENEMY_CARS row, 0x447BB8; the copy puts +0x40 before entries 6 and 7, 0x448B48); a car without a row answers 0 or nothing |
| `DEFAULT_PARTS` | the pairs of table 12, 10, 6 | The car's `ENGINE`, `DRIVETRAIN`, `CHASSIS` rows |
| `CHASSIS` | +0x08, +0x0A u16 | The length and height the menus write, mm (0x446038, 0x446068); the width is of another part ❓ |

The accessors of `GENERIC_CAR` are at 0x448868 to 0x4489A0.

**The program's lists.** Makers, tuners, countries and categories are numbers in the
database and names in the program file: lists of {u32 number, u32 pointer to the name}
(US v1.01: makers at 0x6A7310, 78; tuners at 0x6A7658, 96; countries at 0x6A79C0, 13;
categories at 0x6A6FE0: `NORMAL`, `RACING`, `TUNING`, `CONCEPT`). The names are the pictures'
and texts' keys (`toyota`, `nismo`, `JP`). They are found in the disc's program file by
their content (`src/gt4r/menu/program_tables.*`; `gt4tool menu-names <disc>` prints them).

`gt4tool db-strings <disc> <pool> [index ...]` prints texts of a string pool.

❓ Not read: which cars have no manual gears, the price of parts.

## Events: `RACE` and `ENEMY_CARS`

Code: `race_events.*` (`gt4tool ai <disc> events | event <label>`); how a race uses them:
[specs/ai.md](../specs/ai.md) §7, §8. The columns are the program's: the scripts' `MRaceData`
methods (0x1A0F80 .. 0x1A2E30, registered by 0x1A2EC8), the `RaceSpec` accessors (0x447400 ..
0x4477D0) and the race's copy of a row into its configuration (0x35E108). In memory a row
follows a 0x10-byte header, so the methods' offsets are the row's + 0x10.

| `RACE` (151) | What |
|---|---|
| +0x00 u32, +0x04 u32 | The course: a `COURSE` row and its table (34) |
| +0x08, +0x0C | A part {row, table}, mostly -1 (❓ a set car: the tables 0x2D seen are not a list's) |
| +0x10 u32 | 532 or 533 on the rows seen ❓ |
| +0x14 u32 | Kept by the race as a u16 ❓ |
| +0x18 u16 | `getLimitTime` ❓ unit |
| +0x1A, +0x1C, +0x1E u16 | The gold, silver and bronze times' milliseconds (with the minutes and seconds at +0x76 / +0x77, +0x78 / +0x79, +0x7A / +0x7B) |
| +0x20 u16 x 6 | `getPrize`: the prize by place (✔ Sunday Cup 6, 5, 4, 3, 2, 1; ❓ the unit) |
| +0x2C s16 | `getPrizeGC` (x 100) ❓ |
| +0x2E s16 x 5 | Kept by the race (0x4477D0) ❓ |
| +0x38, +0x3A s16 | `getNeedYearRange`: the model years allowed |
| +0x3E u8 | `entry_num`: the cars on the grid, the player's included (✔ 6 on the races, 1 on the license tests) |
| +0x40, +0x41 u8 | The start: its type (0 a hold of +0x41 seconds, 1 and 2 the opening shots over 8 and 10 s, 3 none) and its seconds (race_flow.md "The start") |
| +0x42 u8 | Kept by the race ❓ |
| +0x43 u8 | `getNumberOfLaps` (✔ 2 on the Sunday Cup) |
| +0x44 u8 | Kept by the race with the laps ❓ |
| +0x45, +0x46, +0x48, +0x49, +0x4A u8 | `getNeedLicense`, `getNeedDrivetrain`, `getNeedAspiration`, `getNeedCarType`, `getNeedTire` |
| +0x4B .. +0x52 | Kept by the race ❓ |
| +0x53 .. +0x56 | Kept by the race per driveline class ❓ |
| +0x57 .. +0x5A, +0x5B .. +0x5E, +0x5F .. +0x62 | The computer drivers' arcs' speed, braking and throttle percents per driveline class (0 = 100; ✔ the amateur FF events 90 / 85 / 100) |
| +0x63 | The rubber band's overall percent (0 = 100) |
| +0x64 | The rubber band's mode ❓ |
| +0x65 .. +0x67 | Slowing the cars ahead: percent, from (m), full at (x 10 m) (✔ 50 % on the 12 license-test events that have it; 0 on every race) |
| +0x68 .. +0x6A | Hurrying the cars behind: the same |
| +0x6B .. +0x75, +0x80 .. +0x8B, +0x91 .. +0x96 | Kept by the race ❓ (+0x85 its first byte, +0x89 a signed byte) |
| +0x92 | `getNeedPs`: power, x 10 PS |
| +0x93, +0x94, +0x95 | `getNeedPrice`, `getLimitedWeight`, `getLimitedLength` ❓ units |
| +0x96 | `getNeedCountryID` |

**An event's opponents**: the file `specdb/<set>/RACES/<table><row>`, named by the event as a
part, 16 hex digits (table 35, the row: `RACES/00000023000008A4` is `preview_entry_6`). 828
files: u32 the count, u32 0, then per entry {u32 row, u32 table 38} (an `ENEMY_CARS` row) and a
second word pair, the paint + 1 (-1: the race's choice ❓). ✔ 698 of the 709 events have a pool,
6,047 entries, every one naming an `ENEMY_CARS` row whose car and parts exist.

| `ENEMY_CARS` (24) | What |
|---|---|
| +0x00 u32, +0x04 u32 | The car: a `GENERIC_CAR` row and its table (0) |
| +0x08 u32, +0x0C u32 | Its parts: a `DEFAULT_PARTS` row (table 36), e.g. `df_pt_sim_levin_bzr_98` |
| +0x10 u32, +0x14 u32 | Its settings: a `DEFAULT_PARAM` row (table 37), e.g. `df_pr_sim_levin_bzr_98` |

| Table (bytes) | What |
|---|---|
| `DEFAULT_PARAM` (37) | An entered car's settings, copied into the equipment record as they are (0x444860): +0x00 u16 the output factor, per mille (into +0x15E, only for a record that names an `ENEMY_CARS` row); +0x02 u16 final drive (x 1000); +0x04 / +0x06 u16 front / rear ride height, mm; +0x08 / +0x0A front / rear spring (+0x09 / +0x0B unread); +0x0C / +0x0D downforce; +0x0E .. +0x10 the front differential's initial, acceleration, deceleration, +0x11 .. +0x13 the rear's; +0x14 the gearbox slider; +0x15 / +0x16 camber; +0x17 / +0x18 toe; +0x19 .. +0x20 the eight damper indices in the record's order; +0x21 / +0x22 anti-roll; +0x23 the first ASCC index; +0x24 the TCS index (✔ 37 bytes) |

Labels name the event's series and the car (`gt4tgs_rr_civic_sir2_91`). A car's
`ARCADEINFO_NORMAL` row's entries 6 and 7 are `ENEMY_CARS` rows (table 0x26), not events (✔ the
Ford Ka '01's: rows 3627 and 4164, its six event entries -1).

## Mod cars

Code: `mod_cars.*` (the files: docs/PLAYING.md "Mod cars"; the menus' lists: `src/gt4r/menu/mod_car_lists.cpp`; online:
`src/gt4r/net/mod_car_package.*`). Not the program's: what the database adds for the mods' cars, from the disc's rows.

- **Mod makers** (`mod_makers.*`): the list's `makers/<key>.ini` files, read before the cars. Each new maker takes the next
  number past the program's makers' list and past its tuners' list (`menu::ProgramTables`, each < 256), and its country's
  number from the program's countries' list by its code; a car naming it gets them in `GENERIC_CAR` +0x0F (maker), +0x10 (the
  brand, the tuner the pages name) and +0x0E (country). The menus (`menu/mod_car_lists.cpp`) put the key on the database's
  makers' and tuners' lists, its name and description in the text `TunerName` and `TunerDoc` (Runtime::add_text), a
  `<key>_carlist` / `_carnum` in the dealers' lists where it has cars, and the key at the end of GTmodeProject's NationData
  `<nation>_dealers` of the nation whose `<nation>2` is its code (`<key>` = that nation's `_hall`, DealerData
  `<key>_subtuner` none). Its pictures by path: `image/piece/tuner_logo2/<key>.png` (the country page's button),
  `piece/tuner_logo/<key>.img` and `piece/tuneshop_logo/<key>.img` (the dealer page's logo, its tune shop's: DealerData
  getSubTuner gives the dealer itself); `piece/tuner_screen/<key>.strb` (the page's top film) has none.
- A mod switched off or removed while the game runs (`SpecDb::remove_mod_cars`, `ModCars::remove`): the cars of the files no
  longer listed leave every table and label list (the rows each car added, its paint rows, its CARS/ file, its entries in
  the pools it joined), the menus' lists (`<maker>_carlist`, `_carnum`; a mod maker's emptied list goes) and the garage's
  places as a load without them makes them (`GarageEntry::missing`: the same save is written); the cars are made again
  under the same ids when the mod is back (`refresh_mod_cars`), and the garage's places are cars again.
- The database reads the file layer's list of the mods' car files (`mods/.cars`) when its first table, label list or pool
  is read, works the cars out from the disc's tables, and adds their rows to every table, list and pool from then on.
- A car's rows have ids its label gives (FNV-1a over the label, the table and a number, in 0x40000000 .. 0x7FFFFFFF, above
  every disc row), so a save keeps it by the same code whatever other mods are loaded.
- The donor's rows the car names are its sets (the `DEFAULT_PARTS` row its `GENERIC_CAR` row names; `df_pt_`, `df_pt_sim_`,
  `df_pt_d_`, `df_pt_s_<donor>`) and the parts that fit it (its `CARS/` file). Of `BRAKE`, `SUSPENSION`, `CHASSIS`,
  `RACINGMODIFY`, `FRONTTIRE`, `REARTIRE` every one is cloned (the shop's parts take the car's weight, tires and treads too);
  of `ENGINE`, `DRIVETRAIN`, `GEAR` the stock rows; the other tables' rows (the shop's parts by percent, the shared steering,
  compounds and sizes) stay the donor's. Labels are the donor's with its label replaced by the car's (`ch_<car>_a`).
- The values written: `CHASSIS` the dimensions and weight (+0x00 .. +0x0D), front weight, the yaw inertia scaled as
  mass x (length² + width²), the center of gravity's term by the height, the widths across the tires and the wheels' tops
  by the tires' change; `RACINGMODIFY` the treads (track + twice the wheel offset) and the drag coefficient; `SUSPENSION` the
  spring rates by each axle's load, the ride heights; `BRAKE` the torques by the weight; `ENGINE` the curve (the donor's
  scaled, or the file's), the catalog power and torque and their speeds' texts, the limits, idle, output factor, sound
  number and texts; `DRIVETRAIN` the layout, a four wheel drive system the disc's four wheel drive cars have most when the
  car becomes one, the front share; `GEAR` the ratios as written (a box of steps), reverse, the final drive; the tires'
  `TIRESIZE` row (the disc's of that size, else a new one) and copies of the stock tires' compounds with the grip scaled.
- Its own: a `GENERIC_CAR` row (the donor's with its set, price, year, maker with the maker's country and the brand
  (+0x10) most of the maker's cars have, category, for sale),
  new `DEFAULT_PARTS` sets, `CAR_NAME_<language>` rows (the name in each language's pool), `CAR_VARIATION_<language>` rows
  naming a new paint group, which is the body car's `VARIATION<language>` rows with the box and frontal area scaled, under
  the body car's label (so the car's model files are the body car's), the donor's `ARCADEINFO_NORMAL` row with its dirt and
  snow `ENEMY_CARS` rows made the car's, the donor's `TUNER_LIST` row, and a `CARS/` file of the donor's parts (its clones
  in their places).
- `[paints]` (a body of its own's): the paint group's rows are one per paint, the body car's first row as the pattern, +0x04
  the paint's number + 1, +0x0C its name (each language's pool), +0x24 its color (one color).
- The body: not in the tables. The race and the menus draw the body car's model (`race/mod_car_body.*`): stretched from the
  body car's length, wheelbase, height (its stock `CHASSIS`) and width (its paints' box) to the car's, or a body of the mod's
  own (the engine's glTF loader) in its place.
- `[opponent] races`: an `ENEMY_CARS` row of the car as sold (its `GENERIC_CAR` set, no settings: the pools' own cars have
  none) at the end of each named race's pool (`RACES/<35, row>`, `SpecDb::race_file`), which the race draws from as it draws
  the disc's.
- An exported disc car (`car_export.*`, the Car database's Export): a records file of every row the car names (from its
  `GENERIC_CAR` row: its sets, the parts they and its `CARS/` file name, tire sizes, compounds and force rows, the engine's
  texts, `ENEMY_CARS` and `DEFAULT_PARAM` rows of its Arcade versions, the `RACE` rows by label, its names and paints in
  every language, its `ARCADEINFO_NORMAL` and `TUNER_LIST` rows), the texts they name and its `CARS/` file. A disc without
  the car adds them under the car's label: a row whose label the disc has in that table is the disc's (a `RACE` row by its
  label, none when it lacks it), every other row new, the references (sets, tire rows, `CARS/` codes, texts, the paint
  group) made to the rows as added; the names and paints per language from the same language, else American; the paint
  group's label the renamed model's.
- A mod installed while the game runs: its cars are worked out on the tables as they are then (their ids kept clear of the
  cars in) and added to every table, list and pool already read (`SpecDb::add_mod_cars`); the menus add them to the makers'
  lists they have read (`menu::refresh_mod_cars`).
