# Courses

Code: `src/gt4r/formats/course/`. `crs/<name>` (packed, 4-12 MB) is one course layout; the US
disc has 110, with their driving-line models in `crs/bestline/` and a `crs/<name>.ad` file each.
All 1,145 model sets of the 238 files under `crs/` decode.

## Container

Gran Turismo 3's table of section offsets
([engine/gt3r/docs/formats/course.md](../../engine/gt3r/docs/formats/course.md)) grown from 48 to
64 u32 slots, with the same roles in the slots both have. Slot 0 is 0 and slot 1 starts at
0x100. A section runs to the next section's offset.

| Slot | Tag | Contents |
|---|---|---|
| 1 | `MDLS` | The scenery of the race view |
| 2, 3, 4 | `Vls0` | Its visibility lists: the race view, the mirror, a two-player half |
| 5 | `MDLS` | The scenery drawn into the cars' reflection picture (6-8: its lists) |
| 9 / 13 | `MDLS` | The scenery mirrored below a wet road, and its mask (10 / 14: lists) |
| 17 | `MDLS` | Drawn after the cars (18: lists) |
| 21 / 22 | `MDLS` | Sky dome / distant backdrop |
| 23 / 24 | `MDLS` | The reflection picture's sky / the mirror's sky |
| 26 | `Tex1` | Tire smoke and dust |
| 27 | `MDLS` | The driving line of the license tests |
| 29 | `GTFX` | Lamp glows |
| 30 | `BLBd` | Billboards |
| 32 | `Env0` | Environment: far clip, fog, lights, sun |
| 33 | `GTCM` | The course map of the race display |
| 35 | `CRSS` | Ambience emitters |
| 37-40, 42, 49 | | Flare shape and `Tex1` sets (flares, particles) |
| 44-46, 48 | `REP4` | Replay cameras; the start's cameras; the preview's |
| 47 | `GDGT` | Cones and boards |
| 50 | `MDLS` | Sky reflection |
| 52, 53 | `Mot1` | Camera motions |
| 55 | `RNW4` | The road: surface, walls, checkpoints, start grid ❓ |

`Vls0` and `CRSS` read with the shared engine's parsers as they are.

## Scenery

The model sets are `MDLS` ([model.md](model.md)); positions are packed at a scale of 0.002 m.
Most shapes are pre-lit (vertex colours). The stored colours are darker than Gran Turismo 3's
(mean about 0.48 of full scale on Tsukuba): the scenery is drawn with them scaled by 2.0,
`Env0` +0x38, which is 2.0 on every course seen. 🔎 This factor has not yet been compared with
the original running.

Animated objects (flags, crowds) are driven by each set's script through registers; a still
scene reads every register as 0.

## Env0

The first fields that match Gran Turismo 3's: +0x10 flags, +0x20 far clip (5000 on most
courses), +0x60 fog colour, +0x70 / +0x74 fog distance and minimum visibility. +0x24..+0x44
are scale factors this format adds (+0x38: the colour scale above). ❓ the rest.
