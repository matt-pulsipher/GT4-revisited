# The saved game (a memory card's `BASCUS-97328GAMEDATA`)

Code: `src/gt4r/save/` (`codec.*` the scrambling, `game_data.*` the game data's blocks,
`memory_card.*` the card's folder), behind `menu::GameState` and the card manager the scripts
call (`src/gt4r/menu/game_natives.cpp`); `tests/test_save.cpp`, `tests/test_menu_run.cpp`
(`disc_menu_game_saved_and_loaded`); `gt4tool save`.

Addresses are the US program's (SCUS-97328 v1.01). Integers little endian. "MT" is the
standard 32-bit Mersenne Twister (`init_genrand`, multiplier 0x6C078965, standard tempering:
0x579438, 0x5795E0, 0x5794C0); "the generator" is the program's small one (0x579330):
`s = s x 17 + 17`, it gives `s XOR (s rotated by 16)`.

How it was checked: the program's own save routines (the Status constructor 0x43A528, size
0x43AA00, write 0x43AAF0, check 0x43AA68, read 0x43ABA0, the file encoder 0x1D4D10 and decoder
0x1D4E60, the header coder 0x1CB900 / 0x1CB9B8, the bit shuffle 0x1D4B78) were run in an
interpreter of the console's processor on made-up data and compared with a reimplementation
from these facts, both ways; a new game's payload written by `src/gt4r/save` is **byte for byte
the program's** (including its CRC). A save the original game wrote on a memory card (its four
files taken from the card's image) reads: the player, the credits, the day, the car with its
record; written again from what was read it is the same bytes (`gt4tool save <disc> show
<card folder>`), and the menus run from it. The state after a new game in the running original
(a captured memory image at the map) gives the same blocks as ours after the same steps, but
the name typed, the garage's key and the display's numbers.

## The folder

The game's folder on the card is `BA` + the product code + `GAMEDATA` (`BASCUS-97328GAMEDATA`;
0x1CC958, 0x1CDA48). Here the card is a folder of the data folder (`<data>/memorycard/`), and
the save is that folder as on a card:

