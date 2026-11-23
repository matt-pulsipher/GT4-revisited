# Models: `MDLS`

Code: `src/gt4r/formats/model/mdls.*`. Coverage on the US disc (`gt4tool model-survey`):
wheels 2,369 of 2,369 model sets, cars 8,348 of 8,366, tires, wings and prize objects all;
the drivers and pit crews (skinned sets) do not decode yet.

`MDLS` is the successor of Gran Turismo 3's `GTM1`
([engine/gt3r/docs/formats/car_model.md](../../engine/gt3r/docs/formats/car_model.md)): the
same draw programs with more commands, the same shape (chunk) tables, 0x50-byte materials and
`Tex1` sets, under a new header and with packed vertex data. The reader rewrites a set as the
`GTM1` block that says the same and hands it to the shared decoder.

All offsets are from the `MDLS` magic, also inside a car or course file.

## Header

```
0x00 "MDLS"
0x04 u32 offset of the relocation list, 0x08 u32 its size (after the data; not needed: the
         offsets on the disc are already from the block's start)
0x10 u32 block size
0x15 u8  bit 0: the set carries a script instance
0x16 u16 models          0x38 u32 -> model records, 0x28 bytes each
0x18 u16 shapes          0x3C u32 -> u32 shape offsets[]
0x1A u16 materials       0x40 u32 -> materials, 0x50 bytes each (as GTM1's)
0x1C u16 texture sets per list
0x1E u16 lists           0x44 u32 -> u32 lists[] -> u32 Tex1 offsets[sets] (0: none)
0x20.. u16 x6            the script's register and method counts; 0x4C / 0x50 / 0x54 their tables
0x2A u16 symbols         0x58 u32 -> {u32 name offset, u32 value}
0x2C u8  material variations   0x60 u32 -> u32 offsets[] -> a whole material array each
0x2E u16 bind matrices   0x48 u32 -> their map (skinned sets)
0x5C u32 script code
0x68 u32 extension records: u8 size in dwords, u24 id, data (0x800001: the set's name)
```

## Model record (0x28 bytes)

```
+0x01 u8  bound points (0 or 8)     +0x04 u32 -> the points (a box's corners, 12 bytes each)
+0x08 f32 radius                    +0x0C f32 x3 centre
+0x18 u32 -> draw program
+0x1C / +0x20 / +0x24 s32 script entry points (-1: none)
```

## Draw programs

One byte of command, then its operands, unaligned. Commands 0x00-0x37 are GTM1's with the
same operands. The game's dispatcher has 0x5B commands; the ones GTM1 lacks, by the size its
handler steps over:

| Command | Operands | What it does |
|---|---|---|
| 0x38-0x3F, 0x47 | 0 | nothing |
| 0x40 | 0 | switches the face that is culled |
| 0x41, 0x42 | 3 | nothing (skipped) |
| 0x43 | 0 | runs the model's script, once per draw |
| 0x44, 0x45, 0x46, 0x48, 0x49, 0x4A, 0x4B | u16 register | multiply matrix, translate, scale, rotate, rotate X / Y / Z by a script register (the forms of 0x10, 0x11, 0x12, 0x13-0x16) |
| 0x4C | u16 register | the morph weight of 0x35 from a register |
| 0x4D | u16 register, u8 n, s16 rel[n] | switch on a register's value; out of range: no branch (as 0x06 on a callback's value) |
| 0x4E / 0x4F | 0 | cull face 2 / 1 |
| 0x50 / 0x51 | u8 / u16 slot | draws with the texture set the game bound to that slot (16 slots) |
| 0x52 / 0x53 | 4 floats | colour scale / colour offset |
| 0x54 / 0x55 | 4 x u16 | the same from registers |
| 0x56 / 0x57 | 0 | texture-coordinate modes |
| 0x58 | 0 | two render states off |
| 0x59 / 0x5A | u8 / u16 id | calls the model's callback with the id (a car: 15-18 draw tires 1-4, 36-39 wheels 1-4) |

Jumps (0x08, 0x09) are from their operand's address; a switch table's entries each from their
own address, as in GTM1.

