# Menus: layouts, pictures, text, fonts

Code: `src/gt4r/menu/` (`layout.*`, `image_pack.*`, `text_table.*`, `font.*`, `project.*`;
running: `runtime.*`, `widgets.cpp`, `widget_classes.cpp`, `actors.cpp`, `packing.cpp`,
`game.*`, `game_natives.cpp`, `program_tables.*`); `gt4tool menu`, `menu-run`, `menu-names`,
`db-strings`; the app's *Menu pages* screen (`src/gt4r/app/screen_menu_viewer.cpp`).

The menus are *projects* (`projects/<name>/`): `boot`, `arcade`, `gtmode`, `option`, `quick`,
... (33 on the US disc, 213 pages, 12,713 widgets; 9 projects in Prologue). A project is:

| File | What |
|---|---|
| `<name>.mproject` | The layout: every page and widget, as text |
| `<name>.adc` | Its script ([script.md](script.md)): a module per page and widget with the event functions |
| `<LANG>/<Page>.gpb` | A page's pictures (`US`; Prologue: `JP`, `TW`) |
| `<LANG>/rtext.rt2` | Its text |

Checked on both discs: every layout reads, every picture a widget names is in its page's pack
and decodes, every text key a widget names has its text in the project's table.

## Layout (`.mproject`, `.mwidget`)

Plain text. A widget is `Type { properties }`; a property is `name type {values}` or a list
`name [count] { items }` whose items are values or widgets:

```
Project {
	name string {"BootProject"}
	has_script digit {1}
	children [6] {
		RootWindow {
			name string {"LanguageRoot"}
			children [3] {
				ImageFace { name string {"bg"}  image_path string {"image/boot/lang_bg_4.png"} }
				Composite {
					name string {"Title"}
					geometry rectangle {0 0 640 56}
					children [2] { ColorFace { ... color [1] { RGBA {0 0 0 192} } }  TextFace { ... } }
				}
```

Value types: `digit` (a number, also with a fraction), `string`, `rectangle` (x y w h),
`RGBA`, `vector`, `vector3`, `region`. A project's children are its pages (`RootWindow`).
The widget kinds by count on the US disc: `ImageFace` 2912, `Composite` 2901, `TextFace` 2817,
`ColorFace` 2212, `FrameImageFace` 1076, `VBox` 220, `RootWindow` 215, `OptionMenu` 150,
`ToolTipFace`, `ColorChipFace`, `SliderBar`, `ScrollBox`, `ModelFace`, `HBox`, `SelectBox`,
`InputTextFace`, `ScaleBar`, `Button`, `InputNumberFace`, `CarFace`, `SceneViewFace`, the
scroll windows, `FBox`, `SelectBar`, `MovieFace`, `TextBoxFace`, `FlashFace`, `ListBox`,
`PhotoRenderFace`, `GraphFace`, `SlideShowFace`, `ProgressFace`, `EyetoyFace`, `LoggerFace`,
`RaceCourseMapFace`, `PhotoViewFace`, `PhotoMapWindow`: each is the program's class of that
name with an `m` in front (`mTextFace`).

