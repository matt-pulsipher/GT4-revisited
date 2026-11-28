# The player's controls

How Gran Turismo 4's program (SCUS-97328 v1.01) reads a pad or a steering wheel into the race's frame, what it sends back,
and what this game does with it. The frame's use in a step is physics.md §3.6.

Verified: `tests/test_input.cpp` `input_program_settings_and_frames_equal` (3,000 made-up ports and settings through the
program's own 0x43CF90: every byte of the frame and the words the race reads equal; the settings' saved form and presets;
what the port sends to the devices).

## The settings (OptionRaceControllerPS2, Option +0x11D0)

Two ports of 0xB8 bytes (from +4; the port's virtuals at 0x687FF0), each with four devices (+0x18 the pads', +0x48, +0x64,
+0x8C the wheels'), by the program's names:

| Device | Is | Axes | Buttons, in the settings' order |
|---|---|---|---|
| `DUALSHOCK 2` (also `CONTROLLER`, `JOYSTICK`, `SINGLESHOCK`, `DUALSHOCK`: 0x43CE08) | any pad | LX LY RX RY, then the sticks' halves LXF LXL LYF LYL RXF RXL RYF RYL (F left or up, L right or down) | LEFT RIGHT UP DOWN TRIANGLE CIRCLE CROSS SQUARE L1 R1 L2 R2 L3 R3 START SELECT |
| `COUGAR` | USB 046D:C293, GT FORCE | STEER ACCEL BRAKE | X Y A B L R |
| `JAGUAR` | 046D:C294, Driving Force | the same | LEFT RIGHT UP DOWN TRIANGLE CIRCLE CROSS SQUARE L1 R1 L2 R2 L R START SELECT |
| `CHEETAH` | 046D:C298, Driving Force Pro | the same | LEFT RIGHT UP DOWN TRIANGLE CIRCLE CROSS SQUARE L2 R2 L3 R3 L1 R1 START SELECT SHIFTUP SHIFTDOWN |

A device keeps (from +0x10 of it, saved as it is) s8 the preset (0 changed by hand), an s8 action per axis, an s8 action per
button, then its feedbacks: "ON" (the pad's vibration, the wheel's forces), and the Cheetah's "POWER" (0 .. 2) and
"POWER_ASSIST"; all 1 in a new game. An axis' actions (names 0x622E08): 0 steer, 1 shift down, 2 shift up, 3 accelerate, 4
brake, 5 side brake, 6 reverse; a button's (0x622E28): 0 / 1 steer left / right, 2 / 3 shift down / up, 4 accelerate, 5
brake, 6 side brake, 7 reverse, 8 special (the nitrous), 9 the play view, 10 back view, 11 front, 12 rear, 13 left, 14 right
view, 15 the license's best line, 16 the ghost, 17 pause; -1 none. A preset (the devices' virtual +0x18: 0x43B808, 0x43B8D8,
0x43B928, 0x43B9D8) fills them: the pad's one layout (the stick steers, the right stick's halves up and down accelerate and
brake, Cross, Square, Triangle reverse, Circle the side brake, L2 / R2 the gears, L1 back view, R1 special, Up the ghost,
Select the view), the Cheetah's three (SHIFTUP / SHIFTDOWN as named, the other way, side brake and reverse).

The option pages (`option.adc`: KeyconfigRoot per device and port, ControllerRoot, SteeringRoot) reach them through MOption:
`searchButtonConfig` / `searchAnalogConfig` (port, device, action) the keys with it, `setButtonConfig` / `setAnalogConfig`
(port, device, key, action: the preset becomes 0), `unset*Key` (port, device, action, keys: those of the keys that have it),
`clear*Function`, `get*Config`, `getDeviceConfig` / `setDeviceConfig` (the preset), `getDeviceFeedback` / `setDeviceFeedback`,
`vibration_1p` ("VIBRATION") and `steering_forcefeedback_1p` ("WHEEL FEEDBACK", all three wheels': no page of the US disc
offers it). The options' reset (0x4363A0 → 0x43DA80) presets every device and leaves the feedbacks.

Saved: 0x60 bytes a port, pieces of 0x20, 0xC, 0x18 and 0x1C (0x43D8A8).

## The game port

`PlayStation2::GT4GamePort` (0x564C10) joins a pad and a USB wheel into one report: the kind (the pad's 1 digital, 2 analog
without motors, 3 one motor, 4 two, 5 with pressures, x 16, + the wheel's 1 .. 3), 27 axes and 48 buttons; the controller's
own configuration ("GT4 default port1", 0x64C6E0) passes the axes as they are and puts 16 menu buttons before the pad's.

- Axes 0 .. 3: the right stick's X and Y, the left's, a byte each, 128 centered, no dead zone of the program's; 4 .. 15 the
  pressures (Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2); 16 .. 23 the halves: 127 - x and x - 128,
  at least 0 (so at most 127: a stick pushed up fully accelerates at 2,040 of 4,096 ✔); 24 .. 26 a wheel's steering (-8192 ..
  8191), throttle and brake (0 .. 255).
