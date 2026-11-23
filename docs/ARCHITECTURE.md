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

## Changing the shared engine

A change the engine needs for both games is made in GT3 Revisited, in terms that apply to any
game on the engine, and arrives here by moving the submodule. Nothing in `engine/gt3r` is
edited in place.
