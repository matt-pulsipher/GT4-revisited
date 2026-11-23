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
| `RACE` | 709 x 151 | The events |
| `ENEMY_CARS` | 2,558 x 24 | The opponents |

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
| `ARCADEINFO_NORMAL` (721 x 65) | 8 x {u32 row, u32 table 0x26}, 1 byte | The car's Arcade events, rows of `RACE`: entry [difficulty 0 to 2] on a dry course, [3 + difficulty] otherwise (0x12E150) |
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

❓ Not read: the `RACE` table's columns (151 bytes), `ENEMY_CARS`, which cars have no manual
gears, the price of parts.
