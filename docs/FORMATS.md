# File formats

Worked out from retail discs: Gran Turismo 4 `SCUS-97328` v1.01 and Gran Turismo 4 Prologue
`SCPS-15055` v1.01. All values are little endian unless marked. ✅ implemented, 🔎 partly
understood, ❓ not yet investigated. Formats shared with Gran Turismo 3 are documented in the
shared engine ([engine/gt3r/docs/FORMATS.md](../engine/gt3r/docs/FORMATS.md)) and only named
here.

## Disc layout ✅ (`src/gt4r/disc/disc.*`)

ISO9660, 2048-byte sectors. The retail disc has two layers, each with its own file system:
the second's primary volume descriptor is the sector where the first's volume ends (as on the
dual-layer Gran Turismo Concept disc).

| File | Purpose |
|---|---|
| `SYSTEM.CNF` | `BOOT2 = cdrom0:\SCUS_973.28;1`, `VER`, `VMODE` |
| `SCUS_973.28` | Boot ELF: loads IOP modules, then `CORE.GT4` |
| `CORE.GT4` | The game program, packed: the same container as `CORE.GT3`, three segments |
| `IRX/`, `IOPRP300.IMG`, `NET/`, `EPSON/` | IOP modules, network configuration, printer drivers |
| `GT4.VOL` (layer 0) | The game's files: 18,352 |
| `GT4L1.VOL` (layer 1) | The movies: 67 files |

Prologue is a single layer with one `GT4.VOL` (2,592 files).

## Volumes ✅ (`src/gt4r/disc/archive_index.*`)

A volume starts with a table of contents in Gran Turismo 3's layout (magic `0xACB990AD`,
u16 2, u16 2, ...).

- **Prologue** reads that table. A file flagged as packed (flag 2) is packed as below, not a
  gzip member.
- **Gran Turismo 4** does not read it: its 2,722 entries are leftovers (the same table is in
  both volumes, and most entries point at unrelated data). The index the game reads starts on
  a later 0x800-byte page (page 0x2159 of both retail volumes; found by its header):

```
0x00 u32 magic 0xACB990AD, u16 1, u16 3
0x08 u32 index size in bytes (header, page table, packed pages)
0x0C u32 pages the index takes: file data starts at index + pages x 0x800
0x10 u16 page size (0x800), u16 packed page count
0x40 u32 page ends[count + 1], each XOR 0x14AC327A x (its position + 1);
     page i is the bytes [ends[i], ends[i + 1]) from the index start
```

Each page is a raw DEFLATE stream of 0x800 bytes, every byte of the result XOR 0x55. Only
one of the 407 pages' streams has an end marker, so a reader stops at 0x800 bytes.

```
0x00 s16 kind      0: entries; else a search page (keys to find a page by name; not needed
                   to list the files)
0x02 s16 items     two per entry (its key and its value)
0x04 s32 next page, s32 previous page
...                keys and values, from 0x0C
end  per entry, backwards from the page's end:
     u16 key offset, u16 key size, u16 value offset, u16 value size
```

- Key: u32 parent folder id, **big endian**, then the name (not terminated).
- Value: u8 kind, then
  - 0 folder: u32 id
  - 1 file: u32 page, u32 time (seconds since 1970), u32 size
  - 2 packed file: u32 page, u32 time, u32 stored size, u32 size

The root folder is `.` (id 1, parent 0). A file's data is at index start + (index pages +
its page) x 0x800. Names keep their case (`projects/arcade/US/CarRoot.gpb`).

**Packed files**: `C5 EE F7 FF`, s32 minus the unpacked size, then a raw DEFLATE stream.
14,261 of the 18,419 files are packed.

A disc's fingerprint (`DiscInfo::digest`) is SHA-256 over the packed `CORE.GT4` and each
volume's index bytes (Prologue: its table of contents).

## What is on the disc

`gt4tool survey` prints this from a disc. Gran Turismo 4, by folder:

