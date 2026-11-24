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
| `clip` 1 | Children are cut to the rectangle. The boxes (`Box`, `HBox`, `VBox`, `FBox`, `MBox`), `RootWindow` (0x232E00) and `SelectBox` (0x2D8C90: the event halls' lists) cut by default (mBox's constructor 0x200C08 sets it, 0x206858); `OptionMenu` (0x2C7498), `ScaleBar` (0x236860) and `SliderBar` (0x2DD820), boxes too, turn it off again; other composites do not (0x204F10): the layouts write `clip 0` on boxes and `clip 1` on the others |
| `ColorFace` `color` | One colour, or four for the corners clockwise from the top left |
| `ImageFace` `image_path`, `color` | The picture over the rectangle, tinted; `blend` 1 adds |
| `FrameImageFace` `tl_`, `t_`, `tr_`, `l_`, `r_`, `bl_`, `b_`, `br_image_path`, `image_path` | Corners at their own size, sides stretched between, the middle over the rest |
| `TextFace` `text`, or `key` | The text; a key is looked up in the page's text (below) |
| `font` | A font's name (below); `hn_14` when absent (the text style's default, 0x246F58); ❓ `hm_16` for a name the files do not have (`sw_20`, `sw_12`) |
| `align` | Where in the rectangle: 0 to 8, rows from the top, columns from the left; the middle when absent |
| `scale_x`, `scale_y`, `text_color`, `shadow`, `shadow_color` | The glyphs' scale and colour (212, 212, 212 when absent); a copy offset by `shadow` behind |
| `translate`, `rotate`, `scale`, `skew` | The widget's transform, carried to its children (below) |
| `ColorFace` `round` | With one colour: the corners rounded to that radius (at most half a side), 8 steps each (0x2857C0) |
| `ImageFace` `texcoord` | The picture's coordinates of the four corners, clockwise from the top left; `from_disk` 1: `image_path` is a Tex1 file of the disc (`piece/nation_bg/japan.img`) |

**The widget's transform** (mWidget, rebuilt by 0x2679B0; the operations 0x24BEB0 translate,
0x24BEF8 rotate, 0x24BE78 scale, 0x24BFA0 / 0x24C000 skew): about the middle of the widget's
rectangle a point's x moves by y tan(`skew` x), its y by x tan(`skew` y), it is scaled by
`scale`, turned clockwise on the screen by `rotate` degrees, put back, then moved by
`translate`. It multiplies the parent's (the draw stack, 0x231508), so a box's children turn
with it; the layouts' italics are `skew {-16 0}` and the like, the graphs' `scale {1 -1}`. The
cursor's target and the focus search keep using the plain rectangles. ❓ A clip under a
transform is the box around the turned rectangle.

A page on its own is drawn with the rectangles the layout gives; the boxes' packing, the focus,
navigation and the actors are part of running the menus (below).

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
- A watcher (`menu::MScriptWatcher(function, interval)`, 0x2CEB78: calls `function(context)`
  and counts) is stepped once a turn of the event loop (0x251CE8, 0x250E08): every queued
  event first, then the watchers in the order they were appended (`interval` <= 0 every turn,
  else every interval-th), then the wait for the next picture. A call waiting in `sync` holds
  the loop, so no watcher runs or counts meanwhile; inside `enterEventLoop` they do. What a
  watcher queues is dispatched on the next turn.

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

`setFocus` changes only the page's focus reference; each picture the page's focus is
compared with the one its last picture had, and an `onFocusLeave` for the old and an
`onFocusEnter` for the new are queued (0x22FBE0): they run the picture after, behind the
events already queued.

`onActivate` and `onCancel` get a third argument, the widget whose function it is (a page's
`object.getFocusedWidget()`; ❓ from the scripts' use).

Other events by name: `onLoad`, `onUnload`, `onKeyRelease`, `onButtonPress`,
`onButtonRelease`, `onEnter`, `onLeave`, `onFocusEnter`, `onFocusLeave` (the widget's slots
66 to 82).

The cursor's crossings (0x22FFD0): the render context keeps the widget the cursor aims at
(+0x88) and the one it last went into (+0x84); when they differ, an `mLeaveEvent` goes to the
old (`onLeave`) and an `mEnterEvent` to the new (`onEnter`, mWidget slot 76, 0x264928). The
cursor aims at the shown page's focus, so these follow the focus: the Gran Turismo mode map's
places spin and show their speech-bubble notices on `onEnter`.

A text face's `value` is what it shows: the value set, else its text, else its key's text in
the page the layout has it in (the memory card pages hand `StoreHouse::nocard.value` to their
dialogs).

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

Boxes (`packing.cpp`) are packed **every picture** on every page, not on a change: the size request bottom
up as part of a composite's update, the allocation top down as part of its draw. A child takes
part when it is no face (`is_face` 0; the `...Face` classes are faces), is visible and its
`packing` equals the box's. What a child asks (0x263B80): its size, or with `packing` its
request on the axes it allocates (`pack_allocate_x/y`, on), at least `minimum_width/height`;
a text its measured size. **HBox / VBox** (mDBox, 0x2E9880, 0x2E99E0): one after another with
`spacing` (between two or more), the pads (`border_width` / `border_height` set both of an
axis, `pad_*` one side); the room left shared by the expanding children (`pack_expand_*`,
`x_expand`), a filling one (`pack_fill_*`) takes its share, else it sits in its middle;
`pack_end` / `pack_reverse` from the far end. **FBox** (also a page, ScaleBar, OptionMenu,
SliderBar): each child by itself; an expanding one is put at the pad and filled or centered.
**MBox**: a grid of `column` columns (2), `column_spacing`, `row_spacing`, columns and rows
expanding when a child of theirs does. Any other composite gives each packing child the size
it asks for. A face keeps its rectangle as fractions of its parent's (taken before any
packing), so it follows a box that grows. Without a rectangle a window has the prototype's
size (a page 640 x 480, a window 128 x 128). Packing the gtmode, setting, cursor and
quick-event pages gives the rectangles their files were saved with, every one.

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

