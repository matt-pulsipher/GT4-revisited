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
| `ENGINE`, `CHASSIS`, `GEAR`, `DRIVETRAIN`, `LSD`, `SUSPENSION`, `BRAKE`, `TIRESIZE`, `TIRECOMPOUND`, `TIREFORCEVOL`, `TURBINEKIT`, `MUFFLER`, ... | | The parts, a table per kind as in Gran Turismo 3's database ❓ columns |
| `DEFAULT_PARTS` | 2,990 x 208 | The parts a car has as sold |
| `RACE` | 709 x 151 | The events |
| `ENEMY_CARS` | 2,558 x 24 | The opponents |

Every one of the 722 cars' models is on the disc.
