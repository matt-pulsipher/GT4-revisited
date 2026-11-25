# Recorded runs: the demonstrations (`replay/license/`) and the replay saves

Code: `src/gt4r/formats/replay/demonstration.*` (the reader), `src/gt4r/sim/ai/demo_driver.*` (playing one on the
session); `gt4tool ai <disc> demo <label|all> [frames]`, `gt4tool ai <disc> demo-run <label>`,
`gt4r_diff <disc> demo <label>`; a race of ours kept the same way `race/race_recording.*`, run from a file `race/recorded_race.*`
(`gt4tool race <disc> replays | recording <file> [out]`, `gt4r_diff <disc> floats replays <name | file> run`); `tests/test_ai_demo.cpp`, `test_recorded_races.cpp`. Addresses are the US program's (SCUS-97328 v1.01).

A license test's "View a demo run" is not driven: it is a recorded run played again. The program keeps a run as
`Serialize::ReplayData` (the memory card's saved replays are the same class): the race's header and the drivers' pad
records, coded. The race is set up from the header and runs again from the records, as any replay.

## The disc's files

`replay/license/<event label>`: one per license test and region, named as the test's event (`usl0b0001` is the US B-1
test; the file the program asks for is the event's own label). On the US disc: `us`, `eu`, `uk`, `kr` and `jp`
sets of `l0b`, `l0a`, `lib`, `lia`, `l0s` (16 each) and `l0c` (the five coffee breaks); `jpl0m0001` .. `0040` (the
missions: 40 copies of one demonstration of a two-car test). ✔ All 465 files of that naming read and decipher.
`jpl0m0000` and the files with three-digit numbers (`jpl0a001`, `jpl0m000`, ...) are an older header layout (the fields below
0x40 bytes earlier) that no name the program builds reaches.

Every demonstration on the disc was recorded on a steering wheel (pad kind 6) with ASM and TCS on and, but one
(`uslia0011`: manual), the automatic gearbox. Some regions' files are another region's recording (the US B-1 file
names the Japanese disc's B-1 row, 0xA66, where the US row is 0xD82): the event is the file's name.

## The file (`Serialize::Base`, 0x44CF60 ..)

| Off | | |
|---|---|---|
| 0x00 | u32 | the header's size, 0x40 |
| 0x04 | u32 | the body's size |
| 0x08 | u16, u16 | 0, 4 (`ReplayData`'s version: 0x44D140 refuses another) |
| 0x0C | u32 | the key: the body's CRC-32 (standard, 0x572C90) low 24 bits, the body's first byte above |
| 0x10 | char[0x30] | the serializer's name, `Base` |
| 0x40 | | the body, enciphered |

**The cipher** (0x579E00, the writer 0x579F38): the state is a float of the console's, its bits at first
`(key & 0xFFFFFF) | 0x3F000000`. Each byte: the plain byte = the stored byte XOR bits 8..15 of the state XOR (bits
0..7 XOR bits 16..23); then the state = (the state x f[stored byte]) / g, both the console's single precision (its
multiplier, then its divider), with f the 256 floats at 0x655888 and g the float of bits `0x3F000000 | p[s & 0xFF]
<< 16 | p[(s >> 8) & 0xFF] << 8 | p[(s >> 16) & 0xFF]`, p the 256 bytes at 0x655C90 (each byte's bits reversed). A
body is read when the deciphered bytes give the key again. Rounding toward zero without the console's own multiplier and divider gives other bytes from the first few on:
the console's rounding is part of the cipher.

## The body

**The race's header** (0x1AA4 bytes, `RaceBase` virtual 27, 0x388E38; read back by virtual 53 / 56):

| Off | | |
|---|---|---|
| 0x000 | u32 | the size (the writer puts 0x1AA4; the file the whole body's) |
| 0x004 | | `GT4M` |
| 0x008 | u32 | 0x40000 |
| 0x00C | u32 | the race's +0xCC4 (0 in every demonstration) ❓ |
| 0x010 | char[0x40] | the race's class: `RaceLicense`, `RaceTraining` (tests 5, 10, 15 of a license: a race behind a pace car) |
| 0x050 | 0x60 | the race information's first 0x60 bytes ❓ |
| 0x0B0 | u64 | the event (RACE, table 0x23) |
| 0x0B8 | u64 | the course (COURSE, table 0x22) |
| 0x0C0 | s32 | the recorded run's time, ms (the first entry's; -1 in some files) |
| 0x0C4 | s32 | 0x3BBB60 of -1: the same in every file |
| 0x0C8 | 0x80 | cleared |
| 0x148 | 0x810 | the race's virtual 28 ❓ |
| 0x958 | u32 | the entries |
| 0x960 | 0x228 each | the entries (below) |
| 0x1AA0 | u32 | 1 once the controls were written (`RaceSolitaire` virtual 58) |

An entry (from the race entry): +0x000 the equipment record (entry +0x20, 0x178 bytes: the license's car, its parts
and settings as the event's pool gives them, not the car as sold), +0x178 the condition block (entry +0x3468),
+0x198 the entry's +0x198 ❓, +0x19C the grid place (+0x3444), +0x1A0 the pedal style (+0x3448: 0 automatic),
+0x1A1 the driver kind (+0x344C), +0x1A2 .. +0x1A5 the aids' switches (+0x3450 ASM, +0x3454 TCS: the car's set-up
0x363B10 turns ASCC off (0x3F1700) and TCS off (0x3F1748) for 0; +0x3458, +0x345C ❓). A training test has two
entries, the pace car (an R34 Skyline, grid place 0, driven by the computer) second.

**The controls** (from 0x1AA4; `AutomobileControlRecord::Manager`, loaded by 0x346BB8): u32 n. n = 0: one stream,
recorder 0's (every demonstration); n > 0: n recorders, each a header of 0x14F4 bytes (0x346CA0) then its stream.
A recorder's header keeps the moment its stream starts at (0x35AB08 writes it, 0x35ABA8 reads it back): +0x00 the recorder's index, +0x02
u8 the race clock's units (0 .. 50 a step) since the line then, +0x03 u8 the units its whole milliseconds leave (0 .. 2, 0x358468), +0x04 u32 the
recorder's frame, +0x0C f32 the share of the step after the line, +0x10 u32 the lap's time (ms), +0x54 the car's record (0x10A0 bytes; its
pointers, its first five words, are not taken back), +0x10F4 the collision body's contacts (0x400 bytes, 0x45CDC8's; all 0: none).
A stream (0x346528): u32 T, u32 its length with these 8 bytes, then the bits, padded to 4 bytes.

**The stream** (0x345D70, once a race step): a bit stream, most significant bit first (0x57AD58), of changes to the
race's 0x20-byte pad record (physics.md "A race entry"). Each step: first the steering carried on (0x345C38: with
|x| >= 16, x becomes 2x - the step before's, within ±4096); then, unless a run of unchanged steps is still counting
down, a mask (gamma - 1: k one bits, a zero, k bits give 2^k + those bits), one delta per changed field in this order
— mask 0x100 +0 flags, 0x080 +1 the controller's kind, 0x010 +2 the shift, 0x002 +4 the steering (s16), 0x001 +7,
0x004 +8 the throttle, 0x008 +0xA the brake, 0x040 +0xC, 0x020 +0xE (u16) — and with 0x200 a count (gamma - 1) at +3
then that many byte deltas from +0x10; then the run (gamma - 1). A delta (0x345CF0): a class k of 3 bits, a sign, 2k
+ 1 bits added to the class's base (0x620218: 0, 2, 10, 42, 170, 682, 2730, 10922), each field adding with its own
width's wrap. The step's record is the result. Frames 1 .. T - 1 give a record each (T - 1 steps); at T the playback
is done (+0x38) and the record cleared.

**The recorder** (0x346138 once a step, 0x346498 the end) writes the same: it carries its state's steering on as the player does, compares
the new record field by field, and on the first record or a change writes the run before (gamma), mask + 1 (gamma), the deltas in the order
above, with 0x200 (the count differs, or one of the first count bytes from +0x10) count + 1 and that many deltas, unchanged bytes too; the
whole record becomes the state. The end: the run, then a 1 (gamma); T is the records kept, the length the whole bytes written + 8: the
last part byte is not in the file (its place is zero), so a playback may read a last change short (its T-th record, which it never hands
out). A delta's class is the least whose range holds it (under 2, 8, 32, ...). ✔ `encode` writes 441 of the disc's 497 recordings byte for
byte again (file header, key, cipher, stream); of the others 16 end inside their last records and the 40 copies of `jpl0m` are an older
recorder's (their first record's bytes from +0x10 take four bits each there), the same frames when written and read again
(`ai_disc_recordings_written_again`).

