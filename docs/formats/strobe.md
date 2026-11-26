# Race movies: `STRB`

Code: `src/gt4r/race/strobe.*` (the reader), `src/gt4r/race/strobe_player.cpp` (the player: the
timelines, the scripts, the buttons' focus), `src/gt4r/app/race_hud.cpp` (`draw_movie`),
`src/gt4r/app/screen_race.cpp` (the pause and the results). Facts from the program (`CORE.GT4`,
SCUS-97328 v1.01; addresses are the program's) and the disc's files. ❓ not read to the end.

## Files

The race's pages are converted Flash movies, not `.pmb` pages: `menu/common/pause.strb` (the
pause menu, every language and mode; path builder 0x3EC2B8 `"menu/%s/%s"` with `common`),
`menu/common/result.strb` (the results, `ResultArcade::vf12` 0x3DF940), `pitmenu.strb`,
`racemonitor.strb`, `racephoto.strb`. The five `menu/*.pmb` (`pause1`..`3`, `replay_panel1`, `2`)
are pages in the shared engine's `.pmb` layout that nothing in the program opens (no `.pmb`
string, no reader); the smaller `menu/pause.strb`, `menu/US/pause.strb`, `pause_license.strb`
have no reference either.

## The container

A converted Flash movie: timelines of placed objects with a key per frame, vector shapes, text fields,
buttons, and AVM1 (Flash 5) action bytecode. The program's library is `strobe::` (classes
`PlaceObjectInstance`, `SpriteInstance`, `EditTextInstance`, `ButtonInstance`; code about
0x473000-0x485800). The menus' `mFlash` widget uses the same loader (0x272880).