**SliderBar** (0x2DD820): a whole `index` in [min, max] (`range` sets both and the display
ends; writing `min` passes the counter's end as it is, so max goes one up; `index =` clamps,
no event); the layout's `repeat` wraps at the ends, `step` per key, `direction` 0 left and
right. **ScaleBar** (0x236860): a number `value` in [min, max] (writing `min` / `max` also sets
the display ends and does not clamp the value; `index` the value rounded half away from zero),
`step` a number the scripts change while a key is held (`repeat_start`, `onStepChanged`).
With a popup (SliderBar: the layout's `popup`; ScaleBar: a child named `popup`, made
invisible at load) accept opens it (sound `cursor`, the cursor hidden) and only then do the
keys along the bar move the value (sound `param`); while open every other key is the bar's;
accept closes keeping the value, cancel puts the old one back and tells the script again.
Without a popup the keys move it directly; at an end the key goes on to the page. SliderBar
calls `onValueChanged(context, index)` at once (with `has_script`); ScaleBar queues
`onValueChanged(context, bar)` and `onStepChanged(context, bar)` (also at focus enter and a
fresh press). The pinch slides after the value (0.4 a picture, 0.2 for the ScaleBar) between
the display ends, in the popup while open, else in the bar. `incIndex` / `decIndex` move by
their argument and send nothing. ❓ A ScaleBar's own `onKeyPress` runs before the value moves
in the program; here after. **ListBox** (`step_min`, `step_max`, `interpolate_ratio`,
`magnify_ratio`, `focus_active`, `repeat`, `alignment`, `h_justify`, `v_justify`): ❓ its own
routines are not read; it answers the scripts' calls, laid out as a select box. Its rows are made
from the template (`setItemTemplate`, `setItemCount`); the rows that come into view are given to
`visible_callback(context, [indexes])` (`getItemVisible(i)`: the row is in view), then every row
in view whose update flag is set to `update_callback(context, [indexes])` (labomode's and
list_box's scripts set the flag until a row is drawn in full; ❓ from their use).

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
  picture, repeating. It sets the face's own alpha (mWidget slot 58: a colour, picture, frame
  or text face's colours; a composite has none and stays as it is), not the opacity.
- **MRotateActor**(widget, destination, cw) (0x2CCBE8, update 0x2CDCE0): the widget's `rotate`
  toward `destination` (`out` on) or 0 by `ratio` (0.2) of the way the way round `cw` says,
  kept in 0..360; with `velocity` set, velocity / 60 degrees a picture. `warp`, `doFlip`.
- **MChaseActor** (0x2841C0, update 0x284270): hops the widget from where it stood: a phase
  of 360 / (`period` x 60) degrees a picture (period 8.5; the fraction dropped each picture),
  y less |sin| x `floating_range_y` (16), x plus sin x `floating_range_x` (0); `doStart`,
  `doStop` (back to its place).
- **MAnchorActor**(widget, anchor, follow_x, follow_y) (0x278630, update 0x2786D0): keeps the
  distance it had from the anchor along the axes it follows.
- **MMagnifyActor** (update 0x2BA2B0): the rectangle grown about its middle by s, s + 1/60 a
  picture (the script's `period` setter passes 1 for any value); with `repeat` back to 1 at
  0.5 or 2. **MButtonActor** (update 0x27E978): while active the rectangle pulses by
  1 + 0.15 v and its child `Active` shows v, v falling 1 to 0 over `period` (1/2 s).
  **MTextActor** (update 0x2E4390): one more letter a picture. **MSwitchActor** (update
  0x2E2D18): t by 0.1 a picture toward 1 (`start`) or 0 (`end`), the button pushed out from
  its parent's middle by 1 + 0.6 (1 - t²), its `base`, `text`, `ring` at alpha t (❓ their
  colours by state are not written).
- **Transitions** (mTransition, 0x24D1B8 on): `panOut`, `panIn`, `panOutIn` start the halves,
  `syncOut`, `syncWait`, `syncIn` wait for them; `period` is the step a picture,
  1 / (period x 60). `MColorTransition` (0x286EA0, update 0x287348) covers the screen with its
  color by 1/15 a picture. **MCrossTransition** (0x28A6B0, 0x28A5B8): its out half is done at
  once; coming in, t falls from 1 by 1/30 a picture (one more picture at 0) and every page is
  drawn at opacity 1 - t and size 1 + t² about the screen's middle. ❓ Its trail (the last
  picture blended over at `magnify_ratio` with a black veil) is not drawn. **MBlockTransition**
  (0x27AEE0, 0x27B2A8): the screen as it was is held in 32 x 24 blocks of 20 pixels; each
  block's timer starts at 1 + a random part of `range` (0.125) + its column / 32 x `slope` (2;
  a negative slope from the right) and falls by 2/15 a picture; a block shows at alpha
  min(timer, 1) (❓ the random source, 0x5792F0, is not read: a fixed generator).

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
that are numbers are saved in the game data (save.md); the named ones by their numbers in the
program's tables of names (0x437758; the accessors 0x436810 on): `unit_velocity` +0x10,
`unit_power` +0x11, `unit_torque` +0x12 (bytes), `sharpness_mode` +0x20 (`NORMAL`, `MILD`),
`sound_type` +0x10B0, `penalty_type` +0x10E4, `view_type` / `view_type_2p` +0x10F4 / +0x10F8
(`SUBJECTIVE`, `BONNET`, `OVERLOOK`), `photo_quality` +0x110C and `ustorage_photo_quality`
+0x117C (`STANDARD`, `FINE`, `SUPER_FINE`), `photo_aspect` +0x1114 (`4x3`, `16x9`, `3x2`, `1x1`),
`photo_shutter` +0x1118 (`typeA` .. `typeS`), `steering_assist_1p` / `_2p` +0x112C / +0x1130
(`AMATEUR`, `PROFESSIONAL`, `SIMULATION`), `replay_mode` +0x113C (`NORMAL`, `DIVE`),
`replay_play_mode` +0x114C (`STRAIGHT`, `SHUFFLE`) (Option offsets); a new game's are the
constructors' (`MILD`, `STEREO`, `NONE`, `SUBJECTIVE`, `STANDARD` / `SUPER_FINE`, `4x3`, `typeA`,
`AMATEUR`, `NORMAL`, `STRAIGHT`). They are also kept as text lines in
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
  `getAspecDistance` in the player's unit; the menu stack (0x42FD00 on): the names one after
  another, each ended by a 0, in 0x100 bytes (a name that would reach the last byte is not
  pushed); `menuRef(n)` the name n places below the top; `saveMenuStack` keeps a copy and its
  length (0x42FE88), `loadMenuStack` puts it back and answers 1, or 0 when none was kept (a new
  game's length -1; 0x42FF48); `isNeedSave` answers whether a day was put in the diary
  (0x42F0E0) or `setNeedSave` was called, and clears both (0x43AC68, 0x43ACA8).
- **MGarage** (0x16DA58 on): places that are never packed; `addNewCar(label, paint)` a car as
  sold on today's date, `addUsedCar(car)` / `addCar(car)` the car's record; `delCar` clears the
  place (1 when it was the car driven), `undelCar` puts it back; `riding_car` is an object on
  the garage's own record. The program reads and writes a car's slot of the garage file in
  `changeRidingCar`, `pushRidingCar`, `popGarageCar`; here the records are in the state (they
  answer `OK`). A change of the car driven (0x435388) takes the car's record (to a car never
  driven: made as sold with its odometer, and marked so, 0x435568), marks the place as having a
  record and moves it first in the order of driving, every car driven more recently one further
  (0x435690); `setRidingCar(place, car)` is that change with the car's record, `(place, label,
  paint)` the car as sold without the marks (0x435490); `cancelRidingCar(place, car)` acts only
  after a change to a car never driven: that car's record is dropped and the car given is driven
  again (0x435418). `pickup(country, tuner, drivetrain)` lists the cars in place order whose
  country and tuner (the car's columns) and the drivetrain of the sheet in use are those named
  ("all": any; 0x170088); `pickupCountry()`, `pickupTuner(country)` the ones the garage has, in
  the order of their numbers (0x170278, 0x1705E0).
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
  tuner, drivetrain, the paint's hue). The gears' ranges (`getGearRange`, 0x3553A0): the gears
  the slider and final drive give (0x354FF8), each gear's ends a third of the way to its
  neighbours' ratios, the first gear's top 9/8 of its ratio, the last's bottom 7/8, the reverse
  gear 7/8 and 9/8 (0x355278; thousandths, truncated); in the array [7 + 2g] the lower end of gear
  g and [8 + 2g] the upper. ❓ The charts' points (0x148940, 0x148B60) are not written.
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
result routines write. The race's menus' work (MQuickWork) gets `prize`, the rows in finishing
order with their times (0x114390; a race the player did not finish has "-" rows and no times,
0x110898: ❓ here the rows are left empty) and `playerGridNumber`, the player's place. A race
sequence starts when the scripts that asked for it finish (a new work object, 0x127E60).

- **An event** (0x114498): not finished, a race counted as such and no prize; finished in
  place p, the row's prize of p, the place counted, the prize added to the prize total, the
  race's best score when the player drove it (0x430A20), and the race's record unit
  ("event", p; 0x430A58) only when p is better than every earlier place of the same sequence;
  the diary's day ("family" races as a course run).
- **A championship's race** (0x119CB8; the last, 0x119F28): every entrant's points by the
  finishing order from the program's table (0x6A6EF0: 10, 6, 4, 3, 2, 1; 0x3EEFE0; when the
  player did not finish, none for the player and the others move up, 0x3EF1E8); the race's
  prize and record as an event's (a watched run leaves the record). On the last race the
  championship is won when no other of the first six entrants has as many points as the player
  (0x3EF298): its own record unit gets "1" and `GrandPrize` the row's +0x2C x 100;
  `SeriesRank` is the number of the others with at least the player's points (0x3EF2D8); the
  diary's note is 0 for a won championship. A championship left in the middle is kept
  (`GameState::suspend_series`, 0x119BD8) in the save and offered again on the map.
- **A mission** (0x11C328): the place's prize whatever the run, the record ("mission", the
  place) and its score, the diary's mission day (place, note 15, time).
- **A license test** (0x1105A8) keeps its result in its unit and its ranking (time, result, the
  real date, the pass code). Its day is the sequence's: when the license sequence ends (0x114E38
  -> 0x110800) the diary gets the last run's day (result, note 14, time), or a day with nothing in
  it when there was no run or its result is 13: every visit to a test's page is a day, as the
  original's calendar shows. **A course's run** (0x115A90) is a day of the diary.

The prize page of the quick projects counts the prize into the credits (`incMoney`, ten at a
time); the presents (prize cars, licenses) are given by Gran Turismo mode's present page on
the way back, from the records' `present_result`. The car's distance goes on its condition as
the race's step adds it (0x35DE20): the odometer (whole meters, capped at 100,000,000), and
unless the car has no wear the engine's distance up to 9,700,000 m, the oil's (from below 0 up
to 0, then from 300,000 m; else up to 6,000,000 m) and the body's up to 3,000,000 m, the limits
read from the program's tables (0x620CD8 +0xD0 on, 0x620AF6; 0x350678, 0x366100). ❓ Added
once for the race's whole distance.

**The race's entry table** (`getEntryCarCode(i)`, `setEntryCarCode(i, car, paint)`): [car,
paint] by the entry's number, entry 0 the player's car (`car_code`, `car_color`), the opponents
the scripts chose from 1 to `overwrite_enemy_num` (`RaceRequest::entries`, `entry_paints`).
Arcade's single race asks `MCarData.PickUpEnemyCars(car, list, offset, range, count, dirt)`
(0x1308C8) when its car has no race of its own: the program scores the player's car and each
car of the list by the showroom car's evaluation (ShowRoomCar 0x364760, 0x364F00), drops those
whose drive (four-wheel or not) is not the player's, sorts them by score, finds the ones within
score + offset ± range (0x130618), widens the window until it holds count + 1, draws count of
them (never the player's car, 0x130690) and a paint each. 🔎 Not built here (the evaluation is
not): it answers none, and the race's own pool fills the grid.

**The options and who reads them** (`game_settings.h`: `GameSettings settings_from(options)`).
The scripts read and write MOption; `Runtime::on_options(GameSettings)` is called at the end of
the start, when a game is loaded and when the scripts apply, save or reset the options.

| Options | GameSettings | Read by | How |
|---|---|---|---|
| `menu_bgm`, `menu_se` and their `_volume_raw` | `menu.music`, `menu.effects` (raw / 128, 0 when off) | the menus' sound (`audio::MenuSound`) | the app's `on_options`: `set_music_option`, `set_effect_option` (screen_menu_viewer.cpp) |
| `race_*`, `replay_*` volumes, `sound_type` | `race`, `replay`, `sound_type` | the race's sound | `audio::race_sound_options(setup.options)` in race_world.cpp |
| `race_color_correction`, `replay_color_correction` | `race_picture`, `replay_picture` | the race's picture (`race::PictureSettings`) | screen_race.cpp: `pictures_from_options` at a race's start from the menus, `pictures_to_options` after it (the pause's changes kept) |
| `unit_velocity`, `unit_power`, `unit_torque` | `velocity`, `power`, `torque` | the menus' figures (MUnit, the car pages) | the natives read the state; the race display's speed: `RaceHud::set_mph` from `settings_from(setup.options).velocity` (screen_race.cpp) |
| `race_display_*`, `replay_display_enable` | `display`, `replay_display` | the race display (race_hud): the map, the times, the laps, the place and the meters hidden when off (`HudState::show_*`); a replay's display (`replay_display`) | ❓ the steering, car name, suggested gear and G meter parts are not drawn as parts of their own |
| `view_type` | `view` | the race's first view (SUBJECTIVE the nose view, BONNET the higher one, OVERLOOK the chase view) | screen_race.cpp, a race from the menus |
| `automatic_gear`, `assist_asm`, `assist_tcs`, `steering_assist_1p`, `active_steering_1p` | `automatic`, `asm_on`, `tcs_on`, ... | Arcade's car page offers them (its script reads the option); the race takes `RaceRequest::manual`, `assist_*` | 🔎 Gran Turismo mode's races: the quick menus' defaults |
| `race_laps`, `tire_damage`, `penalty_type` | `laps`, `tire_damage`, `penalty` | Arcade's scripts (laps); 🔎 the race's rules | |
| `timeout_count_to_autodemo` | `demo_after_s` | the title's script (`onTick`: the demo after that many seconds without a key) | runs here (`disc_menu_title_demo_after_option`) |
| `auto_save`, `opening_after_autoload` | `auto_save`, `opening_after_autoload` | Gran Turismo mode's and the boot project's scripts | run here |
| `wide_mode`, `monitor_size`, `screen_adjust_h/v`, `sharpness_mode` | `wide`, `screen_adjust_*`, `sharpness` | 🔎 the picture's shape and place | the screen's figures are saved (save.md) |
| `vibration_*`, `steering_forcefeedback_*` | | 🔎 the pad | the controller block (save.md) |

**The Replay Theater** (the title's third item; the program's sequence `load_replay`,
0x10CA50): the runtime turns the sequence into the project `labomode` with the menu argument
`ReplayTheaterRoot` (0x42FC98, 0x10B290). Its card manager in MODE_LOAD_REPLAY lists the replays
in the data folder's `replays/` (`game_replays.cpp`; newest first; each file's title its name,
❓ the date the day it was written, the car and course by the database's names, its size in KB).
Play loads the one chosen (`file.load`, `MMemoryCardManager.setAction(1)`) and closes the page:
the project ends, and `race_request()` answers the sequence `replay` with `replay_file` (the
path); after it the theater comes back (the sequence's loop). Exit returns to the title. ❓ The
demonstrations' folder and the play list are empty; `labo_folder` / `labo_list_index` start at -1.
The card's free KB: 8,000 less a KB, the game's save and the replays (the scripts take 8,000
for an unformatted card).

**A game with progress** (`game_progress.*`, `gt4tool save <disc> progress`): save.md.

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
a text of several lines (`multiline`) is broken at spaces as the program's word wrapper does
(0x245C90): each word measured by itself, a word starts a new line when the line so far (its
words each with a space after it) and it reach the face's width less the font's double-byte
step (❓ that margin is the renderer's right edge, 0x490660, which the four reference texts'
breaks agree with; the comparison read at 0x245F1C is against the bare width); a face without
`geometry` takes its parent's width. With `equalized` a line broken to fit is spread at its
spaces to end a space short of the width, each space at most 2.5 spaces wide (0x24600C); the
last line and a line ended by a line end keep plain spaces (❓ the program does this only for
the languages 0x245568 lets through: not index 0 or 9-11); made narrower when
the script asked (`adjustScale`: the widest line to the face's width), or cut with an ellipsis
(`abbreviate`); a key is looked up in the page the layout has the widget in, wherever the
scripts copy it; an open option list is drawn over the page; the cursor and the transitions
as below.

**Before the menus** (`app/menu_advertise.h`; GT4FIRSTADVERTISE::DisplayAdvertiseImage,
0x108F78): `advertise/us/main.img`'s pictures in the middle of the frame buffer on black, each
brightened in over 15 steps, held the seconds given x 30 steps and darkened out over 15 (a step
every other field: 0x109048, 0x109080); the program's list (0x618700) holds texture 2 ("Sony
Computer Entertainment America Presents", 3 s). The copyright notice (texture 0) before it is
shown here for 1 s (❓ judged from the reference: about 2 s on screen; not found in the list).
Texture 1 is the Polyphony Digital logo, texture 3 a demo disc's notice. Shown when the app runs
as the game (scripted runs: `GT4R_MENU=advertise=1`); the boot project starts after them.

**The loading page** (`app/menu_loading.h`; the program's LoadingPS2, 0x102840, its modes
0x104E30): between two projects the "black" mode: a black quad fades in over the last page in
1 s; once whole, one of two pictures of `advertise/gtloading.img` chosen at random (0x579358:
texture 0 the GT logo on the frame buffer's x 220..412, y 168..264; texture 1 the blue and red
stripes, x 240..400, y 176..256) and the tachometer fade in over 1 s together; at the end the
page fades out in 0.25 s. The tachometer is a picture of the program (a Tex1 at 0x68C080: texture
0 a black dial for light pages, texture 1 the needle and its trail for dark ones), 64 x 64 about
(320, 341) of the 640 x 448 frame buffer, coloured (130, 130, 150), turning clockwise 720 degrees
a second (0x104150). The program shows it while the disc loads; here the next project starts
once the black is whole, the page holds 0.5 s after the picture is in (❓ judged) and takes no
keys. ❓ Not done: the "capture" mode the scripts set before a race (the last picture held under
the tachometer), "snow", "fade in", "simple", "off"; the tachometer's red while the disc check
0x548850 fails.

**Text** (`menu/text_layout.*`; the program's 0x491028 a line at a time, 0x4940E8 the marks,
0x492978 a character). Gran Turismo 4's font record has two bytes past Gran Turismo 3's:
+0x14 the step after a double-byte character and +0x15 a pair spacing (1 for `hn_14`,
`hm_16`, 2 for `hc_20`, `tr_30`). The pen:

- a space (and a code the font lacks): the space's advance, and the next glyph starts a word;
- the first glyph of a word stands at the pen and steps its advance (its left offset is not
  added);
- a later glyph: d = its left offset + (unless it is a digit or 0x7F) the pair spacing less
  the pair's kerning (the profiles' rows added with saturation, the least of them, even rows
  to 7 bits, odd rows as bytes, then to 7 bits); it stands at pen + d, the pen steps d +
  advance (all times `scale_x`);
- a code from U+0300 on is a double-byte character: the font's cell (+0x10 size, +0x12 offset,
  one more across after a glyph) from the language's double-byte font, then the font's wide
  step (+0x14, the cell's width in every font of both discs). The font: Gran Turismo 4 loads
  `jp_menu.kf`, `kr_menu.kf` or `tw_menu.kf` by its region and none for the US and Europe
  (0x108D80); Prologue `unicode.kf` (languages 0, 8, 9) or `big5.kf` (10) (0x4439D8). The cell
  is the face's picture in the face's pair of palettes, the even one for a font's outlined
  variant (`#0`), the odd one otherwise, in the text's colour; ❓ of a font with two faces, the
  face whose cells are nearest the font's cell. A full-width form no font has is drawn as its
  Latin letter (the US diary's "1～7 days" shows a tilde).
- Prologue's fonts (layout 0, Gran Turismo 3's 16-byte glyphs; +0x14 the wide step, no pair
  spacing) place every glyph by its left offset less the pair's kerning, digits and the first
  glyph of a word too, and a cell at the pen: Gran Turismo 3's rule. Checked against the
  original's first dialog (each line's width within 3 pixels; the gap after a kana before a
  digit). The text style's default font is `hn_14` (0x246F58), Prologue's `sw_16` (0x2F75D0),
  both with the colour 0xD4D4D4. ❓ Prologue's texts are not broken to the face's width (they
  carry their own line ends; the card dialog's first line passes its face by 5 pixels).
- Each line is aligned by itself; its width is the pen less half a space's advance.
- Marks: `%%` a percent sign; `%Crgb` a colour (hex digits x 17, 0x493BC0); `%Bx` the button
  x of the `Gaiji` font at scale 1, `c` .. `f` (triangle, circle, cross, square) in the
  colours of the program's table before the name "Gaiji" (found by that), the others gray
  (100, 100, 100); ❓ `%F<name> ` (a font) is skipped.

Checked against the original's first dialog: every word of its lines starts where the
original's does (to the pixel, `disc_menu_text_layout`).

**Tool tips** (mToolTipFace): the text is the page's `TOOLTIP|<key>` (0x249B70; ❓ the key's
own text when there is none). A tip is its page's when it loads (0x24A020 calls attach,
0x249FB8: the page's +0xEC), and then shows the focused widget's tool tip (the nearest with
`tooltip` and a `tooltip_key`); `remove()` takes it off the page (0x249FF0; here its text is
cleared too) and `attach()` puts it back. It is not aligned (its style's `aligned` off,
0x248DD0). Its motion (update 0x24A840, states 0x24A2F8 by the table at 0x699310; constructor
0x248DD0: +0x148 8, +0x14C 10, +0x10C 1): a new text is measured; when it fits 95 % of the
face it stands in the middle across, else 5 % in; it starts at the face's height below and
rises (10,000 units a second with +0x10C set: at once); one that fits stays; a longer one
waits width x 0.4 / 80 seconds, goes left at 8 x 10 = 80 units a second until it has gone by
its whole width, waits a second, comes back 5 % in below, waits a second and rises again. It
is drawn eased toward that place (0x57B088): across by 0.1, up by 0.3 a picture; cut to the
face. ❓ The pad's button test that holds it (0x24A8C0) is not done.

