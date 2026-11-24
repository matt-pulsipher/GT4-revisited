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
  trophy times, ended when the car stops in the box, its results with Retry and Exit.

## Known problems

The ones a player meets first:

- Arcade's opponents are always five Mercedes-Benz SL 500s (the game's choice of opponents by
  your car's performance is not built yet).
- The replay after a B-spec race shows the course without the cars.
- Your car's row in the race's grid tables is not marked in orange.
- On some courses an opening shot of the race looks out from inside your car.
- Prologue keeps its memory card in Gran Turismo 4's folder of the data folder: the two games'
  saves overwrite each other when both are played with one data folder.
- The copyright notice and the "Sony Computer Entertainment America Presents" page before the
  first dialog are not shown.

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
