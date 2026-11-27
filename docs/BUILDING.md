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
| Windows x64 | MSVC 2022 | builds, no-disc tests pass; package starts; see [Windows](#windows) |
| Android arm64-v8a + x86_64 | NDK 27 Clang (Gradle) | the APK builds, debug and signed release; see [android/README.md](../android/README.md) |
| Meta Quest (Android arm64-v8a, `-Pquest`) | NDK 27 Clang (Gradle), SDL's development branch | the APK builds, debug and signed release; not run on a headset; see [docs/VR.md](VR.md) |

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

## Windows

### What runs where

| Build | Compiler | State | Checked |
|---|---|---|---|
| Windows x64, native | MSVC 2022 14.44 (Visual Studio 17 2022 generator) | ✅ builds, no-disc tests pass | GitHub Actions `windows-2022`: `gt4r`, `gt4tool`, `gt4r_tests` build; the tests without a disc give the same result as macOS test by test (205 run, 305 skipped, none failing) |
| Windows x64, release package | MSVC 2022, SDL3.dll, ANGLE | ✅ packaged, starts | `gt4r-windows-x64.zip` (11.7 MB: `gt4r.exe`, `gt4tool.exe`, `SDL3.dll`, `libEGL.dll`, `libGLESv2.dll`, `LICENSE-ANGLE.txt`, the notices and docs); started from the archive with no disc on the runner, it draws the disc-choice page through ANGLE on Direct3D 11's software adapter (Microsoft Basic Render Driver) |
| Windows x64, PC VR package | MSVC 2022, `-DGT3R_XR=ON` (SDL's development branch), SDL3.dll, ANGLE, `openxr_loader.dll` | ❓ not built yet | `gt4r-windows-x64-vr.zip`, job `package` of a release run; the workflow checks the loader is in it and starts it as the flat package (no OpenXR runtime on the runner: the window). On a Mac the same option configures and the macOS build compiles ([VR.md](VR.md)) |
| Windows x64, with the discs | MSVC 2022 | ❓ not run | the disc tests, a race and the 3D renderer on a real GPU (Vulkan) need a Windows PC with the discs |
| Windows x64, cross | MinGW-w64 GCC 15 (from macOS) | ✅ builds, all tests pass under Wine | CrossOver's Wine: every test with both discs passes; a race's exact car records (`gt4tool state-hash`, Tsukuba, 6 cars, 60 s) are bit for bit the macOS ones, and the online cars' digests (`gt4tool car-digests`, 722 cars) the same |

The renderer on Windows is Vulkan (SPIR-V shaders); on GPUs without Vulkan, OpenGL ES 3.0 through
ANGLE (Direct3D 11) when ANGLE's DLLs sit beside `gt4r.exe`, as in the release package, else the
driver's OpenGL ES; without either the game runs its 2D screens only.

### With Visual Studio

Needs Visual Studio 2022 or its Build Tools with the "Desktop development with C++" workload,
CMake 3.21 or newer and Git (SDL3, zlib and libchdr are fetched and built the first time).

```bat
git clone --recursive <this repository> gt4r
cd gt4r
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel --target gt4r gt4tool gt4r_tests
build\Release\gt4r.exe "D:\discs\Gran Turismo 4.iso"
```

The tests, without a disc and with the discs (paths separated by `;`):

```bat
build\Release\gt4r_tests.exe > tests.log 2>&1
set GT4R_DISC=D:\discs\gt4.iso;D:\discs\gt4-prologue.iso
build\Release\gt4r_tests.exe > tests-discs.log 2>&1
```

The last line of the log counts the tests run and skipped and the failed checks; every failure is a
line starting with `FAIL`. Run them through `cmd` (`cmd /c "... > log 2>&1"`) from Windows
PowerShell 5: it stops on a program's standard error otherwise.

### The release package

As the release workflow builds it: Release, SDL built as `SDL3.dll`, ANGLE's DLLs beside the game,
no tests, installed and zipped.

```bat
cmake -S . -B build-pkg -G "Visual Studio 17 2022" -A x64 -DGT3R_FETCH_SDL=ON -DGT3R_SDL_SHARED=ON ^
      -DGT4R_BUILD_TESTS=OFF -DGT3R_ANGLE_DIR=<folder with libEGL.dll, libGLESv2.dll, LICENSE-ANGLE.txt>
cmake --build build-pkg --config Release --parallel --target gt4r gt4tool
cmake --install build-pkg --config Release --prefix dist\gt4r-windows-x64
powershell Compress-Archive -Path dist\gt4r-windows-x64 -DestinationPath gt4r-windows-x64.zip
```

ANGLE's folder comes from `packaging/angle.ps1` (vcpkg, about 18 minutes). With
`GT3R_SDL_SHARED=ON` a `gt4r_tests.exe` of that build needs `SDL3.dll` copied beside it; the test
build above links SDL statically.

### On GitHub Actions

The workflow (`.github/workflows/build.yml`) builds and runs the no-disc tests on `windows-2022`, and a
manual run (`gh workflow run build.yml --ref <branch> -f platforms=windows`) builds the package too,
lists its files with the archive's SHA256, and starts it once with no disc (job `package`, step
"Smoke run (Windows)"): its log is printed, and on a manual run the picture is in the log as base64
between `---- smoke picture (base64) ----` and `---- end ----`. No disc image goes to the runners,
so the tests that need one skip there.

### On a PC over SSH

`tools/windows/` (its README) sends the sources to a Windows PC and builds, tests and packages
there with MSVC over SSH, and runs the game in the signed-in user's desktop session (programs
started over SSH have no desktop and no GPU).

