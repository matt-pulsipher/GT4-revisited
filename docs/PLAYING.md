# Playing GT4 Revisited

GT4 Revisited runs Gran Turismo 4 from your own disc image. This page says how to start, how to
play, what works and what does not yet. It describes the US disc (SCUS-97328); the Japanese
Prologue disc starts the same way.

## Starting

1. Start `gt4r`. Without a disc it asks for one: press Enter (Cross) to choose your Gran Turismo
   4 disc image (`.iso` or `.chd`), or drop the file on the window. Nothing is copied from the
   disc; the game's files are read from the image as they are needed.
   The disc images opened before are offered below it.
2. With a disc chosen, the game starts as the console does, without its questions about a memory card: the
   Polyphony Digital logo, the opening movie (Start skips it), the title. The disc is remembered:
   the next launch goes straight there. `gt4r <disc image>` starts with that disc.
3. Your saved games, options and replays are kept in the app's data folder (on macOS
   `~/Library/Application Support/GT4Revisited/GT4Revisited/`; a `portable.ini` beside the program
   keeps them beside it; `GT4R_DATA_DIR` names another folder). With none saved yet, choosing Gran Turismo mode
   starts a new game (the name entry); Arcade needs none. The game saves itself after every race and purchase (Options,
   Miscellaneous, Autosave) and the next launch goes on with it.
   **Saved games** (the home screen, the title's *Save Game*, Prologue's *Save* and *Load*) lists them: load one, start a new one
   beside the others, delete one, *Export to PS2* (a memory card image the original loads) and *Import from a PS2 memory card
   image*.
4. Select and Start together leave the game for the home screen: the disc's cars, courses, menu
   pages and files, a race set up by hand (*Drive*), *Controls*, *Settings*, *Mods*, another disc, and
   *Play* to start the game again. *Settings > Disc & saves > Starts in* opens the home screen first.
   The home screen and the app's own pages take a pad, the keyboard and the mouse (a click chooses, the
   wheel scrolls, the right button goes back); the game's pages a pad or the keyboard.

## Controls

In the menus a pad works as the console's (the Gran Turismo layout of a DualShock 2: Cross confirms,
Triangle and Square go back). In races, unless you change it:

| | Pad | Keyboard | Steering wheel |
|---|---|---|---|
| Accelerate, brake | right, left trigger | Up, Down (or W, S) | the pedals |
| Steer | left stick (or the direction buttons) | Left, Right (or A, D) | the wheel |
| Shift up, down (manual gears) | Cross, Square (A, X) | E, Q | right, left paddle |
| Handbrake | Circle (B) | Space | Circle |
| Reverse | L1 | R | Triangle |
| Change view | Triangle (Y), Select | C, Tab | Select |
| Look back | right stick pressed | X | the direction buttons' Down |
| Nitrous | R1, direction buttons' Down | F | (bind it) |
| Pause | Start | Escape, P | Start |

Menus on a keyboard: arrows move, Enter is Cross, Escape and Backspace Triangle, P Start, Tab
Select. F11 switches full screen (remembered), F12 saves a screenshot in `screenshots/` in the data folder. The
mouse's arrow goes after two seconds without it.

