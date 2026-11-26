# Building GT4 Revisited

The general steps are in the [README](../README.md#building): CMake 3.21+, a C++20 compiler, SDL 3.4+
(found on the system, else fetched and built from source), the `engine/gt3r` submodule checked out.
This page has the platform details.

| Platform | Compiler | State |
|---|---|---|
| macOS arm64 | Apple Clang | the development platform: builds, all tests pass on both discs |
| Linux x86_64 (Ubuntu 24.04) | GCC 13 | builds; all tests pass on both discs, with the macOS results ([what differs](#linux-and-macos-side-by-side)) |
| Linux arm64 (Ubuntu 24.04) | GCC 13 | builds; all tests pass on both discs |
| Linux x86_64 (Ubuntu 22.04) | GCC 11, GCC 12 | builds (the release package's distribution, for its older glibc) |
| Linux arm64 (Ubuntu 24.04) | Clang 18 | builds; all tests pass on both discs (x86_64 with Clang ❓ not built) |
| Windows | | see [Windows](#windows) |

## Linux

### What runs where

The window, input, sound, file dialog and the GPU device are SDL 3's (through the shared engine's
platform layer: Vulkan, else OpenGL ES 3.0, else the 2D screens alone); the data folder is SDL's
preference folder. Ours on Linux: the process's resident memory for the app's statistics
(`/proc/self/statm`, `app.cpp`); the LAN discovery uses the same POSIX sockets as on macOS (`net/discovery.cpp`). The game's code is
the same on every one.

### On a Linux machine

Install a compiler, CMake and the libraries SDL's Linux backends build against (Debian and Ubuntu
names; the shared engine's list, `engine/gt3r/docs/dev/PLATFORMS.md`):

```bash
sudo apt-get install build-essential git cmake ninja-build pkg-config \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libdecor-0-dev libxkbcommon-dev libegl1-mesa-dev libdrm-dev libgbm-dev \
  libudev-dev libdbus-1-dev zlib1g-dev
```

Without the X11, Wayland or sound packages SDL leaves those backends out without saying so (the
game then has no window or no sound on that system). Then:

```bash
tools/linux/build.sh --native                # or: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

`--native` builds into `build-linux-<arch>-<compiler>`. Distributions without an SDL 3.4 package
(Ubuntu 24.04 and older) get SDL fetched and built with the rest (network access on the first
configure); libchdr is always fetched.

### In Docker (from macOS or Linux)

`tools/linux/build.sh` without `--native` builds an image from `tools/linux/Dockerfile` (Ubuntu with
GCC, Clang and the packages above), mounts the source tree read-only and builds into
`build-linux-<arch>-<compiler>-<base>` inside it:

```bash
tools/linux/build.sh                                   # the host's architecture, GCC, ubuntu:24.04
tools/linux/build.sh --arch x86_64 --compiler clang    # x86_64 under emulation on an arm64 Mac
tools/linux/build.sh --base ubuntu:22.04               # an older glibc (GCC 11)
GT4R_DISC="<gt4 iso>;<prologue iso>" tools/linux/build.sh --test        # then the unit tests
GT4R_DISC="<gt4 iso>" tools/linux/build.sh --exec './gt4tool info "$GT4R_DISC"'
```

| Option | |
|---|---|
| `--arch x86_64 \| arm64` | the container's architecture (default: the host's). On Apple silicon x86_64 runs under Rosetta (OrbStack, Docker Desktop) |
| `--compiler gcc \| clang` | default gcc |
| `--base <image>` | the Debian or Ubuntu image (default `ubuntu:24.04`) |
| `--type <build type>` | default `RelWithDebInfo` |
| `--jobs <n>` | parallel jobs (default 4) |
| `--target <t>` | build only these targets (repeatable) |
| `--keep-going` | report every compile error, not only the first |
| `--test [filter]` | run `gt4r_tests` after the build; its log is `tests.log` in the build folder, the `FAIL` lines and the summary are printed |
| `--exec <command>` | a shell command run in the build folder after the build |
| `GT4R_SIM_STATES=<folder>` | the original's saved states two tests read, mounted read-only as well |
| `--native` | build on this Linux machine, no Docker |
| `-- <args>` | more arguments for CMake's configure |

Each disc named in `GT4R_DISC` (paths joined by `;`) is mounted read-only into the container and
`GT4R_DISC` is set to the mounted copies, for the tests and `--exec`. The tests and `--exec` run
headless (`SDL_VIDEO_DRIVER=dummy`, `SDL_AUDIODRIVER=dummy`), as the container's user is the host's,
with `HOME` and `GT4R_DATA_DIR` in the container's `/tmp`: nothing is written outside the build folder.

A binary built on `ubuntu:22.04` runs on distributions with glibc 2.35 or newer; one built on
`ubuntu:24.04` needs 2.39. GCC 11 and 12 (Ubuntu 22.04: the release package is built there with
GCC 12, `tools/release/package.sh`) GCC 13 and Clang 18 build it; GCC's library (libstdc++) does not
bring `<utility>`, `<optional>` or `<cmath>` in through other headers as Apple's does, so each file
includes what it uses.

### Running

```bash
build-linux-x86_64-gcc/gt4r "/path/to/Gran Turismo 4.iso"
```

The settings, saves and logs go in `~/.local/share/GT4Revisited/GT4Revisited/` (SDL's preference
folder, `$XDG_DATA_HOME` when set), unless a `portable.ini` next to the executable keeps them beside it
or `GT4R_DATA_DIR` names a folder. The disc can also be chosen from the file dialog (SDL's: the
desktop's portal, else zenity).

A scripted run (`--shot`, `GT4R_RACE`, ...) draws in a container without a display or a GPU when
Mesa's software Vulkan is there (`mesa-vulkan-drivers` on Ubuntu 24.04: lavapipe with
`VK_EXT_headless_surface`) and `SDL_VIDEO_DRIVER=offscreen`; Ubuntu 22.04's Mesa 23.2 lacks that
extension and the app keeps to its 2D screens. The build image (`tools/linux/Dockerfile`) has it, so
`--exec` can take pictures; for example, from the build folder:

```bash
SDL_VIDEO_DRIVER=offscreen SDL_AUDIODRIVER=dummy GT4R_DATA_DIR=/tmp/d \
  GT4R_RACE="course=tsukuba,cars=2,view=chase,drive=auto,skip=600" ./gt4r "$GT4R_DISC" --screen race --shot-frame 60 --shot race.png
```

### Comparing a race across platforms

`gt4tool state-hash <disc> <course> [cars] [seconds=60] [every=10]` runs a field of computer drivers
headless and prints, every `every` seconds and at the end, a hash of all the cars, one of each car's
published state (`race::CarState`, what the race's display draws) and, after `record`, one of each
car's own record (the step's single floats: place, axes, velocity, attitude, engine speed), then
every car's lap times. Two builds whose `record` columns and lap times are equal ran the race bit for
bit alike; the first line that differs says when they parted. `gt4tool car-digests <disc> [all]`
prints the digest an online race checks every car by (`net::CarBuilder::digest`); machines that race
each other must print the same.

### Linux and macOS side by side

Checked on Gran Turismo 4 (US 1.01) and Prologue (Japan), macOS arm64 (Apple Clang) against Linux
x86_64 and arm64 (GCC 13, Ubuntu 24.04):

- **The race step is the same, bit for bit.** Every car's record is equal at every step of 30 s of
  Tsukuba and at every 30 s of 300 s of Tsukuba, 240 s of Fuji, 180 s of Grand Canyon and of Amalfi
  (six computer cars) and 300 s of Prologue's Tsukuba (two), and so is every lap time. The step runs
  in the program's own float arithmetic (`gt3r::ee::F`, integer operations), which no compiler or
  processor changes.
- **The online cars are the same**: all 722 cars' digests are equal, so a Linux and a macOS machine
  accept each other's cars. (An online race sends each car's record, not its published state.)
- **The published state differs in its last bits from the first step**: `Session::publish` makes the
  display's values (the body's right axis and reference point, the wheels' spin) in host floats, and
  compilers fuse `a * b - c * d` into one multiply-add differently (Apple Clang on arm64 within a
  statement, GCC on arm64 across statements, x86-64 without FMA instructions not at all). It feeds
  the drawing, the sound and the headless field's incident counts (`ai::Field::incidents_after`); the
  step and the computer drivers' decisions take the record's values (the drivers' contacts are the
  collision step's flags).
- **What reads the published state differs with it**: the tests' and gt4tool's line driver
  (`sim::LineDriver`, a test driver in host floats) drives a lap of Tsukuba in 74.767 s on macOS and
  Linux x86_64 and 74.783 s on Linux arm64 (`disc_e2e_sweep_pad_lap`), and the road-height check's
  host-float sample points land on a triangle 13656, 13654 and 13657 times
  (`disc_road_heights_by_cells`); both checks pass on all three.
- Every other line the full suite prints is equal, but for timings.