### MinGW cross build (from macOS or Linux)

With MinGW-w64 (`brew install mingw-w64` on macOS) and the engine's toolchain file:

```bash
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=engine/gt3r/cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DGT3R_FETCH_SDL=ON -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build build-mingw -j 4
```

`GT3R_FETCH_SDL=ON` keeps CMake from taking the host's own SDL3. The static executables need only
the system's DLLs. The tests run under Wine; with CrossOver on a Mac, in a bottle of their own and
with the discs as Wine paths (`Z:\` is the Mac's root):

```bash
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
$CX/cxbottle --bottle gt4r-windows --create --template win10_64
GT4R_DISC='Z:\path\to\gt4.iso;Z:\path\to\prologue.iso' SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy WINEDEBUG=-all \
  $CX/wine --bottle gt4r-windows --wait-children "$PWD/build-mingw/gt4r_tests.exe" > tests-wine.log 2>&1
```

A crash there prints Wine's backtrace; `x86_64-w64-mingw32-addr2line -f -C -i -e build-mingw/gt4r_tests.exe <addresses>`
names the functions.

### Windows in our code

- Host paths: `std::filesystem` writes `\` on Windows. A path kept as text (a file inside the data
  folder, as the save library's marker) is written with `/` and made native again when read
  (`path::make_preferred`), so that it compares equal to the paths the folder listings give.
- `setenv` / `unsetenv` in the tests are `_putenv_s` on Windows (`tests/test_framework.h`);
  `timegm` is `_mkgmtime`; `popen` is `_popen`, read in binary.
- Floats: MSVC x64 and GCC on x86-64 do not fuse `a * b + c`, clang on arm64 does; everything the
  race steps with goes through the engine's EE float arithmetic, which is the same on every host
  (`gt4tool state-hash` compares a race between two builds).

## Making a release

The workflow `.github/workflows/build.yml` builds the release packages and puts them in a draft
GitHub Release; nothing is published until it is reviewed and published by hand.

### Once: the Android signing key

The Android APK is signed with the project's own key. Every release must be signed with the same
key, or phones that have the game installed can't update it (they would have to uninstall it,
which removes its saves). Make the key once, on your own computer:

1. Make the key: `android/make-release-key.sh` (it needs Java's `keytool`, which comes with any
   JDK). It asks for a password twice (the key's password is the same one), and writes
   `gt4r-release.jks` in the folder you run it from. The certificate names the project, not you.
2. Keep it safe: put `gt4r-release.jks` and its password in your password manager, and keep a
   copy of the file somewhere backed up. Never commit it (`.gitignore` leaves out `*.jks`). If the
   key is lost, later releases can't update installed copies.
3. Give it to the repository's Actions as two secrets (with the GitHub CLI, from the repository's
   folder):

   ```bash
   base64 -i gt4r-release.jks | gh secret set ANDROID_KEYSTORE_BASE64
   gh secret set ANDROID_KEYSTORE_PASSWORD
   ```

   The second command asks for the password. On Linux, `base64 -w0 gt4r-release.jks` in place of
   `base64 -i gt4r-release.jks`. `gh secret list` shows both names (never their values).
4. Delete the `gt4r-release.jks` you made in step 1 from that folder once it is in the password
   manager.

Without both secrets the Android jobs (`package-android`, `package-quest`) fail with a message naming them, and
the release run has no APK and no `SHA256SUMS`: a release never carries an unsigned or debug APK.
Add the secrets, then re-run the failed jobs (Re-run failed jobs); job `release` follows them. Each job checks its
APK before it uploads it: signed (`apksigner verify`), not debuggable, and its version the tag's; the Quest APK also
the headset's build (head tracking required, arm64 only, the OpenXR loader in).
The key's certificate fingerprint (SHA-256) is in the job's log; it is the same in every release.

### Each release

1. Set the version in the top-level `CMakeLists.txt` (`project(GT4Revisited VERSION x.y.z)`); the
   app and the APK take it from there (the APK's version code is `x*10000 + y*100 + z`: 0.1.0 is
   100). Push the version's tag from the commit to release: `git tag v0.1.0 && git push origin v0.1.0`.
2. The tag's run (Actions, workflow `build`): job `draft` creates a draft release `v0.1.0`
   (title "GT4 Revisited v0.1.0"; its notes are GitHub's generated ones, only a "Full Changelog"
   link when no pull requests were merged since the last release); the four `package` jobs build
   and upload `gt4r-macos-arm64.zip`, `gt4r-linux-x86_64.tar.gz`, `gt4r-windows-x64.zip` and the PC VR
   package `gt4r-windows-x64-vr.zip` to it, job `package-android` builds, signs and uploads
   `gt4r-android.apk`, job `package-quest` the Meta Quest app `gt4r-quest.apk` (the same key); job
   `release` adds `SHA256SUMS` of those files.
3. On the Releases page, open the draft, check its seven files, write the notes, then publish.

The files keep the same names in every release, so
`https://github.com/<owner>/<repository>/releases/latest/download/gt4r-macos-arm64.zip` (and the
others) always fetches the newest; the game's **Download update** uses them. Inside the desktop
archives the folder is named for the version (`gt4r-v0.1.0-macos-arm64/`).

