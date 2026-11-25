# Playing GT4 Revisited

GT4 Revisited runs Gran Turismo 4 from your own disc image. This page says how to start, how to
play, what works and what does not yet. It describes the US disc (SCUS-97328); the Japanese
Prologue disc starts the same way.

## Starting

1. Start `gt4r`. Without a disc it asks for one: press Enter (Cross) to choose your Gran Turismo
   4 disc image (`.iso` or `.chd`), or drop the file on the window. Nothing is copied from the
   disc; the game's files are read from the image as they are needed.
2. With a disc chosen, the game starts as the console does: the memory card's questions, the
   Polyphony Digital logo, the opening movie (Start skips it), the title. The disc is remembered:
   the next launch goes straight there. `gt4r <disc image>` starts with that disc.
3. Your memory card, options and replays are kept in the app's data folder (on macOS
   `~/Library/Application Support/GT4Revisited/GT4Revisited/`; a `portable.ini` beside the program
   keeps them beside it; `GT4R_DATA_DIR` names another folder). A new data folder is a memory
   card without game data: the game offers to make one (OK), which leads to Gran Turismo mode's
   name entry and introduction; Cancel and "Start game" play without one (Arcade only).
4. Select and Start together leave the game for the home screen: the disc's cars, courses, menu
   pages and files, a race set up by hand (*Drive*), another disc, and *Play* to start the game
   again. `[game] start = hub` in `config.ini` opens the home screen first.

## Controls

A pad works as the console's (the Gran Turismo layout of a DualShock 2: Cross confirms, Triangle
goes back, Start pauses a race). On a keyboard:

| | Menus | Racing (the default "modern" layout) |
|---|---|---|
| Arrows | move | Up / Down accelerate and brake, Left / Right steer |
| Enter | Cross (confirm) | |
| Escape, Backspace | Triangle (back) | Escape: pause |
| P | Start | pause |
| Tab | Select | |
| W / S | | accelerate / brake |
| A / D | | steer |
| E / Q | | shift up / down (manual gears) |
| Space | | handbrake |
| R | | reverse |
| C | | change view |
| X | | look back |

F11 switches full screen.

## What works

- **The boot**: the memory card's dialogs, the logo, the opening movie, the title with its
  moving background. A game saved in Gran Turismo mode starts again on its map, as the console
  does.
- **Arcade**: Single Race (course categories, the course pages, every maker and car, paints, the
  driving options), the race's menus before and after a race, the race itself (A-spec with the pad,
  B-spec watching the computer drive), the results, the replay, back to Arcade for another race.
  Time Trial and the other race kinds reach their races the same way.
- **Options**: every page opens and keeps its values in the data folder; the laps, the units and
  the race settings take effect in the races.
- **Gran Turismo mode**: a new game (name, introduction), the map, the used car and new car
  showrooms (buying a car, getting in it), the events (the Sunday Cup and the others: the event
  page, the regulations, the pre-race page with the drawn field), racing them for real (A-spec or
  B-spec), the results, the prize page paying the prize, the day going by, the game saving itself
  when you come back to the map, and loading it at the next start.
- **Races**: the courses drawn in full with the computer drivers racing, the three driving views,
  the race display (in the units the options set), the B-spec display and commands, the pause
  menu, the results, the replays through the course's cameras.
- **License tests**: the License Center, a test's page, the test driven with its own display and
  trophy times, ended when the car stops in the box or fails (cones, walls, off the course), its
  results with Retry and Exit, the trophy on the license's grid.
- **Later in Gran Turismo mode**: the halls of Professional Events, a championship of five races
  (each race's pre-race page with the points so far, Session Results, Next Race, Abandon), the
  dealers' tune shops (buying a part and fitting it), the calendar going by a day a race.

## Known problems

The ones a player meets first:

- A championship's Point Ranking page shows "car name" and "00:00" on every row (the totals on
  Session Results are right).
- The prize page after a race has no turning money pouch (its place number and prize are there).
- Holding reverse does not back the car up: the brake is held with it.
- A passed license test's page has no trophy and no target times, and its scene stays in a small
  inset.
