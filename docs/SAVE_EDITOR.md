# The save editor

*Save editor* on the home screen (Select and Start together leave the game for it; `--screen saveeditor`) changes a saved game.
Code: `src/gt4r/app/screen_save_editor.cpp` (the pages), `app/editor_popups.h` (their lists, number pad, letters and questions),
`src/gt4r/menu/save_editor.*`, `save_editor_cars.cpp`, `save_editor_prologue.*` (the changes), `src/gt4r/save/save_edit.*` (the
files); tests `tests/test_save_edit.cpp`, `tests/test_save_edit_disc.cpp`; the disc's own program on a save `gt4r_diff <disc>
save <folder or card image>` (`tools/gt4diff/save_check.*`). The saved game's layout is [save.md](formats/save.md).

## Using it

The first page lists the saved games of the save folder (`save/save_library.h`: each with its player, credits, cars and date)
and *Another save*: a PlayStation 2 memory card image (`.ps2`, `.mcd`) the game data file of a save, or its folder; a
card image or game data file dropped on the window is opened too. A save opens on its pages; up and down choose a row, left and right change it, Cross (Enter) opens it or
types its value, L1 and R1 (Q and E) turn the page, Triangle (Escape) goes back, Start (P) saves. The mouse clicks rows, their
arrows, the tabs and Save; the wheel scrolls; letters typed in a list go to the first name that has them. A car's
picture is the disc's own (`piece/car_pict/<label>.img`, in the paint the disc has it in; a few cars have none).

| Page | What it changes |
|---|---|
| Career | name, credits, the day (going on adds days with nothing in them to the diary, going back takes days out), the B-spec driver (its experience for all or each of the eight classes of cars, its knowledge of the courses; its four figures as the Status page shows them), total prize credits and cars, races finished in each place, mileages, the ending movie, what may still be brought over from Gran Turismo 3 and Prologue, a championship under way given up, used cars sold this week put back on sale. Completion and A-spec points are worked out, not set |
| Garage | add any of the 722 cars, new, in a paint; remove; drive; paint, odometer, the day it was got, engine, oil, body stiffness, dirt; the settings sheet, parts (fitted, in stock), the work that is not undone in the game, every setting the fitted parts allow, wheels for each kind of tire, the wing |
| Licenses | every test's medal and best time, one test, one license or all; the coffee breaks |
| Events | every event's and championship's races: the best place, won or cleared one by one, a hall or all; each race's A-spec points; whether an event's prize car waits |
| Missions | the place and the A-spec points of each |
| Prizes | receive the prize cars earned; have the prize cars of the events won waiting again; the licenses', missions' and completion's presents given or not |
| Records | the times kept for each course and the machine tests: delete |
| Arcade | what is open: cars, courses and photo places, one by one or all |

For a Prologue disc the pages are *Career* (the three letters) and *Licenses* (each test's medal): its save keeps no money, garage,
calendar or presents ([prologue.md](formats/prologue.md) "The saved game"); its course times and options are not read there and stay.

## What it does to a save

- **Nothing is written until Save**, and leaving with changes asks. The game is not running beside it: the home screen is reached
  by leaving the game, which reads its save again at the next start.
- **A backup first**: before a save's first write since it was opened its files (or the card image) are copied whole into
  `<save folder>/backups/<date and time> <name>`. A backup's folder or image opens in the editor as any save, and goes back by copying it.
- **Refused, with why**: a save whose checksums are wrong, a garage file of another game, a save of another release, a save that
  something else wrote since it was opened, a backup that cannot be made. A failed write leaves each file the old one whole.
- **Only what the change is of changes.** The payload takes the bytes that the changed state writes differently from the state
  read (so what our state does not name, and anything our reading does not know, stays to the byte); its CRC is made again and the
  file scrambled as the program does. The garage file keeps its key, serials and stamps; its header takes the credits; a car's slot
  is written when its record changed (the driven car's record is in the game data; the car driven before goes to its slot when
  another is chosen, as the program writes it). Files the change does not touch are not written. On a card image the files' own
  pages change (with their error-correcting bytes) and nothing else.
- **Checked before and after**: what is to be written is read back as the program reads it and must give the same state; what was
  written is read from the disk again and compared.

## How a change is made

The editor runs the menus' own natives on the save's state, with Gran Turismo mode's project loaded and no page shown: a car is
added by `garage.addNewCar` and opened in Arcade as a purchase does (no diary day: nothing was bought), driven by
`changeRidingCar`, sold by `delCar`; parts are fitted by `setParts` after the tuning shop's `set_parts_consistent` (a turbine kit
takes the other aspirations' parts off, front and rear tires stay of one kind), the settings' ranges and values are the settings
page's (`getSuspensionRange`, ...; ASM's word less one, TCS counted from the other end, the toe from -128); the list's power and
weight follow (0x434FD0). The names are the menus' texts. The lists are the scripts' data (`EventData`, `PresentData`).

- **A license test**: the record's kind 1 with the medal as best and last result; its ranking loses the times that earn more than
  the medal and takes the medal's own limit when no time left earns it. The limits are the test's `RACE` row's.
- **An event's race**: kind 2 with the place as best; a single race's "shown on the map" the place and its last result none (what
  the present page leaves after a win); a championship's race keeps the place as its last, the championship's own record its win.
- **A prize car waits** when the present page's test for an event (`PresentRoot::win`) would give it: the championship's last result
  first; else every race first on the map or in its last result, and one of them new. The editor sets just that (the results' last
  or shown halves) and the game gives the car at its next start of Gran Turismo mode; *Receive* does what its page does: the car in
  the list's paint (or any), the diary's day, Arcade, the prize cars' count. The Gran Turismo World Championship's car comes with the
  ending: it is left for the game to give.
- **The B-spec driver's book**: a class of cars' column of the three tables set to the share of what the figures count (1,024,
  1,024, 256). The courses: each course whose attribute text's second character is not 0 (58 of them, whose 8 marks of 255 are the
  program's own most 0xE718 in each of the two sums) gets a record and its marks, and the two sums are the marks'.
- **No car's condition is invented**: new engine, run in, worn out are the program's condition limits; oil by `getOilLifeRate`'s
  6,000 km, body stiffness by `getBodyLifeRate`'s 500 to 3,000 km, dirt to its 0.8.
- What a field holds that is not read (❓ in save.md) is not offered and not touched. An entry has no mark for a used car: it is one
  with an odometer, and the diary says how it was got.

## Checked

`test_save_edit`: for 17 changes, one at a time, the payload's bytes change inside that field's block and nowhere else but the CRC,
and the garage file's in the header or that slot or nowhere; the written files load in our game; the refusals; a card image page
for page against a newly made one. `test_save_edit_disc`: 722 cars added, every part of four cars fitted and taken off, every
setting set to both ends, everything won (completion 100.0 %, 165 prize cars received), the B-spec driver at 10,000 and 100 / 100 /
100; what is written goes through the disc's own program: its decoder (0x1D4E60), check (0x43AA68), read (0x43ABA0) and write
(0x43AAF0, the same bytes again), the garage header (0x1CB9B8) and the slots; Prologue's check, load and save (0x4021A8, 0x402288,
0x4021F0). From power on the menus load an edited save and Gran Turismo mode opens on the present page with the one prize car set
waiting. ❓ Not loaded in the original running: its routines were run, not its menus.