- The menus' ports ("GameMenuPortGT4PS2-1") have a dead zone: 90 .. 165 is the center.
- Buttons 16 .. 31 the pad's (libpad's order), 32 .. 63 the wheel's; the wheel's pedals past half are 62 and 63.

## The frame (RaceInput::vf2 0x426F00, the port's virtual +0x30 0x43CF90)

`RaceInput` keeps the frame at +0x150 and its words at +0x170: +0x04 the views (1 back, 2 the play view, 0x4000 left, 0x8000
right, 0x10000 front, 0x20000 rear), +0x08 the sticks, +0x18 the menu's buttons (0x40 the pause), +0x1C a replay's, +0x20 (1
best line, 2 ghost). Its mode: 0 drives; 1 (+0x1C4 1) a replay's controls without a frame; 2 (+0x1D4 set ❓ by whom) drives
with the pad's direction buttons taken (they are none of the settings'); 3 and 4 other machines' and replays.

A DualShock's (0x43BBC0), in order: each button held by its action (0x438870: steer ±2 digital, kind 0; a pedal 1, its analog
flag clear; a shift ±1 for as long as it is held: the gearbox's lock-out finds the press); Up and Down not read while Left or
Right is held. With pressures (kind 5) the direction buttons steer (kind 3) by min(256, 2 p) x 100 / 60, at most 256, x 16,
right less left, whatever they are set to. Then the 12 axes (0x4386F0): a steering axis, when the frame does not steer yet,
(v ± 4) >> 3 within ±4096 and turned (v the table 0x622F58 of its byte: x 256 below 128, x 258 above), kind 2; its center
leaves the frame as it is (digital, kind 0); the others (v the table 0x623158: 32,767 / 255 x the byte, rounded) a pedal
(v + 4) >> 3 at most 4096 with its analog flag, a shift past 0x4000; a pedal of 0 changes nothing. Last the pressures of the
buttons that accelerate (full at 35 % of 2 p: p 45), brake (65 %: 84), side brake (20 %: 26), reverse (35 %).

A wheel's (0x43C260): its buttons, then STEER (-4 x the axis, kind 4, the Cheetah's 6, always analog), ACCEL and BRAKE. ±4096
is the wheel's lock: the steering force's tables (0x359A38) take 8,192 units as 200 degrees for the kinds 4 and 900 for 6, and
kind 6's ratio (0x62025C) is 900 / (60 b).

`RaceInput::vf5` (0x426D50) copies the pit answer's and command's bytes into the frame's +0x10 and gives it to the step.

## Which choices a race takes

Every sequence (0x10F770): the options' `steering_assist_1p` into the entry's +0x344C (the wheel's lock, F +0x443) and
`active_steering_1p` into +0x3458 (F +0x4E5 bit 3, a wheel's slip aid). The gearbox (+0x3448) and the aids (+0x3450 ASM,
+0x3454 TCS): Arcade's single race and time trial from their menus' work (0x111DC0, 0x113248: QuickTuneTransmission not 0
manual; QuickTuneDrivingAssist 0 none, 1 TCS, 2 both); the others the option `automatic_gear` (0x10F848, 0x10F880), the aids
on (the license tests' and missions' 0x10F880 / 0x10F8E0 set them; the events' entries start so) and then the car's own.

## A modern pad against the console's

How each layout's pedals reach the step:
- The console's DualShock 2 (kind 5): a button that accelerates is full at 45 of its 255 pressure and one that brakes at 84 (the
  shares above). A press is analog (frame flag set).
- Ours, the modern layout (the default): each trigger's travel is linear, full at full travel, analog.
- Ours, the console layout on a pad without pressures (kind 4): a held button is a digital pedal.
- In the step (0x349418), with the settings at 0x622110 the pedals move at the same rate in all three cases. A digital pedal
  ramps by 0x620270 (the throttle 1/6 a step up and 1/6 down, the brake 1/3 up and 1/6 down). The byte 0x622121 is 1, so an
  analog pedal slews toward its value at the same rates. Any analog value above 0 is first lifted to 0.1 + 0.9 x (K +0x04, 10).
- So a full press is the same in each case. ✔ `input_full_presses_as_the_console_pad`: the frames are equal, and B-1's car on
  its course gives speeds equal in every bit over 6 s at full throttle and then full brake.
- What differs: a part press. A trigger at half travel brakes at about 0.55, where a DualShock 2's button at half pressure
  brakes fully. A modern pad's face buttons are on or off.

The steering: a stick's byte is turned (0x622F58) with no dead zone of the program's, full lock at the byte's end. Ours reads
the stick finer than a byte, through the engine's race dead zone (0.08 of the travel, the rest rescaled) and its curve (1,
linear). These are the shared engine's defaults, not Gran Turismo 4's. There is no other smoothing or assist of ours. The
lock by speed, the prefilter and the active steering are the program's (physics.md §3.6).

## What is sent back

