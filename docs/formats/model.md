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
- The program blends a group's vertices in VU0 (micro-routine 0x2E8, 0x460 without normals; the micro
  program, 0xA80 bytes at 0x6D3280, is loaded once at 0x4A03C0) with the four bones' palette matrices
  P_k (row vectors): w = int x 1/127 (a multiply by 0x3C010204; the weights are never made to sum to
  1), p = int x scale, the normal's ints as they are; b_k = p x w_k, a_k = n x w_k; then one
  multiply-add chain over the bones 0 to 3 and in each the rows x, y, z and (positions) the translation
  row x w_k; the normal times 1 / sqrt((x x + y y) + z z). The float layout enters after the conversions
  (0x308, 0x478) with scale 1. Without a palette the four bones are the identity. `to_gtm1` writes a
  skinned packet's stored pose as plain positions and normals, and `pose()` moves the decoded model's
  vertices (found again by their stored position and normal) by a palette, each by `blend()`: those
  operations in that order in the PS2's floats.
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
  `wing/lod/<name>`, `wing/menu/<name>`, `<name>` the `WING` row's label (`p001` .. `p021`; a
  car's record keeps the part at +0x100). The race model's wing (RaceCarModel vf20, 0x390340):
  no name: callback 47 reads 0 (as built); an empty name: 2 (the spoiler taken off); a name:
  the set loaded (0x454378) and 1. The draw (0x4533A8, before the body) multiplies by
  translate(parts block +0xD0) and scale(+0xDC / 2) and draws the set's model 0. R34: (0, 1.10,
  2.03), 1.6 wide.
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
- **Parts block** (0x100 bytes): the cockpit's places the driver's posture reads (below, "The
  driver in the car"), `+0xD0 f32 x3` where a fitted wing goes, `+0xDC f32` its width (the wing
  model is drawn scaled by half of it).
- **Info +0x02** is the cockpit kind (0x20 on 787 cars, 0x11 on the single-seat racers, 1, 0x0F,
  0 on a few), **+0x03** a co-driver (1 on 29 cars, the rally cars).
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

The second entry point (level of detail, command 0x07; 0x4539A8): a level forced by the race
when model +0x8C is not negative (-1 when made); else, with T the car's threshold (model +0x90:
220 when made, 200 set for every car at the race's start): v > T: 0, v > 0.33 T: 1, else 2.
The race bodies' switch comes after a 0x22 at the program's start; its size is 3.0 on the cars
looked at (R34, Panoz, Legacy, Celica, Supra, XA), its centre about 0.45 m up the car's middle.
**K** (the view's factor, kept at 0x8465E4) measured in the original's saved states: 721.2 in the
chase view at Tsukuba's start, 121.4 in a wide start shot, 1851.6 in a replay shot, 3905.6 in a
telephoto opening pass. A car 3.0 in size in the chase view is at level 0 nearer than 10.8 m
of depth, level 1 to 32.8 m, level 2 beyond. (The race view here: K = 721.2 / tan(A / 2), A the
lens's angle across a 4:3 picture, calibrated on the chase view, which this race view draws at
90 degrees 🔎 the lens itself is judged: camera.md.)

**The far level** (level 2) draws the wheels as shapes of the body, binding with callback 46
the wheel set's last texture set (its far level's turning face) to slot 2; no wheel or brake
callback runs there. (The race view here: that set appended to the body's lists for the build.) A wheel set's callback: the forced level, else the default
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

The body's program draws the brake parts here (unscaled shapes of the body set, in metres),
under the colour scale (1, 1, 1, glow) the callback sets (offset 0 first): the disc's glow is
their added shape (an orange ring picture, unlit, no depth writes), whose strength is that alpha.
The race's sponsor gives the glow per wheel (vf24, 0x3D75F0): **heat² x `Env0` +0x4C / `Env0`
+0x38**, heat the simulation's wheel +0xA4 (0x3F3A28 read at the step's end: the brake's heat,
0..1, physics.md); the environment reached as car +0x08 -> +0x04 -> +0x80 (the same chain
reads +0xEA0 and +0x1794). Tsukuba: 0.2 / 2.0, so a disc at full heat adds its ring at 0.1.
Callback 15 + i goes on:

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

Code: `src/gt4r/race/driver_figure.*`, `gt4tool race <disc> driver <model>`.

