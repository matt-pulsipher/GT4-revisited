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
| `GENERIC_CAR` | 722 x 24 | The cars; labels like `_117_coupe_68`. +0x0C u16 model year ❓ the rest |
| `CAR_NAME_<language>` | 804 x 16 | Per car id: four string numbers, the first the car's name |
| `CAR_VARIATION_<language>` | 722 x 8 | Per car id: the id of its rows in the paint table |
| `VARIATION<language>` | 3,849 x 52 | A row per paint of a car, the rows of a car sharing an id; labelled `<model>_<n>`, where `<model>` names the car's files (`car/lod/<model>`, `car/menu/<model>`) |
| `COURSE` / `COURSE_NAME_<language>` | 23 x 4 | The courses; the label is the course file's name (`capri` is "Amalfi") |
| `CHASSIS` | 722 x 37 | u16 mm: +0x00 length, +0x02 height, +0x04 wheelbase, +0x06 weight (kg), +0x12 / +0x14 width across the front / rear tires, +0x16 / +0x18 front / rear tire diameter (checked on the AC 427 S/C: 3962, 1244, 2286; the Lancer Evolution II: 4310, 1420, 2510, 1250 kg) |
| `TIRESIZE` | 286 x 4 | +0 u8 aspect / 5, +2 u8 rim inches x 10, +3 u8 width / 5 mm (205/60R15 is `0c 00 96 29`) |
| `FRONTTIRE`, `REARTIRE` | | Pairs of u32 row id, u32 table id; the first is the tire's `TIRESIZE` row |
| `ENGINE`, `GEAR`, `DRIVETRAIN`, `LSD`, `SUSPENSION`, `BRAKE`, `TIRESIZE`, `TIRECOMPOUND`, `TIREFORCEVOL`, `TURBINEKIT`, `MUFFLER`, ... | | The parts, a table per kind as in Gran Turismo 3's database ❓ columns |
| `DEFAULT_PARTS` | 2,990 x 208 | The parts a car has as sold, labelled `df_pt_<car label>`: 26 pairs of u32 row id, u32 table id (the label lists' ids: 6 `CHASSIS`, 12 `ENGINE`, 25 `FRONTTIRE`, ...; -1: none) |
| `RACE` | 709 x 151 | The events |
| `ENEMY_CARS` | 2,558 x 24 | The opponents |

Every one of the 722 cars' models is on the disc.

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
