# Building GT4 Revisited

## Windows

### What runs where

| Build | Compiler | State | Checked |
|---|---|---|---|
| Windows x64, native | MSVC 2022 14.44 (Visual Studio 17 2022 generator) | ✅ builds, no-disc tests pass | GitHub Actions `windows-2022`: `gt4r`, `gt4tool`, `gt4r_tests` build; the tests without a disc give the same result as macOS test by test (205 run, 305 skipped, none failing) |
| Windows x64, release package | MSVC 2022, SDL3.dll, ANGLE | ✅ packaged, starts | `gt4r-windows-x64.zip` (11.7 MB: `gt4r.exe`, `gt4tool.exe`, `SDL3.dll`, `libEGL.dll`, `libGLESv2.dll`, `LICENSE-ANGLE.txt`, the notices and docs); started from the archive with no disc on the runner, it draws the disc-choice page through ANGLE on Direct3D 11's software adapter (Microsoft Basic Render Driver) |
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