**Which set.** The car's own set (+0x34, on 801 of the 807 race cars; 0x38FE30 gives it to the
car's `RaceDriverModel`, a `HumanModel`, at RaceCarModel +0x5E0, and to a second one at +0xE20 for a
co-driver). On most cars it is a 3,464-byte set: one dark head-and-shoulders board (R34: 27
vertices, 0.5 m wide) whose symbol `CRANIUM` = 0 draws it at the skeleton's node of that name;
the ten single-seat racers (cockpit kind 0x11, `pscl0001`, `audi0015`...) carry a whole figure:
`SKINNING` = 0 (the suit, by the bones), `SCAPULA` = 1 (empty), `CRANIUM` = 2 (the helmet). A car
without a set (0x3B5438, by the info block's cockpit kind) takes a file of `character/`: kind 1
one of {driver2, driver, driver3} by the car's number mod 3, 2 driver4, 5 and 7 classic1, 6
classic2, 0x0A driver_nike, 0x0E driver, 0x0F driver_mtr with a co-driver, else driver_nike;
other kinds none (0x20 never comes here: those cars have their own set). On this disc only
`cthm0001` (kind 1) and `toyt0030` (0x0F) go that way. `character/driver.mdl`: five models,
`RHAND_BONE` 0 and `LHAND_BONE` 1 (the gloves), `SKINNING` 2, `CRANIUM2` 3, `CRANIUM` 4, 20 bones.

**The skeleton** (HumanModel +0x10, the "pdiRiderman" body library 0x414000-0x424000): 20 nodes,
names in program table 0x622A50: PELVIS, L/RTHIGH, L/RSHANK, L/RFOOT, LOWER_SPINE, MIDDLE_SPINE,
SCAPULA, L/RUPARM, L/RFOREARM, CRANIUM, L/RCLAVICLE, L/RHAND, NECK (`_BONE` on the limbs); node i
at the skeleton + {0, 0x70, 0x110, 0x1B0, 0xC0, 0x160, 0x200, 0x250, 0x2A0, 0x2F0, 0x360, 0x400,
0x3B0, 0x450, 0x4A0, 0x4F0, 0x540, 0x590, 0x5E0, 0x630} (0x41A988): f32 x3 position, then a unit
quaternion (x, y, z, w) at +0x10 (to a matrix by 0x4875B0), **in the car's frame**. It is built
(0x419F80) from 15 sizes in every driver set's extension record 0x800000 (the same on all:
0.177, 0.4513, 0.469, 0.25, 0.01, 0.1239, 0.16, 0.112, 0.0797, 0.1416, 0.1505, 0.39, 0.292, 0.262,
0.0974) and record 0x800001 (two floats to +0x708 / +0x70C, else 0.5 / 0.25). The bones of a set
are matched to the nodes by name (0x3B0D18); a model named by a symbol is drawn at its node's
matrix (0x3B1070), the skinned model by the palette (node x inverse bind); the extra symbol
`CRANIUM2` (0x6219C0) is drawn by the CRANIUM bone's palette matrix (its coordinates are the
stored pose's, like the skinned model's).

**The posture** (`race/driver_posture.*`, `posture_solver.*`, `posture_hands.*`, `posture_math.*`): each frame
StandardComputeDriverPostureCaller → 0x3B19F0 → 0x3B2568, with a flag that is 1 in a race (RaceCarModel's virtual 30,
0x390F70 → 0x38DB30). The class of a figure by the cockpit kind (0x38D710 ...): 0 none, 0x10 / 0x11 *torso* (the
single-seaters), 0x20 / 0x21 *board* (the closed cars' head and shoulders), else whole.
- **A board sits still**: one posture when the car is made (0x3B4040: hands on the wheel, feet on the pedals, the eyes 5 m
  ahead), in the car's frame. ✔ Every bone of the six closed cars of `03-race/state_before_first_corner_chase`.
- **A torso moves** (0x3B2708), in the **course's** frame: the legs off, plain hands on the wheel, no twist of the body, the
  head by the gaze. So the feet keep (+-0.089, -0.920, 0) in saved states, as if never posed, and the upper body holds places
  in the course.
- Its inputs: the car's geometry, the parts block (NormalCarGeometry: wheel +0x00, column +0x0C, the wheel's radius +0x18,
  pedals +0x1C, seat +0x34, recline +0x40, lever +0x4C, its pivot +0x58 and scale +0x64, its kind +0x6A, +0x6C, +0x78, +0x7C,
  the co-driver's seat +0xC4; SpecialCarGeometry's and MTRGeometry's own numbers for kinds 0x0A and 0x0F); the tuning object
  0x846468 (0x44F5A0; its cockpit offsets are zero); the driver's record, two kept a step apart (car +0x9A0 + 0x370 x seat,
  +0x160 and +0x260) and blended by the frame's time (0x386818 with the wheel's and hands' round-trip rules of 0x403BD8);
  the car's place and turn.
- The solver 0x416FD8 → 0x4173B0 → 0x41CB20 / 0x41D140 / 0x41E678 / 0x41EFE0: pelvis, spine, clavicles and arms to the
  handles, legs to the pedals, neck and head to the gaze, on quaternions (0x4872xx .. 0x4895xx) in the program's order of
  operations, EE floats.
- A figure shows while the view's size factor / depth / 420 is 0.025 at least (0x3B1750, the set's record 0x800001 beside it).

✔ `gt4r_diff <disc> posture <eeMemory.bin>...`: the program's own routines run on saved states' memory against ours, 264
solves in eight states (36-driver-sky's Chaparral 2J, Pescarolo C60 and MGF, 25-sound-cars, 20-race-chase): every bone and
every input bit for bit. (The program leaves two floats of `wrist_phase` unwritten; not compared.)

**The record's step** (0x403260: the wheel's angle, the hands taking new grips, the lean, the heave, the gaze, the gear
change, the feet) is ported in `race/driver_state.*` as its reset only (0x402320: hands at the rest grips, the eyes straight
ahead); what is ported of the step is listed there.

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
