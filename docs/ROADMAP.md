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
- [x] Menus running, a first part: a project's widgets as objects of the script world, the
      context, events, focus and keys; the title page builds its menu from the game's script
      and leads on to the project chosen (`gt4tool menu-run`; R1 in *Menu pages*).
- [x] Models (`MDLS`): car bodies, wheels, tires, wings and prize objects decode and draw as
      still models; a 3D viewer from the asset browser (Square on a file with models).

- [x] The race view (`src/gt4r/race/`, *Drive* on the home screen, `gt4tool race`): a course
      drawn in the game's order (sky, backdrop, the scenery by the visibility list of the
      camera's place, the flags and people moved by the sets' scripts, billboards, the sun, the
      lamps' glows, fog and lights of the environment block, smoke and dust); cars on their
      wheels (the body by the wheels' places, steered and turning wheels with their turning
      pictures, brake parts, lit brake lamps, both shadows, lamps and their glows at night, the
      database's paints); the three driving views and the course's replay cameras; the race
      display (position, laps, times, the course map, the meters of both views, the start's
      count) from the disc's pictures, fonts and program tables; a finished run shown again
      through the replay cameras. The cars move on a stand-in (`rail_simulation.*`) behind
      `simulation.h` until the simulation is in.

Next, in order:

1. **Sound formats that changed since Gran Turismo 3**: instrument banks, sequences, the music
   index; the double-byte fonts (`.kf`).
2. **Cars, the rest**: the far level's wheels, the brake discs' glow, fitted wings, the moving
   parts of the few cars with scripts, the skinned drivers, how a race picks opponents' paints.
3. **Courses, the rest**: the road reflection (slots 9, 13, 50), the mirror, cones and boards
   (`GDGT`), skid marks, the models that move or turn by registers, the animated skies, the
   glow filter of the photo locations.
4. **The car database's columns**: specifications, parts, events, opponents.
5. **The menus running**: the widgets as objects the scripts drive (focus, navigation, the
   actors), the functions of the program's classes the scripts call, events.
6. **The race**: physics, computer drivers, the race display, replays.

[FORMATS.md](FORMATS.md) has each format's state.
