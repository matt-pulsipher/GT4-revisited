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

## Installing

Download the package for your computer from the [Releases](../../releases) page. Each release
lists `SHA256SUMS`, and every file carries a build attestation: `gh attestation verify <file>
--repo <owner>/<repository>` checks that it was built by this repository's workflow from the
tagged source. The packages are not signed with a developer certificate, so the first launch
needs one extra step:

- **macOS** (Apple Silicon, macOS 12 or later): `gt4r-macos-arm64.zip`. Unzip it and move
  `gt4r.app` to Applications. On the first launch macOS says it cannot verify the app; open
  **System Settings → Privacy & Security**, scroll down and choose **Open Anyway** next to GT4
  Revisited. Later launches open normally.
- **Windows** (x64): `gt4r-windows-x64.zip`. Unzip it and run `gt4r.exe`. If SmartScreen says
  "Windows protected your PC", choose **More info → Run anyway**.
- **Linux** (x86_64): `gt4r-linux-x86_64.tar.gz`. Extract it and run `bin/gt4r`. 3D needs a
  Vulkan driver (Mesa's `mesa-vulkan-drivers` on AMD and Intel, or your GPU maker's), or on
  older GPUs OpenGL ES 3.0.

On the first start the app asks for your disc image: press Enter (or Cross) and choose your
Gran Turismo 4 image (`.iso` or `.chd`, made from your own disc), or drop the file on the window.
It is remembered, and later launches go straight to the game. Your saved games and settings are
kept in the app's data folder, not beside the program, so a new version replaces the old one
without touching them. [docs/PLAYING.md](docs/PLAYING.md) has the controls and what works.

`gt4tool`, next to the app, is a command-line tool for the disc's files (texture names for
texture packs, and checks); playing needs only the app.

Or build it yourself ([Building](#building)); a build from source needs none of the steps
above.

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

A release package (`gt4r-macos-arm64.zip` or `gt4r-linux-<arch>.tar.gz` with `SHA256SUMS`, built
from a clean build folder as CI builds it): `tools/release/package.sh`. The app icon is drawn by
`packaging/make_icon.py` (Pillow).

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

## License

GT4 Revisited's own code is released under the [MIT License](LICENSE). The engine and the
third-party components it includes keep their own licenses:
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), which every package carries.
