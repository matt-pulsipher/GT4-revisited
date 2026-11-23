# Roadmap

Done:

- [x] Disc: identification, both layers, the volumes' file index, packed files; Prologue's
      volume. `gt4tool`. Tests on made-up volumes and on real discs.
- [x] Asset browser: textures, music and voice streams, movies.

Next, in order:

1. **Sound and text formats that changed since Gran Turismo 3**: instrument banks, sequences,
   the music index, fonts, the string tables (`.rt2`).
2. **Models** (`MDLS`): cars, wheels, tires, wings, prize objects; a model viewer.
3. **Courses**: the container, scenery, the road (`RNW4`), a course viewer.
4. **The car database** (`specdb`).
5. **The menus**: the script interpreter (`.adc`), the widget toolkit (`.mproject`, `.gpb`),
   the functions the scripts call.
6. **The race**: physics, computer drivers, the race display, replays.

[FORMATS.md](FORMATS.md) has each format's state.
