# Roadmap

Done:

- [x] Disc: identification, both layers, the volumes' file index, packed files; Prologue's
      volume. `gt4tool`. Tests on made-up volumes and on real discs.
- [x] Asset browser: textures, music and voice streams, movies.
- [x] Courses: the container, all scenery and the road (surface, lookup tree, walls,
      checkpoints, start grid); a course opens in the 3D viewer.
- [x] Scripts (`.adc`): all 488 read and list (`gt4tool adc`). The interpreter runs them
      (`src/gt4r/script/vm.*`, `gt4tool run`): every instruction the discs' scripts use, the
      built-in methods of strings, arrays and modules; the program's own classes are stand-ins.
- [x] Whole cars as still models: the painted body with its wheels and tires in place.
- [x] The car database: every table unpacks; the cars and courses by name on the home screen.
- [x] Menus as still pictures: the layouts (`.mproject`), the pages' pictures (`.gpb`), the
      text (`.rt2`) and the fonts (`.fnt`) read; the app draws any page of any project
      (*Menu pages*); `gt4tool menu`.
- [x] Menus running: the game's own scripts from power on (the main script, the
      configuration, scripts that wait), the events, keys, focus search, boxes and actors as
      the program has them, the game's state behind `main::game`. Boot to the title, Arcade
      to the race it asks for, the options and back, a new game into Gran Turismo mode's map,
      home and garage, a car bought at a dealer (`gt4tool menu-run`; *Menu pages* in the app,
      `GT4R_MENU="boot=1"`).
- [x] Models (`MDLS`): car bodies, wheels, tires, wings and prize objects decode and draw as
      still models; a 3D viewer from the asset browser (Square on a file with models).

- [x] Sound: the instrument banks, sequences and indexes that changed since Gran Turismo 3
      read; a sound driver after the game's plays the menus' effects and sequenced music, the
      scripts' sound object (`audio::MenuSound`) and a car's engine sounds chosen from the
      database (`gt4tool sound`). The double-byte fonts (`.kf`).
- [x] The race view (`src/gt4r/race/`, *Drive* on the home screen, `gt4tool race`): a course
      drawn in the game's order (sky, backdrop, the scenery by the visibility list of the
      camera's place, the flags and people moved by the sets' scripts, billboards, the sun, the
      lamps' glows, fog and lights of the environment block, smoke and dust); cars on their
      wheels (the body by the wheels' places, steered and turning wheels with their turning
      pictures, brake parts, lit brake lamps, both shadows, lamps and their glows at night, the
      database's paints); the three driving views and the course's replay cameras; the race
      display (position, laps, times, the course map, the meters of both views, the start's
      count) from the disc's pictures, fonts and program tables; a finished run shown again
      through the replay cameras; the cars' engine, exhaust, wind and tire sounds. The cars
      move by the simulation behind `simulation.h` (a stand-in, `rail_simulation.*`, where it
      cannot be set up).
- [x] Gran Turismo 4 Prologue ([formats/prologue.md](formats/prologue.md)): its database in
      Gran Turismo 4's layout, its roads (`GTRW`) as Runways, its drivers from the course files,
      its cars in the race view, its program recognized with its own physics tables, its menus
      from power on through the School Mode into a license test.
- [x] The simulation, a first part (`src/gt4r/sim/`, `gt4tool car`, `road`, `drive`): a car's
      parts, default settings and specification as the game builds them, and its physics
      parameters, checked against the user's own program on every car (`gt4r_diff`); the
      road query, surfaces and walls; a session that steps cars with the shared engine's
      vehicle rules ([specs/physics.md](specs/physics.md) lists the rules the game changed).
- [x] Online races (Arcade's Multi-LAN Race with the game's own pages, on one network or through a relay server:
      [specs/net.md](specs/net.md)).

Next, in order:

1. **The race's own rules**: the per-step routines the game changed since Gran Turismo 3
   (tires, suspension, driveline, aids: [specs/physics.md](specs/physics.md)), the computer
   drivers (the courses' `.ad` and `crs/bestline` files), the race's flow (grid, start,
   finish, results), pits, tire wear.
2. **Sound in the race**: read from the program and built (`audio::RaceSound`,
   [formats/sound.md](formats/sound.md) "A race's sound"): the views' gains, the microphones,
   the engine's and exhaust's levels by the throttle, the wheels' notes by surface, the course's
   ambience, tunnels and reverb, the count's beeps, the race's music by race type and the play
   list, contacts, landings and the gearbox's whine from the simulation; checked against the
   original's recordings (`gt4tool sound compare`; the engine's pitch, the players' volumes, the
   count, which music plays when). Left: the Drive screen's calls for a rolling start and the
   replay, the play list from the save, the replay's camera kind for the master's duck, the streams' level in the IOP's module (about 2 dB under the program's), the pit crews' sounds (their scene is not built; the jack's drop plays).
3. **Cars, the rest**: the far level's wheels, the brake discs' glow, fitted wings, the moving
   parts of the few cars with scripts, the skinned drivers, how a race picks opponents' paints.
4. **Courses, the rest**: the mirror, cones and boards (`GDGT`), skid marks, the models that
   move or turn by registers, the animated skies, the glow filter of the photo locations.
5. **The car database, the rest**: the events (`RACE`) and opponents (`ENEMY_CARS`).
6. **The menus, further**: Gran Turismo mode past the home page (used cars, parts, events,
   licenses), the list box and the idle actors from the program, the 3D faces, Japanese
   text, Prologue's arcade mode and its results.
7. **Saved games and replays**.

[FORMATS.md](FORMATS.md) has each format's state.