A still model is drawn with these replaced by no-ops of the same size (0x4D by a jump to its
first branch), so no callback runs (a car body draws without wheels) and every register reads 0.

## Shapes

GTM1's chunk: `u32 0, f32 scale, u16 flags, u16 packets, u16 vertices, u16 triangles`, then
per packet `u32 offset (from the shape), u16 quadwords, u16 texture and material`. Flags bits
0-4 are the kind (as GTM1's); bit 7 marks packed data.

A packet is a VIF stream. VU memory holds 0x40 vectors each of positions (from address 0x00),
texture coordinates (0x40, with the strip table in their fourth lane) and normals or colours
(0x80), and the GIF tag at 0xC0; a packet with two key frames splits each range in half.

| Data | Format | Value |
|---|---|---|
| positions | V3-32 floats, or V3-16 / V3-8 | integer x the shape's scale (+0x04) |
| normals | V3-32, or V3-8 | integer / 127 |
| texture coordinates | V2-32, or V2-16 / V2-8 | integer / 4096 |
| reflection normals (in the coordinates' place) | V3-32, or V3-8 | integer / 127 |
| colours | V4-8 | as GTM1 (0x80 = 1.0) |

An array may come as several UNPACKs of different widths, each continuing at the address the
last one stopped (`V2-16 @0x40` then `V2-8 @0x51`). A packed run can be offsets from the VIF
row registers: `STROW` sets them, `STMOD 1` adds them to every vector, `STMOD 2` adds and keeps
the sum.

Scales seen: 0.002 (courses), 2 / 32767 (wheels), 5 / 32767 and 6 / 32767 (car bodies).

## Texture sets

A draw program selects a set of the current list with 0x2B / 0x2C. Wheel files (`wheel/`):
sets 0 and 1 are the rim band, 2 an empty set, 3 onwards face pictures, still and turning by
turns (the file's symbols `NRM` and `ROT` number them 0-3); the face is drawn through slot 2 of
command 0x50, which the game fills by the wheel's speed.

## Cars: `CAR4`

Code: `car_model.*`. `car/menu/<model>` is a car's showroom model and `car/lod/<model>` its race
model; `<model>` comes from the database ([database.md](database.md)). Offsets from the file's
start:

```
0x00 "CAR4"                      0x08 u32 file size
0x10 u32 -> car info             lamps, exhausts, camera mounts, the driver's place
0x14 u32 -> collision shape      u8 count, then count x {f32 x3 point, f32 x3 normal}
0x18 u32 -> body MDLS            0x1C u32 -> body Pat0 (0: the file <model>.pat next to it)
0x20 u32 -> wheel MDLS           0x24 u32 -> wheel Pat0
0x28 u32 -> wing MDLS
0x2C, 0x30 u32 -> front / rear tire MDLS
0x34 u32 -> a driver model built into the car
```

- **Body**: model 0 the body, 1 its light shadow, 2 its contact shadow (flat, at y = 0: the
  ground), 3 the exhaust flame. Metres, y up, the front at +z.
- **Paint** (`Pat0`): `+0x10 s16 paints, +0x12 s16 patches per paint`, then `u32 offsets[paints
  x patches]`, each to `u32 target (from the body set's start), u32 bytes`, the bytes. A paint is
  put on by copying its patches over the set before it is read: they rewrite material colours
  and texture palettes.
- **Wheels and tires** are in units of their own. A tire is one unit wide from its outer face
  at x = 0 and about 1.3 units in radius; the rim is 0.8 units in radius in the same scale.
  The wheel set has four models: front left, front right, rear left, rear right; a set's
  models are left wheels (outer face towards -x). In the game the car's callback draws them
  (draw commands 0x59 with ids 15-18 and 36-39).
- **Placement** used here for a still car, from the database: across by the tire's radius
  (`CHASSIS` +0x16 / +0x18, the tires' diameters) over the tire model's, along by the tire's
  width (`TIRESIZE` +3 x 5 mm); the outer faces at half of `CHASSIS` +0x12 / +0x14 either side;
  the axles at +- half the wheelbase (`CHASSIS` +0x04); the tires standing on y = 0.
  🔎 Fits the wheel arches of the cars looked at; the game's own rule has not been read.