## Playing one

The race steps from its first step with car 0's record from the stream (the start's hold and count included: the
B-1 recording revs at its step 345 and goes to full throttle at 478, as the count ends). Run through the program's
own race step in the interpreter (`gt4r_diff <disc> demo usl0b0001`: the file's equipment on `testcourse_100m`, the
records into the entry's controller frame), the Demio pulls away at step 478, reaches 66 km/h at step 960, brakes
at 969 and comes to rest at step 1083, 102.27 m from its place: in the stopping box. ✔ Our session with the same
records (`gt4tool ai <disc> demo-run usl0b0001 hold=0`) runs the same to the millimetre (rest at step 1083, 102.27 m;
every speed equal to 0.01 km/h). With the race's 8 s hold (`hold=480`) it rests 603 steps after the hold, the file
recording 10.083 s (605 steps) ❓ the stop rule's own timing.

The reference's capture of the B-1 demonstration (`08-more`, `11-license-tt`) shows the original's cameras and title
card; the car there only creeps, the emulator not playing the recording as the console does (the reference's note):
its driving is not a reference.

## The Replay Theater's demo replays (`replay/Rdemo<n>/`)

The Demo tab (`menu/game_replays.cpp`, `install_demo_list`): the folders the configuration names for the region
(`DemonstrationFiles`: `Rdemo0` .. `Rdemo12` on the US disc, four of them the Korean disc's `k`, the European disc's own
`Rdemo0p` .. `p10`), each a replay of the same name, `icon.sys`, `icon0.ico` and a 256-byte `thumbnail`. A replay is
scrambled as the card's files with two other swap fractions (0x3E6B3EF8 and 0x3F322311: FileGT4Replay's 0x1CFE68 write and
0x1CFFF8 read; the game data's are 0x3E0B0C88 and 0x3F27A4E7), then a `ReplayData` as above. ✔ All 13 decipher and parse:
the recorded times, courses and first cars are the reference's list (28-demo-replays), the mode word (+0x0C) is the A (0) /
B (1) tag, the size the folder's KB plus 2 entries and one (the original's 43, 24, 39 ... KB). The title `DEMO_000` is the
row's number. The list's course is the label; the page translates it (`CourseNameCaption`); the row's picture is the icon's (save.md "The icon's picture"). `load` gives the sequence the file's path on the disc; the race plays it as described below. (The original in the emulator answers "Data is corrupted.": its cipher is the console's multiplier and divider.)