A release run stores nothing in Actions storage: the packages go straight to the release, whose
files don't count against the account's storage quota. A failed `package` job can be re-run
(Re-run failed jobs): it replaces its file in the draft, and job `release` then runs again. Pushing
the tag again for a draft that already exists adds to that draft; a tag whose release is already
published is left alone (job `draft` stops the run).

**Build attestations** (`gh attestation verify <file> --repo <owner>/<repository>`) need a public
repository. A run on a private repository makes none and still succeeds. For a release made while
the repository was private: after the repository is made public, re-run job `release` of the tag's
run (in the run's page, the job's re-run button, or `gh run rerun <run id> --job <job id>`); it
checks the repository's visibility as it runs, rewrites the same `SHA256SUMS` and attests the
release's files.

**A test without a tag:** a manual run with `release_tag` set runs the same path into a draft of
that name and makes no git tag:

```bash
gh workflow run build.yml --ref <branch> -f release_tag=v0.1.0-citest
gh release delete v0.1.0-citest --yes     # afterwards: the test draft
```

Without `release_tag`, a manual run builds the packages as workflow artifacts (Actions storage)
and makes no release. A manual run for Android alone (the phone and Quest APKs): `-f platforms=android`; `-f
platforms=windows` builds the Windows and PC VR packages.

❓ The private repository has no Actions minutes: jobs `package-android` and `package-quest` and the
PC VR package have not run on GitHub yet. They run first on the release tag after the repository is made public. The
APK jobs' steps were checked on a Mac: `assembleRelease` (and `assembleRelease -Pquest`) with a throwaway key,
`apksigner verify` and the APKs' checks.