| Folder | Files | Contents |
|---|---|---|
| `piece/` | 5,074 | `Tex1` pictures for the menus (logos, car pictures), `STRB`, prize `MDLS` |
| `sound/` | 3,604 | `ENGN` engine sounds, `INST` banks, `GTSE` effect indexes |
| `car/` | 2,575 | `CAR4` car models and `Pat0` paint data |
| `wheel/`, `tire/`, `wing/` | 2,575 | `MDLS` models |
| `specdb/` | 1,801 | the car database: `GTDB` tables, `GTID` ids, `GTST` strings |
| `replay/` | 738 | demonstration replays as memory-card saves |
| `projects/` | 671 | the menus: `ADCH` scripts, layouts as text, `gpb1`/`gpb2` pictures, `RT03` text |
| `usedcar/`, `config/`, `script/` | 499 | `ADCH` scripts, `.mwidget` layouts |
| `crs/` | 348 | courses, their driving lines (`MDLS`) and `.ad` files |
| `bgm/`, `narration/` | 260 | `SShd` streams, `MADS` index, `BMRK` markers |
| `mpeg/` | 67 | MPEG-2 movies, `ipua` stills |
| others | | fonts, music sequences, race display pictures, cameras, icons |

## Formats

| Format | Where | State |
|---|---|---|
| `Tex1` texture sets | `.img` files; inside cars, courses, models, menus | ✅ the shared decoder, unchanged |
| `SShd` streams (`.ads`) | `bgm/`, `narration/` | ✅ shared, unchanged |
| MPEG-2 movies (`.pss`), `ipua` stills | `mpeg/` | ✅ shared, unchanged |
| `ENGN` engine sounds | `sound/engine/` and others | ✅ shared, unchanged (5 of 3,576 files fail a layer-order check) |
| `GTRC` camera scripts | `cameras/` | ✅ shared parser reads them |
| `.pmb` menu pages | `menu/` (the race pause menus) | ✅ shared parser reads them |
| `MLTX` strings, `GTBR` brake parts, `MSEQ` music index, `race/flare.bin` | | ✅ shared parsers read them |
| Course sections `Vls0`, `CRSS` | `crs/` | ✅ shared parsers read them |
| `INST` instrument banks | `sound/`, `music/` | ❓ newer layout than Gran Turismo 3's |
| `Ssqt` sequences (`.sqt`) | `music/` | ❓ |
| `MADS` music index | `bgm/` | ❓ newer layout |
| `FT01` fonts | `font/*.fnt` | ✅ ([formats/menu.md](formats/menu.md)); `KNJI` (`font/*.kf`) ❓ a newer layout |
| `MDLS` models | cars, wheels, tires, wings, courses, prizes, characters | ✅ still models ([formats/model.md](formats/model.md)); ❓ scripts, callbacks (a car's wheels), skinned sets |
| `CAR4`, `Pat0` | `car/` | ❓ |
| Courses | `crs/` | ✅ the container and the scenery ([formats/course.md](formats/course.md)); the road `RNW4`; ❓ billboards, lamps, cameras |
| `GTDB` / `GTID` / `GTST` | `specdb/` | ✅ the tables, labels and strings ([formats/database.md](formats/database.md)); ❓ most columns |
| `ADCH` scripts | the menus, game flow, settings | ✅ read, listed and run by the interpreter, waits included ([formats/script.md](formats/script.md)); the menus' classes of the program are written ([formats/menu.md](formats/menu.md)) |
| Layouts (`.mproject`, `.mwidget`: text), `gpb0` / `gpb1` / `gpb2` pictures, `RT02` / `RT03` text | the menus | ✅ read, drawn, and run from the game's scripts ([formats/menu.md](formats/menu.md)) |
| Replays (`REP4`), saves | | ❓ |

Prologue mixes the two generations: `CAR4` cars and `ADCH` scripts next to `GTM1` models,
`GTTR` tires, `IMGS` picture sets and `GTDT` tables in Gran Turismo 3's formats, `gpb0`
pictures and `RT02` text.