**The race a replay is** (0x10CA50 the Replay Theater's sequence, `RaceReplayInformation` 0x3DD838): the race's class by its name, the event's
row (+0xB0: 0xA04, `sample_entry_6`, for an arcade race), the course (+0xB8), the race information's first 0x60 bytes as the file has them
(+0x00 the laps, +0x04 the penalty option, +0x08 the tire damage, +0x0C .. +0x1C two-player options, +0x20 the B-spec driver's record of 0x40
bytes), each entry's equipment, condition, place, gearbox, driver and aids (0x3DDAD8); the race's mode (+0xCC8) 2, or 3 for a B-spec run's
(mode word 1 or 3), which gives car 0 to its driver (0x3F1760). The start is the race's own: rolling by the course's start code, else held by the
row's type (+0x40, 2 in every arcade race: 10 s, 600 steps). The records run from the race's first step, the hold or the run-up included.
A B-spec run's records carry its commands (0x3F19E0): +0x14 the command (4 the pace, 5 the overtake, 6 the pit, 10 the mode), +0x15 its
argument; +0x10 the pit menu's choice (0x35BFE8: 1 the choice, 2 the refuelling stopped), +0x11 / +0x12 the tires, +0x13 the fuel: taken only
while the stop asks (F +0x4B8 bit 0x10). A pit command before the first crossing or on the last lap is not taken.
**A free run's** (RaceFreeRun's virtual 158 loads its one recorder, its virtual 38 asks 0x359D40): no hold, no run-up; the car put at the
recorder's moment, its crossings 2, the clock at 100 units, its times from (100 - units + the remainder - 1) / 3 ms, whole.
+0x148 is the music's play list (0x810 bytes) 🔎. The European disc's own (`p`) run at 50 steps a second.