Once a step, after it (0x354A78 in 0x34DB30), each car's pad block (cond + 0xCA58 + 0x10 k) goes to its controller
(`RaceInput::vf6` 0x426EA0 → the port's virtual +0x38 0x43D520), unless it replays. Each frame the controller's update makes
the stop first (0x43D680): so a paused race, a menu and a replay send it.

- **The pad.** +0 and +1 of the block are the small and the large motor, sent as they are when the pad's "ON" is set (else 0).
  The port packs them for libpad (0x552200): bit 0 of the small (a DualShock 2's small motor is on or off), the large's byte
  beside it. The vibration (0x36CEE8) writes +1 alone, 0 .. 127: **the small motor never runs**.
- **The wheel.** +2 the force (0 clockwise ✔ (a car the computer drives is given 0x80 - its steering / 32, left positive: the
  force that centers), 128 none, 255 counterclockwise), when its "ON" is set: a constant force of 2 x it - 255 of 255
  (0x555C90), gain 90 of 255 ❓. The stop: no force, and the wheel's own centering spring on (both coefficients and
  saturations 128: 0x555AA0). A Cheetah is also given a spring at the block's +8 (off: coefficients 0) and, on a paved course,
  a second condition effect from its +0x0D .. +0x0F (128, 255, 32 ❓ which kind: Logitech's type 0x10). At its connection a
  wheel is swept (0x5555C0: full one way, the other, then still), and a Cheetah set to 900 degrees (its command 4 with 1).

## What runs where

| Part | Runs |
|---|---|
| The settings, their pages' calls, their saved block | Gran Turismo 4 (`input/key_config.*`, `menu/game_controllers.cpp`, `save/game_data.cpp`) |
| Their pages (Options' button configurations, Vibration, Steering) | ours: each shows only a Controller Settings button into our Controls (`app/screen_menu_viewer.cpp`), which reads and writes the same option fields (`app/game_options.*`: the running game's, or the saved game's from the home screen) |
| A port's report into the frame and words (0x43CF90 with 0x43BBC0, 0x43C260, 0x438870, 0x4386F0) | Gran Turismo 4 (`input/race_input.*`): equal to the program's |
| The frame's use | Gran Turismo 4 (`sim/rules_input.*`, physics.md §3.6) |
| The second port | the two-player battle's (`app/screen_split.cpp`): its own device, settings (`*_2p`), frame, vibration and force; without a device the keyboard's second set of keys, for the battle's time |
| The race's gearbox, aids and wheel options | Gran Turismo 4's rules above (`RaceScreen::player_choice`) |
| The devices: SDL's gamepads, wheels, keyboard, hot plug, bindings, dead zone and response curve | the shared engine's input layer (`gt3r::InputManager`) on a virtual DualShock 2 |
| Nitrous on any device (`input/player_controls.*` kNitrousControl) | ours: the engine's extra control 1 (`input/extra_controls.h`), "Nitrous" in Controls' wheel bindings, by default a wheel's R2 button (`joy.button6` on the usual USB wheels: where the Driving Force Pro's own layout has Special); held, it presses the key the player's button configuration gives Special (wheel and console layouts) or Special itself (the modern layout) |
| The devices into a port (`input/player_controls.*`) | ours: a pad without pressures is the console's kind 4 (its buttons pressed or not); the sticks are read finer than a byte; a trigger's travel set to a pedal moves it as an axis; a wheel's axis x its rotation over the original wheel's (200 or 900 degrees) |
| The modern layout (triggers for pedals) | ours, into the same frame (kind 2, the pedals as axes); left for the Key Configuration's keys once they differ from its preset's (the page's OK leaves the preset 0 without a change too; Controls' *Restore the game's layout* puts the presets back) |
| The vibration's level, the wheel's force | Gran Turismo 4's (physics.md §3.6), sent as they are with "Gran Turismo 4 original" chosen: the pad block as the port packs it (bit 0 of +0x00 the small motor, +0x01 the large motor's byte; `input/race_feedback.*` `RaceFeedback::rumble`) |
| The Modern rumble and the Modern wheel force (the default) | the shared engine's (`gt3r::race::RumbleModel`, `force_feedback.*`), fed from Gran Turismo 4's step (`input/race_feedback.*`) |
| The triggers (a DualSense's resistance and buzz, an Xbox pad's impulse triggers) | the shared engine's, from the step's tires |

The Modern rumble's feeds (judged): a hit: the step's hit records (0x350530), |j| over the mass (a car's strength x 15,000);
rough ground: F +0x62C; kerbs: a loaded wheel on the surfaces 0x36CEE8 takes for kerb strips (1, 10, 11, 21); the slide: the
sliding measure (wheel +0xB4); wheelspin and locked wheels: the slip ratios with the pedals. The triggers': the slip ratio
over the tire record's +0x168 x the axle's slip scale (the ABS's target), and the slips over the record's +0x164 and +0xB8
with the friction circle (the throttle's buzz: nothing up to the tire's best slip, then rising slowly with how far past it,
to 0.6 at two and a half times it; under 0.05 nothing); the TCS at work, by the share of the pedal it cuts. Nothing is sent for a car the pad does not drive, as the program's vibration has it;
the program's centering force of a car the computer drives is not sent (safety).

Not done: a Cheetah's condition effect (the engine plays a spring and a constant force), the wheel's own spring while the
race does not drive, the ghost's and best line's buttons (read, nothing
uses them), the wheel's left and right views.
