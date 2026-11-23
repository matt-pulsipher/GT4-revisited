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
      home and garage (`gt4tool menu-run`; *Menu pages* in the app, `GT4R_MENU="boot=1"`).
- [x] Models (`MDLS`): car bodies, wheels, tires, wings and prize objects decode and draw as
      still models; a 3D viewer from the asset browser (Square on a file with models).

Next, in order:

1. **Sound formats that changed since Gran Turismo 3**: instrument banks, sequences, the music
   index; the double-byte fonts (`.kf`).
2. **Cars in motion**: the car callbacks as the game runs them (steering, suspension, wheel
   spin and the turning wheel pictures), the model scripts, the skinned drivers, paint choice.
3. **Courses**: billboards, lamps, the environment block, visibility lists in a drawn course.
4. **The car database's columns**: specifications, parts, events, opponents.
5. **The menus, further**: Gran Turismo mode past the home page (dealers, events, licenses,
   a saved game behind the state), the list box and scroll window from the program, the 3D
   faces, Japanese text, Prologue's flows; the race the menus ask for started.
6. **The race**: physics, computer drivers, the race display, replays.

[FORMATS.md](FORMATS.md) has each format's state.
