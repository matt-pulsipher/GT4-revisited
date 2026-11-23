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
| 55 | `RNW4` | The road: surface, walls, checkpoints, start grid (below) |

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

## Road: `RNW4` (slot 55)

Code: `runway.*`. All 110 roads read (`gt4tool runway-survey`). Offsets from the magic.

```
0x0C u32 flags          0x10 u32 version (4; 0 on some courses)
0x14 f32 lap length (m) 0x18 f32 start, 0x1C f32 goal: distances along the course
0x20 f32 x3 bounds min  0x2C f32 x3 bounds max
0x38 u16 checkpoint lists   0x58 u32 -> the lists
0x3A u16 grid places        0x5C u32 -> {f32 x, y, z, heading (radians)}
0x3C u16 checkpoints        0x60 u32 -> 0x28 bytes: left, middle, right (f32 x3 each), f32 distance
0x3E u16 checkpoint lookups 0x64 u32 -> u16[]
0x44 u16 gadgets            0x70 u32 -> 0x20 bytes each
0x46 u16 vertices           0x74 u32 -> 0x10 bytes: f32 x, y, z, s16, u8, u8
0x48 u16 triangles          0x78 u32 -> 0x0C bytes: u16 x3 vertices, u8, u8 kind, u32 flags
0x4A u16 clusters           0x7C u32 -> 0x10 bytes: u16 triangles, u16 lookup count, u16 lookup
                                         first, u16 0, u32 -> u16 triangle numbers[]
0x4C u16 tree depth         0x80 u32 -> the tree
0x4E u16 wall vertices      0x84 u32 -> 0x10 bytes: f32 x, y, z, u32 tag
0x88 u32 wall lookups       0x8C u32 -> s16[]
0x52 u16 pit stops          0x90 u32 -> as grid places
0x54 u16 sections           0x9C u32 -> the sections
```

- **Triangle kinds** (US disc, all courses): 0 on 93% of the triangles (the road); 1, 2, 3, 4,
  8, 16, 17, 24, 25, 32, 33 the rest (verges, sand, grass, dirt: which is which ❓).
- **The tree** finds the triangles under a point. It is a full binary tree of the given depth
  stored one byte per node, depth first with the left subtree before the right: bits 0-5 a
  place p, bits 6-7 the axis (0 x, 1 y, 2 z). A node splits its box (the root's is the bounds)
  at min + (max - min)(p + 0.5) / 64; the leaves, left to right, are the clusters, each a list
  of triangles. Checked on every course: a triangle's centre leads to a cluster that lists it.
- **Walls** are closed chains of the wall vertices in order. A chain's first vertex has its
  length in the tag's low 16 bits, and its last vertex minus (length - 1) there. ❓ tag bit 16,
  the wall lookups.
- **The start grid**: most courses have 6 places that stand on the road; some list places that
  do not (❓ the sections name the places a layout uses).