**They come out as recorded.** ✔ Through the program's own race step (`gt4r_diff <disc> floats replays <name> run`) the twelve races, A-spec and
B-spec, one to five computer cars, pit stops among them, end on their recorded times and best laps to the millisecond, Rdemo9's lap
(Nürburgring, 472,780 ms, from the recorder's moment) too: 13 of 13. ✔ So does our own race set up from the file alone (`race/recorded_race.*`,
test `recorded_races_end_on_their_recorded_times`): 13 of 13; beside the program's, every car's place is equal in every bit over the whole run on
Rdemo1, 2, 5, 8, 9 and wherever else it was compared (`GT4R_FLOATS_ALONE=1`; `GT4R_FLOATS_SAVE` / `GT4R_FLOATS_TRACE` keep the program's
run in a file and compare with it). The app plays them from the theater's Demo tab (`screen_race.cpp` `load_recorded_race`, `play_demo`).
`Rdemo15` (the Japanese disc's, the same course and car as Rdemo9) is exact too; the other regions' files name cars and courses by other builds'
rows and do not come out (a car of theirs is another here, or none).
`Rtest1`, `Rtest2` and a file `BISCPS-17001R1UWiYUY` in Rdemo2's folder read, but are other builds' too.

## A replay of ours

A saved replay (race_flow.md "Saved replays") of an arcade single race, driven or B-spec's, holds beside its states the program's own
file of it, in a section `RPLD`: the scrambled `ReplayData` a card's replay file is (`race/race_recording.*`). What each part is made of:

- **The header**: the class `RaceArcadeSingle`, the mode word 0 (A) or 1 (B), the event's and the course's codes, the race information (the
  laps, the penalty and tire damage options, 2 at +0xC and 1 at +0x1C as every arcade recording of the disc has, the B-spec driver's
  record), the race time and best lap (an unfinished race's last lap, else 0 and -1), the races' music as the options keep it (+0x148).
- **The entries**: the equipment, condition (none: the default, 1 at +0x14), place, gearbox, driver kind and aids the race was set up with;
  +0x198 0x100 for the player and 0 for the computers, the fourth aid byte 2, as every recording. A computer's third aid switch (+0x3458) is
  1 in every recorded race; the races here give it now too.
- **The records**: the player's frame each step from the race's first, as the input phase read it, with the step's mail at +0x10 (the count
  6): B-spec's commands and the pit menu's choice (0x35BFE8, post_mail). Both take effect a step after they are asked, here as there. A
  B-spec car's pit wait that runs out (300 steps, 0x35C0B8) posts its choice by itself (0x35BF08: 1, 0xFE, 0xFE, 0xFE); a run played from
  records takes its choices from them alone.
- Not kept so (states only): runs of other races, a run whose driver was changed in the pit (command 10), a scripted run driven by the
  computer.

✔ Made from the disc's twelve arcade recordings' own races, the files are the disc's byte for byte (10; the other two end inside their
last record). ✔ Races driven in the app and saved, then run from the saved file's `RPLD` alone (`gt4tool race <disc> recording`): a driven
one into walls and traffic (Tsukuba, six cars, 2,400 steps), B-spec ones with commands (7,500 and 4,900 steps, to the recorded time) and a
pit stop (13,500): every car's place equal in every bit, every step. PROGRAM_RESULT
`gt4tool race <disc> recording <saved replay> <out>` writes the card's file out; the race screen plays a card's replay file by its path (as the
Demo tab's: run again from its records).

A saved replay's race is run again behind its states while it plays, for what they do not hold (the sound's hits, wheels and engine torques);
where a car leaves its kept place by a bit, the states alone are heard (a log line says so).

**The original's card**: its replay folder (`BASCUS-97328BemU5YYY` on the reference card, a license run's) has the replay of the folder's name,
`icon.sys`, `icon0.ico` and a 256-byte `thumbnail`, as the disc's demo folders. The file unscrambles with the replay class's factors and holds a
`ReplayData` header, so its container is this one; its body does not decipher to its key here, as none of PCSX2's does (the cipher is the
console's multiplier and divider: physics.md §9b). A replay saved on a console is not at hand ❓. Nor is a card folder written here: the
icon, the thumbnail (❓ its layout) and the name's letters (by the class `R`, `B` for a best lap, `P` a photo; 🔎 the rest) are not made.
