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

Gran Turismo 4 `SCUS-97328` v1.01 (NTSC-U/C), `SCES-51719` v1.00 (PAL: English, French, German,
Italian and Spanish, chosen at each start as on the console) and `SCPS-17001` v1.05 (NTSC-J), and Gran Turismo 4
Prologue `SCPS-15055` v1.01 (NTSC-J). Every disc races at 60 frames a second, the PAL one too. Each release keeps its own
saved games. The Japanese release's own driving rules are still being added (it races on the US release's for now).

## Installing

Download the package for your computer or phone from the [Releases](../../releases) page. Each
release lists `SHA256SUMS`, and every file carries a build attestation: `gh attestation verify
<file> --repo <owner>/<repository>` checks that it was built by this repository's workflow from the
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
- **Android** (7.0 or later, arm64 or x86_64, with Vulkan): `gt4r-android.apk`. Open it on the
  phone or tablet (from the browser's downloads or the Files app) and allow installing apps from
  that source when Android asks. Where Android requires verified developers for apps installed
  this way, use its install flow for unverified apps (in Developer options), or install it from a
  computer with `adb install gt4r-android.apk`. Copy your disc image to the device (or an SD card
  or USB drive), start GT4 Revisited, tap **Choose a disc image** and pick the image in Android's
  file picker; it is remembered. A newer release's APK installs over this one and keeps your saves
  and settings. A copy you built yourself is signed with another key: Android won't update it
  with the release's APK, so uninstall it first, which deletes its storage (move the saves out
  through the Files app, "GT4 Revisited", before).
- **Meta Quest** (Quest 2, 3, 3S and Pro): `gt4r-quest.apk`, the game in the headset: the menus on a large screen in
  front of you, races in 3D from the car's seat ([docs/VR.md](docs/VR.md)). Install it with `adb install gt4r-quest.apk`
  from a computer (the headset in developer mode) or with a sideloading app, copy your disc image to the headset, and
  choose it in the file picker as on Android. It has the Android app's identity, so a headset holds one or the other.
- **PC VR** (Windows x64, a headset through Quest Link, SteamVR or another OpenXR runtime): `gt4r-windows-x64-vr.zip`.
  Unzip it and run `gt4r.exe` with the headset's software running; without a headset it opens its window as the
  Windows package does.

On a computer, the first start asks for your disc image: press Enter (or Cross) and choose your
Gran Turismo 4 image (`.iso` or `.chd`, made from your own disc), or drop the file on the window.
It is remembered, and later launches go straight to the game. Your saved games and settings are
kept in the app's data folder, not beside the program, so a new version replaces the old one
without touching them. [docs/PLAYING.md](docs/PLAYING.md) has the controls and what works.

`gt4tool`, next to the app, is a command-line tool for the disc's files (texture names for
texture packs, and checks); playing needs only the app. On macOS its first run from a download is
blocked the same way: choose **Allow Anyway** for it in **System Settings → Privacy & Security**,
then run it again.

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
`git submodule update --init` if it is empty. Linux (on the machine or in Docker) and the other
platforms: [docs/BUILDING.md](docs/BUILDING.md). Android phones and tablets:
[android/README.md](android/README.md) (an APK built with Gradle).

VR headsets (the Meta Quest APK, a desktop build with OpenXR): [docs/VR.md](docs/VR.md).

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
- [docs/BUILDING.md](docs/BUILDING.md): building on each platform
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how this sits on the shared engine
- [docs/FORMATS.md](docs/FORMATS.md): the disc's layout and file formats
- [docs/specs/](docs/specs/): the race step, the computer drivers, online play
- [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md): ground rules and workflow
- [docs/ROADMAP.md](docs/ROADMAP.md): what is done and what comes next

## License

GT4 Revisited's own code is released under the [MIT License](LICENSE). The engine and the
third-party components it includes keep their own licenses:
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), which every package carries.