- In the license grids, test 8 opens a page titled "B-0" (and shows another test's car), and the
  tests after it open with the wrong numbers.
- A B-spec pit stop happens, without the PIT STOP menu, the tire diagram and the fuel bar.
- Game Status' B-spec Mileage does not move after a B-spec race.
- The used car list's scroll bar does not follow the list, its paint swatches are too bright, and
  some torques are 0.01 ft.lb too high.
- In the Tuning Car Grand Prix's first race the computer's cars are 1 to 3 % faster than the
  original's, and further apart.

## Scripted play (for checks)

The app can be driven without a pad, for pictures and long runs:

```
GT4R_DATA_DIR=<a scratch folder> SDL_AUDIODRIVER=dummy gt4r <disc> \
    --keys "wait120,left,cross,wait60,right,cross,wait700,start,wait150,shot:title" \
    --shot-frame 1200 --shot out.png --stats stats.txt
```

- `--keys`: presses (`cross`, `down`, `start`, ...), `waitN` (N frames), `shot:<name>` (a
  picture beside `--shot`'s), `quit`; race controls held until changed: `gas=0..1`,
  `brake=0..1`, `steer=-1..1`, `reverse=0..1`, `handbrake=0|1`, `release`; `@<file>` reads keys
  from a file (`#` comments; files may name files).
- With `--shot` a frame is a 60th of a second and the frames are held to that pace, so the
  movies keep step with the keys (`--fast`: not held).
- `--stats <file>`: a line a second: the frame, the seconds since launch, the frames' mean and
  longest work in ms, the resident memory in MB.
- `GT4R_MENU` and `GT4R_RACE` set up the menu pages and *Drive* screens for a check (the tops of
  `src/gt4r/app/screen_menu_viewer.cpp` and `screen_race.cpp`).
- Scripted runs (`--shot`, `--sweep`) open their window without making it the active one, and it
  cannot take the keyboard's focus.

## Sweeps (for checks)

- `gt4r <disc> --sweep <list> --sweep-out <dir>`: the races of a list, one after another in one
  window, a picture of each at the frames the list asks for. A line: `<name>` TAB `<GT4R_RACE's
  setup>` [TAB `<frames, ';' between>`]. `<dir>/results.tsv` gets a line a race: its status, the
  race screen's setup time, the frames' work, the memory after it, the warnings it logged. Started
  again with the same list, the run goes on after the last race it finished, and a race it ended
  inside is written as a crash and passed over (`src/gt4r/app/screen_sweep.cpp`).
- `gt4tool sweep <disc> courses [filter]`: every course file with a field of computer drivers for a
  lap: set-up time, lap times, spins (and where they began), contacts, cars stuck.
- `gt4tool sweep <disc> cars [filter] [course=..] [seconds=..]`: every car of the database driven
  by the course's computer driver: distance, top speed, 0-100 km/h, spins, stuck time.
- `gt4tool sweep <disc> pad [courses]`: the player's car driven through the pad's path (an analog
  stick and pedals following the road), with the automatic and the manual gearbox, the aids on and
  off, against computer drivers: lap times, contacts, shifts, the place at the end, spins, the time
  spent sliding.
- `gt4tool sweep <disc> stop [course=..] [car=..]`: the player's car driven, braked to a stop, stood
  and launched at full throttle: gear, speed and engine speed every quarter second.
- A Gran Turismo mode game played forward (every license, 22 cars, the presents) for playing the
  later events: `gt4tool save <disc> progress <data folder> [name] [credits]`, then
  `GT4R_DATA_DIR=<data folder> gt4r <disc>`.

## What runs where (the app's shell)

- The window, the frame loop, the screens, the scripted keys, the pictures and the sweeps are the
  app's own (`src/gt4r/app/app.*`, `screen_sweep.cpp`); the home screen, the setup screen and the
  asset browser are tools of this app, not pages of the game.
- The pad and keyboard are read by the shared engine's input layer (`gt3r::InputManager`, its
  button mapping and the "modern" keyboard layout), which hands the race a pad state; from there the
  race's own path (`sim::Session`, the pad's frame) turns it into the car's controls: from the frame on
  that is the game's own input phase (0x349FD0, `docs/specs/physics.md` §3.6). What fills the frame
  from the pad is not the game's controller (0x426D50): the engine's mapper reads the stick (with a
  dead zone and a response curve of its own) and the pedals' pressures.
- The 2D drawing toolkit of the home screen, the setup screen and the browser is the shared
  engine's (`ui.cpp`, `ui_font.cpp`, `button_prompts.cpp`); the game's pages are drawn by the menu
  code (`src/gt4r/menu`, `screen_menu_viewer.cpp`).