Everything is little-endian; every pointer is a file offset (the loader 0x473CC0 adds the load address:
`+0x0C` is 0 on disc and receives the file's own address). A "list" is `{u32 offset, u32 count}`.
Verified by parsing `common/pause.strb`, `common/result.strb` and the two small `pause.strb`.

Header:

| Off | What |
|---|---|
| 0x00 | `"STRB"` |
| 0x08 | u32 file size |
| 0x10 | f32 width 640, f32 height 480 (the canvas, y down) |
| 0x18 | u32 0x00999999 (❓ the movie's background color; nothing is cleared with it) |
| 0x1C | f32 frames per second: 30 |
| 0x20 | u16 frames of the root timeline (99), u16 1 |
| 0x30 | list **sprites**, 0x24 bytes each (fix-up 0x474430); sprite 0 is the root |
| 0x38 | list **shapes**, 0x18 each (0x47E988) |
| 0x40 | list, 0x28 each (0x47F1D8), empty here ❓ (morph shapes?) |
| 0x48 | list, pointer only, empty ❓ |
| 0x50 | list **fonts**, 0x18 each (0x479730) |
| 0x58 | list, 0x30 each (0x4798D0), empty ❓ (static texts?) |
| 0x60 | list **text fields**, 0x34 each (0x479BB8) |
| 0x68 | list **fill styles**, 8 each (no pointers) |
| 0x70 | list, empty ❓ |
| 0x78 | list **gradients**, 0xC each |
| 0x80, 0x88 | lists, empty ❓ |
| 0x90 | list, 0x14 each (0x4790B0), empty ❓ |
| 0x98 | list **fill matrices**, 0x40 each (4x4 f32) |
| 0xA0 | list **buttons**, 0x14 each |
| 0xA8 | list **action blocks**, 8 each `{offset, byte length}` |
| 0xB0 | list **textures**, 0x10 each `{offset, byte size, u16 width, u16 height, u32 0}`; data is a `Tex1` set (the shared engine's `gt3r::Tex1`) |
| 0xB8 | list, 0x18 each (0x47FA40), empty ❓ |

**Sprite** (0x24): list tracks (0x28 each); list frame actions (u32 each: low 16 bits the frame, high 16
the action block: run when the timeline enters that frame); list labels (0xC each: `{u32 frame, ptr
name, u32 name bytes}`); list ❓ (8 each, empty); u32 frame count.

**Track** = one object on a depth for a span of frames (0x28):
`u16 kind, s16 first frame, s16 end frame (exclusive), u16 index` into the kind's table;
`u16 depth` (draw order, ascending), `u16 clip depth`: 0 none, else this object is a mask for the tracks of its sprite of depths up to it: it is not drawn itself, and those are drawn through it (0x480518: the object's `lhu 0xA`, drawn by 0x47D660 after 0x474698, the masked ones between 0x474750 and 0x4747B8). Here the mask is its shape's box (its tracks are a bar; `StrobePlayer::Draw::clip`). Seen in Prologue's `result_license.strb`: the bar that wipes in "New Record" (it was drawn as a white box). A `kind 1` track (index 0) sits beside it at the same depth ❓ (a morph shape: none in the list; not drawn); `u32` union of the clip-event masks; list clip actions (8 each:
`{u32 event mask, s16 action block}`; mask 0x40 = key down, dispatched by 0x47D968); list keys (0x24
each); list instance name (chars; the count is the byte length). Kind (0x47D510): **2 sprite, 5 text
field, 6 button, anything else a plain object (0 = shape)**.

**Key** (0x24), one per frame of the span: f32 rotation (radians), f32 ❓ (0 everywhere: skew), f32 scale
x, f32 scale y, f32 x, f32 y (the object's origin in its parent, canvas units), u32 color multiply
(0xAABBGGRR, 0x80 = x1: alpha fades by it), u32 color add (0xAABBGGRR), u32 ❓ (not needed).
Which key (0x47D410): `k = max(frame - first, 0)`, and when the track has two keys or more
`k = min(k, keys - 1)` (the last key holds). A track with no key is at its parent's origin, unscaled,
color x1 (the buttons' state sprites). Rotation pi with scale (1,-1) is how the left arrow is the
right one mirrored.

**Shape** (0x18): f32 bounds x0, y0, x1, y1; list parts (8 each = a list of groups). Group (0xC):
`u16 fill style index, u16 flags (0 or 0x0300 ❓ on the highlight plates: an additive blend?)`, list
paths. Path (0x14): `s16 -1, u16 1` ❓, f32 start x, y, list edges. Edge (0x14): u32 0, f32 control x, y,
f32 end x, y (quadratic; in the pause movie every edge is a straight line: the control is the middle
point or the start). All shapes of the pause movie are one closed polygon per group (boxes, boxes with
4-unit notched corners): fill the polygon.

**Fill style** (8): `u16 kind, u16 index, u32 value`: kind 0 solid color (value 0xAABBGGRR, alpha 0x80
opaque); kind 1 gradient (index into gradients, value = fill matrix index); kind 4 texture (index into
textures, value = fill matrix index); kind 5 (style 0) unused ❓. **Fill matrix**: texture coordinate
(0..1 of the texture) `u = x*m[0][0] + y*m[1][0] + m[3][0]`, `v = x*m[0][1] + y*m[1][1] + m[3][1]` for a
shape point (x, y) (checked on the button plate: its box maps to u 0..0.578, v 0..0.8125 of the 256x32
picture). **Gradient** (0xC): list stops (8 each `{f32 position 0..1, u32 color}`), u32 ❓.

**Font** (0x18): list ❓ (empty), list spec string, u32 0x04000000 x2 ❓. The spec is the program's usual
font string: `hm_16*80,80` = font `hm_16`, drawn at 80% x 80% (the shared engine's
`gt3r::menu::FontSpec::parse` reads this form; `docs/formats/race_display.md` "Text").

**Text field** (0x34): f32 box x0, y0, x1, y1 (relative to the track's origin); u32 color (its RGB
shows as it is, 0xFF white: the pause title's 0x7A is the original's mid gray, the results rows'
0xD4 its near white, measured on the reference pictures; the alpha with 0x80 opaque); s16 font
index (+0x14); u16 height in twips (+0x16; 320 = 16 px); u16 ❓; u8 flags (+0x1A; bit 0x80 is set by the
loader once translated); u8 align (+0x1B; 2 in every field here: centered in the box ❓ by Flash's
numbering 0 left, 1 right, 2 center); +0x1C..0x21 ❓ margins/indent (0); s16 leading in twips (+0x22;
40); list ❓ (variable name, empty); list text (+0x2C; the count includes the NUL). Read by the drawing
routine 0x479CA0.

**Button** (0x14): u16 x4 sprite indices for the states **up, over, down, hit area**; u16 0; u16 union of
the condition masks; list conditions (4 each: `{u16 mask, u16 action block}`; Flash's bits: 0x01 the
pointer comes over it = it gets the focus, 0x04 pressed).

**Actions**: plain AVM1 bytecode (0x88 constant pool, 0x96 push, 0x1C/0x1D get/set variable, 0x3D call
function, 0x52 call method, 0x4E/0x4F get/set member, 0x9B define function, 0x9D if, 0x99 jump, 0x81
goto frame, 0x06 play, 0x07 stop, ...). The pause movie uses 27 opcodes: Push, Pop, GetVariable,
SetVariable, CallFunction, CallMethod, GetMember, SetMember, GetProperty (13 = `_name`), DefineFunction,
Return, If, Jump, Not, Equals2, StrictEquals, Greater, Less2, Add2, Increment, Decrement,
StoreRegister, ConstantPool, GotoFrame, Play, Stop, End (`result.strb` adds PushDuplicate).


## The pause menu: `menu/common/pause.strb`

51 sprites, 21 shapes, 3 fonts (`hm_16*80,80`, `hm_16*97,100`, `hm_16*85,85`), 14 text fields, 33 fills,
3 gradients, 27 matrices, 6 buttons, 53 action blocks, 14 textures (four 4x4 corner pieces, the 256x32
button plate, 16x16 arrow, 2x32 x2, 128x4, 256x32 x3, 256x16 x2). 

### Root timeline (99 frames at 30 per second), six labeled states

| Label | Frames | Panel (at 224,178) | Rows, top to bottom (row centers x 320; y) |
|---|---|---|---|
| `MENU_NORMAL` | 3-18 | 192x104 | CONTINUE 198.2, EXIT 228.2, Screen Setting 258.2 |
| `MENU_RETRY` | 19-34 | 192x134 | CONTINUE 198.2, RETRY 228.2, EXIT 258.2, Screen Setting 288.2 |
| `MENU_NO_SCREEN` | 35-50 | 192x104 | CONTINUE 198.2, RETRY 228.2, EXIT 258.2 |
| `MENU_NO_SCREEN_NO_RETRY` | 51-66 | 192x74 | CONTINUE 198.2, EXIT 228.2 |
| `SCREEN_SETTING` | 68-82 | 192x134 | "Brightness" label 186 + slider 211.85, "Contrast" label 231 + slider 256.85, OK 292 |
| `CONFIRM` | 83-98 | 192x104 | text "Exit Sure?" (box top 188), CONTINUE 228.2, EXIT 258.2 |

Always there from frame 3: the title bar, a 192x20 box with notched top corners at (224,158), solid
black at alpha 0x5A/0x80; four 4x4 corner pictures at (224,158), (412,158) and at the panel's bottom
corners (224 and 412; y 278, 308 or 248 by the panel's height), drawn with color multiply 0x59000000;
the panel body is the same black at 0x5A. Title text "Paused" ("Screen Setting" in that state): font 0,
16 px, color R 0x7A G 0x7A B 0x80, box x 255.4-384.5, y 161.4-186.2. Nothing dims the rest of the screen.

Each menu state fades in: the color multiply's alpha goes 0, 0x2A, 0x55, 0x80 over its first four
frames, then the timeline stops (frames 7, 23, 39, 55; `CONFIRM` stops at 87; `SCREEN_SETTING` at 68,
no fade). The rows appear one frame after the panel.

### A row (sprites 7, 11, 15, 19, 23, 26, 27; 15 frames each)

- Frame 0: a button; the timeline stops there. Frames 1-14: the "chosen" animation (the plate flashes
  out over 8 frames, the text flashes white), and **frame 14 runs the row's action**: 14 movie frames
  (about 0.47 s) pass between the press and the effect.
- The button's states: *up* = plate (shape 6: 159.85 x 26 box, the 256x32 texture) + the word in the
  buttons' font (font 1, 16 px, drawn black: field color white x track multiply 0x80000000), box x
  -75.1..75.0, y -10.9..12.7 around the row's center; *over* (focused) = the same with a pulsing
  highlight (sprite 1: two additive plates whose color goes from multiply 0x80404040 + add 0x00004373 to
  plain over 4 frames, looping every 5 frames); *down* = as over; hit area 142x20.
- Conditions: getting the focus plays `PLAY_SOUND("cursor")`; a press plays `PLAY_SOUND("ok")`
  (**CONTINUE plays `"cancel"`**), calls `_parent.inactivateButton()` and plays the row's timeline.
- The row whose instance name is `DEFAULT` gets the focus when a state appears: CONTINUE in every
  menu and in `CONFIRM`.

### The scripts (all of them)

- Root frame 0: `topMenu` = `menu_no_screen_no_retry`; with `retryEnable()`: `menu_retry` if
  `screenSettingEnable()` else `menu_no_screen`; without: `menu_normal` if `screenSettingEnable()`.
  `confirm = confirmExit()`. Then `gotoAndPlay(topMenu)`.
- State frames call `NavigateAll()` (menus, confirm) or `NavigateVertical()` (screen settings).
- Row actions (frame 14): CONTINUE `EXIT_MENU("continue")`; RETRY `EXIT_MENU("retry")`; EXIT: if
  `_root.confirm` then `_root.gotoAndPlay("confirm")` else `EXIT_MENU("exit")`; Screen Setting
  `_root.gotoAndPlay("screen_setting")`; OK `_root.gotoAndPlay(_root.topMenu)`; in `CONFIRM`: CONTINUE
  `EXIT_MENU("continue")`, EXIT `EXIT_MENU("exit")`.
- A slider (sprite 25, instances named `BRIGHTNESS` and `CONTRAST`): a 158x24 bar (two 1-unit edges at
  alpha 0x33 and a gray gradient at alpha 0x4D), arrows `LA` and `RA` at x -67 and +67 (16x16 picture,
  the left one mirrored; each plays a 2-frame blink), a text field `TEXT` (font 1, 13 px, black) and a
  110x20 button. Its script: `min = -5`, `max = 5`, `v = defaultVal(_name)`; `settext()` writes `v`
  (with a `+` before a positive value) into the field and calls `setVal(_name, v)`; `inc()`/`dec()`
  step `v`, at the limit they play `play_sound("disable")` and stay, else blink the arrow, `settext()`
  and `play_sound("cursor")`. Getting the focus sets `_root.sel = _name` and plays `cursor`; the OK
  row's focus sets `_root.sel = 0`.
- A key-down clip event on the `BRIGHTNESS` instance: `s = _root.getSel()` (the slider named by `sel`,
  0 for none); key code 37 (left) calls `s.dec()`, 39 (right) `s.inc()`.

## The pause as the program drives it

- `RacePause::vf2` (0x3C2FC0, "open/reset"): makes a player (0x19C bytes, 0x474F38) on the loaded
  file, registers the command handler 0x3C3260 (0x475D78), sets the player's pad mode (`+0x10 = 1`).
- Each paused frame (`RacePause::vf4`, 0x3C3820): the first time, the movie restarts from frame 0
  (0x4751D0); then it advances by 1/60 s (0x475488: an accumulator of dt x 30; **a movie frame every
  second call**); then the pad is fed (0x3EBF40).
- Pad to movie (0x3EBF40; pad bits as 0x426A08 returns the newly pressed ones): bit 0x01 up -> key 0x26,
  0x02 down -> 0x28, 0x04 left -> 0x25, 0x08 right -> 0x27, 0x10 accept -> key 0x20 and a pointer press
  (0x476500; its release 0x476558), 0x20 cancel -> key 0x1B.
- Focus (0x475BB0, 0x475AF0, 0x4759B0): the buttons on screen are a list; the focused one is where a
  virtual pointer sits (its center), which is what puts it in the *over* state. Up/down move to the
  nearest button in that direction (wrapping round) when vertical moves are allowed, left/right when
  horizontal ones are (`NavigateAll` allows both, `NavigateVertical` only up/down, so left/right reach
  the sliders' key handler). When buttons appear and none is focused, the one in the sprite named
  `DEFAULT` is (0x4757D0).
- Functions the movie calls that the program answers (0x3C3260; names compared without case):

| Call | Program |
|---|---|
| `PLAY_SOUND(name)` | plays the named sound (0x4857C8): `cursor`, `ok`, `cancel`, `disable` (the menus' sound names) |
| `EXIT_MENU("continue" / "exit" / "retry")` | result 3 / 4 / 5 (`RacePause+0x1C`); the menu is marked closed unless the result is 4 |
| `RETRYENABLE()`, `SCREENSETTINGENABLE()`, `CONFIRMEXIT()` | 1 or 0 from `RacePause+0x28`, `+0x30`, `+0x2C` |
| `DEFAULTVAL(name)` | the stored value: `BRIGHTNESS` the first number, any other name the second |
| `SETVAL(name, value)` | stores it at once |
| `NAVIGATEVERTICAL()`, `NAVIGATEALL()` | which directions move the focus |

- Screen settings are kept in the game object (the pointer at 0x622F4C) at +0x39E34: four s32
  `{brightness, contrast, ❓, ❓}`; a second record at +0x39E44 is used when `RacePause+0x34` is set
  (`RaceArcade` sets it in the B-spec mode). Both -5..+5. The race's final picture pass builds a color
  correction from the record (0x3EFDF0 -> 0x3EFC10, the record of the photo mode's corrections): a **gamma**
  2^(brightness / 10) on every channel (out = in^(1 / gamma); 1 at 0), the red and blue ones moved by
  -/+ (T - 5500) x 0.2 / 2000 with T = 5500 + 200 x balance (0.02 a step), the black level 0, the **white level**
  1 - 0.06 x contrast, a saturation (saturation + 10) / 10; used by 0x33F2E8, the race's last picture pass. The records
  are the options' `MOption` `race_color_correction` and `replay_color_correction` (the Picture Quality page's Racing
  Views and TV Live Relay Views: `[brightness, contrast, saturation, balance]`). **A new game's racing views
  have brightness +3** (the original's options page and pause after a new game), the replays' 0.
  Here (`race/picture_settings.*`) they are the 3D view's grade: the white level, the curves through the gammas' middle,
  the saturation. (It was taken for a gain and a stretch about the middle before: wrong.)
- Drawing (`RacePause::vf5` 0x3C3988 -> 0x3EC0B0): the movie on the 640x480 canvas over the whole
  frame, global color 0x80FFFFFF, in the race's 2D pass (`RaceBasic::vf107`, only while paused).

## The race around the pause

- **Which key, when** (`RaceBasic::vf149`, 0x38B0E0, every frame; `+0xCC8` is the race's mode: 0 race,
  1 B-spec race, 2 and 3 replays, 4 the scene behind a quick menu): a new press of the pad's bit 0x40
  (Start):
  - mode 4: ends the scene with code 1;
  - the race is over (`+0xCDC`) or the mode is 2 or more: ends the run with code 2 after a 0.5 s fade
    (so Start ends a replay; **replays have no pause object**: `RaceArcade::vf38` 0x3870E8 installs
    none in modes 2 and 3);
  - else (modes 0 and 1), unless the end fade is running (`organization+0xD4 >= 0`) or the lock
    `+0xCD0` is set (❓ set by the mode setter): opens the pause (`vf150`, 0x38B428).
  - ❓ Nothing was found that refuses the pause during the start's count.
- **Opening** (`RaceBasic::vf150`): the idle time-out = `organization+0xD0` seconds x 60 frames (the
  race information's `PauseTimeOut`; 0 = none); the paused flag `+0xCD8 = 1`; the race's time step
  (global 0x6214DC, normally 1/60) becomes 0; the race's sounds and its music are paused
  (`RacePS2Base::vf90`: 0x399300 on the sound object, 0x3D33C0 on the music); event 0x18 is posted.
  The display keeps being drawn (the pause is one more 2D layer).
- **While paused** (`RaceBasic::vf77`, 0x38B2B0) the pause's result decides:

| Result | From | The race |
|---|---|---|
| 1 | still open | nothing |
| 3 | `EXIT_MENU("continue")`; **or a press of cancel at any time** (0x3C3820 returns 3 at once, whatever page is shown); or the race's events 0x16 | resumes (`vf151` 0x38B4B8: flag off, sounds on, time step back to 1/60) |
| 4 | `EXIT_MENU("exit")`; or the race's event 0x17 | ends the run with **end code 2** (`vf87(2)`: `+0xCE0 = 2`, no fade) |
| 5 | `EXIT_MENU("retry")` | ends the run with **end code 4** |
| 2 | the idle time-out ran out | ends the run with end code 3 |

  (`RaceFreeRun::vf77` 0x3BA260 differs: exit calls 0x3BA3E8 and resumes.) What a sequence does with the
  end codes is the mode's own routine (the license, mission and machine-test sequences read `+0xCE0`;
  `RacePS2Base::vf101` runs the race again at once on code 5). Start does nothing while paused.
- **Entries per mode** (flags at construction; the movie picks the page):

| Race class | retry | confirm | screen | Page |
|---|---|---|---|---|
| `RaceArcade` (single race), `RaceFreeRun` (the time trial's sequence 0x112620 and the free run's 0x116E20 make one), `RaceFreePractice` (0x1181C8) | 0 | 0 | 1 | `MENU_NORMAL`: CONTINUE, EXIT, Screen Setting |
| `RaceLicense` (0x3BC05C), `RaceMachineTest` (0x3BDE20), `RaceTrainingBase` (0x3ECD98: missions) | 1 | 0 | 1 | `MENU_RETRY`: CONTINUE, RETRY, EXIT, Screen Setting |
| `RaceGTmode` (0x3EDF68), the championship's race (0x3EF5C0) | 0 | 1 | 1 | `MENU_NORMAL`, EXIT asks "Are you sure?" (`CONFIRM`: CONTINUE, EXIT) |
| `RaceLanBattle` (0x3366D4) | 0 | 0 | 0 | `MENU_NO_SCREEN_NO_RETRY`: CONTINUE, EXIT |
| replays | | | | no pause menu: Start ends the replay |

- Other pause classes: `SimplePause` (0x681B60; in `RaceSinglePlayer`, `RaceSolitaire`, the split-screen
  race): no menu, the line `SimplePauseMessage` ("Press %Bb to resume") in font `hn_14#1*107,100`;
  closes on the pad's bit 0x80 or 0x20. `PhotoPause` (0x681B98) is the photo mode's camera page
  (`racephoto.strb`). In the B-spec modes the pad's bit 0x80 opens a second object (the race monitor,
  `racemonitor.strb`) the same way.

## Text

The movie's strings are **keys** looked up when the file loads (0x3EBE80 -> 0x474130 -> 0x479BE8 with
the callback 0x3EBE28 -> 0x3AEAE8: the race's text table, the one the race display uses): page
`RaceDisplay` of `rtext/race<LANG>.rt2`; a key without an entry stays as it is. US values:

| Key in the movie | Text |
|---|---|
| `Paused` | Paused |
| `CONTINUE`, `RETRY`, `EXIT`, `OK` | the same |
| `Screen Setting` | Screen Settings |
| `Brightness`, `Contrast` | the same |
| `Exit Sure?` | Are you sure? |
| `-` | (the slider's value, written by its script) |


## Playing (`strobe_player.cpp`)

- A sprite's instance plays its timeline at the movie's 30 frames a second, looping unless a
  script stops it. Entering a frame places the tracks that span it (a sprite, a button or a text
  field becomes an instance with the track's name; leaving the span removes it) and runs the
  frame's action blocks; a newly placed instance starts on its first frame and moves on from
  the next. A track's clip actions run on its instance: mask 0x01 when it is placed, 0x02 every
  frame, 0x40 on a key.
- The scripts are AVM1 (Flash 5) bytecode: names are compared without case; the movies use
  ConstantPool, Push (string, float, null, undefined, register, boolean, double, integer,
  constants), Pop, Get/SetVariable (paths with `.`), Get/SetMember, GetProperty (13 `_name`),
  CallFunction, CallMethod, DefineFunction, Return, If, Jump, Not, Equals2, StrictEquals,
  Less2, Greater, Add2, Increment, Decrement, StoreRegister, PushDuplicate, GotoFrame, Play,
  Stop. A clip answers `gotoAndPlay`, `gotoAndStop` (a frame from 1 or a label), `play`, `stop`,
  `_name`, `_parent`, `_root`, `_visible`, `inactivateButton` (the player's own method, 0x47B5E0: every
  button under the clip is switched off, below); `Key.getCode()` gives the key; a text field's `text`
  is its words. A function the movie does not define is the host's (the program's handler).
- Buttons: the focused button shows its *over* state (*down* for a moment after a press). When
  buttons are on the stage and none has the focus, the one inside an instance named `DEFAULT`
  takes it (else the highest), and its focus conditions (mask 0x01) run. Up / down (and left /
  right when the movie asked `NavigateAll`) move to the nearest button that way, round the
  list at the end; accept runs the press conditions (mask 0x04) on the button's parent.
  A switched-off button (`inactivateButton`: a sprite's vf10 0x480D28 passes it to its children, a
  button's 0x485358 sets its +0x98) is left out of the focus list (0x4852F8 lists only active ones, and
  only they can be the DEFAULT) and keeps the look it had (its state update 0x484D48 is skipped). The
  pause's rows switch their menu off as they are pressed (`pitmenu.strb` names the method too), so while the chosen row
  plays its animation no row has the focus: none other lights, and further presses do nothing.
  Ours also takes a mouse and a finger (the program's player has only the pad's virtual pointer): a
  hover, or a tap on a button not focused, gives that button the focus (its focus conditions run); a
  tap on the focused one presses it as accept does. The button under the pointer is the one whose hit
  area (its fourth state's shapes on their first frame, through the instances' matrices; a pause row's
  is 142 x 20) holds it.
- Drawing: each shape group is its polygon filled (solid, a picture through its fill matrix, or a
  gradient along the matrix's u); colours are 0xAABBGGRR with 0x80 a full channel, through the
  instances' colour changes (multiply 0x80 = x1, add / 255) from the root down; texts in the
  menus' fonts (`font/menu.fnt`) by the font string `name#palette*x%,y%` (palette 1, the plain
  glyphs, when none is given), the glyphs at their own pixel size (scale 1) times the font string's percentages and the
  instance's scale, laid out by Gran Turismo 4's own text layout (`menu/text_layout.h`: its pair
  spacing and space) in the field's box less its 2-unit gutter, aligned as the field says, the first
  line at the box's top; the words looked up in the race's text table. A picture is placed at the size the
  movie's texture list gives (a power of two, 128 x 64), the set's texture (its own size, 99 x 34)
  at its top left: the fill matrices map the list's size.

## The results movie (`menu/common/result.strb`)

142 root frames. Frame 0 asks `isChampionship()` and stops (a championship goes on at frame 31,
the points pages). The program puts it at frame 0 when the page goes up (0x4751D0); the root waits there until its sprite 1 (40 frames, 3.3 s at the movie's 12 frames a second) plays it on; rows
`R1`..`R6` slide in from frame 1, each with a `BAR` instance whose per-frame script asks
`cb_stable(row)` and, once the row's result is fixed, fills `RANK` (`gotoAndStop(1)` for the
player's row `cb_playerrank()`, else 2), the name (`cb_getname(row)`) and the time
(`cb_gettime(row)`); `cb_isvisible(row)` hides the rows past the field. Frame 20 stops with an
invisible full-screen button whose press asks `cb_EnableExit()`, plays `ok` and plays on; frame
30 calls `cb_raceexit()` (the run ends). Here the callbacks answer from the race's results
(`RaceFlow::results`): rows in finishing order, times written `M'SS.mmm`. 🔎 Rows not yet fixed
show the movie's placeholders ("car name", "00:00"); ❓ whether the program hides them.


## The license test's results (`menu/US/result_license.strb`)

`ResultLicense::vf12` (0x3E1BF8) names `result_license.strb` (the US disc has it in `menu/US/`, a
larger one in `menu/`). 232 root frames: frame 0 asks `PrizeLevel()` (0 gold, 1 silver, 2 bronze, 3
failed) and, unless it is 3, plays on at the label `PASSED` (frame 72); frames 1-71 are the FAIL
page. Its first track is a picture the program supplies (kind 7, no name): **the race's live
picture**, 300 x 225 at its scale, which starts at 2.13 (the whole frame) and shrinks to (214, 239)
over the page's first frames; over it the run's time and its gap in red (`GetTime()`,
`GetTimeDif()`: seconds with three decimals, "133.366" and "+122.066" in the original's B-1 run,
the gap to the bronze time), the test's title (`GetTitle()`, "License B-1"), the FAIL picture,
then RETRY and EXIT (`EXIT_MENU("retry")` / `EXIT_MENU("exit")`, the RETRY row named `DEFAULT`).
The movie's pictures the program draws are listed in the header (+0xB8: 0x18 bytes each, f32 x0, y0,
x1, y1 the box about the track's origin, u32 name, u32 length), an external track's index naming one:
here `screen` (300 x 225: the live picture) and `TropyPanel` (282.3 x 270.5). ResultLicense's picture
callback (0x3E0E38): `screen` sets the race view's box (the FAIL page's inset; the PASSED part has
none, so a passed test's page shows the race over the whole picture), any other name draws the
trophy (0x3E0FF8): `race/trophy.mdl` in its material set by the result's kind (table 0x6A38F0: kinds
1, 2 set 0, 3 set 1, 4, 5 set 2; gold, silver, bronze), through a lens 50 degrees high (0x49AC00:
0.1 .. 100 m, the 640 x 448 picture's aspect), the whole picture then put into the panel's box; three
passes a frame averaged, the motion stepped half a frame between them. The motion (0x3E18D0, stepped
by 0x3E1A80, each value v += ((target - v) x gain + rate) x dt): the place's z from -0.5 to -2.5 at
gain 0.5 (0.35 for kinds 1, 2), the turn about y at the rate of a spin easing to 60 degrees a second
(gain 0.5), the picture's opacity 0 to 1 (gain 1); kinds 1, 2 start turned -22.5 degrees, kind 3 at 180
with its spin from 1000 (gain 1.2), kinds 4, 5 at 270 and turned over (about x from -180 to 0, gain
1.5; the spin's gain 3). The race display's three target times with their trophies stay over the
page (the original's pass pictures). Here: the trophy drawn once a frame (no averaged passes), the
grade's kind gold 1, silver 3, bronze 4 (❓ the result's kind +0x88 is not read), the live picture
from the course's TV cameras (❓ the program's camera for the page is not read).
Here: the grade from the run's time against the page's gold, silver and bronze times (❓ the
tests' own failing rules are not read), the gap to the next grade up, the title from the race code
(❓ the program's title routine is not read); RETRY runs the test again at once.

## The race monitor (`racemonitor.strb`)

B-spec's R1 opens it over the race (the pad's bit 0x80 in the B-spec modes). The program fills the movie's
named instances each frame (0x3D8DB8 → 0x3DAF60 the current car, `BAR%d` rows, `LAP` with the race words
`RaceMonitorLapFormat1` / `2`, `TOTAL`; 0x3DBF90 / 0x3DC5D8 the rows and `CARPOS_%c`). The instances
(`gt4tool race <disc> strobe-tree menu/common/racemonitor.strb`): `CURRENTCAR` (text), `CURRENTCAR_ICON`
(the car's picture, an external), `CURRENTCARGRID` (a letter, sprite of 6 frames A .. F), `TOTAL`, `LAP`,
`SPEED` (the replay speed "x 1"), `PACEPANEL` with `PACETEXT` and `PACE1` .. `PACE7` (3 frames each),
`BAR1` .. `BAR6` (2 frames: the second lit, the player's row) each with `NAME`, `GRID` (6 frames), `TIME`,
`DIFF`, `LAP`, `PIT`, `BEST`, `ICON` (6 frames), `BESTMARK` and two externals; `CARPOS_A` .. `F` (the map's
letters: `GRID` of 12 frames, `PACEUP_MARK`), `GAP1` .. `GAP5` (`TEXT`: the gap strip), `PITPANEL` (three
stops' lap and front / rear tires, `TIRE_PANEL` of 80 frames), `DISTUNIT`, `MAP_PANEL` (an external: the
course map). Here (`screen_race.cpp` `fill_monitor`): the followed car's name, letter and picture, the
pace's word and lit key, the total time `h:mm'ss`, the lap, a row per car in race order (its short name, its
grid letter, its best lap, its gap to the leader: the time since the leader was where it is ❓), the
player's row lit; the course map drawn in `MAP_PANEL`'s place with each car's grid letter at its place
(the followed car's orange; ❓ the program moves the movie's `CARPOS_` letters instead); the gap strip and
the pit panel are not filled (hidden). The
reference's B-spec race (`03-race/bspec_race_monitor.png`) has the same rows, letters and lit row.
## The PIT STOP menu (`menu/common/pitmenu.strb`)

Opened (RaceBasic::vf74) in the step 0x35BC70 finds the player's car asked for its choice (F +0x4B8 bit 0, set by 0x35AF18 as the car
comes onto the pit lane; made 0x10 there): tires when its bit 2, fuel when its bit 3, the driving mode by the race's
+0x970 (1 from its making; an event's, 0x110498: its row's +0x45 is 0 or the license of that number, `l0b` .. `l0s`, is passed). In arcade, free run, Gran Turismo mode and championship races. The
race goes on under it, its movie moved through with up and down only. Instances `FTIRE`, `RTIRE`, `FEED`, `DMODE`; the movie asks, by the instance's name, `nItem` (tires: the kinds + 1,
fuel and mode 2; 1 where not allowed), `defaultVal`, `item(name, i)` (a tire's word `Tire_%02d`, past them `Tire_NoChange`; `DoFeed` /
`DontFeed`; `BSpecMode` / `ASpecMode`; "---" where not allowed) and gives `returnVal(name, i)`; `exit` makes the choice (0x35BF08: the
kinds, 0xFF for no change, the fuel, the mode, 0xFF where it may not be chosen). A mode other than the car's control source F +0x466
(0 the pad, 1 B-spec) goes by command 10 (0x3F1BD8 → 0x3F19E0): F +0x466, the race's mode (0x387D38: the display's), the
suggested gear cleared; the pace level stays, every player's car having the starting one from the race's start (command 2). The kinds (0x4076E0): those of the fitted kind's class (0x441300: 0 .. 5, 6 .. 10, 11, 12), in a
Gran Turismo mode race only those the garage's car owns (0x4078D0: the record's bits 0x40 and 0x4D on) and the fitted. A key while it is up
stops B-spec's 300-step wait (0x35BCC8); it closes with the choice, off the lane, or a step before the wait runs out (0x35BCE8). The help
line is the race words' `TOOLTIP|PitMenu`, while refuelling `TOOLTIP|Refuel` (Circle stops it: command 2, 0x35BFC0).

