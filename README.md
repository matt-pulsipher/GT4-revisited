# GT4 Revisited

A reimplementation of Gran Turismo 4 for modern platforms, built on
[GT3 Revisited](https://github.com/matt-pulsipher/GT3-revisited)'s engine. It contains no game
code, data or assets: everything is read at runtime from your own disc image.

**Status: the whole game runs, with gaps.** From power on to the title, Arcade, Gran Turismo mode
with its licenses, events, championships, missions, the garage, shops and saves, 2 Player Battle,
the Replay Theater, Photo Travel and Prologue's School Mode, on macOS. The race step, the
computer drivers and the race rules are the game's own, checked step by step against its program
and against the disc's demonstration runs recorded on a real console. What still differs from the
original is listed in [docs/PLAYING.md](docs/PLAYING.md) "Known problems" and in each area's
"What runs where" in the docs; what comes next in [docs/ROADMAP.md](docs/ROADMAP.md).

## What it adds

All of it is optional.

- Any resolution and aspect ratio, high refresh rates, anti-aliasing (Settings).
- Saved games are files: several games side by side, saved by themselves, no memory card
  questions; export to and import from PlayStation 2 memory card images; a save editor.
- Controllers, wheels with force feedback, rumble made for modern controllers, remappable controls.
- Online races in Arcade's Multi-LAN Race, on one network or with a session code.
- Mods layered over the disc's files, and texture packs.
- Replay effects and extra views as options.

## Discs

Gran Turismo 4 `SCUS-97328` v1.01 (NTSC-U/C) and Gran Turismo 4 Prologue `SCPS-15055` v1.01
(NTSC-J). Other releases are not supported yet.

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
`git submodule update --init` if it is empty. Built and played on macOS so far.

## Running

```bash
build/gt4r "/path/to/your/Gran Turismo 4.iso"
```

Without a path the app asks for the image (or drop it on the window); it remembers the last one.
[docs/PLAYING.md](docs/PLAYING.md) says how to play, the controls, and what works.

Tests: `build/gt4r_tests` runs without a disc; set `GT4R_DISC` to one or more images
(separated by `;`) to also run the tests that read game data.

## Documents

- [docs/PLAYING.md](docs/PLAYING.md): starting, controls, what works, known problems
- [docs/RELEASE_NOTES.md](docs/RELEASE_NOTES.md): what this release has
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how this sits on the shared engine
- [docs/FORMATS.md](docs/FORMATS.md): the disc's layout and file formats
- [docs/specs/](docs/specs/): the race step, the computer drivers, online play
- [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md): ground rules and workflow
- [docs/ROADMAP.md](docs/ROADMAP.md): what is done and what comes next
