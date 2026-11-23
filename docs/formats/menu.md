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
options and back, and through making a game into Gran Turismo mode's map, home and garage
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

Other events by name: `onLoad`, `onUnload`, `onKeyRelease`, `onButtonPress`,
`onButtonRelease`, `onEnter`, `onLeave`, `onFocusEnter`, `onFocusLeave` (the widget's slots
66 to 82).

### The focus moves

A page's layout properties (mRootWindow, 0x232E00 for the defaults): `navigate_clip_angle`
(0), `navigate_wraparound` (on), `navigate_start_front` (off), `navigate_slant_move` (off),
`navigate_sound` (`cursor`); also `preload`, `noload`, `bgm`.

The search (0x26EE28, 0x270118; measuring 0x26F0A8, ordering 0x26F2B8):

- A candidate must lie ahead: its middle further along the direction than the focus's.
- In line with the focus (their extents across the direction overlap): angle 0, distance the
  gap from the focus's far edge (its near edge with `navigate_start_front`) plus 1.
- To the side: the angle of the gap across against the distance along, in degrees; it counts
  only below the page's clip angle when one is set; the distance is the straight one.
- Order: valid before not, then a whole-number priority, the smaller angle, the nearer, the
  closer across.
- With wraparound and nothing ahead, the search goes around: every widget is measured as
  10000 further along.
- A widget is measured by its `navigate_target` region when the layout gives one, the
  focused one by its `navigate_source`.

❓ Which widgets are candidates (0x26F9E0) is not read: every visible widget that can take
the focus (`can_focus`, the buttons, option menus, sliders) and is not insensitive.

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

**SliderBar, ScaleBar** (layout `range`, `step`, `popup`, `direction`, `repeat`,
`fix_pinch_size`; `value`, `min`, `max`, `step`, `horizontal`, `scale_direction`), **ListBox**
(`step_min`, `step_max`, `interpolate_ratio`, `magnify_ratio`, `focus_active`, `repeat`,
`alignment`, `h_justify`, `v_justify`), **ScrollWindow** (`interpolate_ratio`, `floating`):
❓ their own routines are not read; they answer the scripts' calls, the list box laid out as
a select box, and a scroll window does not follow the focus yet (Gran Turismo mode's map).

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
❓ A new game's values are not read from the program (0x189A60): plain choices, miles and
horsepower for the US. They are kept as text lines in `<data folder>/gt4r-options.txt`
(`GT4R_DATA_DIR`), written when the scripts apply or save them and when a project ends.

**The memory card** (`menu::MMemoryCardManager`, 0x17AC20): answers as a console with a
formatted card that has no saved game (`card.connected`, `formatted`, `has_game_data`,
`free_kb` choose other answers; `gt4tool menu-run ... card=none|empty|saved`). Saving sets
"has a game" and nothing else: no file is written or read. A saved game goes behind
`GameState`.

**Cars and courses** (`menu::MCarData`, 0x131068; `menu::MCourseData`, 0x1B34D0; the car
object `menu::MCarGarage`): from the disc's car database ([database.md](database.md)) and
the program's lists of makers, tuners, countries and categories, found in the disc's
program file by their content (`program_tables.*`). ❓ Not read: weight, the numbers with
other parts fitted, which cars have no manual gears, the RACE table's columns
(`menu::MRaceData` answers "none" for requirements and prizes), the meaning of
`IsTireNormal` and `IsDirtRunnable`.

**Other objects**: `menu::MSystem`, `MLocale`, `MUnit` (the calendar's order by language,
0x48EE00: ❓ which language has which is not read, the US takes month first), `MUtility`
(`GetFormatString` uses the program's text format, [script.md](script.md)), `MRandom`,
`MCalendar` (❓ the first day's date), `MNetwork`, `MShell`, `MProgress`, `MGpb`
(`context.loadGpb(folder, name)` adds `<folder>/<LANG>/<name>.gpb` to the pictures).

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

Gran Turismo mode beyond the map, home and an empty garage (dealers, events, licenses);
the garage with cars from a saved game; the option pages' effect on anything but the stored
values; Prologue's flows (its main script, configuration and projects load and show a first
page; ❓ which page each project starts on); two-player and LAN paths.

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

❓ The double-byte fonts (`font/*.kf`, `KNJI`: a newer layout than Gran Turismo 3's) and
`font/jis2uni.dat`.
