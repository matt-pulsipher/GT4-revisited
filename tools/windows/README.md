# Windows build scripts

Developer scripts for building, testing and packaging GT4 Revisited natively with MSVC on a Windows
PC driven from another machine over SSH (OpenSSH server on Windows; its shell is `cmd.exe`, so
anything longer than one line goes in a script file). Everything lives in one folder on the PC,
`D:\gt4r` unless `-Root` says otherwise. The PC needs Git, CMake 3.21 or newer and Visual Studio
2022 or its Build Tools (the C++ workload); docs/BUILDING.md "Windows" has the plain build.

| Script | What it does |
|---|---|
| `send.sh <user>@<pc> [root]` | (on the other machine) Sends the checked-out commit with any uncommitted changes, the engine submodule's commit, and these scripts. |
| `build.ps1 [-Clean]` | Brings `<root>\src` up to the sent sources (only changed files get a new time, so the build stays incremental) and builds `gt4r`, `gt4tool`, `gt4r_diff` and `gt4r_tests` (Visual Studio generator, Release, static SDL) in `<root>\build`. Lists the warnings of our own code. |
| `test.ps1 [-Discs] [-Filter name]` | Runs `gt4r_tests`: without discs, or with every `.iso` in `<root>\discs` as `GT4R_DISC`. Writes `<root>\tests.log` / `tests-discs.log` and prints the summary and every `FAIL` line. |
| `package.ps1` | The release package as the release workflow builds it (SDL3.dll, ANGLE from `<root>\angle` when there), installed into `<root>\dist\gt4r-windows-x64` and zipped as `<root>\gt4r-windows-x64.zip`. |
| `desktop-run.ps1 -Script <cmd> [-ScriptArgs ...]` | Runs a `.cmd` in the signed-in user's desktop session as a one-off scheduled task and waits for it: programs started over SSH have no desktop and no GPU. |
| `title-shot.cmd <exe> <disc> <data> <out.png> <frame> [keys]` | A picture of one frame of a scripted run: muted (`SDL_AUDIODRIVER=dummy`), its own data folder (`GT4R_DATA_DIR`), no window shown. |

From the other machine:

```bash
S="ssh discs@pc"   # your account on the PC
tools/windows/send.sh discs@pc
$S 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\build.ps1'
$S 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\test.ps1 -Discs'
$S 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\package.ps1'
$S 'powershell -NoProfile -ExecutionPolicy Bypass -File D:\gt4r\desktop-run.ps1 -Script D:\gt4r\title-shot.cmd -ScriptArgs "D:\gt4r\run\gt4r-windows-x64\gt4r.exe D:\gt4r\discs\gt4.iso D:\gt4r\run\data D:\gt4r\run\title.png 900"'
```