**The cursor** (0x230B68, 0x230CF8, 0x230378, drawn 0x230558): a pointer that travels to a
point of the focused widget chosen by its `cursor_align` (default 8; table 0x6987E0), two
pixels inside the rectangle, with an angle: 0 left middle 45°, 1 bottom left 22.5°, 2 bottom
middle 0°, 3 (and 9 on) bottom right -22.5°, 4 right middle -45°, 5 top right 45°, 6 top
middle 45°, 7 top left 45°, 8 the middle -22.5°; kept on the screen. Each picture it moves
0.15 of the way (snapping under a pixel), then turns the same way; the picture
(`CursorRoot::Cursor`) hangs from the middle of its top edge, turned clockwise by the angle.
`cursor_target` 0 leaves it where it was; shown again it starts from where it last aimed.

**Units**: a course's length "%.2t%s" (hundredths rounded, `miles` or `km`, 0x1AEE18), its
height "%.1t%s" (tenths, `ft` or `m`, 0x1AEFC0): "12.94miles", "984.2ft".

The menus step at the program's 60 pictures a second whatever the display's rate. The faces
with content of their own are drawn in their place in the page's order (`draw_live` of
`app/menu_page_drawer.h`): movies, cars and models below.

A closed option menu shows its chosen item in the item's own state (the menu's focus lights
its button, not the item: the options' dialogs); ❓ judged from the original's pictures.