**The game's own Options** work as the console's. *DUALSHOCK 2 button config 1P*: once you change it,
races read your pad through that page, button for button (Cross accelerates unless the page says
otherwise; a trigger set to Gas or Brake works by its travel); the page's Default brings the layout above
back. *Driving Force Pro button config 1P* (or *Driving Force*'s, if you choose that wheel below) holds
for a wheel. *Vibration*: the rumble and the triggers on or off. *Steering*: for a wheel, the active
steering, the steering type (Amateur to Simulation), the power assist and the force's strength. The
race's options: the gearbox for Gran Turismo mode's races, license tests and missions; Arcade asks on its
own pages.

**The home screen's Controls** (Select and Start together, then Controls; player 2's in Settings > Controls) has
what a computer's devices need: which device drives; the race controls, Modern (the table above), Gran Turismo 4
original (always the console's pad) or your own bindings (Custom, once there are any); the steering's dead zone and
response; the rumble, Modern (Automatic is Modern too: hits, rough ground, kerbs, slides, on both motors) or Gran
Turismo 4 original (the console's one level on the large motor, as the game sends it); the triggers' effects (a
DualSense's brake resistance and the edge of grip under the throttle, an Xbox pad's impulse triggers); tilting a
controller to steer; for a wheel which of the console's wheels it is, how far it turns, and its force feedback; what
each control is bound to; the songs' controls; the menu buttons; and bars that show what the race reads. On a
control, Cross binds it, Square adds one more input to it, Select (Delete) clears it. *Songs*: the next song of the
race's music and the one before, on any buttons or keys held together (none to begin with). Every controller and the
keyboard work on this page, whichever device is chosen. It is kept in `config.ini` when you leave the page:

```
[player1]
race = modern            ; modern | original
rumble = auto            ; auto (Modern) | modern | original
[wheel]
kind = driving_force_pro ; driving_force_pro (900 degrees) | driving_force (200 degrees)
rotation = 900           ; how far your wheel turns, lock to lock, as its own driver has it
[ffb]                    ; wheel, wheel_mode (auto | modern | original), strength, steering, spring, understeer,
                         ; triggers, trigger_force, invert
```

**Settings** (the home screen) has pages for the display, the sound, the controls, the disc and the saves, and the mods.
Every change is applied at once and kept in `config.ini`; the 3D renderer is taken at the next start (the row says so).

- *Display*: window or full screen; the aspect ratio (16:9, 4:3, or the whole screen: races fill it, the game's
  pages stay 4:3 in the middle); vsync; the frame rate (the display's, or about 60 on a faster one); a frame
  rate counter; gamma over the whole picture; anti-aliasing; anisotropic filtering; the 3D views' resolution, and
  whether it is lowered while the GPU is too slow; the menus' cars and prizes at the screen's resolution or the
  console's 640x448; the 3D renderer.
- *Audio*: the output device; the music's and the effects' volume, on top of the game's own options; whether sounds behind you
  are sent as the console sends them (one side turned over, for a surround decoder) or plain.
- *Controls*: both players' controls; whether controllers keep driving while another window is in front; on Windows how
  Xbox controllers are read.
- *Disc & saves*: the disc in use, whether its files are a tested release's own and whether the image is complete; the
  disc images opened before (Cross opens one, Square takes it off the list); the folder the games, replays and photos
  are kept in (a new one gets a copy of what it does not have; nothing is written over); what opens at the start; the data
  folder; the log of this session and of the one before (`gt3r.log`, `gt3r.prev.log` in the data folder: an uncaught error or
  a crash is its last line; your name is taken out of the paths); the version.
- *Mods*: below.

```
[video]
fullscreen = off         ; on | off
aspect = 16:9            ; 16:9 | 4:3 | fill
vsync = on               ; on | off
limit_60fps = off        ; on: about 60 frames a second on a faster display
show_fps = off           ; on | off
gamma = 1.00             ; 0.50 .. 2.00
msaa = 4                 ; 1 | 2 | 4 | 8
anisotropy = 4           ; 1 | 2 | 4 | 8 | 16
render_scale = auto      ; auto | 0.5 .. 4.0
dynamic_resolution = off ; on | off
menu_models = screen     ; screen | console
gpu = auto               ; auto | gles | gles_driver | off
texture_packs = on       ; on | off
[audio]
device =                 ; an output device's name; empty: the system's
music_volume = 0.80
effects_volume = 0.80
rear_cue = on            ; on | off
[saves]
folder =                 ; empty: the data folder
[game]
start = game             ; game | hub
```

**Mods** are folders in `mods/` in the data folder: `mods/<name>/mod.ini` (`[mod]` with `name`, `version`, `author`, `description`,
`priority`, `discs`), `files/` with files that stand in for the disc's (or add to them) under the same paths, and
`textures/` with pictures. *Install a mod* takes a `.zip`, as does dropping one on the app's own pages. Each mod is
used or not for each disc, and where two change the same file the upper one of the list wins (*Move up* and *Move
down* on a mod's page write its `priority`). A texture pack's pictures (PNG, any size up to 4096 a side) are named by
the texture they replace: *Save the game's textures* writes every car's, course's and figure's texture under that name
(`gt4tool textures <disc> <folder> car/lod/fdeu0006` a few of them). Packs change what is loaded from then on. They replace
the textures of the 3D models; the menus' pictures, the fonts and the race display are not replaced. Mods'
cars, events and liveries are not read.

A race pauses when its window loses the focus (unless the controller is set to keep driving) and when
the pad or wheel is unplugged. Rumble, triggers and force feedback are sent only while you drive: they
stop on a pause, a replay, the menus and the end of a run.

### To try with a real pad and wheel

Nobody has driven this with a real device yet: the frames, the levels and the stops are tested with
simulated ones. What to try, and what to look for:

Pad (a DualSense and, if there is one, an Xbox pad):
1. *Controls* with the pad plugged in: its name at the top right; the bars follow the stick and both
   triggers from end to end; the stick at rest shows no steering.
2. Unplug and plug it in on the home screen, in the menus, and in a race (the race pauses; plugged in
   again it drives on).
3. Arcade, a road car, automatic, aids on: at half trigger the car accelerates gently; the car stops
   straight; a light touch of the stick steers finely. Then Transmission Manual: one press, one gear.
4. Rumble, Automatic: still on a smooth straight; pulses on the kerbs; holds on grass; a thump on a wall
   (both grips, the left stronger; on a DualSense closer together); light in a slide or wheelspin. Then
   Gran Turismo 4 original: the left motor only, busy over bumps.
5. Triggers on a DualSense: resistance on the brake from a fifth of its travel, a buzz with the ABS, a
   buzz under the throttle near the grip's edge (aids off, a powerful car). Triggers: Free stops them.
6. Each stops at once on Start (pause), at the finish, in the replay, on leaving the race, when another
   window comes to the front; Options > Vibration off silences it (the triggers too).
7. Options > DUALSHOCK 2 button config 1P: swap two controls, OK, race: the pad follows the page.
   Default there, then OK: the Modern layout again.

Wheel:
1. *Controls*: it says the wheel's name; set "Wheel's rotation" to what its driver has; the steering bar
   is full only at 450 degrees each way (Driving Force Pro) and moves from the first degree; the pedals
   go from empty to full. Bind the steering axis, the pedals, paddles and buttons where the defaults
   (axis 0; axes 1 and 2) are not your wheel's.
2. A race with the force's strength low first (Strength 30 %). Straight on: no pull. Steering at speed:
   the wheel pushes back toward the middle. **If it pulls into the turn instead**, "Force's direction:
   The other way", and say so.
3. The force grows with the corner and goes lighter as the front slides (Modern); at a stand it only
   centers. Gran Turismo 4 original: the game's force alone over a plain spring.
4. It keeps working for minutes, through a pause and back, and after a wall.
5. Nothing is left pulling on the pause, at the finish, in the replay, in the menus, with another window
   in front, or after quitting.
6. Options > Steering: the feedback strength Weak and Strong differ; the steering type and active steering (Driving
   Force) change how it feels. Wheel driven as Driving Force: 100 degrees is full lock.

## What works

- **The boot**: the logo, the opening movie, the title with its
  moving background. A game saved in Gran Turismo mode starts again on its map, as the console
  does.
- **Arcade**: Single Race (course categories, the course pages, every maker and car, paints, the
  driving options), the race's menus before and after a race, the race itself (A-spec with the pad,
  B-spec watching the computer drive), the results, the replay, back to Arcade for another race.
  Time Trial and the other race kinds reach their races the same way.
- **Options**: every page opens and keeps its values in the data folder; the laps, the units and
  the race settings (the gearbox among them), the button configurations, the vibration and the wheel's
  settings take effect in the races.
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
- **Photo Travel and Photo Lab**: the 15 places with the car you drive; the map (place the car and the
  camera: select them with Cross, the left stick moves them, L2 / R2 turn the car, L1 / R1 zoom), the
  camera (hold Cross on a button, or L1, L2, R1, R2, with the left stick), lens, aperture, focus, filters,
  the wheels' angle, portrait, the aspect and quality options; the photo, saved (Start) with the game's save files in
  the original's format; Photo Lab's list, preview, renaming, deleting and the slide show.

## Known problems

The ones a player meets first:

- Driving Missions race the wrong cars, one lap, from a standing start, and a wall hit ends the run; the
  wall penalty is not shown.
- A time trial (Arcade, World Circuits) ends after two laps, and shows a lap total.
- More than half of the license tests begin with the car moving in the original (a run-up at 30 to
  200 km/h); here every test starts from rest.
- After an Arcade race a license test runs on the Arcade race's course.
- A B-spec pit stop happens without the PIT STOP menu and the SPEED LIMIT notice.
- While a championship's Point Ranking comes up, its point column shows race times for a moment.
- The pre-race pages of Practice and the Arcade time trial have no B-spec icon.
- The gloss on the used car list's white and yellow paint swatches is too faint.
- The B-spec driver's Ka is somewhat faster than the original's at Tsukuba (about 2 % over several
  races; a race varies by more than that with the traffic).
- Photo Lab: films cannot be developed, and the printer and USB memory pages find no device. In photos the
  car reflects the sky more strongly than the original's.

## Scripted play (for checks)

The app can be driven without a pad, for pictures and long runs:

```
GT4R_DATA_DIR=<a scratch folder> SDL_AUDIODRIVER=dummy gt4r <disc> \
    --keys "wait120,left,cross,wait60,right,cross,wait700,start,wait150,shot:title" \
    --shot-frame 1200 --shot out.png --stats stats.txt
```

- `--keys`: presses (`cross`, `down`, `start`, ...), `waitN` (N frames), `shot:<name>` (a
  picture beside `--shot`'s), `quit`; the mouse on the app's pages (`click:x:y`, `move:x:y`, `back:x:y`,
  `wheel:turns:x:y`, `drag:x:y:x:y` in 640 x 360 units), `drop:<file>`, `f12`; race controls held until changed: `gas=0..1`,
  `brake=0..1`, `steer=-1..1`, `reverse=0..1`, `handbrake=0|1`, `lx=` `ly=` `rx=` `ry=` (the sticks, -1..1),
  `l1=0|1` and any other button so (held), `release`; `@<file>` reads keys
  from a file (`#` comments; files may name files).
- With `--shot` a frame is a 60th of a second and the frames are held to that pace, so the
  movies keep step with the keys (`--fast`: not held).
- `--stats <file>`: a line a second: the frame, the seconds since launch, the frames' mean and
  longest work in ms, the resident memory in MB.
- `GT4R_MENU="boot=1,photo=ss_times,car=ford_gt_05"` starts a photo shoot at once; `GT4R_MENU_LOG=1` logs
  what the menus do (`calls`: each script call too).
- `GT4R_MENU` and `GT4R_RACE` set up the menu pages and *Drive* screens for a check (the tops of
  `src/gt4r/app/screen_menu_viewer.cpp` and `screen_race.cpp`).
- Scripted runs (`--shot`, `--sweep`) open their window without making it the active one, and it
  cannot take the keyboard's focus; they open no file windows, folders or full screen (`GT4R_DIALOG=<path>` is a file
  window's answer). `GT4R_WINDOW=1600x700` is the window's size, `GT4R_SHOT_GAMMA=1` takes the pictures with the
  gamma, `GT4R_CRASH_TEST=throw` (`segv`, `abort`) closes the app as a fault would.

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
- `gt4tool sweep <disc> wall [event=..] [car=..] [steer=..]`: a wall hit under an event's rules: the
  penalty's seconds and the speed after it.
- `gt4tool sweep <disc> grid [filter]`: where every course's grid places stand along the course.
- A Gran Turismo mode game played forward (every license, the Beginner and Professional events
  but the World Championship won, 26.8 % complete, the presents) for playing the
  later events: `gt4tool save <disc> progress <data folder> [name] [credits]`, then
  `GT4R_DATA_DIR=<data folder> gt4r <disc>`.

## What runs where (the app's shell)

- The window, the frame loop, the screens, the scripted keys, the pictures and the sweeps are the
  app's own (`src/gt4r/app/app.*`, `screen_sweep.cpp`); the home screen, the setup screen and the
  asset browser are tools of this app, not pages of the game.
- The pad, wheel and keyboard are read by the shared engine's input layer (`gt3r::InputManager`: devices,
  hot plug, bindings, the steering's dead zone and response); from its virtual pads on the race's frame is
  the game's own (the controller 0x43CF90, `src/gt4r/input`, equal to the program's), and so is what the
  step makes of it (0x349FD0). The original rumble's level and the wheel's force are the game's; the Modern
  rumble, the triggers and the Modern wheel force are the shared engine's, fed from the game's step
  (`docs/specs/input.md`).
- Settings, Mods, Controls and the mouse are the app's own (`src/gt4r/settings`, `app_settings.cpp`, `page.*`,
  `screen_settings.cpp`, `screen_controls.cpp`) over the shared engine's parts: the GPU device's options, dynamic
  resolution, the gamma pass, the output devices, the log file and crash notes, the disc list, the mods' archives,
  the texture packs. The race display's place on a 4:3 picture is the program's; on one wider than 16:9 its parts go out
  with the picture's edge (ours: `race/game_area.h`).
- The 2D drawing toolkit of the home screen, the setup screen and the browser is the shared
  engine's (`ui.cpp`, `ui_font.cpp`, `button_prompts.cpp`); the game's pages are drawn by the menu
  code (`src/gt4r/menu`, `screen_menu_viewer.cpp`).
