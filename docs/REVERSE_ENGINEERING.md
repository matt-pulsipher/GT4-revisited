# Reverse-engineering workflow

GT4 Revisited follows GT3 Revisited's rules and workflow
([engine/gt3r/docs/REVERSE_ENGINEERING.md](../engine/gt3r/docs/REVERSE_ENGINEERING.md)).

## Ground rules

1. **Never commit original code**, in any form: no disassembly, decompiler output, or code
   transcribed from either, whoever produced it.
2. Record *what* the game does (formats, algorithms, constants, behaviour) in `docs/`, then
   implement from that description.
3. Facts are fine to record: file layouts, constants, table values, observed behaviour.
4. Never commit game files or extracted assets, including test fixtures. Tests that need real
   data read it from the user's disc at test time (`GT4R_DISC`) and skip without one.

## Tools

- `gt4tool` (this repository): `info`, `ls`, `survey`, `extract`, `tex`.
- The shared engine's analysis toolkit, `engine/gt3r/tools/re/`
  ([RE_TOOLKIT.md](../engine/gt3r/docs/dev/RE_TOOLKIT.md)), reads Gran Turismo 4's program
  too: `CORE.GT4` is in the same container as `CORE.GT3`. Give `analyze.py` the packed
  `CORE.GT4` (not the disc image, where it looks for `CORE.GT3`) and the end of the code
  with `--text-end` (the automatic detection stops early on this program):

  | Disc | `--text-end` | `$gp` | BSS |
  |---|---|---|---|
  | Gran Turismo 4, SCUS-97328 v1.01 | `0x617A14` | `0x6DDDF0` | `0x6D5E00`-`0x8A215C` |
  | Gran Turismo 4 Prologue, SCPS-15055 v1.01 | `0x4FC12C` | `0x5B4970` | `0x5AC950`-`0x761F8C` |

  The program has 27,795 functions and 836 classes with run-time type names (Gran Turismo 3:
  11,392 and 486). `build_map.py` matches it against the earlier games' programs; a chain
  through the Gran Turismo Concept builds and Prologue finds 70% of the physics routines
  GT3 Revisited's specifications cite (about a third of those with changed arithmetic or
  constants).
- `gt4r_diff <disc>` (`tools/gt4diff`): runs routines of the user's own `CORE.GT4` in the
  shared engine's R5900 interpreter (`engine/gt3r/tools/diffharness`) and compares what they
  make with ours ([specs/physics.md](specs/physics.md) §9). The routines are named by address
  and run from the disc; nothing of the program is kept.
- Outputs go to a folder outside the repository and are never committed.
