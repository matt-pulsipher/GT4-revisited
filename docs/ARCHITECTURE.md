# Architecture

GT4 Revisited is its own game on a shared engine. The engine is GT3 Revisited, pinned as the
git submodule `engine/gt3r`; this repository adds what Gran Turismo 4 needs on top.

```
   src/gt4r/app/      the executable: window, screens (asset browser, ...)
   src/gt4r/...       Gran Turismo 4: disc, formats, game (this repository)
          │
   engine/gt3r/src/   the shared engine (submodule): base, disc, vfs, formats, audio,
                      video, render, input, ...; its src/app is GT3's own executable
```

## Building against the submodule

`CMakeLists.txt` compiles the submodule's engine sources into `gt3r_engine` exactly as its own
build does (same sources, dependencies and generated headers), then `gt4r_engine` from
`src/gt4r` (everything but `app/`), then the `gt4r` executable. The executable also compiles
three files of the submodule's `src/app`: its UI toolkit (`ui.cpp`, `ui_font.cpp`,
`button_prompts.cpp`), which is not game specific.

Includes tell the two apart: `"gt4r/disc/disc.h"` is ours, `"disc/iso9660.h"` the shared
engine's. Our code is in namespace `gt4r`, the engine's in `gt3r`.

The app's name, "GT4 Revisited", is in `src/gt4r/app/app_identity.h`: the window, the system's
app name (`org.gt4revisited.gt4r`), message boxes, `--version`. At start it gives the engine the
name, version and log file stem (the engine's `base/app_identity.h`), which the engine uses for
the log (`gt4r.log`, `gt4r.prev.log`, its first line), the text a host sends a player whose copy
is too old, and the disc's volume in the log. An engine without that header (before its
submodule carries it) still writes `gt3r.log` with a "GT3 Revisited" first line.

## Principles (the shared engine's, kept here)

- **No game data in the repository, none written to disk.** Files are read from the user's
  image on demand. Only `gt4tool extract` writes files, when the user asks.
- **Everything through the file layer** (`gt3r::Vfs`), so a mod can replace any file.
- **Decode through hardware models where the data depends on them** (textures through
  `GsMemory`).
- **SDL3 is the only platform layer.**

## The disc under the shared engine's file layer

The shared engine reads game files through `gt3r::Volume`, a reader of Gran Turismo 3's
volume layout. Gran Turismo 4's volumes are indexed and packed differently
([FORMATS.md](FORMATS.md)), so `gt4r::Disc` lists the files itself (`archive_index.*`) and
hands the engine an `ArchiveImage` (`archive_image.*`): a read-only view of the disc that
presents those files as one volume in Gran Turismo 3's layout, every file stored plain.
Plain files are read from the disc where they are; a packed file is unpacked when a read first
touches it. Above that, `gt3r::Volume`, `gt3r::Vfs` (mods), the music and movie streams and
every decoder work unchanged.

## The race view

`src/gt4r/race/` draws a race from the states of a `race::Simulation` (`simulation.h`) and
nothing else of it: `course_scene.*` (a course's model sets in the game's order),
`animated_models.*` (its scripted models), `car_visual.*` with `car_pose.*` and `car_spec.*` (a
car's parts placed from a state), `drive_camera.*` and `replay_script.*` (the views),
`race_display.*` (the display's places and numbers). The model sets become the shared engine's
(`formats/model/mdls.*`) and are drawn by its renderer; the environment block, the visibility
lists, the billboards, the lamp glows, the sun and the smoke are the shared engine's layouts
and go to its own code as they are. `rail_simulation.*` is a stand-in that runs the cars along
the road's line. The app's part is `app/screen_race.cpp` (the *Drive* screen) and
`app/race_hud.*` (the display's drawing).

## Loading a race

`app/race_world.cpp` reads the cars' files, then decodes their shapes on threads
(`CarVisual::make_unuploaded`: a few cars ahead, the first of them while the course is read)
and hands them to the GPU on the main thread, a car's equal pictures once. What several parts
of a race read is worked out once: the program unpacked (`Disc::program_image`, begun on a
thread as the disc opens; every `sim::Program` of a disc holds that one image), the sound banks
of the race's cars (`audio::EngineBanks`), the triangles of a road by cells of the ground for
the heights beside it (`course::Runway::Cells`). A course's animated models are decoded a key of
all of them at a time (`race/animated_models.cpp`). A `gtm1::Gtm1` points into the block it was
parsed from: the block has to outlive it.

## What comes from the shared engine

On a Gran Turismo 4 or Prologue disc no value of Gran Turismo 3's serves as a fallback for one of
Gran Turismo 4's.

- **The program's tables.** Gran Turismo 4's are values in our code (`sim/program_tables*`,
  SCUS-97328 v1.01's). `sim::Program` serves them by their v1.01 address on any Gran Turismo 4
  disc and checks the disc's own program against them once, logging what differs or is missing
  (physics.md "Program tables"). Prologue's are values too (`sim/program_tables_prologue*`,
  SCPS-15055 v1.01's), served on any Prologue disc and checked against its program once
  (formats/prologue.md "The program").
- **The engine's GT3 tables** are never used: `sim::core_program` (the engine's `ProgramData`) holds Gran
  Turismo 4's static block and soft materials on a Gran Turismo 4 disc, Prologue's own tables on a Prologue
  disc, and 0 everywhere else; the engine's own step rules do not run on any disc (physics.md "What runs where").
- **A program that is neither Gran Turismo 4's nor a placed Prologue build** races nothing: the race shows the
  player "this release is not supported yet".
- **What still runs the engine's code on a Gran Turismo 4 disc** is listed where it runs:
  - the race step: physics.md "What runs where" (only the shared math, checked bit for bit);
  - sound: formats/sound.md;
  - the camera: formats/camera.md;
  - drawing: formats/course.md and race_display.md;
  - input and rumble: specs/input.md.

## Changing the shared engine

A change the engine needs for both games is made in GT3 Revisited, in terms that apply to any
game on the engine, and arrives here by moving the submodule. Nothing in `engine/gt3r` is
edited in place.
