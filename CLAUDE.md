# GT4 Revisited: notes for Claude

- **What this is**: Gran Turismo 4 reimplemented on GT3 Revisited's engine (`engine/gt3r`, a
  submodule; never edit it in place). Read `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` first.
- **Rules** (`docs/REVERSE_ENGINEERING.md`): never commit original code in any form
  (disassembly, decompiler output, transcriptions), game files or extracted assets. Record
  facts in `docs/`, implement from them. Values come from the disc's data, never hard-coded
  copies of it.
- **Discs** (the user's own, never committed; `*.iso` is ignored): Gran Turismo 4 US at
  `../GT3-revisited/SCUS-97328 (1.01).iso` (the GT3 Revisited checkout next to this one, with
  the GT3 and GT Concept discs); Prologue (Japan) in this folder.
- **Build**: `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j 12`.
- **Test**: `GT4R_DISC="<gt4 iso>;<prologue iso>" build/gt4r_tests` (without the variable only
  the no-disc tests run). Every change: both.
- **Run the app for a check** without touching the user's settings or making sound:
  `GT4R_DATA_DIR=<scratch dir> SDL_AUDIODRIVER=dummy build/gt4r <iso> --browse <path> --keys "down,cross,wait30" --shot-frame 90 --shot out.png`,
  then look at the picture. Never drive the app with OS-level keystrokes.
- **Writing**: American spelling; say what the player sees; docs and comments state facts and
  behavior, not history. Do not name other games as the model for a feature, and do not justify
  anything on legal grounds.
- **Program analysis**: `engine/gt3r/tools/re` with `~/gt3re-venv/bin/python -I` (see
  `docs/REVERSE_ENGINEERING.md` for this program's segment addresses). Outputs go outside the
  repository.
- The repository is private for now: do not mention it in the public GT3 Revisited repository.
- **Outside sources**: the Gran Turismo modding hub (nenkai.github.io/gt-modding-hub) and the MIT
  format templates (Nenkai/GT-File-Specifications-Documentation) may be read for facts. Do not
  read or use Nenkai/PDTools or Nenkai/GTPS2ModelTool: their authors forbid AI use of those
  repositories. Never read The Cutting Room Floor. Prefer facts checked on the user's own disc.