How the viewer draws the properties (✅ read from the files and matching the pages by eye;
none compared with the program's own code yet):

| Property | Drawn as |
|---|---|
| `geometry` | The widget's rectangle in its parent's; a widget without one fills its parent. The screen is 640 x 480 |
| `children` | Drawn in order over the widget |
| `visible` 0 | Not drawn, nor its children |
| `visible_condition`, `visible_compare` | Drawn when `(state & visible_compare) == visible_condition` (either absent: 0, and the mask is the condition). The state's bits by the older files' `cond_active`, `cond_focus`, `cond_prelight`, `cond_insensitive`: 1 active, 2 focus, 4 prelight, 8 insensitive; a focused widget's faces test 2 or 4 (the runtime sets both) |
| `opacity` | Multiplies the alpha of the widget and its children |
| `clip` 1 | Children are cut to the rectangle |
| `ColorFace` `color` | One colour, or four for the corners clockwise from the top left |
| `ImageFace` `image_path`, `color` | The picture over the rectangle, tinted; `blend` 1 adds |
| `FrameImageFace` `tl_`, `t_`, `tr_`, `l_`, `r_`, `bl_`, `b_`, `br_image_path`, `image_path` | Corners at their own size, sides stretched between, the middle over the rest |
| `TextFace` `text`, or `key` | The text; a key is looked up in the page's text (below) |
| `font` | A font's name (below); `hm_16` when absent or unknown |
| `align` | Where in the rectangle: 0 to 8, rows from the top, columns from the left; the middle when absent |
| `scale_x`, `scale_y`, `text_color`, `shadow`, `shadow_color` | The glyphs' scale and colour; a copy offset by `shadow` behind |

❓ Not drawn: `round`, `rotate`, `scale`, `skew`, `texcoord`, `ModelFace`, `CarFace`,
`MovieFace`, `SceneViewFace`, the tool tips. A page on its own is drawn with the rectangles
the layout gives; the boxes' packing, the focus, navigation and the actors are part of
running the menus (below).

## Running (`runtime.*`, `widgets.cpp`, `widget_classes.cpp`, `actors.cpp`, `packing.cpp`)

The program's menu system as the scripts drive it: from power on the game's own scripts run
to the title page, through Arcade to the point where a race is asked for, through the
options and back, through making a game into Gran Turismo mode's map, home and garage, and
through a dealer to a bought car in the garage
(`tests/test_menu_run.cpp`; *Menu pages* in the app, `GT4R_MENU="boot=1"`;
`gt4tool menu-run <disc> boot settle left cross ...`). What the program has that is not
written is a stand-in that records the call (`gt4tool menu-run ... stubs` lists them).

Addresses are the US disc's program (SCUS-97328, v1.01). A line marked ❓ is judged from how
the scripts use the thing, or by eye, and is not read from the program.

### From power on

1. The main script `/projects/GT4Application` runs (loaded at 0x10AFC8). It makes the world's
   objects (`main::game`, `main::sound`, ...) and reads the configuration script whose name
   the program gives it: `config-main` (0x43A4F0); Prologue's disc has `config-prologue`
   (❓ taken by which file the disc has). The configuration names the first project
   (`StartProject`) and the title's (`TopProject`).
2. A project runs (0x109D00, `/projects/<name>/<name>`): each widget of the layout is a module
   of the script world named as in the layout
   (`ArcadeProject::TopRoot::StartScroll::SelectBox`), its children members, its properties
   attributes. The project's script adds the event functions to those modules. `Module.load`
   names files in any case and from the top (`/projects/arcade/us_carlist`).
3. `onLoad(context)` is called on every widget that has one, the children before their
   parent and the project last, each with the same context (mComposite, 0x207010). The
   project's `onLoad` opens the first page (`context.startPage`).
4. A page that opens gets `onInitialize(context)` (mRootWindow's slot 90), one that closes
   `onFinalize(context)` (slot 91).
5. `context.finish()` ends the project. `main::game.next_menu_project` names the next one,
   or `main::game.next_sequence` a race (the program's sequence names, table at 0x6A53D8:
   `boot`, `menu`, `single_race`, `time_trial`, ...).

The common project `cursor` (the cursor, `ConfirmDialog`, `ProcessDialog`, `ProgressDialog`)
is loaded beside every project (`context.getCommonPage(name)`, `getCursorProject()`).

### Scripts wait

The program runs a project's script on a thread of its own. `context.sync(seconds)` holds
it for seconds x 60 pictures, one when no time is given (0x22C138, 0x24F748, the wait for the
next picture at 0x4A22C0). Here a call is a task that keeps its frames across the wait
([script.md](script.md)), and one *event thread* runs them:

- Keys, `context.pushEvent(...)` events (`menu::MScriptEvent(context, module, function,
  arguments)`, `menu::MFunctionEvent(function, arguments...)`) and the runtime's own calls
  queue; the next is taken when the function before it has returned.
- `context.getUpdateContext().enterEventLoop()` runs the queue from inside a function until
  `leaveEventLoop()` (the dialogs: a function opens the dialog, waits there, and goes on with
  the answer).
- `context.startPage(page)` with a transition set waits for the transition's first half.
- A watcher (`menu::MScriptWatcher(function, interval)`, 0x2CEB78: calls
  `function(context)` and counts) is called when the thread is idle and no event waits.
  ❓ The program's order between watchers and events is not read.

### Events and keys

An event goes to a widget's function by name (mWidget, 0x265C80); the function's result is
0 "go on", 2 "filtered", anything else "stop". An event not stopped goes on to the widget's
parents up to the page (0x28E858), and the page starts from its focused widget (0x235A20).

A key press (mRootWindow, 0x235680): the page's `onKeyPress(context, event)` chain first
(`event.keysym`); then

| Key | What |
|---|---|
| Accept (0xFF0D) | `onActivate(context, event)` from the focused widget up; nothing without a focus |
| Cancel (0xFF1B) | `onCancel(context, event, item)` from the focused widget, or the page |
| The directions (0xFF51 to 0xFF54, diagonals 0xFEE4 to 0xFEE7) | The focus moves (below); the page's `navigate_sound` plays (default `cursor`) |

The pad's buttons as key numbers (table at 0x6192A0): up 0xFF52, down 0xFF54, left 0xFF51,
right 0xFF53, accept 0xFF0D, cancel 0xFF1B, start 0xFF8D, select 0xFF63, and 0xFFC8, 0xFFC9,
0xFFD2, 0xFFD3, 0xFFFF, 0xFF13, 0xFF61 for the pad masks 0x100, 0x200, 0x800, 0x1000, 0x40000,
0x80000, 0x100000. ❓ Which buttons those masks are is not read: taken as L1, R1, L2, R2 for
the first four.

`onActivate` and `onCancel` get a third argument, the widget whose function it is (a page's
`object.getFocusedWidget()`; ❓ from the scripts' use).

Other events by name: `onLoad`, `onUnload`, `onKeyRelease`, `onButtonPress`,
`onButtonRelease`, `onEnter`, `onLeave`, `onFocusEnter`, `onFocusLeave` (the widget's slots
66 to 82).

### The focus moves

A page's layout properties (mRootWindow, 0x232E00 for the defaults): `navigate_clip_angle`
(0), `navigate_wraparound` (on), `navigate_start_front` (off), `navigate_slant_move` (off),
`navigate_sound` (`cursor`); also `preload`, `noload`, `bgm`.

**Candidates** (the collector 0x26F9E0): a widget that is shown (its visible flag and its
visible condition against its state, 0x265DC8), can take the focus (`can_focus`) and is not
insensitive is a candidate, and **what is inside it is not searched**. Any other composite is
searched into when it is shown and not insensitive; a list box never is. In a direction
search the candidates are cut to the clips they are under (a composite with `clip`), and one
cut away entirely is no candidate; the scroll window's `space` that holds the focus is pushed
as a clip of its own and its widgets are group 1 (searched first), so inside a scroll window
every place counts, also those scrolled out of view.

**Regions**: `navigate_source` / `navigate_target` (`navigate_box`) are corners x0 y0 x1 y1
from the widget's top left (0x262668, 0x2626A0), kept as fractions of the widget's size
(0x268478); without one, the widget's rectangle.

**The search** (0x26EE28, 0x270118; measuring 0x26F0A8, ordering 0x26F2B8): every rectangle
is turned so the key's direction is "along" (the four edges' middles turned, their box).
The focused entry is the topmost widget above the focus that can take it; F is its source, S
and T a candidate's source and target.

- Ahead: the middle of S further along than the middle of F.
- From F's back edge (its front edge with `navigate_start_front`): `a = T.x0 - edge + 1`,
  `b = T.x1 - edge + 1`. In line (T and F overlap across): angle 0, distance the smaller of
  those not negative. To the side: the one of smaller size, the angle of the gap across to it
  in degrees, the straight distance; valid only when the angle is **below the clip angle**:
  with the default 0 nothing to the side is ever taken.
- Order: valid before not; the larger group; the smaller angle; the nearer; the closer across
  (S against F); then the one made first.
- Nothing valid and wraparound on: everything is measured again 10,000 further along.

A direction key is the page's whether or not the focus moved; `navigate_sound` plays on a
move. Gran Turismo mode's map: from Home right is ExtremeHall, left GTAuto, up DealerItaly,
down License.

**A page's first focus** (0x234D60): a focus kept from when the page closed, else the
script's `setFocus` in `onInitialize`, else the "next" search (0x26EEF8): the candidates by
their parents' places (y, then x), then their own, then the order they were made in.

### Widgets

The classes (the program's registration tables; `M` before the layout's name): mWidget is the
base of the faces; mComposite of mBox, which is the base of mDBox (mHBox, mVBox), mFBox
(mOptionMenu, mRootWindow, mScaleBar, mSliderBar) and mMBox; mScrollable of mListBox,
mSelectBar, mSelectBox, mTextBoxFace.

Any widget: its layout properties as attributes; `doCopy()`, `doInitialize(context)`,
`appendChild`, `insertChild`, `removeChild`, `replaceChild`, `clearChildren`, `clearWindow`,
`countChild`, `getChild`, `movetoLast`, `first`, `last`, `next`, `prev`, `name` (assignable),
`actor`, `interpolateX/Y`. `widget["child"]` is nil when the widget has nothing of that name.

`interpolate` (0x25BFE0): a value within 1 of its target arrives; otherwise it moves by the
distance x the ratio.

Boxes (mDBox: the size asked 0x2E9880, the places given 0x2E99E0): children one after
another with `spacing`, the box's `border_width`, `border_height`, `pad_*`; a child that
expands (`x_expand`, `y_expand`) shares the room left, one that fills (`x_fill`, `y_fill`)
takes its share, `pack_end` packs from the far end. ❓ Written in a reduced form
(`packing.cpp`): windows with `packing` and widgets with `is_face` 0 are placed, a text takes
its natural size, faces with a rectangle follow the box as it grows, expanding children are
centered.

**SelectBox, SelectBar** (0x2D8C90: ratio 0.2, magnify 1, item 16 x 16, no repeat): the first
child appended gives the rows their size (0x2DB8B8); rows lie along `direction` with
`spacing`, across at `border_width` (vertical) or against the far edge less the border
(horizontal) (0x2DAA10); the list slides so the chosen row is at the box's start, by the
ratio each picture (0x2DA2B0, 0x2DA6F0). A key along the box moves the index and is
"filtered"; at an end without `repeat` the box's `onValueDisabled` is called and the key goes
on (0x2DAD18). A change plays the sound and calls `onValueChanged(context, index)`
(0x2DAB50). Layout: `magnify_ratio`, `alignment`, `repeat`. Functions: `index`, `incIndex`,
`decIndex`, `moveIndex`, `getItem`, `getFocusedWidget`, `Count`, `callFocus`, `outFocus`,
`insertSortChild`, `sort`.

**ScrollBox** (0x2CF868): children named `pinch`, `inc_arrow`, `dec_arrow` beside its
scrolling child.

**OptionMenu** (`index`, `setIndex`, `getItem`, `Count`, `applyAppend`, `setInsensitive`):
the rows are the children of `popup::vbox`; accept opens the list with the focus on the
chosen row, accept on a row chooses it (`onValueChanged`), cancel closes. ❓ The list's
place, the keys and `selected_item` (here the chosen row) are from the scripts and by eye.

**ScrollWindow** (0x2D2720; layout `interpolate_ratio`, 0.3 when absent, and `floating`):
its child `clip` shows a part of `clip::space`. The space wants to be where the page's
focused widget is in the middle of the clip, kept against the clip's edges, and 0 along a
direction it does not exceed the clip in (0x2D3430); each picture it moves the ratio of the
way there (0x2D36A0); `warp(widget)` puts it there at once (0x2D2918). Gran Turismo mode's
map is one. ❓ `floating` (the space follows a pointer) is not written; `updateGeometry` makes
the space as tall as what is in it, from the scripts' use.

**SliderBar, ScaleBar** (layout `range`, `step`, `popup`, `direction`, `repeat`,
`fix_pinch_size`; `value`, `min`, `max`, `step`, `horizontal`, `scale_direction`), **ListBox**
(`step_min`, `step_max`, `interpolate_ratio`, `magnify_ratio`, `focus_active`, `repeat`,
`alignment`, `h_justify`, `v_justify`): ❓ their own routines are not read; they answer the
scripts' calls, the list box laid out as a select box.

Faces: `TextFace` (`getTextSize`, `abbreviate`, `adjustScale`: the text made narrower to fit
its rectangle), `ImageFace` (`adjustSize`), `InputTextFace` (`putString`, `backspace`,
`max_input_length`), `ColorChipFace.setData`, `CarFace` (`car_data`, `car_model`,
`setColorIndex`; not drawn), `ToolTipFace`. ❓ From the scripts' use.

### Actors and transitions

A widget's `actor` is stepped each picture.

- **MFadeActor** (0x20B520, 0x20B650, update 0x20C478): from 1 to 0 at 1/30 a picture until
  told otherwise; `period` sets the step to the distance / (seconds x 60) (0x20C2B0), `out`
  turns it round (0x20C300), `warp()` arrives at once (0x20C3E0), `over`, `src_over`,
  `dest_over` hold it past an end. It sets the widget's opacity (0 to 1), or a `ColorFace`'s
  corner alphas with `slope`.
- **MMoveActor** (update 0x2C1960): each of its four values moves by what is left x `ratio`;
  `linear`: by the whole distance x ratio; `repeat`, `flip`.
- **MBlinkActor** (0x278E98, update 0x279908, period 0x279870): 0 to 1 and back at 1/30 a
  picture, repeating.
- ❓ `MRotateActor`, `MChaseActor`, `MMagnifyActor`, `MSwitchActor`, `MTextActor`,
  `MButtonActor`, `MAnchorActor` keep what the scripts set and do nothing.
- **Transitions** (mTransition, 0x24D1B8 on): `panOut`, `panIn`, `panOutIn` start the halves,
  `syncOut`, `syncWait`, `syncIn` wait for them. `MColorTransition` (0x286EA0, update
  0x287348) covers the screen with its color by 1/15 a picture. ❓ `MCrossTransition` and
  `MBlockTransition` are drawn as the color one.

### Sound

Every call of the scripts on `main::sound` and on a music object arrives in one place,
`Runtime::sound(object, method, arguments)` with the names and arguments as the scripts gave
them (`sound.play("ok")`, `sound.load("ARCADE")`, `sound.fadeOutMusic(...)`): it is written
to the log, kept in `Runtime::sound_calls()` and given to `Runtime::on_sound` when one is
set. Nothing is played.

### The game's state (`game.h`, `game_natives.cpp`)

`main::game` and what hangs off it are views of one plain structure (`GameState`): where the
menus go next, the race being set up, the options, the credits, the garage, the calendar's
day, the player's name, what Arcade has unlocked, and the memory card as the menus see it.

**A race request.** When a project finishes with `next_sequence` naming a race,
`Runtime::race_request()` gives what the scripts set (`RaceRequest`): the sequence
(`single_race`, `time_trial`, ...), the course and car by their labels, the paint's index,
the event of the car database (`race`: Arcade gives `sample_entry_6`), laps, difficulty
(`EASY`, `NORMAL`, `HARD`), the opponents' level, the cars on the grid, the driving aids,
manual gears, the tire, the second player's, and the opponents when the scripts chose them.

**Options** (`main::game.option`, MOption's attributes, registered at 0x189E38). Numbers and
names; the names are the ones the option pages' scripts list (`sound_type` `MONOPHONIC`,
`STEREO`, `PROLOGIC2`; `penalty_type` `NONE`, `SPEED_LIMITATION`, `PASSING_PITLANE`;
`unit_velocity` `KMPH`, `MPH`; `unit_power` `PS`, `HP`, `BHP`, `CH`, `KW`, `CV`, `PF`;
`unit_torque` `KGFM`, `FTLB`, `NM`; `race_display_mode` `normal`, `480p`, `1080i`; ...).
`game_zone` is a zone's name (`UNDEF`, `JP`, `US`, `UK`, `EU`, `KR`; 0x622DA8), set from the
program's region by `setGameZoneDefault` (0x435D48: us US, uk EU, kr KR, else JP). The options
that are numbers are saved in the game data (save.md); ❓ the named ones' numbers and a new
game's values of the named ones are not read. They are also kept as text lines in
`<data folder>/gt4r-options.txt`.

**The program's start** ends with the configuration's `last_strap()` (0x108BF8 -> 0x10B268):
the zone, the credits a new game has (10,000), the music play lists. A loaded game's credits
then come from the garage file.

**The memory card** (`menu::MMemoryCardManager`, 0x17AC20): the card in slot 1 is the folder
`<data folder>/memorycard/`, holding the game's own files (save.md): `saveNew`, `save`, `load`,
`isExist`, `readSize`, `remove` and the file's `size` are real; a game survives a restart and
the boot project loads it. Without a data folder a stand-in answers (`GameState::card`:
`connected`, `formatted`, `has_game_data`; `gt4tool menu-run ... card=none|empty|saved`).
❓ `loadInstance` (a second game beside the one played, for trading cars) answers its result
only; the probes of earlier games' saves answer none.

**The game's state** (`GameState`) is the program's `Status` as it is saved: the player's
counters, the calendar and its diary, the garage's list with each driven car's record, the
records, the presents, the used cars sold, the favorites, the championship left. The natives
over it are the program's, with their addresses in save.md and here:

- **MGame**: `money` is the money text ("10,000"; "--" for nothing, 0x42E6B8), `incMoney` has
  no cap (the scripts keep theirs); `getScore` with commas; `getTotalRankCount(i)`;
  `getAspecDistance` in the player's unit; `menuRef(n)` the name n places below the top of the
  menu stack; `saveMenuStack` / `loadMenuStack` (❓ the stack's saved form is not read: the
  names, each ended by a 0).
- **MGarage** (0x16DA58 on): places that are never packed; `addNewCar(label, paint)` a car as
  sold on today's date, `addUsedCar(car)` / `addCar(car)` the car's record; `delCar` clears the
  place (1 when it was the car driven), `undelCar` puts it back; `riding_car` is an object on
  the garage's own record. The program reads and writes a car's slot of the garage file in
  `changeRidingCar`, `pushRidingCar`, `popGarageCar`; here the records are in the state (they
  answer `OK`). ❓ `setRidingCar(place, car)` and `cancelRidingCar(place, car)`, and what
  `pickup`, `pickupCountry`, `pickupTuner` compare, are from the scripts' use.
- **MCarGarage** (a car; 0x141B90, 0x14EF00; `game_car.cpp`): its record's three sheets, `slot`
  the sheet in use; the parts that fit it (its file `specdb/<set>/CARS/<the car's code>`: u64
  count, then {u64 part, u8 category, 7 bytes}); a parts type is the table id, type 31 the
  body's stiffness work (Cr. 30,000); a category the stage, a byte of the part's row; the
  price a column of the row (`getPartsPrice`); `getParts` marks one owned (96 bits), `setParts`
  fits it with its settings at the row's defaults, on the sheet in use, or on all three for
  the parts that cannot be undone (lightweight, port polish, balancing, displacement, wheels,
  wings, stiffness); the settings pages' ranges and values (`getSuspensionRange`, ...); the
  current figures (`updateCurrentStatus`: the spec of the sheet in use, `sim::build_spec`, with
  the engine's wear and oil: a new engine 97 %, run in 100 %, old 95 %; fresh oil 105 %, old
  95 %); wheels and wings by label; the condition (odometer, oil, dirt, body); `makeUsed(seed,
  paint, meters)` (the program's random numbers; meters by model year); used prices by the
  thousands of km driven (85 % below 1,000 km ... 10 % from 100,000); the sorts, one key each
  (power, weight, kg/PS, date, order of driving, aspiration, name, price, year, country,
  tuner, drivetrain, the paint's hue). ❓ The gears' ranges (0x3553A0) and the charts' points
  (0x148940, 0x148B60) are not written.
- **MRaceData** (0x1A0F80 on; `game_race.cpp`): the RACE row's columns (database.md "RACE"):
  the medal times as text, entries, the course, the opponents' pool (`car_code(i)`), the
  requirements (license, drivetrain, aspiration, car type, tires, power, weight, length,
  country, model years), the prizes (x 100), the championship's points (10, 6, 4, 3, 2, 1),
  the allowed cars, laps, time limit.
- **MCalendar**, **MRaceRecord** / **MLicenseRecord** / **MCourseRecord** and their units,
  **MPresent**, **MUsedCar**, **MFavorite** (`game_records.cpp`): as save.md has their blocks;
  `getEvent(y, m, d)` gives a past day as an array by its kind; the license predicates
  (`isLicenseAllBronze`, ...) and `isMissionClear` test the units of `"%s%04d"`.
- **MRandom** (0x2CB888): the program's generator, its state from 0x12345678; with a third
  argument one draw from that seed. ❓ The float made of a draw (0x578598) is as read.
- **MColorObject.interpolate(a, b, t)** (0x2039E0): a x t + b x (1 - t).

**A race request.** When a project finishes with `next_sequence` naming a race,
`Runtime::race_request()` gives what the scripts set (`RaceRequest`), with what the program
fills in: an event's course from its RACE row (labels tried as they are, then with "us" and
"jp" in front), the car driven (`garage_slot`) when the scripts name none, and a license
test's or a mission's car, the first of the race's pool.

**After a race** (`Runtime::race_finished(RaceResult)`, `game_race.cpp`): what the program's
result routines write (0x114498 an event; 0x1105A8 a license test; 0x11C328 a mission;
0x115A90 on a course's run): an event finished in place p pays the row's prize of p (answered
for the scripts' `prize`; the money is the scripts' `incMoney`), counts the place and the
prize, and updates the race's unit ("event", p: a better place is kept); one not finished
counts as such; a test keeps its result in its unit and its ranking (time, result, the real
date, the pass code); every run is a day of the diary. ❓ The car's distance is added to its
odometer and wear (the adders 0x343518 on exist; who calls them is not read).

**Cars and courses** (`menu::MCarData`, 0x131068; `menu::MCourseData`, 0x1B34D0): from the
disc's car database ([database.md](database.md)) and the program's lists of makers, tuners,
countries and categories, found in the disc's program file by their content
(`program_tables.*`).

**Other objects**: `menu::MSystem`, `MLocale`, `MUnit` (the calendar's order by language,
0x48EE00: year first for JP, KR, TW), `MUtility` (`GetMoneyString`, `GetTimeString` as the
program's text, `GetPriceMagnification` 100 for "jp"), `MNetwork`, `MShell`, `MProgress`,
`MGpb` (`context.loadGpb(folder, name)` adds `<folder>/<LANG>/<name>.gpb` to the pictures).

A project's script names files of the disc's top beside its own (gtmode's
`usedcar/US_used0000` is `usedcar/US_used0000.adc`; ❓ the program's search is not read).

### Drawn in the app

A running page is drawn from the live widgets with the rules of the table above, and:
a text is wrapped to its rectangle unless a box places it, made narrower when the script
asked (`adjustScale`), or cut with an ellipsis (`abbreviate`); `%%` in a text is a percent
sign, `%B.` and `%C...` marks are left out (❓ from the scripts' texts); a key is looked up in
the page the layout has the widget in, wherever the scripts copy it; an open option list is
drawn over the page; the cursor is the common project's `CursorRoot` at the focused widget
(❓ its place by eye); a transition covers the screen with its color.

❓ Not drawn: the 3D faces (`CarFace`, `ModelFace`, `SceneViewFace`), movies, Japanese text
(Prologue's pages show their pictures and Latin text).

### Not done

Gran Turismo mode: the places run to their pages and to the race asked for (used cars, events,
license tests, missions, GT Auto, tune shops, the garage, the status, the diary, the car's
settings); not checked page by page: trading cars between saves, the presents' pages after
a race, the championship's sessions, the photo travel (its page stops on an actor that is not
made), the B-spec driver. The option pages' effect on anything but the stored values;
Prologue's flows (its main script, configuration and projects load and show a first page; ❓
which page each project starts on); two-player and LAN paths.

## Pictures (`.gpb`)

```
"gpb2": 8 zero bytes, u32 files; each {u32 name, u32 data, u32 bytes, u32 0}
"gpb1": 8 zero bytes, u32 files; each {u32 name, u32 data}
"gpb0" (Prologue): u32 files; each {u32 name, u32 data}
```

Names are NUL-ended paths as the layout writes them (`image/boot/lang_bg_4.png`). A file is a
`Tex1` texture set (one picture), often packed like the volume's files (`C5 EE F7 FF`); a few
are models (`MDLS`), motions (`Mot1`) and `STRB` files.

## Text (`.rt2`)

```
"RT03", 8 zero bytes, u32 pages          ("RT02", Prologue: u32 pages right after the magic)
each page: u32 name, u32 pairs, u32 pair table, 4 filler bytes ("^^^^")
each pair: u32 key, u32 text
```

All NUL-ended UTF-8, pages and keys in byte order. A page is named for a menu page
(`BootProject::LanguageRoot`) or a subject (`CourseName`, `MemoryCard`); a widget's `key` is
looked up in its own page's. `rtext/<LANG>.rt2` is the common table, `rtext/race<LANG>.rt2`
the race's. Text can hold `%d`, `%s` for the script's `format`, colour marks (`%Cfff`), and
line ends `\r\n`.

❓ `rtext/US.rtt` (`RT01`, a leftover of an older layout).

## Fonts (`font/*.fnt`, `FT01`)

Gran Turismo 3's font file with one change (Prologue's files are still Gran Turismo 3's
layout: u16 at 0x12 is 0):

```
0x10 u16 fonts, u16 layout (1)
0x14 u32 font offsets, 0x18 u32 texture set offsets (a set per font: 4-bit, two palettes)
font:  u32 name, u32 glyphs, u16 first code, u16 last code, u8 size, u8 line height, s8 ascent,
       u8 profile rows, then the double-byte font's cell (u8 w, u8 h, s8 x, s8 y)
glyph (20 bytes; Gran Turismo 3: 16): s8 left, s8 top, u8 width, u8 height, u8 advance, 3 unused,
       u16 x, u16 y in the picture, u32 left profile, u32 right profile
```

Width and height 0xFF: no picture (the space). Codes are Unicode (0x20 to 0x17F in the text
fonts). `font/system.fnt`: `hm_16` (the menus' body text), `hb_24`, `hc_20`, `hn_14`,
`hn_20R`, `FC_26`, `FC_48`, `Gaiji`, `GT4meter`, `GT4oddmeter`; `font/menu.fnt`: `tr_30`
(titles); `font/race.fnt`: the race display's. The program names them as Gran Turismo 3 does
(`hb_24#1*130,130`: palette 1, scaled 130%). The glyphs are laid out by the shared engine's
text layout (kerning by the profiles), ❓ assumed unchanged from Gran Turismo 3.

## Double-byte fonts (`font/*.kf`, `KNJI`)

Code: `src/gt4r/menu/kanji_font.*`, `tests/test_kanji_font.cpp`. Gran Turismo 3's kanji font
file ([engine/gt3r/docs/formats/text.md](../../engine/gt3r/docs/formats/text.md) "Kanji
fonts") with the cells indexed by **Unicode code point** (the text is UTF-8) in place of a JIS
code:

```
0x00 "KNJI"   0x0C u32 0, or 0x80 in the files of two faces
0x10 u16 faces, u16 palettes (two per face)
0x14 u32 offset of the faces' offsets (u32 each)   0x18 u32 offset of the palettes
palette: 16 x RGBA, alpha as the GS has it (0x80 opaque)
face, 0x18 bytes:
  +0x00 u16 first code point, u16 last
  +0x04 u32 offset of the cells   +0x08 u32 bytes from one cell to the next
  +0x0C u16 cell width, u16 cell height
  +0x10 u32 offset of the index: u16 cell per code point first..last
  +0x14 u32 offset of the face's name
```

A cell is 4 bits a pixel, row by row, low nibble first, (width + 1) / 2 bytes a row. A code
point without a cell has index 0xFFFF; in the files of two faces it has index 0, a blank cell.
Even palettes are the outlined look (light grays, a dark edge), odd ones white on a ramp of
alpha, as in the Latin fonts' two texture variants.

| File | Faces | Cells |
|---|---|---|
| `unicode.kf` (both discs) | 22 x 22 `medium` (Prologue: `ShinGoM`), U+0391..U+FFE5 | 7,381 |
| `big5.kf` (both), `hangul.kf` | 22 x 22 `DFHeiBold-B5`, `AsiaNew` | |
| `jp_menu.kf`, `jp_race.kf` | 16 x 16 `medium` and 22 x 22 `bold` | 7,260 + 1,412 in `jp_menu` |
| `cn_`, `kr_`, `tw_` `menu.kf` / `race.kf` | 22 x 22 `medium`: the characters that language's menus or race display use | |
| `europe.kf` | 22 x 22 `ShinGo-M`: three cells (U+201C..U+25A0) | 3 |

All 14 files of the two discs read (35,892 and 13,174 cells). ASCII and the Latin letters are
not in these fonts: a line of text takes them from the `FT01` fonts. The `FT01` font record's
"double-byte font's cell" places a cell of these fonts on that font's line.

❓ Which file each language's menus load (the program names `jp_menu.kf` to `tw_race.kf` in
one routine, 0x108D80) and `font/jis2uni.dat` (Gran Turismo 3's `UTBL` table).
