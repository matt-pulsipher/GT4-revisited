# Roadmap

Done:

- [x] Disc: identification, both layers, the volumes' file index, packed files; Prologue's
      volume. `gt4tool`. Tests on made-up volumes and on real discs.
- [x] Asset browser: textures, music and voice streams, movies.
- [x] Courses: the container, all scenery and the road (surface, lookup tree, walls,
      checkpoints, start grid); a course opens in the 3D viewer.
- [x] Scripts (`.adc`): all 488 read and list (`gt4tool adc`).
- [x] Whole cars as still models: the painted body with its wheels and tires in place.
- [x] The car database: every table unpacks; the cars and courses by name on the home screen.
- [x] Models (`MDLS`): car bodies, wheels, tires, wings and prize objects decode and draw as
      still models; a 3D viewer from the asset browser (Square on a file with models).

Next, in order:

1. **Sound and text formats that changed since Gran Turismo 3**: instrument banks, sequences,
   the music index, fonts, the string tables (`.rt2`).
2. **Cars in motion**: the car callbacks as the game runs them (steering, suspension, wheel
   spin and the turning wheel pictures), the model scripts, the skinned drivers, paint choice.
3. **Courses**: billboards, lamps, the environment block, visibility lists in a drawn course.
4. **The car database's columns**: specifications, parts, events, opponents.
5. **The menus**: the script interpreter (what each instruction does), the widget toolkit
   (`.mproject`, `.gpb`), the text (`.rt2`), the functions of the program's classes the scripts
   call.
6. **The race**: physics, computer drivers, the race display, replays.

[FORMATS.md](FORMATS.md) has each format's state.
