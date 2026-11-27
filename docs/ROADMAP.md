# Roadmap

The order of work: each detail of the game made right and checked against the original, one item
at a time; broad sweeps only as a backstop. Then the features GT3 Revisited adds, each with an
equivalent here. Each area's docs keep a "What runs where" list of what is still not the game's
own code or is judged rather than read.

## Done

- **Disc**: identification, both layers, the volumes' file index, packed files; Prologue's volume.
- **The race step**: every phase of the game's own step, checked call by call against its program
  ([specs/physics.md](specs/physics.md)); the car's parts, settings and specification as the game
  builds them, on all 722 cars; the road, surfaces, walls, cones and boards. Floats as the console
  computes them: the disc's demonstration runs and the Replay Theater's demo replays, recorded on a
  console, end on their recorded times (§9b).
- **Computer drivers** ([specs/ai.md](specs/ai.md)): the decision, rubber band, B-spec, pit flow,
  demonstrations, the opponent picker.
- **The race**: every race kind's flow (grid, rolling and standing starts, finish, results),
  license and mission rules, pit stops with the crew, penalties, the race display, the replay with
  its effects and controls, the driver figures, 2 Player Battle.
- **Sound**: the race's engine layers, effects and music as the program posts them; the menus.
- **Menus**: every page from the game's own scripts and layouts, drawn as the original draws them;
  Gran Turismo mode's whole loop; Photo Travel and Photo Lab; the Replay Theater.
- **Saves**: the game's own format, several games, saved by themselves, PS2 card images in and
  out, the save editor.
- **Prologue** ([formats/prologue.md](formats/prologue.md)): its own step, drivers and School Mode.
- **The app**: Settings, Controls with rumble and wheels, mods and texture packs, online races
  ([specs/net.md](specs/net.md)).

## Next

1. **Details of the game still open**, from the docs' "What runs where" lists, `PLAYING.md`'s
   known problems, the game rules not yet the game's ([RULES_AUDIT.md](RULES_AUDIT.md), ranked) and the menu comparisons: Arcade's Quick Tune in the race, machine tests' start,
   photo mode's reflections and films, the driver's gaze and gear changes, Prologue's race sound,
   the pages and screens not yet compared with the original's captures.
2. **Online, the rest**: other players' pit stops in the prediction of their car, then racing
   between computers on different systems.
3. **Platforms**: Linux, then Windows; packages.
4. **What GT3 Revisited adds that this lacks yet**, in the order players miss them: Quick race
   and custom races, mouse and touch on the game's pages, own music, custom cameras, VR.

[FORMATS.md](FORMATS.md) has each format's state.
