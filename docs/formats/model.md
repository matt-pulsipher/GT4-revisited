# Models: `MDLS`

Code: `src/gt4r/formats/model/mdls.*`. Coverage on the US disc (`gt4tool model-survey`):
wheels 2,369 of 2,369 model sets, cars 8,348 of 8,366, tires, wings and prize objects all;
skinned sets (the GT Auto mechanic, the drivers, the pit crews) decode in their stored pose and
the menus pose them by their motions (below); the race does not move its drivers yet.

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
| 0x59 / 0x5A | u8 / u16 id | calls the model's callback with the id; what it returns is not used (a car's ids: [the car's callback](#the-cars-callback)) |

Jumps (0x08, 0x09) are from their operand's address; a switch table's entries each from their
own address, as in GTM1.

A still model is drawn with these replaced by no-ops of the same size (0x4D by a jump to its
first branch), so no callback runs (a car body draws without wheels) and every register reads 0.

### The switches (0x06, 0x07) as the game runs them

- **0x06** `u16 id, u8 n, s16 rel[n]`: asks the callback for the id's value. A value below n
  jumps through the table; any other value goes on at the byte after the table, where the
  programs put a jump to the end of the construct. A set without a callback reads 0. Callbacks
  that draw or set a matrix (a car's tires, its wheel frames) are called this way too, for
  the value they return.
- **0x07** `f32 x3 centre, f32 size, u8 n, s16 rel[n]` picks a level of detail. The value is
  `v = K x size / max(w, 0.0001)`, w the centre's depth in view space; K is set per view
  (program 0x456918): the height on screen, in pixels, of one unit at depth 1, x 10 / 7, x a
  factor the caller passes ❓. So v is the size's height on screen in pixels x 10 / 7. The
  level is the callback's choice (its second entry point, given v); without a callback, with
  T = 200 (the program's default, 0x6238A4): v > T: 0, v > T / 2: 1, v > T / 4: 2, else 3. The
  level is clamped to n - 1. With the switch turned off (0x6238B4 = 0) v is 0: the last level.

A model set with more than one texture list (header +0x1E) is drawn with list 0 unless the
game asks otherwise: a car's tires name the list (below); other sets then take it from the
model record's size on screen the same way against a threshold of 400 (0x6238AC).

## Shapes

GTM1's chunk: `u32 0, f32 scale, u16 flags, u16 packets, u16 vertices, u16 triangles`, then
per packet `u32 offset (from the shape), u16 quadwords, u16 texture and material`. Flags bits
0-4 are the kind (as GTM1's); bit 7 marks packed data.

A packet is a VIF stream. VU memory holds 0x40 vectors each of positions (from address 0x00),
texture coordinates (0x40, with the strip table in their fourth lane) and normals or colours
(0x80), and the GIF tag at 0xC0; a packet with two key frames splits each range in half.

| Data | Format | Value |
|---|---|---|
| positions | V3-32 floats (a shape without the packed mark); V3-16 / V3-8, or V3-32 integers, in a packed shape | integer x the shape's scale (+0x04) |
| normals | V3-32, or V3-8 | integer / 127 |
| texture coordinates | V2-32, or V2-16 / V2-8 | integer / 4096 |
| reflection normals (in the coordinates' place) | V3-32, or V3-8 | integer / 127 |
| colours | V4-8 | as GTM1 (0x80 = 1.0) |

An array may come as several UNPACKs of different widths, each continuing at the address the
last one stopped (`V2-16 @0x40` then `V2-8 @0x51`). A packed run can be offsets from the VIF
row registers: `STROW` sets them, `STMOD 1` adds them to every vector, `STMOD 2` adds and keeps
the sum.

A packed shape (flags bit 7, a scale above 0) whose positions do not fit 16 bits has them as
32-bit **integers**, the first vector of a run or all of it: a course's backdrop (Tsukuba's: 105
of its 244 packets; -228,940 x 0.002 = -457.9 m) and 251 packets of its scenery, among them the
power lines. A shape without the mark (scale 0) has floats there. Checked on nine courses and
120 cars: every 32-bit position run of a packed shape reads as whole numbers that land on the
course, none as a float.

Scales seen: 0.002 (courses), 2 / 32767 (wheels), 5 / 32767 and 6 / 32767 (car bodies).

## Texture sets

A draw program selects a set of the current list with 0x2B / 0x2C, or draws with one of 16
outside slots (0x50 / 0x51) that the game fills. A car in a race uses two of them (checked on
all 807 `car/lod` bodies: only slots 0 and 2 appear): slot 0, the picture of the surroundings
the body and the rim reflect (the race binds it per view 🔎 program 0x4568C0, 0x451A90), and
slot 2, the wheel's face (below). The car's draw clears slots 1-3 when it ends.

## Skinned sets

Code: `src/gt4r/formats/model/mdls_skin.*` (the bones and vertex blocks; posing), the menus'
motions in `src/gt4r/menu/model_motion.*`. Checked on GT Auto's `pitmanFix.mdl` (20 bones, 49
packets), `carwash_cloth.mdl` (4 bones), `man_shadow.mdl` and the 20 skinned packets of the
race's driver sets.

- **Bones** (header +0x2E count, +0x48 table): 0x50 bytes each, sorted by name (the program
  searches them by name, 0x454970): +0x00 the inverse bind matrix (16 floats, the program's
  row-vector form: model space to the bone's space in the stored pose), +0x40 u32 the bone's
  palette slot (what the packets' bone bytes name), +0x44 u32 its name's offset.
- **Symbols** (+0x2A count, +0x58 table of {u32 name offset, u32 value}): `SKINNING` = the model
  drawn with the bones; another name (`careerPos` on the oil change's cart) draws model `value`
  at the motion node of that name.
- **Shape flags** (+0x08): 0x8000 a vertex block follows each packet's VIF data (outside the
  packet's quadword count; the next packet starts at the next 16 bytes); 0x2000 the packed block,
  else the float one (`man_shadow.mdl`); 0x4000 ❓ the EE-built unpacks' mask (0x49D448). The
  packets themselves hold the GIF tag, the strips and the texture coordinates (or, for kind 2,
  the reflection pass's colours).
- **The packed block** (0x49DA60 → 0x49DCB0 / 0x49DB08): f32 position scale (1.00003e-4), u8
  vertices (the strips' total), u8 the normals' place (0x80 lit, 0x40 the reflection pass's UV
  slot, 0 none); then groups until the count: u8 count (| 0x80: another group follows), u8 4, u8
  bone[4]; per vertex s16 position[3] x scale, s8 normal[3] / 127 (when they have a place), s8
  weight[4] / 127 (summing to 127): 13 bytes, 10 without normals.
- **The float block**: u16 vertices, u16 the normals' place; groups: u16 count (| 0x8000), u16 4,
  u8 bone[4]; per vertex f32 position[3], normal[3], weight[4].
- The program blends a group's vertices on the EE (VU0 micro-routines: ❓ not readable) with the
  four bones' palette matrices: position = sum of weight x (position x palette[bone]), the normal
  the same way; without a palette the set draws as stored. `to_gtm1` writes a skinned packet's
  stored pose as plain positions and normals, and `pose()` moves the decoded model's vertices
  (found again by their stored position and normal) by a palette.
- **Motions** (`Mot1`, docs/formats/camera.md, and the scene program 0x42B440): ops 2 push, 3 pop,
  4 a node (its name), 5 translate, 6 / 7 / 8 rotate about X / Y / Z (radians), 9 scale, 0x13
  rotate X then Y then Z (Euler to quaternion 0x48B888 to matrix 0x48A970), 0x0A to 0x10 as the
  camera motions, 0x11 / 0x12 two- and three-name values; each multiplies the current matrix
  from the left in the program's row form, a node takes the current matrix (a node after a node
  without a push is its child). A clip's +0x04 is its loop start. A bone takes the node of its
  name: palette[slot] = inverse bind x the node's matrix (0x42A660); the first bone's place is
  taken out of all and put back on the draw (0x4289E0, a precision measure).

## Cars: `CAR4`

Code: `car_model.*` (the file, a still car), `src/gt4r/race/car_visual.*` (a car in the race
view), `src/gt4r/race/car_spec.*` (its sizes from the database), `gt4tool race <disc> car <model>`.
`car/menu/<model>` is a car's showroom model and `car/lod/<model>` its race
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

- **Frame** of everything in the file: metres, y up, left at -x, the front at **-z** (checked
  on the pickup `dodg0009`: hood, cab, bed from -z to +z; on the info block's lamps: the
  headlamps at z < 0; the wing mount at z > 0). The origin is the middle of the wheelbase;
  y = 0 is the ride height above the road 🔎 (the body's underside, as the shared engine's
  cars: the R34's ride height of 0.130 puts its hubs at y = 0.197).
- **Body** (four models on all 807 race cars): model 0 the car (three levels of detail on
  nearly all: one draw program, a 0x07 switch at its start); model 1 a plain copy of the body,
  the shadow the light casts; model 2 the shadow under the car (flat, at y = 0; like model 1 an
  untextured black shape whose vertex alpha is its strength: R34 22 to 153 of 255);
  model 3 the light of the headlamps on the road (flat, at y = 0, ahead of the car).
  [How a race draws them](#a-car-in-a-race).
- **Paint** (`Pat0`): `+0x10 s16 paints, +0x12 s16 patches per paint`, then `u32 offsets[paints
  x patches]`, each to `u32 target (from the body set's start), u32 bytes`, the bytes. A paint is
  put on by copying its patches over the set before it is read: they rewrite material colours
  and texture palettes.
- **Wheels and tires** are in units of the **rim's radius** (the game draws both scaled by it,
  below). The tire sets hold one tire shape for every car (1,515 of the 1,614 sets measure the
  same: one unit wide from the outer face at x = 0, radius 0.86 to 1.32), which the game
  reshapes for the car's tire size when it loads the car. The wheel set has four models: front
  left, front right, rear left, rear right, each modelled as a left wheel (outer face towards
  -x). Its symbol table (header +0x58) names the models by number: `NRM` 0 and 2; 1 and 3 are
  `ROT` on 792 cars (the right wheel is the model turned half a turn) or `SYM` on 15 (the right
  wheel is the mirror image: after the half turn the game also mirrors it front to back and
  swaps the culled face). The wheel set's extension record 0x800000 is a float d for the
  test of the eye being behind the wheel (0 on 768 cars, up to 0.31; -1 on nine: no test).
  The wheel set's texture sets (9 on 715 cars; 15 on 72 ❓): 0, 1 and 2 the rim band of the
  three levels of detail, then a pair of face pictures per level, still and turning (3 / 4,
  5 / 6, 7 / 8). A level's draw program asks the set's callback with the still set's number
  (0x06 with id 3, 5 or 7, one case; checked on 786 sets) and draws the face through outside
  slot 2.
- **Wing**: no race car file carries one (+0x28 is 0 on all 807); fitted wings are the files
  `wing/lod/<name>`, `wing/menu/<name>`.
- **Driver**: +0x34, on 801 of the 807 race cars.

### Car info (+0x10)

Offsets inside the block are from the block's start; the offsets it holds are from the
**file's** start. Checked on all 807 `car/lod` files (counts in brackets).

```
+0x00 u8  flags (0 on 627; 4 on 158; ...) ❓   +0x02 u8 (0x20 on 787) ❓   +0x03 u8 (0 / 1) ❓
+0x04 u32 3
+0x08 u32 x4 -> four 8-byte records, all zero on every car ❓
+0x18 u32 count, u32 -> lamps that are lit whenever the lights are on: the headlamps (2 to 8;
                         a formula car's single rear light is here too), 0x70 bytes each
+0x20 u32 count, u32 -> tail lamps (0x70 bytes)
+0x28 u32 count, u32 -> brake lamps (0x70 bytes)
+0x30 u32 count, u32 -> 0x10-byte records (one per car): f32 x3 ❓ (R34: 0, 0, 1.329)
+0x38 u32 count, u32 -> exhaust ends (1 or 2; 0x18 bytes): f32 x3 position, f32 x3 direction
+0x40 u32 count, u32 -> camera mounts (25 on every car; 0x20 bytes)
+0x48 u32 count, u32 -> suspension links (six open-wheel cars; 0x38 bytes)
+0x50 u32 -> the parts block (0x100 bytes)      +0x54 u32 -> the wing list
+0x58 f32 x2  the height the top of a front / rear tire may reach (`CHASSIS` +0x16 / +0x18
              hold the same in mm: R34 0.6106 / 0.6549 against 610 / 654)
```

- **Lamp** (0x70 bytes): `+0x00` a 4 x 4 matrix (rows: the lamp's x, y, z axes and its
  position; the z row is (about 0, 0, 1) on headlamps and (0, 0, -1) on rear lamps: it points
  into the car, against the way the lamp shines), `+0x40 f32 x2` half sizes a, b (1, 1 on
  most), `+0x48 f32 x3` the point tested for being in view (on or a little outside the lens),
  `+0x54 f32` 1, `+0x58 f32 x2` ❓ (headlamps: 0.83, 0.5 or 0.67, 1; rear lamps 0, 0),
  `+0x60 u32` kind ❓ (0x10 on most headlamps, 4 on rear lamps), `+0x64 u16` flags: bit 0 a
  brake lamp fades with the brake's four-frame ramp (else it is lit only while the brake is
  down), bit 1 the lamp is on the stock spoiler (not lit with a wing fitted or the spoiler
  taken off), `+0x66 u16` 0, or the number (1, 2) of a flashing pattern (ten lamps on the
  disc), `+0x68 f32` its phase. R34: four headlamps at (-+0.46, 0.52, -2.07) and (-+0.60,
  0.53, -2.00), four tail lamps at z = 2.19 / 2.24, the outer two again as brake lamps.
- **Camera mount** (0x20 bytes): `f32 x3` position, `f32 x3` pitch, yaw, roll in degrees,
  `f32` zoom (1 / tan of half the view angle: 1.732, 2.05, 2.414), `u32` flags (0: unused).
  The position is in the simulation's frame: x right, y **down**, z forward (R34 mount 1:
  0, -1.7, -6.0, pitch 8: 6 m behind and 1.7 m above the origin). The same 25 places on
  every car, e.g. on the R34: 0 (0, -1.01, -0.2) in the cabin's middle, 1 the chase view, 5
  (0, -0.6, 1.58) over the front, 6 (0, -1.40, -0.23) on the roof, 9 at the back looking
  back (yaw 180), 3 / 4 and 10 / 11 looking back along the sides and sideways, 14-19 outside
  views. 🔎 Which driving view takes which mount has not been read.
- **Suspension link** (0x38 bytes): three points of 0x10 bytes, `f32 x3, s16 kind, s16 index`:
  kind 0 a point of the body, 1 a point in wheel `index`'s frame (the frame callback 11 + index
  sets), 2 a point in link `index`'s frame. The game builds a matrix per link each frame (at
  most 16): origin at the first point, x axis towards the second, the third fixing the roll;
  the body's program draws the link's part under it (callback 20 + link).
- **Parts block** (0x100 bytes): `+0xD0 f32 x3` where a fitted wing goes, `+0xDC f32` its
  width (the wing model is drawn scaled by half of it); the rest ❓ (the driver's place is
  expected here: R34 +0x00 = (0.382, 0.735, -0.241), on the right-hand-drive car's right).
- **Wing list**: `u32 count` (8 on every car), then count x two 8-character names
  (`pdi_0003`...), files of `wing/`.

### The car's callback

A car's body is drawn with a callback object the draw program calls by number (0x06, 0x59).
Program: `GT4Model::CarModel::Callback` (0x453698, a 48-way switch). "Value" is what a 0x06
switch reads; ids not listed do nothing and read 0. Uses counted on the 807 `car/lod` bodies.

| Id | Cars | What it does or returns |
|---|---|---|
| 0 | 789 (0x06, 2 cases) | **Brake lamps**: 1 while the brake is on (the simulation's brake value above 0), else 0. The program draws the lit lenses in case 1 (case 0: the unlit ones, or nothing) |
| 1-4 | 0x59 on a few rally cars | Sets the morph weight (command 0x35's) to a value per wheel, clamped to 0..1: 0.5 + 0.5 x a per-wheel value of the simulation 🔎 (the mud flaps) |
| 5 | 0 | Morph weight from the steering value (the script value `steering`) |
| 6, 42-45 | 6: `audi0013`...; 42-45: `mits0012` | Morph weights from up to four values of the car's moving aerodynamic parts ❓ (6 is the first of them) |
| 7 | nearly all (0x06 with 2, 3 or 4 cases, several per body; also in model 3) | **Lamp level**: 0 lights off, 1 on (the race sets 0 or 1 at its start, by the course). Most three-case switches end in the construct's end, as the default does; 366 switches on the disc have a third case of their own ❓ what sets level 2. Retractable headlamps are the two cases' shapes (`mazd0039`) |
| 8 | 14 (one case) | Turns about z by the steering wheel's angle (radians in, x 57.2958), then reads 0: the steering wheel of an open car follows in the case |
| 9 | 9 (one case) | Multiplies by a matrix the driver's model gives 🔎 (a part that moves with the driver), reads 0 |
| 11-14 | all (2 cases) | Sets the frame of wheel id - 11 (below) and the brake disc's glow. Reads 1 when the eye is behind the wheel's face (the program then draws the tire alone), else 0 (brake parts, tire, wheel) |
| 15-18 | all (0x06 one case, or 0x59) | Draws the tire of wheel id - 15 in the frame set by 11-14 (below). Reads 0 when the tire was drawn (the program then calls 36-39), 1 when it was not or tires are off |
| 20-35 | six open-wheel cars (one case) | Multiplies by suspension link id - 20's matrix, reads 0 |
| 36-39 | all | Draws wheel id - 36: model id - 36 of the wheel set (the fitted wheel's set when one is fitted) |
| 46 | 785 | Binds the wheel set's last texture set (the far level's turning face) to outside slot 2: the body's far level draws its wheels as shapes of its own with that picture (`0x50 2`) |
| 47 | 588 (3 cases) | **Stock spoiler / wing**: 0 as built, 1 a wing is fitted (the game draws the wing set before the body, at the parts block's place), 2 the spoiler taken off |

The second entry point (level of detail, command 0x07): a level forced by the race when it is
not negative; else, with T the car's threshold (200, set for every car at the race's start):
v > T: 0, v > 0.33 T: 1, else 2. A wheel set's callback: the forced level, else the default
rule (four levels).

**Brake lamps** are a switch (id 0) between two sets of shapes, not a texture swap: all but
18 bodies have it (27 bodies switch a lens on by a script's colour scale instead or as well:
[below](#what-a-cars-script-reads)). The model also keeps a brake ramp, rising to 1 over four
frames (0.25 a frame) while the brake is on and falling the same way: the lamp glows use it,
scripts can read it (`taillamp`), and the body's draw hands it to the renderer as the third
of three brightness values (the second and fourth are 1; program 0x49B478) 🔎 taken to be
the lamp materials' light, as the shared engine's lamp materials
([car_model.md](../../engine/gt3r/docs/formats/car_model.md)); which materials take it has
not been read.

### Wheels as the game places them

Per wheel i (0 front left, 1 front right, 2 rear left, 3 rear right; side = i & 1) the model
keeps: hub position, spin, steering and camber angles (degrees), spin speed, tire width W, tire
radius R, rim radius r. Callback 11 + i then multiplies the body's matrix by, in this order:

1. translate(hub)
2. rotate about y by the steering angle
3. rotate about z by the camber, negated on the right
4. translate(-W / 2, 0, 0) on the left, (+W / 2, 0, 0) on the right: from the hub's middle to
   the outer face

The body's program draws the brake parts here (unscaled shapes of the body set, in metres;
their colour scale's alpha is the disc's glow, from the brake's heat 🔎). Callback 15 + i goes on:

5. rotate about x by the spin angle
6. scale by r, all three axes
7. on the right: rotate about y by 180 degrees
8. draw tire model (side) of the front or rear tire set, with texture list 0 (still) or 1
   (turning)
9. `SYM` wheel models only: scale (1, 1, -1)

and callback 36 + i draws the wheel model in the same matrix (with the culled face swapped
for `SYM`). Command 0x58 after each ends the scaling.

Callback 11 + i's value: with the eye taken into the frame after step 4 (x negated on the
right, so +x runs inwards from the outer face), 1 when x > -d r and the eye's distance from
the axle is at most R (1 + x / (d r)), d the wheel set's extension float (d = 0: whenever
x > 0; d < 0: never), else 0.

Where the values come from:

- **Tire size** (program 0x34BA30, per axle, from the tire's `TIRESIZE` row): W = width byte
  x 5 mm; rim inches = the rim byte, / 10 when it is above 60; rim radius = inches x 0.0127 m;
  sidewall H = W x aspect byte x 0.05; **R = rim radius + H**. A fitted wheel of another size
  changes the rim radius by its inches and the sidewall by the opposite (rim radius at least
  0.0254, sidewall at least 0.0127); R stays. **r = R - H**.
- **Tire shape**: when the car loads, each axle's tire set is reshaped in place (program
  0x458A30): with r_in and r_out the smallest and largest distance of its vertices from the
  axle and x_max their largest |x|, every vertex gets x x (W / r) / x_max and its distance d
  from the axle becomes 1 + (d - r_in) x (H / r) / (r_out - r_in). Scaled by r the tire is W
  wide, r inside and R outside.
- **Hub**: x from the simulation's wheel (at rest -+ the half track), z = -+ half the
  wheelbase (front at -z), y = (R - q + x_s) - x', x' the suspension's coordinate and x_s its
  value at rest (the shared engine's suspension block,
  [physics.md](../../engine/gt3r/docs/specs/physics.md)): **y = R - q at rest**, q the ride
  height, so the tire stands q below the body's underside, on the road. y is then limited to
  (info +0x58 / +0x5C) - R: the tire does not go through the arch. From the database (as the
  simulation reads it 🔎 not run against the original): wheelbase `CHASSIS` +0x04; half
  track = (tread - 2 x offset) / 2, tread `RACINGMODIFY` +0x02 / +0x04 (u16 mm), offset
  +0x13 / +0x14 (s8 mm); ride height `SUSPENSION` +0x04 front, +0x0A rear (u16 mm). R34:
  treads 1480 / 1490, no offset, ride height 130, 245/40R18 (R 0.327, r 0.229): hubs at
  (-+0.740, 0.197, -1.3325) and (-+0.745, 0.197, 1.3325), free to rise to y = 0.284 / 0.328.
  `CHASSIS` +0x12 / +0x14 is the width across the tires the half track is cut down to on
  some tire kinds, +0x16 / +0x18 the arch limits above; neither is a tire's size.
- **Angles**: spin = -(the wheel's angle); steering and camber the simulation's (camber from
  the suspension's travel). All are blended between the last two simulation steps by the
  frame's position between them.
- **Body**: translate(position), rotate y, x, z by the body's angles, as the car's matrix.

A still car (the showroom's rule, `GT4Model::ShowRoomCarParamSponsor`): the same with x = -+
half track, y = R - q, z = -+ wheelbase / 2, no angles.

### The turning-wheel pictures

The model keeps a spin speed per wheel: s = (the wheel's turn in one simulation step, radians)
x the course environment's wheel-blur factor (`Env0` +0xEA0 🔎 as the shared engine's) x
57.2958. In the views drawn with blur kind 2 or more it is also multiplied by exp(-20 x d), d
the distance the hub moved on screen since the last draw (in units of the half screen).

- **Tire**: texture list 1 instead of 0 when |s| > 5 (one step: still or turning).
- **Wheel face**: the wheel set's callback gets the still set's number n from the program and
  binds to slot 2: set n with blur kind 0; set n + 1 instead when |s| > 5 with kind 1 (the
  prepared turning picture, one level); with kind 2 and above a picture the game makes by
  smearing set n round the axle by s (program 0x453BB0 🔎 not read). The race passes the kind
  per view ❓.

### A car in a race

`RaceCarModel` (a `VehicleModel`) holds a `GT4Model::CarModel` and reads the car through a
`RaceCarParamSponsor` (the showroom and replays have their own). Each frame it copies the
brake state, the per-wheel values and the lamp level into the model; drawing is:

- **Body**: the wing (callback 47's state 1), then model 0 with the callback above. The body's
  level of detail is the 0x07 switch at the program's start (size 3 on the R34). `car/lod` is
  the race's file, `car/menu` the showroom's.
- **Shadows**, in a frame of their own (a position and angles the sponsor gives: the road
  under the car), lifted 0.02 m, with a depth bias:
  - model 1 flattened onto y = 0 along the light: x += lx x y, z += lz x y, y = 0, (lx, lz)
    the race's light vector taken into that frame; colour offset = the race's shadow colour,
    alpha = shadow alpha x a second factor;
  - model 2 (the shape under the car) with the same colour and the shadow alpha;
  - model 3 when the course's headlamp-pool brightness is above 0 (`Env0` +0x58 🔎), with
    that as its alpha.
  ❓ The light vector and shadow colour are globals of the race (0x8465A0, 0x8465B0); their
  source in the course has not been read. The race view here takes the shared engine's fields
  of `Env0` for them (+0xA60 colour, +0xA6C alpha, +0xA70 / +0xA74 / +0xA78 direction and
  length) and for model 1's second factor the lit shade factor +0xAB0 over the picture's scale
  +0x38 (2.0 / 2.0 on Tsukuba: taken at face value the shadow comes out solid black) 🔎 by eye.
- (The race view here: the lamp lists go to the shared engine's car lamp glows with the lamp's
  first axis, its third axis as the viewing axis, its place, half sizes and test point; the
  glow's color white for the first list and red for the other two 🔎 by eye, the record's own
  color not having been found, at the strengths below.)
- **Lamp glows** (when the lamp level is not 0): the info block's three lamp lists, the first
  at the course's headlamp brightness (or a flashing pattern's), the brake list at the brake
  brightness x the ramp (flag bit 0) or while braking, the tail list at the tail brightness
  without glare (🔎 `Env0` +0x1794 / +0x1798 / +0x179C).

### What a car's script reads

31 of the 807 race bodies carry a script (header +0x5C; the body's program runs it with 0x43
before the shapes that use its registers). The header's table at +0x54 names the set's
registers (`u32 name, u16 count, u16 first register`, sorted by name), the table at +0x4C the
values it wants from the game, by name (`u32 name, u16 count, u16 index`). A car offers eleven
(program table 0x6AAB70); the script reads value i of that table with its opcode 0x12 and the
operand 0x8000 + i.

| Name | Value | Bodies using it |
|---|---|---|
| `brake` | the simulation's brake value (float, 0 = off) | 27 |
| `taillamp` | the brake ramp, 0..1 over four frames | 0 |
| `timezone` | the lamp level (int: callback 7's value) | 0 |
| `time` | a counter of the model (int) ❓ its step | 4 |
| `engineRPM` | engine speed | 2 (`benz0001`, `dmlr0001`) |
| `pedalLeft`, `pedalRight` | from the driver's model | 2 (`dmlr0001`, `nike0001`) |
| `steering` | the steering value (callback 5's morph weight) | 0 |
| `steering_rot` | the steering wheel's angle, radians (callback 8's) | 0 |
| `dirtiness` | the car's dirt (the body's draw also sets a render value to 1 - it 🔎) | 0 |
| `wing` | the first moving-aero value (callback 6's) | 0 |

- **Brake lamps by script** (27 bodies: `ac__0014`, `bmw_0002`, ...): registers `gtalc<n>`
  and `gtala<n>` per lamp; the script sets `gtalc` to 1 and `gtala` to 1 while `brake` is
  above 0, else 0, and the program draws the lit lens under `0x54 gtalc, gtalc, gtalc, gtala`:
  a colour scale whose alpha switches the lens on. A still model's zero registers leave it off.
- **Moving parts** (`benz0001`, `dmlr0001`, `nike0001`): registers `builtin_ra<n>`,
  `builtin_rx / ry / rz<n>` (an angle and an axis, command 0x48) and `blend_tw<n>`,
  `blend_br<n>` (morph weights, 0x4C; a 0x4D switch picks the shape) from `engineRPM`, `time`
  and the pedals 🔎 the scripts' arithmetic has not been read.

The script's two "ask the game" opcodes (0xC0, 0xC1) go to the callback's third and fourth
entry points, which a car leaves at their defaults (0).

### The driver in the car (+0x34)

A model set: 3,464 bytes on 771 of the 801 cars that have one (the R34's: one model, one
shape, bound to one bone named `CRANIUM`: the driver's head); 21,927 bytes on six cars 🔎
more of the figure; other sizes on the rest. The race gives it to a `RaceDriverModel` (a second one
for a co-driver when the event has one), which moves it; the driver's model in turn gives
the car the steering wheel's angle, the pedals and callback 9's matrix. ❓ Where the head
sits (the parts block is the candidate) and the driver's motion have not been read; the
skinned sets decode (see "Skinned sets"); the race does not pose its drivers yet.

### Paints in the database

A `VARIATION<language>` row (52 bytes; a car's rows share the id that its
`CAR_VARIATION_<language>` row holds):

```
+0x00 u32 string number   (the same on all of a car's rows; +0x08 repeats it) ❓
+0x04 u32 paint number, from 1: Pat0's paint (number - 1). The rows are not in order
+0x0C u32 string number: the colour's name
+0x10 f32 x4  the body's box in mm: height above y = 0, width, length ahead of and behind
              the origin (R34: 1243.9, 1779.8, 2262.8, 2335.1)
+0x20 f32     ❓ (R34 2.01, the Miata 1.65)
+0x24 4 x {u8 r, g, b, u8 kind}: the colour chips, up to four (kind 0: none after the
              first; 1 and 3 seen on single colours ❓ plain / metallic)
```

Checked on the R34 (`niss0021`: 7 paints) and the MX-5 (`mazd0039`: 4): the palettes of Pat0
paint (number - 1) average to the row's chip (R34 paint 2: chip 18 23 62, the blue).
❓ How a race picks an opponent's paint (`ENEMY_CARS`) has not been read.