Here: every model face draws through a view of its own (the engine's view is read when the
frame is submitted, so two faces of one size through one view showed the same picture), and a
picture's texture is kept by what the picture holds (pictures are freed with their pages).

The title page left alone for the option `timeout_count_to_autodemo` (20 s) asks for the
sequence `autodemo` (its `onTick`, `context.time_after_last_input`): ❓ here the opening movie
(`demo_movie` with `opening`, starting with the Polyphony logo as the original's capture
shows), then back to the title; the configuration lists demo replays (`Rdemo0`..) for it,
which are not played.

❓ Not drawn: `SceneViewFace`, `SlideShowFace`, `RaceCourseMapFace`, `PhotoViewFace`,
`PhotoRenderFace`, `EyetoyFace`, `VirtualFace`, `LoggerFace`; the loading page between
projects (the program's LoadingPS2 with `/advertise/gtloading.img` and the cursor's `load`
icon).

#### Graphs (`GraphFace`; `menu/faces_graph.*`)

`mGraphFace` (draw 0x2E88F8): `points` as fractions of the rectangle (the layouts' `scale
{1 -1}` puts y up), `type` "line" (a strip) or "break_line" (pairs, the default), `line_width`
(2) across each segment after the transforms (0x218430), `color`'s first. The settings pages'
curves come from `MCarData.buildEngineCurveData()` (0x148B60): [power points, torque points,
peak PS, peak kgf·m, lowest rpm, highest rpm]; here from the spec's engine points (speeds
+0xC0, torques +0xD8 with the output factors), x the speed from the first point's to the
last's, y over the curve's own peak. ❓ The program's highest rpm and which points it keeps
past the rev limit; `buildGearRatioData` is not written.

#### Flash (`FlashFace`; `menu/faces_flash.*`)

`mFlashFace`: `flash_path` an entry of the page's packs (or with `from_disk` a disc file) in
the program's STRB format; time 1/60 s a picture from the face's start or `reset(t)`, round
with `repeat` (default 1), else nothing past the end; the frame fitted inside the rectangle
and centred (0x292D70, 0x292E70, 0x272C60). The menu movies use only: the header (0x10
stage width, height; 0x1C rate; 0x20 frames), sprite 0's placements (0x28 bytes: kind, first
frame, end, shape, keys of 0x24 bytes: scale, place, a colour multiply with 0x80 = 1) and
frame lists, the shapes' bounds and fill styles (kind 3 / 4 a picture through its texture
matrix: u = m0 x + m4 y + m12, v = m1 x + m5 y + m13) and the pictures (Tex1). Keys k and
k + 1 are blended by the frame's fraction. The dealers' banners (`piece/tuner_screen/*.strb`),
the license center's and the event halls' backgrounds.

#### Movies (`MovieFace`; `menu/faces_movie.*`, `app/menu_movies.*`)

Class `mMovieFace` (vtable 0x664398, derived from `mImageFace`, 0x160 bytes). Its settings
(constructor 0x220730, layout reader 0x220A38):

| Property | Default | |
|---|---|---|
| `movie_name` | "" | the file `mpeg/<MpegRoot>/<movie_name>.pss` (0x220850, 0x2208E8) |
| `pixel_width`, `pixel_height` | 640, 0 | |
| `interlace` | 1 | the movie holds its two fields as the frame's halves |
| `audio` | 0 | its sound played |
| `repeat` | 1 | starts again at its end |
| `ac3` (scripts: `AC3`), `audio_track` | 0, 0 | |

`MpegRoot` is the configuration's (`config-main`: `main`, `main_pal` for the UK; Prologue's
`prologue`), `preview` when it has none. A face with no `geometry` fills the page; all seven
are their page's first widget. The per-picture update (0x21F440) calls the face's script
function named by `setFrameHook(frame, name)` when the movie shows that frame, and once, when a
movie that does not repeat ends, its `onMovieFinish(context)`; `setUnUpdate()` stops it.
Here: played by the engine's player (silent unless `audio`), restarted when the scripts change
the name, stopped when the face is no longer drawn; until its first picture the still of that
name from `mpeg/<MpegRoot>/course.ipic` (`ipua`, 56 entries on the US disc: `start_game`,
`maker_game`, `category` and the course movies; 640 x 448, Prologue's 512 x 448). An interlaced
movie shows its first field until the player has detected the layout from its pictures. A
movie counts as ended when no new picture came for a second while the clock ran (the opening's
sound does not drain at its end in the player).

| Face | Plays |
|---|---|
| `boot/MovieRoot::Mpeg`, `demo_movie/MovieRoot::Mpeg` | `opening_us` / `ending_us` (the script), with sound, once |
| `arcade/TopRoot::mpeg` (the title), `online/TopRoot::bg` | `start_game` (`maker_game` with the special tuner option) |
| `arcade/CategoryRoot::Mpeg` | `category`: the four categories' footage in quadrants |
| `arcade/CourseRoot::Mpeg`, `arcade/PregridRoot::Mpeg` | the focused course's code (without `r_` on the pre-grid page) |

#### Cars (`CarFace`; `menu/faces_car.*`, `app/menu_cars.*`)

Class `mCarFace` (vtable 0x65B0C0, 0x2BC bytes) with an `mCarModel` (`car_model`). Its camera
(0x201EC8 / 0x201FA8) and lights (0x210830 / 0x210988) are the same readers as the model
face's:

| Property | Default | |
|---|---|---|
| `view_angle`, `near`, `far` | 50, 0.1, 32 | degrees across the face's height |
| `camera_trans`, `camera_rotate` | (0, 0, 4), (0, 0, 0) | view = Ry(-ry) Rx(-rx) Rz(-rz) T(-t) (0x21AA30) |
| `world_ambient` | (0, 0, 0) | |
| `lN_enable`, `lN_pos`, `lN_amb`, `lN_dif`, `lN_spe`, `lN_sdw` (N 0..2) | light 0 on; (0, 1, 0, 0); 0.3; 0.5; 0.5; (0, 0, 0, 1) | world-space directions toward the light (w 0); colours 0..255 / 255 |
| `image_path` | "" | the picture the car reflects |
| `trans`, `rotate` | 0 | where a newly set car starts |
| `mirror` | 1 | normal mode: the car turned upside down about its floor (Scale(1, -1, 1)), without its shadow, at 0.3 of its alpha, drawn first (0x156820, 0x217058) |
| `action_type` | 1 "advanced" | scripts write "normal" / "advanced"; only the arcade and event car pages keep "advanced" |

Projection (0x217058): the picture's middle is the face rectangle's; d = (h / 2) / tan(view /
2) over the face's height h; on NTSC a page unit across is d x 448/480 and down d / 1.098; the
whole page is the viewport.

The car (`car_code`) is `car/menu/<model>` in paint `car_color` (a car's paints in the order of
their paint number, VARIATION +0x04: the Ka's Capri-Blau, paint 1, first). Its motion
(0x153BB8, one step a picture): alpha eases by `alphaInterpolateSpeed` plus a step (+1/120 after
a car is set: a two-second fade); `motion` 1 turns it like a turntable (velocity v = (v + 80/60)
x (1 - 4/60) degrees a second, settling at 18.67), 2 eases the yaw toward `ry` by a fifth a
picture; the position eases toward `tx ty tz` by `posInterpolateSpeed` (0.1), `rx`/`rz` by 0.1.
The program's setter of `rotInterpolateSpeed` writes the position's speed. `warp()` jumps to the
targets; `fadein`, `transparentRatio` as their names say. Model = T(cur) Rz Rx Ry(yaw); the model's
origin is on the body's underside, so the car is raised by its ride height to stand on y = 0
(❓ judged: the program's ShowRoomCar is not read).

A car given as a garage car (`car_garage`, `car_data` set to an MCarGarage) shows in that
car's paint (its record's +0x18) and stands where the layout says from that moment, so what the
script does after it holds (Home turns the garage car to its front, ry 180, when yesterday had no
run that failed: gtmode's `isGarageCarFront`). The scripts' `loadModelOptionMotion(path)` gives a normal face
a camera motion from the page's packs (`image/gtmode/gtauto/*_cam.mot`: Mot1 files read as the
stage's below): its eye, the point looked at, up and field of view at its clock's time (below;
0 until the page plays it: the wheel and wing shops never do), and its named values: `fade` the
model sets' opacity (not drawn under 0.02), `lift` the car raised on the jack (0.276 m in the oil
change; ❓ the whole car is raised here, the program lets each wheel hang, 0x1569A0),
`shadowOpacity` the car's shadow (mCarModelPS2::vf58 0x156F18). `loadModelSet(path)` more model
sets (MCarModel +0x68, in their order: `garageShadow.mdl`, the GT Auto mechanic `pitmanFix.mdl`,
the wash cloths, the oil change's cart); a skinned set is posed by its motion ([model.md](model.md)
"Skinned sets").

**Model motions** (`menu/model_motion.*`, `app/menu_cars.cpp`): `loadModelMotion(path, set,
align = 0, shadow = 0, rot = 0)` (0x139030 → 0x1540C8) appends {motion, set, align, shadow, rot}
to MCarModel +0x78; every entry is drawn every picture (a set listed twice is drawn twice: the
split motions park the copy not in use 20 m away). Without entries each set is drawn as stored.
An entry's motion starts from the car's matrix, moved to a place on the car by `align` and turned
(0x156AA0, table 0x68FDC0), turned by `rot` degrees and scaled by `additionalModelScale` (flat in
y with the shadow flag, which the mirror pass leaves out):

| align | place (car frame: x right, the front at -z) | turned |
|---|---|---|
| 0 | the car's origin | 0 |
| 1 / 2 | the left / right tire's outer face at the front axle | 0 / 180 |
| 3 / 4 | the same at the rear axle | 0 / 180 |
| 5 / 6 | left / right, midway between the axles | 0 / 180 |
| 7 / 8 | left / right, three quarters of the way back | 0 / 180 |
| 9 / 10 | the centre line, at the body's front / rear end (VARIATION +0x18 / +0x1C, mm) | -90 / 90 |

❓ The program reads the axles, track and tire width from the car's simulation geometry (+0x618,
+0x8D4, +0x12, +0x14: their meaning is believed, not read); here they come from the database's
sizes. The time (mModelMotion, 0x21B398; step 0x21B4F8, before a draw 0x21B528): 1/60 s a
picture while it plays, 0 before `playModelMotion`; past the clip's length L it goes back by
L - S (S the clip's loop start) when it loops (the default), else it stops there and holds.
`playModelMotion` / `stopModelMotion` / `setModelMotionLoop` act on every entry and the camera
motion; `isPlayingModelMotion` answers for the camera motion alone (0 without one, 0x153FA8).
GT Auto: the mechanic walks in, stands at the car's left by the front and fades out after 7 s;
the car wash: after the car's turn, two mechanics wipe it with cloths for 10 s; the oil change:
the mechanic walks over, lies on the cart and slides under the car on the jack. ❓ A skinned
set's reflection passes are left out (which picture the program binds to their slot is not
read), and its clothes' texture alpha is taken as opaque.

Here a car's showroom model is built on a worker thread and sent to the GPU a few models a
picture (about 3 ms of each), and its shadow made only when its look changes: on the arcade
car page a new car costs about 23 ms in its worst picture, the first one about 39 ms (the stage
loading), where building it in the picture cost 70 to 140 ms.

The "advanced" stage (0x157378) takes the car at (x, y, -z) turned half round. Its time steps
1/60 s a picture round the light motion's length. **The stage's motions**
(`menu/stage_motion.*`): `menu/select/camera.mot` and `light.mot` are `Mot1` files whose scene
is a byte program of the motion library (0x42B440; the root's +0xC points at it): 0 end, 1..3
the hierarchy's steps, 4 a name, 0x0A the perspective (u8 flags, the field of view in
radians), 0x0B the camera (u16 flags; eye, point looked at, up), 0x0C / 0x0D / 0x0E a light's
ambient / diffuse / specular (u8 light, u8 flags, 4 values), 0x0F its place (3 values), 0x10
a named value (u8 flags, the name, 1 value). A value whose flag bit is set is a u16 channel of
the clip, else a constant f32 (0x42D688). The camera stands still: eye (0, 1.28, -9.137)
looking at (0, 0.426, 0.004), up (0, 0.996, 0.093), 0.4495 rad across the page's height (❓
the axis). `light.mot` (24.15 s) keys `Alpha0`..`Alpha3`, the weights of bg.mdl's four models
(the backdrops), drawn cross-faded over black. Its lights: light 0 and 1 move (place, ambient,
diffuse, specular keyed), light 2 stands at (3.90, -3.19, -9.43) with ambient and specular 0.2,
light 3 is all zero; here each light's place is taken as the direction toward it (❓ the
program's stage lights, CustumLightPosition 0x1D6C98, are not read as points or directions).
`ColorScale` (1 to 1.06) scales the car's colour; light 0 throws the car's shadow (the stage
passes its negated place to the shadow, 0x1D6D50), `shadowDarkness0` (0.55 to 0.75) its
strength, `bottomShadow` (0.7) the shadow under the car. `BlackLevel` 0 and `WhiteLevel` 1 are
constants (no change to the picture). ❓ `shadowBlur0` (3 to 20), light 3, env.mdl and tire.mdl
are not used.

#### Models (`ModelFace`; `menu/faces_model.*`, `app/menu_models.*`)

Class `mModelFace` (vtable 0x66ED98, 0x30C bytes): the camera and lights above (the layouts also
use `far`), `model_set_path` (an entry of the page's picture packs, or with
`model_set_from_disk` a file of the disc), `env_image_path`, `model_translate`, `model_rotate`
(the home rotation), `model_rotate_v` (degrees a picture). Every model of the set is drawn at
its most detailed level with Model = T(translate) Rz Rx Ry. Update (0x2BD2E8): `target_mode` 0
spins by `rotate_v` (kept in 0..360); 1 eases back to the home rotation by 5 % of the shorter
way a picture; `stopRotate()` stops at home. The Gran Turismo mode map's 37 faces are its places'
icons (`image/gtmode/map/panel_*.mdl` of the page's pack); entering a place sets
`rotate_v = [0, 2, 0]`, leaving it `target_mode = 1` (nine places stop where they are). Here each
face is drawn over a part of the page three times its rectangle around it.

### What runs where (the drawing)

The menus' drawing is this repository's code throughout; where it still leans on the shared
engine's Gran Turismo 3 code or on a judgment:

| What | Where | Runs | The Gran Turismo 4 routine to port |
|---|---|---|---|
| 3D faces' models (car faces' extra sets, model faces, the car stage's backdrops) | `app/menu_cars.cpp`, `app/menu_models.cpp` via `formats/model/mdls.cpp` `to_gtm1` | the engine's GTM1 decoder and renderer (`gtm1.cpp`, `render::View3D`, its GT3 lighting model) | ModelSet2's draw (0x455528, the shape submit 0x49D328) and the VU1 lighting |
| A skinned set's blend | `formats/model/mdls_skin.cpp` `pose()` | ours, the weighted sum judged from the data (the program's VU0 micro-routines 0x2E8 / 0x460 are not readable) | (VU0) |
| The menu car | `app/menu_cars.cpp` | `race::CarVisual` (Gran Turismo 4's car parts) drawn by the engine's renderer; ❓ the reflection strength (1.0 here; the program's 1.3, 1.1 not read), the ride-height raise | mCarModelPS2's draw (0x156AA0, 0x156F18) |
| The car stage's lights | `app/menu_cars.cpp` | light.mot's lights as directions (❓), no shadow blur, env.mdl and tire.mdl unused | GT4CarSelect::AnimationStage (CustumLightPosition 0x1D6C98) |
| The car's shadows | `race/car_visual.cpp` | the race's shadow models | the stage's shadow (0x4523B0..) |
| Movies | `app/menu_movies.cpp` | the engine's MPEG / IPU player | (same format) |
| Fonts | `menu/font.cpp` | the engine's font reader plus Gran Turismo 4's two bytes; the layout is ours (`text_layout.cpp`, 0x491028) | — |
| Text breaking | `app/menu_page_drawer.h` `wrapped()` | the program's word wrapper (0x245C90) with a margin judged from the pictures (❓); the non-equalized renderer's own per-character break (0x490660) is not ported | 0x491418's break |
| The loading page | `app/menu_loading.h` | the black mode; its hold judged; the capture mode before races not done | LoadingPS2's other modes (0x104B68) |
| The cursor, transitions, actors, tool tips, widget transforms, the 3D faces' cameras | `app/menu_cursor.h`, `menu/actors.cpp`, `menu/faces_*` | ported from the program (addresses in the sections above) | — |

### Not done

Gran Turismo mode: the places run to their pages and to the race asked for (used cars, events,
license tests, missions, GT Auto, tune shops, the garage, the status, the diary, the car's
settings); a race's whole loop runs with a made-up result in place of the race
(`tests/test_menu_run_loop.cpp`: an event, a championship and one left in the middle, a
license, a mission, the prize cars, saved and loaded); not checked page by page: trading cars
between saves (`loadInstance` answers its result and no second game: the trade page needs a
second MGame over another card's state), the photo travel's own pages after its ticket (the place's page runs to the
`photo_shoot` sequence it asks for, `disc_gt_mode_photo_travel`), the B-spec driver. The Replay Theater's demonstrations and play lists. The options marked 🔎 in "The options
and who reads them". Arcade's two-player battle reaches its race with both cars; the LAN battle says
the Network Adaptor is needed. Prologue's arcade mode and results ([prologue.md](prologue.md) "The menus": boot to a license
test runs); its scripts ask for `MGame.license` (its own license record), answered with a new
game's results. ❓ The charts' points on the settings pages (0x148940, 0x148B60).

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