| File | What | Size |
|---|---|---|
| `BASCUS-97328GAMEDATA` (the folder's name again, 0x1CC158) | the game data, scrambled | 237,760 (0x3A0C0) |
| `garage` | the garage's file: a 64-byte header, then 1,000 car slots of 0x500 | 1,280,064 |
| `icon.sys` | the disc's `icon/icon.sys` with the title and the icon's names | 964 |
| `icon0.ico` | a copy of the disc's `icon/GT4_Gamedata.ico` | 9,796 |

A new save writes them in the order `garage`, the game data, `icon0.ico`, `icon.sys`. The
space the game asks for is the files' KB, plus one KB for two entries and one (0x1CC3C0):
1,498 KB, the number the game's first dialog names.

To use a save from a PlayStation 2 card, copy its `BASCUS-97328GAMEDATA` folder (all four
files, from any tool that copies a card's files as they are) into `<data>/memorycard/`; the
same folder written here goes back onto a card the same way.

## A card image

An 8 MB card image (`.ps2`, `.mcd`; with or without the 16 spare bytes a page) is read and made
by the shared engine's card layout (`formats/ps2/memcard.h`); `src/gt4r/save/card_image.*`:

- **Into the game**: `gt4tool save <disc> import <image> <data>/memorycard` copies the image's
  `BASCUS-97328GAMEDATA` folder in; or put the image itself in `<data>/memorycard/`: when that
  folder has no game of its own, the first image by name that holds one is copied in the first
  time the menus ask for the card (the game then loads it at power on as from a console's
  card).
- **Out of the game**: `gt4tool save <disc> export <data>/memorycard <out image> [another
  image]` writes the folder onto a newly formatted card, or onto a copy of another card's image
  with that card's other saves kept (a game folder already on it is replaced). The files go in
  the order the game writes them; their directory entries have the modes the game's own card
  has (folder 0x8427, files 0x8497).

- **A game with progress** (for checks against the original): `gt4tool save <disc> progress <data
  folder> [name] [credits]` runs the menus from power on to a new game on that folder's card,
  then buys cars at the dealers as the purchase page does (`addNewCar`, the diary's day, the car
  open in Arcade), drives the last one, passes every license test (the result routine of a
  license test, with the test's medal time), wins events (each race through the result routine of
  a race or of a championship's race, first place, a day each: every Beginner and Professional
  event but the Gran Turismo World Championship, then the Japanese, American and European halls'
  events until the completion is 26.0 % or more), lets Gran Turismo mode's present page give what
  the records entitle the player to (the licenses' papers and cars, cars by completion, courses
  by the weeks gone by), drives the Ford GT '05 and saves (`src/gt4r/menu/game_progress.*`).
  `export` then puts it on a card image. What that opens, by Gran Turismo mode's own script:
  - **Completion** (Home's "Complete", `getRaceEventRecordStatistics`): the races won (result
    "1") per mille of every race of the event lists (`EventData::AllEvents`, each event's races
    and a championship's own record) and the missions (`l0m0001` on), in integers. The card made
    this way reads 26.8 %.
  - **The Endurance hall** (`canEntryEndurance`): completion 250 per mille or more.
  - **The Gran Turismo World Championship** (`canEntryGTWorldChamp`, on its event's entry):
    every other Beginner and Professional event won (`wonRace`: a championship's own record, else
    every race of the event).
  - **The Extreme hall** (`canEntryExtreme`): the Gran Turismo World Championship won; left shut.
  - **The Supercar Festival**: 493 HP or more (the reference's message); the Ford GT has 558.
  Checked in the original with this card: Complete 26.8 %, the Endurance hall's 16 races, the Supercar Festival and the
  World Championship enterable (a won event's entry asks whether to clear its records first).
  - **Missions**: each mission's license. **Photo travel**: its places (`EventData::PhotoStageList`)
    open as Arcade's courses do; 40 weeks in, all 15 are open.

Checked: the reference card made by the running original (a new game and a used car) imports
and loads (the player, credits, the car with its record; written again the same bytes);
exported again onto a new card and onto another of the reference cards, both read back file for
file the same. A card made here loads in the original (US 1.01; a progressed game from
`progress`, exported onto a new card): it boots straight to Gran Turismo mode's map with the
player, credits, day, the 22 cars, the licenses and their rankings as written. Its option block
then needed the screen's figures (+0x24, below): without them the original's license test drew
its display over a black picture.

## The game's other saves (🔎 not written here; the Replay Theater lists the race's own replay files)

The card manager's modes (0x179570's table) name the other kinds of save: `MODE_LOAD_REPLAY`,
`MODE_SAVE_FULL_REPLAY`, `MODE_LOAD_BESTLAP_REPLAY`, `MODE_SAVE_BESTLAP_REPLAY`, the photo
films and pictures, the replay and slide-show play lists, the patch. Each is a file class of
`GT4MC::FileGT4` (RTTI): `FileGT4ReplayFull` (the data's name `ReplayData`, icons
`icon/GT4_Rep_to_race.ico`, `_to_license`, `_tt1`), `FileGT4ReplayBest` (`GhostData`),
`FileGT4ReplayDemo`, `FileGT4FilmData` (`F%04d`), `FileGT4PictureData` (`P%04d`),
`FileGT4ReplayList` (`LSTRPLAY`), `FileGT4SlideShowList` (`LSTSLIDE`). The Replay Theater is the
program's sequence `load_replay` (the title's third item asks for it), which runs the project
`labomode` in `MODE_LOAD_REPLAY`: its scripts list the card's replays through the manager's
`list`, `count`, `getFile(i)` and each file's `title`, `racemode`, `racesubmode`, `size`,
`load`, `remove`, `isInfoValid`, `writeInfo`. Not read further: the folders' names and the
replay's own layout. Here MODE_LOAD_REPLAY lists the replays the race saved in the data
folder's `replays/` (`menu/game_replays.cpp`; menu.md "The Replay Theater").

## Results the scripts see (0x1CBF40)

0 `OK`, 1 `DEVICE_ERROR`, 2 `INVALID_FILE`, 3 `FILE_NOT_FOUND`, 4 `DIFFERENT_MEMORYCARD`,
5 `NO_CARD`, 6 `NO_OPERATION`. `file.isExist(manager)`: 0 the game's data is there, 1 not, -1
the card cannot be read. `file.load(manager)` answers `OK`, `DEVICE_ERROR` or `INVALID_FILE`,
and "" for any other result.

## The game data file

A payload of N bytes is stored in `S = (N + 0x47) & ~0x3F` bytes:

| Off | Size | |
|---|---|---|
| 0 | 4 | R: any word (the program takes a clock and its generator) |
| 4 | 4 | C = CRC-32 of bytes 8 .. S (the payload and its zero padding), XOR the key (0 for the game data) |
| 8 | S - 8 | the payload, enciphered |

- The standard CRC-32 (reflected, 0xEDB88320, start and end inverted; 0x572C90).
- Enciphering (0x5D2680; back 0x5D2750): MT seeded with `C + R`, the generator seeded with
  `C XOR R`; for each 32-bit word, `a` from MT first, then `b` from the generator:
  `w' = (w XOR a) - b`; back `w = (w' + b) XOR a`.
- Then two 4-byte swaps (0x1D4CA8; back 0x1D4C68 in the other order): the word at 4 with the
  word at `4 + trunc(0.135790... x (S - 8))`, then the word at 0 with the word at
  `trunc(0.654860... x (S - 4))`. The two factors are the single-precision constants
  0x3E0B0C88 and 0x3F27A4E7; the products are whole numbers of the exact product (the
  console's toward-zero rounding gives the same).

Reading undoes the swaps, deciphers, and refuses a CRC that does not match (`INVALID_FILE`).

## The garage file

Money and the full records of the cars not driven are here, tied to the game data by a 32-bit
key and a serial the game data keeps (key at payload 0x135E4, serial at 0x135F8).

**Header**, plain (0x1CB8D8 on):

| Off | |
|---|---|
| 0x00, 0x04 | the credits: high, low 32 bits |
| 0x08 .. 0x2B | 0 |
| 0x2C, 0x30 | two serials |
| 0x34 | the stamp before |
| 0x38 | the key |
| 0x3C | the stamp: the one before plus a clock word, never 0 |

It matches a game (0x1CBA80) when its key is the game's, one of its serials is the game's,
and (once a header was seen this run) one of its stamps is the one last seen; else
`DIFFERENT_MEMORYCARD`. Saving keeps the serial slot holding the game's serial and gives the
other `serial + 1`, writes the game data with the serial it had, then takes `serial + 1`: a
new save has {0, 1}, its first save {2, 1} with the game data holding 1.

Coding (0x1CB900): with MT seeded `key + stamp`, bytes 0 .. 0x3B XOR the low byte of each
draw; the stamp XOR the generator's first draw (seeded with the key); then the 512 bits
shuffled by MT seeded with the generator's second draw. Back (0x1CB9B8) in the other order.

**The bit shuffle** (0x5D2818, back 0x5D2900): for `i = n - 1` down to 1, `j = trunc(u x (i +
1))` with `u` the draw's top 24 significant bits over 2^32 (single precision, toward zero), and
bits `i` and `j` swap (bit k is byte k / 8, mask `1 << (k % 8)`). Back: the same `j` list,
swapped from `i = 1` up.

**A slot** (0x1CEAA8, read 0x1CEEC0): slot i at `0x40 + i x 0x500`: u32 T (a clock word), u32
CRC-32 of the record's 0x4C0 bytes (0x4B8 and 8 zeros), then the record; the 0x4F8 bytes from
+8 enciphered as the game data's with MT seeded `key XOR T` and the generator seeded with the
CRC; then all 0x500 x 8 bits shuffled by MT seeded `key + i`. A new garage file (0x1CE630)
fills every slot with noise (`low 8 bits of (generator draw XOR MT draw)`, both seeded with the
key, MT first), so only the slots of cars that have been driven hold records.

🔎 The program writes a car's slot when the car driven changes (`MGarage.changeRidingCar`,
`pushRidingCar`, 0x16E350, 0x16EAA0). Here the records are kept in the game's state and every
car's slot is written when the game is saved: the files are the same once saved.

## icon.sys (0x1CD088)

The disc's `icon/icon.sys` (the standard `PS2D` layout) with: the title at +0xC0 in
Shift-JIS, line one the program's text at 0x694DD8 (`ＧＲＡＮ　ＴＵＲＩＳＭＯ　４`, 14
full-width letters) and line two the title the script passes (`FILENAME|game_data` of the
card's text) in full-width letters (0x1D0438); the u16 at +6 the byte where line two starts
(28); the three icon names at +0x104, +0x144, +0x184 `icon0.ico`.

## The game data (N = 0x3A080)

The program's `GranTurismo4::Status` written as fixed blocks, each padded with zeros to 16
bytes (the sizes below without the padding). Header: u32 0xFF60BAD3, i32 minus the body's
length (-(N - 0x10)), u32 CRC-32 of the body (0x10 .. N), 0. A payload is the game's when all
three match (0x43AA68); there is no version: another size is another program.

| Payload | Size | Block | GameState |
|---|---|---|---|
| 0x00010 | 0x218 | the player: name char[0x20], password char[8], last entry name char[0x10], score u64 (+0x38), prize credits u64, prize cars u32, finishes u32[7] (0 not finished, 1 .. 6 the places), A-spec and B-spec distance u64 (+0x68, +0x70), withdrawn u32, withdrawn (Prologue) u32, u8 (`met_type`); at +0x94 (payload 0xA4, Status +0x3E4) the B-spec driver's book, u16 [3][8][8] (below); the rest not named | `username` ... `met_type`, `bspec_book` |
| 0x00230 | 4 + 2,922 x 0x10 | the diary: the days kept, then the days oldest first (see "The calendar") | `diary` |
| 0x0B8D4 | 4 | today, a day number | `today` |
| 0x0B8E0 | 1,000 x 0x20 | the garage's list (below) | `garage` |
| 0x135E0 | 0x1C | the car driven's place (-1 none), the key, four words (not named), the serial | `riding_car`, `garage_key`, `garage_serial` |
| 0x135FC | 0x4B8 | the car driven's record (below; no car: three cleared equipment records) | the entry's `record` |
| 0x13AC0 | 768 x 4 | the races' record units | `race_units` |
| 0x146C0 | 128 x 0x238 | the courses' rankings | `course_record` |
| 0x262C0 | 4 x 0x238 | the machine tests' rankings | `machine_record` |
| 0x26BA0 | 125 x 0x1F8 | the license tests' rankings | `license_record` |
| 0x361C0 | 0x2404 | Arcade's unlocked cars (u64 codes [1024], ascending, -1 after), courses ([128]), "all" (u32) | `available_*`, `all_available` |
| 0x385D0, 0x386E0 | 0x108 each | favorite cars, courses: u32 limit (30), u32 count, u64 codes [32] | `favorite_*` |
| 0x387F0 | 0x74 | presents given: a bit a race (0 .. 867), then a bit a time present | `presents` |
| 0x38870 | 0x88 | the championship left (0x119BD8): its label char[0x20], u8 kept (1: left in the middle), u8 races run, u8 race count, 5 bytes, then six entrants {u64 the car's code, s32 paint, s32 points} (the player first; 🔎 for an opponent the program writes its entry's second code, here the car's) | `suspended_*`, `series`, `championship_rest` |
| 0x38900 | 0x24 | used cars sold: 256 bits, the week they belong to | `used_sold`, `used_week` |
| 0x38930 | 0x11C0 | the options (Option +8: its size 0x11C0, then the fields; the play lists and the fields below) | `options`, `bgm_playlist`, `slide_playlist`, `monitor_params` |
| 0x39AF0 | 4 | the zone (0 UNDEF, 1 JP, 2 US, 3 UK, 4 EU, 5 KR) | `options["game_zone"]` |
| 0x39B00 | 2 x 0x60 | the two players' button maps (not decoded) | kept |
| 0x39BC0 | 0xAC | the LAN settings: names "Player1", ... | kept |
| 0x39C6C | 0x258 | the network settings (not decoded) | kept |
| 0x39ECC | 0x50 | event settings: char name[0x40], u16 Arcade's time limit | kept |
| 0x39F1C | 5 | the logger's (1 each) | kept |
| 0x39F30 | 0x144 | the menus' place: project char[0x20], page char[0x20], the kept menu stack: i32 its length in bytes (-1: none kept), the names each ended by a 0 (0x100; 0x42FE88) | `major_menu_*`, `saved_menu_*` |

What the state does not name is kept as it was loaded (`GameState::saved_image`); a new game
starts from the constructors' values (`save::new_game_payload`: the option block's numbers,
the play lists' empty rows, the button maps, the LAN names, three network words, the logger's
ones). Not saved: the race being set up, `isNeedSave`, and the credits (the garage file's).

Codes are 64-bit `{u32 row, u32 table}` of the car database: cars table 0, courses 0x22, races
0x23, wheels 29, wings 30.

### The B-spec driver's book (payload 0xA4, 0x180 bytes; menu/game_bspec.h)

Three tables of 8 x 8 u16 (table t, row r, column c at 2 (64 t + 8 r + c)): the B-spec driver's experience, a
column a class of the car (0x468418: the parameter block's +0xFE0 rating, (x - 0.35) / 0.55 within [0, 1], x 8, at
most 7). The cell [7][0] of tables 1 and 2 holds instead the sum of the courses' marks (each course record's
+0x228 .. +0x22F: two sets of four bytes). Before a race the program copies the car's column and the course's marks
into the driver's record of 0x40 bytes (0x468710); after it, it writes the record back (0x468860: as it is, or with
the change doubled, at most 0xFFFF; the marks likewise, at most 255, their change added to the sums). The change is
doubled (0x468A60) when the run's conductor's vf6 gives 0 (the base 0x358248: races, free practice, missions and
training, 0x3F7050; a free run 0x3F5518, a license test 0x3F5FA0 and a machine test 0x3F6990 give 1), of two cars or
more (cond + 0xF8A0, the car count), that the car finished (its +0x5B6) first (its place byte +0x5B3 at 1). The course's
marks are read and written (0x468A40) only when the race's byte cond + 0xCBE7 is set: 0x34C7E0 fills cond + 0xCBE6 ..
+0xCBED from the course's attribute text (0x447F08, `MCourseData.GetAttrString`), a character a value (digits, hex
letters); +0xCBE7 is the second character's: Tsukuba "1ff0" set, Grand Canyon "0000" not. The record is written back only over the book and marks the
run started from (`RaceResult::bspec_start`), and a doubled change below zero, which the program's 16-bit store would wrap,
is not written (❓ whether a run can lower its record is not read). Tests: `disc_gt_mode_bspec_book_after_a_race`,
`disc_race_request_*`. The Status
page's figures (0x468B58): Machine (0x467C58), Course (0x467D90), Battle (0x467F60), each 0 .. 100, and the B-spec
Skill (0x4680D8) 0 .. 10000, weighed by the program's 0x623E40 (8 .. 15). ✔ Equal to the program's on made-up books
(`gt4r_diff <disc> book`) and to the original's Status page on the reference's card (29-bspec-record: 508 / 10 / 7 /
0, then 524 after a race).

### The garage's list (0x20 an entry; 0x434D80 .. 0x435958)

| Off | |
|---|---|
| 0x00 | u64 the car's code (-1: a place never used) |
| 0x08 | u64: bit 0 in use, bit 1 has a record (it has been driven), bits 2-8 the paint, bits 9-26 power (PS x 100), bits 32-44 weight (kg) (0x434FD0, the list's columns) |
| 0x10 | low 24 bits: the day got, as days after 2005-04-02; the top byte kept |
| 0x14 | u32 the odometer it came with (meters) |
| 0x18 | low 10 bits: the order of driving (0 the car driven; 0x3FF never driven) |

A free place: FF x 8, 0, 0, 0, 0x3FF. A car sold keeps its place (bit 0 cleared); the list is
never packed. A car never driven is the car as sold in its paint, with its odometer.

### The car record (0x4B8; CarGarage 0x43DC20)

| Off | |
|---|---|
| 0x000, 0x178, 0x2F0 | three settings sheets (Set A, B, C), each a car equipment record (`docs/specs/physics.md` §4.1; 0x0F0 is the wheel, 0x100 the wing) |
| 0x468 | u64 [4]: the wheel chosen for each tire category (road, racing, dirt, snow) |
| 0x488 | u32 the sheet in use (`MCarGarage.slot`) |
| 0x48C | 96 bits: the parts owned |
| 0x498 | the condition: u32 odometer (m), f32 engine distance (m; -300,000 new), f32 oil distance, f32 dirt (0 .. 0.8), f32 body distance, u8 no wear |

The race's car is the sheet in use: `garage[slot].record->active()` is the
`sim::Equipment` bytes.

### Records

- **A race's unit** (u32; its place is the race's row among the RACE table's ids, 0x4483B8):
  bits 0-3 the kind (0 none, 1 license, 2 event, 3 mission), 4-7 the best result, 8-11 the
  last result, 12-15 the result shown on the map, 16-23 the best score. Results: 0 .. 7 the
  places, 8 abandon, 9 gold, 10 silver, 11 bronze, 12 red, 13 failed, 14 view, 15 none. A new
  unit: 0x0000FFF0.
- **A license test's ranking** (0x1F8): 10 entries of 0x2C (u32 time ms, u16 year, u8 month,
  u8 day of the real clock, u8 result, 32 bytes of pass code, "--------" when empty), then 16
  u32 section times. Empty times are 359,999,999.
- **A course's ranking** (0x238): u64 the course's code (-1 unused; the units kept in order of
  the code), 10 entries of 0x30 (u32 time ms, or a float speed in the machine test of top
  speed; the date; u64 the car's code; the pass code), 16 u32 section times, 8 bytes, the best
  score at +0x230.

### Options kept other than by number (MOption's accessors, 0x189E38's table)

| Option + | |
|---|---|
| 0x14 | `wide_mode` (16:9) |
| 0x24 .. 0x4F | the screen's figures (11 floats: 0, 1.0, 238.157, 275, 205, 0.97, 1.0, ...) the `monitor_size` setter copies from one of three program tables (0x436D60: "subaru" 0x6A61E8, else 16:9 0x6A61B8, else 0x6A6188); a new game's constructor leaves zeros and every save the original writes has a table's figures. Here they are copied from the user's program whenever the game is saved or the options applied (`GameState::monitor_params`) |
| 0x1140 | the race display's parts, a bit each (0x437408): 0 `replay_display_enable`, 1 `race_display_map`, 2 `_steering`, 3 `_meter`, 4 `_time`, 5 `_lap`, 6 `_position`, 7 `_car_name`, 8 `_suggested_gear`, 9 `_g_meter`, 10 `race_display_config`; a new game's word 0x3FA |
| 0x1184, 0x1194 | `race_color_correction`, `replay_color_correction`: four s32 each (brightness, contrast, saturation, color balance; the scripts' arrays, here the text "b c s w"); a new game's 3 0 0 0 and 0 0 0 0 |
| 0x16B0 | `race_display_mode` (not in the saved block) |

`vibration_1p/2p` and `steering_forcefeedback_1p/2p` are the controller settings' (Option +0x11D0, the saved button maps' block; "VIBRATION", "WHEEL FEEDBACK"), not decoded.

### The play lists (Option +0x90, +0x8A0; 0x810 each)

The races' music list (`bgm_playlist`) and the slide show's (`slide_playlist`), the program's
MPlayList: u32 rows (the streamed `racing` group's pieces, 65; 0 in a new game until the
menus fill it), u16 the position in the order, u8 shuffled, u8 no row enabled, u16 the shuffle's
seed (a new game's 0x3355 and 0x3476), a word; then 128 rows of 16 bytes: 8 bytes (❓ kept),
u8 played this round, u8 enabled, s8 the place in the order (-1 none), s8 its copy, 4 bytes. The
scripts' `play_data` rows are [place, played, enabled, copy]. A saved game made by the original
after its first race has the US list: 36 of the 65 pieces, the first 15 disabled, place 0 at
row 15 (the configuration's init_raceplaylist gives the same here).

### The calendar

`today` is a day number: the proleptic Gregorian ordinal + 1,721,424 (0x5797A8); a new game
starts on 2,453,462 = 2005-04-02. Every event put in the diary is a day passing (0x42F0E0):
the event is appended (the oldest dropped past 2,922) and `today` + 1. A day (16 bytes): u8
kind, three bytes and a u32 by kind, u64 a code:

| Kind | Bytes 1, 2, 3 | u32 | Code |
|---|---|---|---|
| 0 nothing | | | |
| 1 a car got | paint, how (1 new, 2 used, 3 trade, 4 present) | | the car |
| 2 a car sold | paint, the price (u16, capped at 65,535) | | the car |
| 3, 4, 5 a race, a license test, a mission | result, note (0 .. 7 the championship's place, 8 .. 12 the licenses, 13 a new record, 14 none) | time ms | the race |
| 6 wheels bought | category (1 normal, 2 racing, 3 dirt, 4 snow), a flag | | the wheel |
| 7 a wing bought | | | the wing |
| 8 a course run | result, note, the kind (1 free run, 2 event, 3 practice, 4 photo, 5 travel, 6 machine test, 7 time trial) | time ms | the course |
