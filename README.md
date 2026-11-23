# GT4 Revisited

A reimplementation of the Gran Turismo 4 engine for modern platforms, built on
[GT3 Revisited](https://github.com/matt-pulsipher/GT3-revisited)'s engine. It contains no game
code, data or assets: everything is read at runtime from your own disc image.

**Status: early.** The disc opens and its files can be browsed and previewed. Nothing of the
game itself runs yet. See [docs/ROADMAP.md](docs/ROADMAP.md).

## What works

- Opens a Gran Turismo 4 disc image (`.iso` or `.chd`), including both layers of the retail
  disc, and Gran Turismo 4 Prologue.
- Asset browser: every file of the disc by folder, with previews of textures (alone or inside
  cars, courses and menus), music and voice streams, and movies.
- 3D viewer for the game's models (car bodies, wheels, tires, wings, prize objects): open a
  file in the asset browser and press Square.
- `gt4tool`: identify a disc, list and extract its files, decode textures to PNG, describe and
  survey models.
- Mods layered over the disc's files, as in GT3 Revisited (`<user data>/mods/<id>/files/...`).

Tested discs: Gran Turismo 4 `SCUS-97328` v1.01 (NTSC-U/C) and Gran Turismo 4 Prologue
`SCPS-15055` v1.01 (NTSC-J).

## Building

```bash
git clone --recurse-submodules <this repository>
```
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```
```bash
cmake --build build -j
```

Needs CMake 3.21+, a C++20 compiler and SDL 3.4+ (found on the system, else built from
source). The shared engine is the `engine/gt3r` submodule; run
`git submodule update --init` if it is empty.

## Running

```bash
build/gt4r "/path/to/your/Gran Turismo 4.iso"
```

Without a path the app asks for the image (or drop it on the window); it remembers the last
one. Controls are GT3 Revisited's menu controls: arrows and Enter, Escape to go up, or a pad.

```bash
build/gt4tool info "/path/to/your/Gran Turismo 4.iso"
```

Tests: `build/gt4r_tests` runs without a disc; set `GT4R_DISC` to one or more images
(separated by `;`) to also run the tests that read game data.

## Documents

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how this sits on the shared engine
- [docs/FORMATS.md](docs/FORMATS.md): the disc's layout and file formats
- [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md): ground rules and workflow
- [docs/ROADMAP.md](docs/ROADMAP.md): what is done and what comes next
