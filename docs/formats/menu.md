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
| `visible_condition`, `visible_compare` | Which states a widget shows in (0x265DC8): with a compare, when the compared bits equal the condition's; with a condition alone, when any of its bits is on; with neither, always. The bits: 1 active, 2 focus, 4 prelight, 8 insensitive (the older files' `cond_active`, `cond_focus`, `cond_prelight`, `cond_insensitive`). Whose state: "States" under "Running" |
| A clip's edges | The scissor is in whole lines: the page's 480 on the frame buffer's 448 and a field's 224, the place and the size each cut to a whole line (0x215F78, 0x215FE0, 0x4A2DF8), so a box loses up to a line at its bottom: the gaps of a regulations page's rows (`menu::scissor`). 448 / 480 is rounded toward zero: an exact number of lines comes out one less (the page's own last line is black in the original; kept here). ❓ Prologue's is not read |
| `opacity` | Multiplies the alpha of the widget and its children |
| `clip` 1 | Children are cut to the rectangle. The boxes (`Box`, `HBox`, `VBox`, `FBox`, `MBox`), `RootWindow` (0x232E00) and `SelectBox` (0x2D8C90: the event halls' lists) cut by default (mBox's constructor 0x200C08 sets it, 0x206858); `OptionMenu` (0x2C7498), `ScaleBar` (0x236860) and `SliderBar` (0x2DD820), boxes too, turn it off again; so does the list box a layout's is copied from (its constructor 0x2AEA98 does not set it; measured in four saved states, in the same set of widgets whose faces have `is_face`, and the Replay Theater's rows are cut; Arcade's list writes `clip 0`); other composites do not (0x204F10): the layouts write `clip 0` on boxes and `clip 1` on the others |
| `ColorFace` `color` | One colour, or four for the corners clockwise from the top left |
| `ImageFace` `image_path`, `color` | The picture over the rectangle, tinted; `blend` 1 adds |
| `FrameImageFace` `tl_`, `t_`, `tr_`, `l_`, `r_`, `bl_`, `b_`, `br_image_path`, `image_path` | Nine parts about the layout's `frame_width` x `frame_height` (16 x 16 when it gives none: 0x2999C0), whatever the pictures' sizes: the corners that size, the sides between them, the middle over the rest (0x298288); each part its picture stretched over it, or the face's colour where it has none (0x2979A8); four colours (clockwise from the top left) run over the whole face, each part's corners having the colours there |
| A picture's colour | One: halved, so 255 leaves the picture as it is (0x4AA168). Four, at the corners: whole, so 255 makes it twice as bright, up to white (0x49A150: the Replay Theater's scroll thumb) |
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
| Start (0xFF8D), Prologue | Prologue's page (0x230548) takes it as Accept and makes the activate event on the page itself: without a focus the page hears `onActivate` (the opening movie's skip); Gran Turismo 4's (0x235680) has no case for it |
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

### States

A widget's four bits are its own. At drawing a face (`is_face`, bit 13 of the flags word at +0x98)
first takes its parent's in place of its own (0x231508); any other widget keeps its own, so a
row's active flag reaches the row's faces and not those of a box inside the row (the Replay
Theater's row: its plate asks active or focus, its A / B tag is a box with an active flag of its
own). The `...Face` classes are faces unless a layout says otherwise, the others are not (measured
in the reference's saved states, all twelve looked at).

Setting active (0x265FF0), focus (0x266098) or insensitive (0x266200) on a face sets its parent's
too, up to the first that is no face. Each picture a widget's focus bit is whether it is the
focus of the context's top page (0x2319C0: the last page pushed; 0x235C00 its focus), so a page under a dialog
shows its focused widget at rest, and its prelight bit whether it is the one the cursor points at (0x263A68); a box
with a child that has one has it too (0x207208, the list's rows 0x2B3518), so all the parents of
the focused widget have both (measured: five saved states). A list box's chosen row has the cursor's
prelight while the list has the focus, and no focus bit (0x2B3518). ❓ Else the pointed widget is the
focused one here.

### Widgets

The classes (the program's registration tables; `M` before the layout's name): mWidget is the
base of the faces; mComposite of mBox, which is the base of mDBox (mHBox, mVBox), mFBox
(mOptionMenu, mRootWindow, mScaleBar, mSliderBar) and mMBox; mScrollable of mListBox,
mSelectBar, mSelectBox, mTextBoxFace.

Any widget: its layout properties as attributes; `doCopy()`, `doInitialize(context)`,
`appendChild`, `insertChild`, `removeChild`, `replaceChild`, `clearChildren`, `clearWindow`,
`countChild`, `getChild` (a name a sibling has is made its own when a widget is put in: the name without its last
digits and the first free number from 0: "Item", "Item0", "Item1", 0x206888 -> 0x25ADF8; the missions' list finds its
row again by it), `movetoLast`, `first`, `last`, `next`, `prev`, `name` (assignable),
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
scrolling child. The pinch (0x2CFBE0) lies along that list: from the list's start by the shown part's
place x the list's length, the shown part x the length long; shown part = extent / total, its place
min(1 - extent / total, -scroll / total), not under 0 (a select box 0x2DB9E8 / 0x2DBAD0, a list box
0x2B45E0 / 0x2B4668). An arrow is active while the list says there is more that way (a list box 0x2B46B0 / 0x2B4728: a row after
the chosen one and the rows reaching past the box's end; a row before it and the rows moved).

**OptionMenu** (mOptionMenu, 0x2C7498; `index`, `setIndex`, `getItem`, `Count`, `applyAppend`,
`setInsensitive`; a new one is 128 x 24): its children `popup`, `popup::vbox` (the list) and
`popup::shadow` are found at load and put at y 0 (0x2C84A0). The index is a counter over the
list's rows (the children that are no faces), held at the ends unless the layout's `repeat`
(0x2C9DB0; 0x57CF10). Closed, the menu shows a copy of the chosen row named `selected_item` at
its top left, not active (0x2C8198); the chosen row of the list is active (its dot, 0x2C8410).
- **Accept opens** (the menu's activate, 0x2C9568 -> 0x2C9098): sound `cursor`, the script's
  `onPopupOpen(context)` (with `has_script`); the rows stacked from the list's top pad, `spacing`
  between two, the bottom pad, each row made to fill the list's width (x_expand, x_fill) when the
  menu does not pack (0x2C8FA8); the list as tall as that; the list moved up by the chosen row's
  top, so that row lies on the menu. The page's band is its height less a twentieth at each
  edge (24 to 456 on a 640 x 480 page): a list taller than the band stays where it is and
  scrolls; else a list above the band is put at its top, one below it moved up to end at its
  bottom. The shadow lies 4 right and 4 down of the list at its size; the popup's opacity is
  0 and rises 0.1 a picture to 1 (0x2C9C90); the focus goes to the chosen row; the open popup is
  drawn over everything.
- **Open, the menu has every key** (0x2C95E8): up and down step the counter, then to the next
  row that way that is not insensitive (0x2C9740, 0x2C97E8; none: the index stays); a move plays
  `cursor` and focuses the row (0x2C8DD0). The chosen row above the band sets the list to scroll
  down by what is missing, one below it up by what is over (0x2C98F0); the list eases 0.4 of the
  way a picture. Accept and cancel go to the focused row as events and reach the menu.
- **Closing** (0x2C93C0): sound `cursor`; cancel puts the index back as it was at opening;
  accept shows the chosen row and always calls `onValueChanged(context, index)`, changed or
  not; the focus back on the menu; then `onPopupClosed(context)`.
- `setIndex(context, i)` (0x2C9898): the first sensitive row at or after i (none: unchanged), no
  script call. `applyAppend()` (0x2C8CC8): the counter over the rows there now, the list closed.
  `setInsensitive(context, flags)` (0x2C9B68): a flag per row; the chosen row made insensitive
  moves the index to the first sensitive row. `getItem(i)` the row (0x2C8100), `getItem()` the
  chosen one, `Count` the rows.

**ScrollWindow** (0x2D2720; layout `interpolate_ratio`, 0.3 when absent, and `floating`):
its child `clip` shows a part of `clip::space`. The space wants to be where the page's
focused widget is in the middle of the clip, kept against the clip's edges, and 0 along a
direction it does not exceed the clip in (0x2D3430); each picture it moves the ratio of the
way there (0x2D36A0); `warp(widget)` puts it there at once (0x2D2918). Gran Turismo mode's
map is one. ❓ `floating` (the space follows a pointer) is not written. `updateGeometry` (0x2D2BF8) makes the space
the rectangle its children cover with its own start (none: the window's size) and lays out the bars
`hbar` and `vbar` (0x2D2068): the arrows `dec` and `inc` at the ends, the `base` between them as broad
as the bar, the `pinch` after the first arrow. Each picture (0x2D3970, 0x2D2288) the pinch stands at
the arrow's length less the space's place x r, the window's size x r long, r = the base's length over
the space's.

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
their argument and send nothing. The pad's logical buttons (0x214AC0) come from a map not read: here Cross accepts, Triangle and Square cancel (the US pages' foot says "Square Return", the reference's driver went back with Triangle). A ScaleBar's script name `popup` is whether it is open (its child of that name is `::popup`). A ScaleBar's key (0x238D10): in popup mode accept opens or
closes it and cancel closes it; a fresh press along it sets `repeat_start` to the value and tells the
step; then its own script (`onKeyPress`) has the key, and only when that does not take it does the
value move a step (and the value and step are told); closed in popup mode the key goes on. **ListBox** (mListBox, 0x2AEA98): its rows are items, not children (`setItemCount(n)`,
0x2B4A00: rows kept up to n, new ones empty; the focus kept among them, -1 on a new list and the
first row once there are rows). Each item has a place and size, an update countdown and flags
(in range, active, insensitive), and a widget only when the script makes or gives one
(`getItemWidget(i, context)` copies the template, `getItemWidget(i)` answers nil until then,
`setItemWidget(i, context, w)`); a row without one is drawn as the template
(`setItemTemplate(context, w)`, 0x2B48F0: a copy, whose size is the rows' size; 16 x 16 before).
Layout and attributes with their defaults (0x2B3D50): `step_min` 1, `step_max` 1,
`interpolate_ratio` 0.2, `magnify_ratio` 1, `alignment` 0.5, `h_justify` 0, `v_justify` 0
(also `justify`), `focus_active` 1, `repeat` 0; `direction` 0 lays the rows left to right, 1 top to
bottom; `spacing` between two.
- **Each picture** (slot 70, 0x2B29C0): with `postRedraw` pending, every row in range leaves it.
  The list's start wants (0x2B2400): rows longer than the box (pitch x (n - 1) + the focused
  row's length, magnified) put the focused row at `alignment` of the box, held so neither end
  leaves a gap; rows that fit start at 0. The scroll eases `interpolate_ratio` of the way (within
  0.1 it arrives; more than the box away it starts a box away). The rows in range: all when they
  fit; else as many pitches (rounded up) as the box's room before and after the focused row
  (`alignment` of the box less the focused row, the other part after), widened by how far the
  scroll still has to go, moved back inside the rows. Each row in range is placed along from the
  scroll (when they fit, moved by `h_justify` of the room left) and across at `v_justify` of the
  room left; its size eases toward the row's size (magnified for the focused row) by the same
  ratio; only the focused row is active (with `focus_active`).
- Rows that came into range or left it change their visible flag, their countdown goes to 0,
  and they are given to `visible_callback(context, [indexes])`; a row's countdown
  (`setItemUpdate(i, n)`) falls by one a picture while in range and at 0 the row goes to
  `update_callback(context, [indexes])`; a focus other than the last one told goes to
  `focus_callback(context, new, old)`. All three are queued as events (visible, focus, update).
- **Drawn** (slot 93, 0x2B3848 / 0x2B3670): each row in range, its widget at the item's place and
  size with the item's flags; while the list has the focus the cursor points at the focused
  row's widget (0x230B68).
- **Keys** (slot 72, 0x2B3918), after the list's own script: back (left, or up when vertical) or
  on by `step_min` rows (`step_max` once a held key has gone 10 rows from where it started),
  passing insensitive rows without counting them; at an end the move wraps with `repeat`, else
  does not happen and the key goes on to the page. A move plays the page's `navigate_sound`.
  `incIndex(n)` / `decIndex(n)` the same; `focus_index = i` sets it (kept among the rows).
- `getItemVisible`, `getItemUpdate`, `getItemActive`, `getItemInsensitive` and their setters
  read and write the item's flags. ❓ Drag mode (`enterDragMode`, `drag_index`) is not written.

Faces: `TextFace` (`getTextSize`, `abbreviate`, `adjustScale`: the text made narrower to fit
its rectangle), `ImageFace` (`adjustSize`), `InputTextFace` (`putString`, `backspace`,
`max_input_length`), `ColorChipFace.setData`, `CarFace` (`car_data`, `car_model`,
`setColorIndex`; not drawn), `ToolTipFace`. ❓ From the scripts' use.

**`adjustSize`.** A picture face's (`ImageFace`, registered at 0x2A19C8) takes its picture's size.
A composite's (every box too: mComposite's, 0x207DB0, registered at 0x205C80) goes through its
children that are faces (bit 13 of +0x98): each goes to the box's top left (0, 0) and gives its
size: a face of the picture classes its picture's width and height when it has one (mImageFace's
slot 55, 0x2A2360), any other its own (mWidget's slot 55); the box takes the largest width and
the largest height (0 x 0 with no face). Other children neither move nor count. Arcade's makers'
page (`MakerRoot`) sizes each copied maker item so, then halves it about its centre: each logo at
half its picture's size, in its own shape (a face without a rectangle of its own fills the item).

**`setColor(color, index)`** (mColorFace's and mColorWindow's, 0x284FB8 → 0x286210: `ColorFace`, `ColorWindow` and the
picture faces): the color (an `MColorObject`, r g b a from 0 to 1) into every one of the face's colors, so a face with
four corner colors keeps four; with an index of 0 or more only that one (the last past the end). A face with no color
takes it as its one color. **`texcoord_top` / `_bottom` / `_left` / `_right`** (mImageFace's, 0x2A1748 / 0x2A17E8 /
0x2A1888 / 0x2A1928) move one edge of `texcoord` (the whole picture until set): the top corners' v, the bottom
corners', the left corners' u, the right corners'. The photo-saving progress bar narrows itself and sets
`texcoord_right` to the same fraction, so it shows that part of its picture rather than the whole picture squeezed.
An **InputTextFace** keeps the place the next letter goes (the keys of the script's keyboard: `putString` puts there, `left` / `right`
move it, `backspace` takes the letter before it, `delete` the one after) and draws a caret there (0x2A8170): 4 wide, a line high,
half the text's color, in the focus state, 15 pictures of 30 (0x2A8480).

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
  or text face's colours; a composite has none and stays as it is), not the opacity. A colored face's `alpha` is that of its
  colors (mColorFace 0x2862C0, 0x2862F8): what a script or an actor sets takes the place of what the layout gave.
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
  drawn at opacity 1 - t and size 1 + t² about the screen's middle, and each picture of the in
  half takes its trail (0x2182E0): the picture shown last drawn over at `magnify_ratio` (0.5),
  from its rectangle cut by `magnify_diff` (0) pixels a side, then black at 0.1 (`with_black`,
  1); `type` "capture" (0) instead holds the captured screen over the new page at alpha t (❓
  not drawn here: no script sets it). **MBlockTransition**
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
**Trading cars** (Home's Trade): `MMemoryCardManager(MODE_LOAD_GAME, 1)` is the card in slot 2,
the data folder's `memorycard2/` (not connected without that folder); its file's `loadInstance(manager)`
answers [result, a game object] with the game on that card loaded beside the one played (its garage,
the cars' records with it). The Garage page lists that garage in its trade mode with each car's used
price (200,000 when it has none), and buying adds the car to the garage played (`addUsedCar`), pays
the price and puts a trade day in the diary (`disc_gt_mode_trade_cars`). ❓ The probes of earlier
games' saves answer none.

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
  current figures (`updateCurrentStatus`, 0x146648: the sheet in use built as a showroom car with
  the record's condition block, below "The showroom car"); wheels and wings by label; the condition (odometer, oil, dirt, body); `makeUsed(seed,
  paint, meters)` (0x440FB8; its arguments by their toInt, so the lots' paints given as text count:
  the program's generator from the seed; a paint below 0 drawn among the car's paints; meters below 0
  drawn between the program's table 0x6A6910's {least, most} x 1,000,000 for the model year from 1990
  (0 .. 14), lo + (hi - lo) x draw; then the odometer (at most 100,000 km) and, unless the car has no
  wear, the engine's, oil's and body's distances the meters, each at most the condition table's +0x0C,
  +0x1C and the body's limit; the dirt 0); used prices (0x441128) the new price times the factor of the
  first row of the program's table 0x6A6988 ({s32 thousands of km, f32 factor}, -1 the last) whose
  distance is above the odometer's (85 % below 1,000 km ... 10 % from 100,000), a quarter of that when
  sold (0x4411F0). Used Car Showroom II's first rows on 9 APR, 17 JUL and 23 JUL (names, prices,
  mileage, power, torque) are the reference's (`disc_gt_mode_used_cars_by_day`); the sorts, one key each
  (power, weight, kg/PS, date, order of driving, aspiration, name, price, year, country,
  tuner, drivetrain, the paint's hue), by the program's own sort (0x2F03C8, an introsort, not stable:
`menu/program_sort.h`; a garage of more than 16 cars by a column they share stands newest first, as the original's). The gears' ranges (`getGearRange`, 0x3553A0): the gears
  the slider and final drive give (0x354FF8), each gear's ends a third of the way to its
  neighbours' ratios, the first gear's top 9/8 of its ratio, the last's bottom 7/8, the reverse
  gear 7/8 and 9/8 (0x355278; thousandths, truncated); in the array [7 + 2g] the lower end of gear
  g and [8 + 2g] the upper. The charts (`sim::Showroom::charts`): `buildEngineCurveData([own = 1, [PS
  = 1000, [kgf m = 200]]])` (0x148B60: the sheet in use's spec 0x4413C8, then 0x34B960: the engine and
  driveline blocks, the engine's figures 0x34F3F8 with the car's condition, the driveline's extra on the
  peak) answers [power points, torque points, peak PS, peak kgf m, the first point's rpm, the rev limit],
  x = (rpm - the first) / (the rev limit - the first), y the point's power (torque) over its peak, or over
  the larger of the peak and the given scale when the first argument is 0; `buildGearRatioData([automatic])`
  (0x148940: the argument in the spec's +0x156; 0x34B808) a pair of points per forward gear, (road speed /
  100 m/s, rpm / the highest gear's top rpm) at the gear's lowest rpm (1,000 for first, else the down-shift
  speed into it) and its highest (the up-shift speed; the rev limit in top). The engine's chart equals the
  program's on every car, new and worn (`gt4r_diff <disc> charts`); the gear chart on 719 of 722: ❓ the three
  cars with a stepless box of kind 1 (the Fit, the Colt, the i concept) show their second gear's lowest rpm as the
  converter's hold speed (0x360780 sets it after 0x34ACA8), where the chart (0x34B8E8, 0x34ACA8 alone) has 0x34ACA8's.
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
  diary's note is 0 for a won championship. The race's menus' rows (0x11A168), in finishing order, carry the place's points (the work's +0x37C + 4 i, read by `getWeight`: Session Results' Pts) and the entrant's points so far (+0x364 + 4 i, `getPower`: Total; the pre-race page's Pts column reads it too, 0 before the first race); the work's race label is the session's race (the series' label with the race's number), whose last two digits the prize page writes as "Race n". A championship left in the middle is kept
  (`GameState::suspend_series`, 0x119BD8) in the save and offered again on the map.
- **A mission** (its sequence 0x11BB60): a run that ended other than by Retry (end code 4) counts only when the
  player's car finished (its +0x5B6): then 0x11C328, the place's prize whatever the place, the record ("mission",
  the place) and its score, the diary's mission day (place, note 15, time), `IsSessionFinished` 1 and the page's
  Results rows; otherwise (the pause's Exit, end code 2; a time-out) 0x11C2E0 puts only the day in the diary (place
  15, note 15, no time) and the mission's page comes back as before the run with `canReplay` 1 (Replay and Save
  added). ✔ The original's mission 1 exited (reference 21-gtmode-deep `mission1_page_after_exit.png`); ours the
  same, and a finished run's Results.
- **A license test** (0x1105A8) keeps a graded run only (the race's grade +0x88: 2 gold, 3 silver, 4 bronze, as
  results 9, 10, 11): its unit and its ranking (time, result, the real date, the pass code); a failed or unfinished
  run is kept nowhere. Its day is the test's sequence's (0x11CB38 keeps a result from 13 and a best from 359,999,999;
  a graded run faster than the best takes both): when it ends (Exit, or NEXT_RACE to the next test; 0x110800) the
  diary gets that day (result, note 14, time), or a day with nothing in it when no run was graded: every visit to a
  test's page is a day, as the original's calendar shows. The page has no Replay after a demonstration (0x11CED8) nor
  after a run that ended under 2 s on the race clock (0x11CE3C); both load the test's settings again (0x1100D0, as at the
  sequence's start: its demonstration file `replay/license/<label>`, the entry's transmission and aids +0x1A0 .. +0x1A4 into the
  race information, its equipment into the player's entry): here the demonstration's entries leave the race as it ends and
  every START takes the test's car and the player's choices anew (0x10F880). **A course visit** is one day of the diary, put when its sequence ends whatever its
  runs (none without a run is still a day): RUN_COURSE (0x42F448) with the course, the kind (the free run 1,
  practice 3, photo drive 4, machine test 6), place 15 and note 14, no time; the machine test's at 0x115E90,
  practice's 0x1184DC, photo drive's 0x11B0D8; the free run's (0x1179F8) with the session's best lap (its +0x498, no
  time when none) and note 13 when that best is the course's record (the ranking's first time and not its second's,
  0x4334F8; the course's unit made when it has none, 0x433C10). So a visit moves the calendar, and the used cars'
  weeks, by one day (`GameState::end_race_sequence`; ✔ test `disc_gt_mode_course_visit_is_a_day`).
  Arcade's race and time trial are none (0x111498 and 0x112620 reach none of 0x42F0E0's callers).

- **The course rankings**: a driven time trial run (0x112620 -> 0x113320) or free run (0x116E20 -> 0x117898, only
  when the run made a best-lap recording, race information +0x98; ❓ here: a lap timed) whose best lap (0x35E3F0) beats
  the sequence's best goes into the course's ranking (MCourseRecord's unit, made when it has none, 0x433C10; insert
  0x433260: the time, the real date, the car's code, the pass code) and becomes BestTime (the Session Record); a B-spec
  run inserts nothing. A machine test the car finished (+0x5B6) inserts its best lap every run (0x1161A0): the 400 m
  test into the machine record's unit 0 (mt400_lasvegas into 3, 0x116268), the 1000 m into 1 (0x116440), the top speed
  into 2 by speed (0x1165B8, 0x433348; not here: the machine test's conductor is not built). ❓ The race's kind (+0xD68
  0x10 .. 0x12) is taken from the RACE row's +0x10 (0x16 .. 0x18) less 6. `game_race.cpp` race_finished.
- **A ranking entry's pass code** (license tests and courses, 0x10F718 -> 0x44B258; `game_race.cpp` ranking_pass_code,
  `menu/prologue_passcode.*` PassLayout::gt4): ten fields of 5, 11, 9, 1, 12, 12, 3, 22, 4, 4 bits (83) — the kind 17
  (0x44AC20), the car's place among GENERIC_CAR's row ids and the course's among COURSE's (0x449190), the car as sold (the
  free run's and machine test's, 0x445B88; ❓ the parts compared judged), the showroom's PS and kg (0x364E90, 0x364FA8), the
  run's kind (0 time trial, 1 free run, 2 license, 3 / 4 / 5 the 400 m, 1000 m and top-speed tests), the time (the top speed's: the car's +0x6CC x 1000, truncated), the front and rear tires' kinds (0x4462E0,
  0x446320) or a license test's category (B 0, A 1, IB 2, IA 3, S 4, coffee 5; RaceLicense::vf96 0x3BCB38) and number less
  one — packed into 11 bytes after two zero bytes, the first four bits a hash of the real clock's seconds (0x44AF40: ❓ taken
  from 2000-01-01, local), the next twelve the folded CRC-32, enciphered as Prologue's (SAFER SK-128, 13 rounds) under the
  program's key 0x6A7C88 (a program table, found in all three releases) at each offset of the 13 bytes, written as 32 decimal
  digits with no check digit (0x44C918). ✔ `gt4r_diff <disc> passcode` (the program's 0x44B258 against ours: 300 of 300); ✔
  the reference cards' codes (a B-1 bronze, a time trial of the Ka at Tsukuba) decode to the fields ours makes of those runs.

- **An Arcade single race** (0x111E98): the player drove it and finished: the A-spec points taken are the course record's
  best score (+0x230) when higher, at most 255, shown on the course page.

The race's menus' `IsASpec` (0x110498; an event's 0x114334, a championship's 0x119A78): 1 when the row asks for no license
(+0x45), else whether all 16 tests of that license are passed with bronze or better; without it A-spec's Start answers "no
license" and B-spec's goes on.

**Arcade's penalty and tire damage options** go from MOption (+0x10E4, +0x10C4) into the race's settings (RaceInformation
+0xB4 / +0xB8, RaceOrganization +0x04 / +0x08, 0x3BFB10), where the race's conductor takes them (0x34CA20, 0x354F30):
race_flow.md.

The prize page of the quick projects counts the prize into the credits (`incMoney`, ten at a
time); the presents (prize cars, licenses) are given by Gran Turismo mode's present page on
the way back, from the records' `present_result`. **The car's condition** after a run: the race
starts from the garage car's block (CarGarage +0x498, 0x20 bytes, into the car's +0x6DC), its step
adds each step's distance (0x35DD00 in 0x35DE20): the odometer (whole meters, capped at
100,000,000), and unless the car has no wear (+0x14) the engine's distance up to 9,700,000 m, the
oil's (from below 0 up to 0, then from 300,000 m; else up to 6,000,000 m) and the body's up to
3,000,000 m (the limits from 0x620CD8 +0xD0 on, 0x620AF6); and the dirt (+0x0C) from the smoke and
dust of the wheels on loose ground (0x367148 -> 0x3438C8: x 4 on a paved course, at most 0.8). The
Gran Turismo mode sequences copy the block back into the riding car's record after each run
(0x10FA58: the event's, championship's, free run's, practice's, machine test's and photo drive's;
not the license tests', missions' or Arcade's, whose cars are not the garage's), so Car Wash's
gauge (`dirtiness`) shows the dirt (`RaceResult::condition`, ✔ test `disc_car_gets_dirty`: 20 s
of Grand Canyon 0.016, Tsukuba's road 0). A run without Gran Turismo 4's step (no block kept) has
its distance added once instead (`add_distance`). The race draws no dirt: the race car's dirt
(+0x6E8) has one reader, `RaceCarParamSponsor::vf22` (0x3D75B0 -> 0x344A70), the model scripts'
value `dirtiness` (0x6AAB70), and no car model's script on the US, European, Japanese or Prologue
disc reads that value (`gt4tool car-scripts`: the values read are brake, engineRPM, pedalLeft,
pedalRight and time).

**The race's entry table** (`getEntryCarCode(i)`, `setEntryCarCode(i, car, paint)`): [car,
paint] by the entry's number, entry 0 the player's car (`car_code`, `car_color`), the opponents
the scripts chose from 1 to `overwrite_enemy_num` (`RaceRequest::entries`, `entry_paints`).

**Arcade's dirt and snow versions.** Arcade's `doGame` passes every entry, the player's (0) and the opponents' (1 ..
`overwrite_enemy_num`), through `MCarData.ShiftCarCode(car, course)` (0x12F3E0) and writes it back with `setEntryCarCode`:
on a course of surface 2 (dirt) the car's `ARCADEINFO_NORMAL` entry 6, on surface 3 (snow) entry 7, an `ENEMY_CARS` row's
label (`gt4sample_d_ka_01`, `gt4sample_s_ka_01`); else the car. The entry table holds a code of either table (0x4307D0
through 0x447BB8), so the race builds that car from the row: its parts and settings. On the US disc the 1,112 rows differ
from their cars' own parts in the two tires (the race puts dirt or snow tires on every entry anyway, sim/race_tires.h) and
for three cars in one more part (mtr_gmv_04 the steering, trd_celica_sportm_00 the muffler, pikespeak_quattro_03 the TCS);
none names settings. Here (`game_natives.cpp`, `RaceRequest::entry_enemies` / `car_enemy`, `screen_race.cpp`
`entry_row_car` and `draw_grid`): the entry keeps the row's car with the row beside it and answers the row's label;
`car_code` answers the car's label (the program's answers the row's), and setting it clears the row. ✔ through the menus
(`disc_menu_arcade_dirt_entries`; Grand Canyon, the Ka: the pre-race page's table as 07-courses
`grand_canyon_prerace_menu.png`, Ka 60 HP, 965 kg, tires D). The split battle's two cars keep their own parts (❓ the
program's split race reads the codes the same way).

**Arcade's opponents** (`MCarData.PickUpEnemyCars(car, list, offset, range, count, dirt)`, 0x1308C8;
`game_opponents.*`). Arcade's single race asks for them when its car has no race of its own
(ARCADEINFO), with its list of cars (`EnemyData::enemy_carlist`, or `dirt_enemy_carlist` on a dirt
course), offset (the opponents' level - 2.5) x 0.01, less 0.04 for a car without four wheel drive on
dirt, and on a rolling-start course (`MCourseData.GetAttrString`'s first character '1') less 0.02 for
a race under 3,000 m and 0.01 under 5,000 m; range 0.005; count `max_entry - 1`. The program:
- defaults: offset 0, range 0.002 (0x3B03126E), count 5; a count of 6 or more is 5;
- scores the player's car (the car object's own equipment) and each car of the list, set up as sold
  (0x4443A8; 0 when it cannot be), by the showroom car's evaluation (below);
- keeps the list's cars whose category (GENERIC_CAR +0x11 modulo 100, 0x448908; a concept car (3)
  counts as normal (0)) is the player's car's, or racing (1) for a tuned (2) player's car; a label the
  database lacks keeps the category last read;
- sorts them, the highest evaluation first (the C library's qsort 0x5A4BF8, the order 0x130660);
- finds the window [lo, hi): lo the search (0x130618) for target + range, hi for target - range, target
  = the player's evaluation + offset; the search halves [lo, hi) until its middle is lo, taking the upper
  half when the middle's evaluation is not below the value;
- widens it to count + 1 cars: hi on up to the list's last place but one, then lo back toward 0;
- excludes the player's own car when it is in the window;
- draws count places (0x130690): each `(int)(draw x the window's size) + lo` from the program's generator
  (0x579398 on 0x874D50, the generator MRandom uses), again when it is the player's car or drawn before;
  -1 when the window runs out (with the player's car in it, one place earlier);
- on a dirt course (0x1307E8) moves each place on to the first car whose ARCADEINFO_NORMAL row (by its
  GENERIC_CAR id; the program opens it when first asked, 0x447A78) has a dirt race (entry 6, read as
  bytes +0x30 .. +0x37 not all 0xFF) and is not the player's; a car with no row stops the step;
- draws a paint for each place in turn: `(int)(draw x the car's paints)` (0x445828: less the last when
  GENERIC_CAR +0x12 bit 1 is set);
- puts the places in falling order (an insertion sort of the places alone: the weakest car first; the
  paints stay in their turns, so a paint drawn for one car can go with another);
- answers [[car, paint], ...] without the places of -1.
The sort, the search, the draw and the dirt step equal the program's on made-up lists
(`gt4r_diff <disc> picker`, `sim_program_picker_steps_equal`). With the reference's settings (level 0,
Tsukuba: offset -0.035) the Ford Ka '01's window is the six cars of the reference's four Arcade races
there (VW Beetle 1100 '49, Honda Z ACT '70, N360 '67, TODAY G '85, Daihatsu Midget II '98, Honda S500
'63), and a Ford GT '05's at Special Stage Route 5, Normal (-0.025), holds the reference's five
(`disc_menu_arcade_opponents`); the grid drawn is the reference's grid (`r5/shots/a3.png`).
❓ The generator's state differs from the original's, which has drawn for other things since power on.

**The showroom car** (ShowRoomCar, 0x364760; `sim/showroom.*`): the car of an equipment built as the
menus build one: a 0x1800-byte object with the spec at +0, the parameter block at +0x600 (0x364868:
the spec builder 0x3F3B68 on a paved course, the converter 0x361908 (`sim::build_record`) with the rule block 0x6203C8 and
the condition block given, else a default one, 0x3433E8: no wear) and the engine's figures at +0x1700
(0x364818):
- the engine's figures (0x34F3F8): the curve's points' speeds rounded to 100 rpm; from the first to the
  rev limit (or the last point when higher), every 100 rpm, the full-throttle torque (0x34E9A0: the
  curve x the condition's factor) under the power cap (0x34EA68), in kgf m (/ 9.8) and PS (x rpm /
  716.2); the peak power and torque at or below the rev limit and their speeds; +0x1700 the power,
  +0x1704 the torque, +0x1708 / +0x170A their speeds;
- the power the menus show adds the driveline's extra (0x356EA8): a car with motors (layout 7) (2 x the
  program's byte 0x62045A + 0x62045B) / 0.7355 (0x356E08, 0x355978); a driveline byte +0x13 of 4:
  13.596193 PS;
- the weight (0x364FA8): P +0x1048, the spec's weight (+0x20) times its percentage (+0x08) / 100 when that
  is neither 0 nor 100, plus the ballast (+0x05);
- the evaluation (P +0xFE0, made at the converter's end, 0x360780 -> 0x3F86E8; 0x364F00 reads it):
  x = mu² x P² x √T / m^1.5 x (1 + (D_f + D_r) / 2 - C) x (G_f + G_r) / 2, with mu the front paved
  tire's grip on the first surface (P +0x418), P the power with the extra, T the peak torque (kgf m), m
  the mass (P +0x48), D the downforce coefficients (+0x106C / +0x1070), C the drag (+0x1068), G the axles'
  grip (+0xFD0 / +0xFD4), the grip times 1 + the lift (+0x1084) / the weight (+0x103C) for the part that
  changes with speed of kind 7; the evaluation the eighth root of x (three square roots) / 2.5, at most
  1, and 0.01 below 0.
Bit for bit on all 722 cars as sold, and with worn condition blocks (`gt4r_diff <disc> showroom`,
`sim_program_showroom_equal`). The Panda Super i.e. '90: 44.545 PS, evaluation 0.335.

**The engine's condition** (0x350868; `sim::engine_condition`): a factor on the torque from the
condition block (the record's +0x498: +0x04 the engine's distance, +0x08 the oil's, +0x14 no wear: 1),
by limits made once from the static block's bytes +0xD0 .. +0xD8 (0x350678, x 100,000 m and x 0.001;
US 1.01: below -300,000 m 0.97, rising to 1 at 0; 1 to 1,000 km; falling 0.05 to 11,000 km; the oil
1.05 at -100,000 m and fresher, to 1 at 0, 1 up to 5,000 km, falling 0.05 to 6,000 km), the engine's
times the oil's; equal to the program's on 260 made-up blocks. **A car's figures**
(`updateCurrentStatus` 0x146648, a garage entry's 0x434FD0) are its showroom car's with its record's
condition: power and torque x 100 truncated, the weight; 0 when GENERIC_CAR +0x12 bit 3 hides them. A
car added to the garage (0x434E38) is set up as sold and then given its odometer's condition (0x440F78):
a new car's engine at 0 m is run in, so a never-driven Panda shows 44 HP as in the original.

**A course's attribute** (`MCourseData.GetAttrString`, 0x1B3320): the string of UnistrDB (the
database's +0x1E0) the COURSE row's +0x08 names (0x447F08): "1ff0" Tsukuba, "1000" Tsukuba Wet, "0000"
Grand Canyon; Arcade's script takes a first character '1' as a rolling start.

**The options and who reads them** (`game_settings.h`: `GameSettings settings_from(options)`).
The scripts read and write MOption; `Runtime::on_options(GameSettings)` is called at the end of
the start, when a game is loaded and when the scripts apply, save or reset the options.
`main::game.option` (0x161CB0) is the game's own options, written at once. `main::game.getOption()` (0x161B88) is a
copy (0x18FEE8 copies the block, 0x16B8 bytes, into the object): what is written into it and its `apply` (0x4365B8 on
the copy's block) are the copy's alone, until `main::game.setOption(copy)` (0x161C38) copies it into the game's
(0x43A970) and applies it. Prologue's option pages (project `option2`: the Screen, Race, Memory Card and Controller
panes) and the memory card's autosave switch work on copies: the Screen pane's sliders preview a copy (applyOption),
Cancel reads `getOption` again and applies it, Confirm is `setOption` (✔ test `disc_prologue_option_screen_cancel`).
The US disc's option project writes `main::game.option` on OK and reads it again on Cancel. Here: a copy's apply
hands its settings to `on_options` without keeping them; `setOption` keeps them (the data folder's options file).

**`main::game.setReturnToLastMenuProject()`** (0x166398 -> 0x42FC50 with the last project's name; the menus' names at
Status + 0x3A368: +0x34 the last project, +0x54 the next): the next project is the one the menus came from, the one
asked for until then (here the running one when none was asked) becomes the last. The option project's Exit when
`major_menu_project` is empty, the slide show's exit, the memory card's pages after a save (with `next_sequence`
"menu"), the Analyzer's exit and load and the network's LAN pages leave by it (✔ test
`disc_menu_options_back_to_last_project`: the options from the title, no major project: the title again).

| Options | GameSettings | Read by | How |
|---|---|---|---|
| `menu_bgm`, `menu_se` and their `_volume_raw` | `menu.music`, `menu.effects` (raw / 128, 0 when off) | the menus' sound (`audio::MenuSound`) | the app's `on_options`: `set_music_option`, `set_effect_option` (screen_menu_viewer.cpp) |
| `race_*`, `replay_*` volumes, `sound_type` | `race`, `replay`, `sound_type` | the race's sound | `audio::race_sound_options(setup.options)` in race_world.cpp |
| `race_color_correction`, `replay_color_correction` | `race_picture`, `replay_picture` | the race's picture (`race::PictureSettings`) | screen_race.cpp: `pictures_from_options` at a race's start from the menus, `pictures_to_options` after it (the pause's changes kept) |
| `unit_velocity`, `unit_power`, `unit_torque` | `velocity`, `power`, `torque` | the menus' figures (MUnit, the car pages) | the natives read the state; the race display's speed: `RaceHud::set_mph` from `settings_from(setup.options).velocity` (screen_race.cpp) |
| `race_display_*`, `replay_display_enable` | `display`, `replay_display` | the race display (race_hud): the map, the times, the laps, the place and the meters hidden when off (`HudState::show_*`); a replay's display (`replay_display`) | ❓ the steering, car name, suggested gear and G meter parts are not drawn as parts of their own |
| `view_type` | `view` | the race's first view (SUBJECTIVE the nose view, BONNET the higher one, OVERLOOK the chase view) | screen_race.cpp, a race from the menus |
| `automatic_gear`, `assist_asm`, `assist_tcs`, `steering_assist_1p`, `active_steering_1p` | `automatic`, `asm_on`, `tcs_on`, ... | Arcade's car page and Quick Tune offer the first three (their scripts read and write the option); the race: `RaceScreen::player_choice` | Arcade's race and time trial by their menus' work, the others by `automatic_gear` with the aids on; the wheel's two always (input.md "Which choices a race takes") |
| `race_laps`, `tire_damage`, `penalty_type` | `laps`, `tire_damage`, `penalty` | Arcade's scripts (laps); 🔎 the race's rules | |
| `timeout_count_to_autodemo` | `demo_after_s` | the title's script (`onTick`: the demo after that many seconds without a key) | runs here (`disc_menu_title_demo_after_option`) |
| `auto_save`, `opening_after_autoload` | `auto_save`, `opening_after_autoload` | Gran Turismo mode's and the boot project's scripts | run here |
| `wide_mode`, `monitor_size`, `screen_adjust_h/v`, `sharpness_mode` | `wide`, `screen_adjust_*`, `sharpness` | 🔎 the picture's shape and place | the screen's figures are saved (save.md) |
| `vibration_*`, `steering_forcefeedback_*`, the Key Configuration, the wheel's POWER and POWER_ASSIST | `controllers` | the player's controls and what is sent back (`src/gt4r/input`) | the controller block ([input.md](../specs/input.md)); screen_race.cpp `player_choice` |

**The Replay Theater** (the title's third item; the program's sequence `load_replay`,
0x10CA50): the runtime turns the sequence into the project `labomode` with the menu argument
`ReplayTheaterRoot` (0x42FC98, 0x10B290). Its card manager in MODE_LOAD_REPLAY lists the replays
in the data folder's `replays/` and Gran Turismo 4's own on the memory card (`game_replays.cpp`; the newest first; a row
shows the picture, time, title, date, course, car, A or B and KB: save.md "A replay's particulars", race_flow.md "Saved
replays"). Play loads the one chosen (`file.load`, `MMemoryCardManager.setAction(1)`) and closes the page:
the project ends, and `race_request()` answers the sequence `replay` with `replay_file` (the
path); after it the theater comes back (the sequence's loop). The pen renames (`writeInfo`). Exit returns to the title.
The **play list** (the PlayList folder; the card's `LSTRPLAY` file, save.md "The Replay Theater's play list"): ToListMov puts the
chosen replays at the list's top (`append`, then `rotate` by the count before), asking first to make the list when the card has
none ("no playlist slot n create?", then the KB it takes); the Replay folder's rows of the list show their icon lit
(`ListCheck`). The PlayList folder lists the list (MODE_REPLAY_PLAYLIST: an entry whose replay is gone is taken out as it is
listed, 0x1D2EA8); Trash takes rows off the list, the rows drag into another order (`order`), PlayMode switches
`replay_play_mode` STRAIGHT / SHUFFLE and restarts the count (`resetPlayListCounter`). Its Play (`menu_result` "list") plays
the list one replay after another (game_replays.cpp `play_list_next`: 0x10CA50's loop over 0x4349A8): from the
demonstration's count (MDemonstration +0x64, kept across visits; past the end the first again), shuffled through an order
drawn from the count's seed (+0x68, drawn anew by resetPlayListCounter and initialize: the timer and the small generator,
0x579358): 0 .. n - 1, each place from the second swapped with the one at the Mersenne Twister's draw x (its place + 1)
(0x6012C0; the title's time-out picks from the same list first, "The title's demonstrations"). A replay not on the card is passed over; as many passed over as the list is long end it. Each replay's end plays
the next until the player ends one with Start (the race's end code 2, 0x10CDD4: ❓ that Start is that code is not read), then
the theater again. Ours: the race's own replay files have no card folder: their entries are the letter 0 and their file's
name hashed (FNV-1a) into the 42 bits of a folder's letters. `labo_folder` / `labo_list_index` start at -1.
Save Replay (the race's menus) is the same project on `SaveReplayRoot` with MODE_SAVE_FULL_REPLAY: `getNewFile()` is the run
(`GameState::pending_replay`), `saveNew` writes it. The card's free KB: 8,000 less a KB, the game's save and Gran Turismo 4's
own replays on it (the scripts take 8,000 for an unformatted card); the race's own replays are in the data folder and take
none of it.

**A game with progress** (`game_progress.*`, `gt4tool save <disc> progress`): save.md.

**Cars and courses** (`menu::MCarData`, 0x131068; `menu::MCourseData`, 0x1B34D0): from the
disc's car database ([database.md](database.md)) and the program's lists of makers, tuners,
countries and categories, found in the disc's program file by their content
(`program_tables.*`).

**Other objects**: `menu::MSystem`, `MLocale`, `MUnit` (the calendar's order by language,
0x48EE00: year first for JP, KR, TW), `MUtility` (`GetMoneyString`, `GetTimeString` as the
program's text, `GetPriceMagnification` 100 for "jp"), `MNetwork`, `MShell`, `MProgress`,
`MGamePort` (a controller port's device by name: "USB port1" a wheel's or "DISCONNECT"; Prologue's wheel settings ask it),
`MGpb` (`context.loadGpb(folder, name)` adds `<folder>/<LANG>/<name>.gpb` to the pictures;
`existGpbBinary(path)` whether a loaded pack has the picture: the event halls' rows and the dealers' lists pick theirs by it).

A project's script names files of the disc's top beside its own (gtmode's
`usedcar/US_used0000` is `usedcar/US_used0000.adc`; ❓ the program's search is not read).

### Drawn in the app

A running page is drawn from the live widgets with the rules of the table above, and:
a text of several lines (`multiline`) is broken by its style: an `equalized` one as the
program's word wrapper does (0x245C90; not in Japanese, Korean or Chinese, language index 0 and 9
to 11, 0x245568, nor in Prologue): each word measured by itself, a word starts a new line when the
line so far (its words each with a space after it) and it reach the face's width less the font's
double-byte step (❓ that margin is the renderer's right edge, 0x490660, which the four reference
texts' breaks agree with; the comparison read at 0x245F1C is against the bare width); a line broken
to fit is spread at its spaces to end a space short of the width, each space at most 2.5 spaces
wide (0x24600C), the last line and a line ended by a line end keep plain spaces. Any other text
`aligned` 1 (the default) breaks only at its line ends; one `aligned` 0 at the character after which
the pen reaches the width less the double-byte step (0x490660), unless the next one may not start a
line (. , ' and after a double-byte character also 、 。 「 ゃ ゅ ょ っ －: 0x492748). A text that is not
`aligned`, and a spread one, starts at the rectangle's top left whatever its `align`, and runs on
below a rectangle it passes. A face that is not `multiline` is one line: a line end in it does
nothing (0x490760). A carriage return is as wide as a space, as is any character the font lacks
(0x492978). `getTextSize` measures by the renderer, not the word wrapper (0x2452A8): an `aligned` 0
text as wide as its rectangle and as high as its pen came down, without the last line (0x491618).
A line's width, for its alignment, is its pen's less half a space (0x491028), but the pen's whole in a
face of one line (0x4912AC).
❓ `TextBoxFace`'s own break is not read: at spaces, where the next word would pass the width. A face
without `geometry` takes its parent's width. A text is made narrower when
the script asked (`adjustScale`, 0x2455C0: the widest line to the face's width, and by itself the height to the face's), or cut with an ellipsis
(`abbreviate`); a key is looked up in the page the layout has the widget in, wherever the
scripts copy it; an open option list is drawn over the page; the cursor and the transitions
as below.

**Names in the menus' language**: the car database's names come from `CAR_NAME_<language>`
and `<language>_StrDB` of the menus' language (Prologue, Japan: `japanese`, "フェアレディ 240ZG
`71"; the US disc: `american`; Europe's `british`, `french`, `german`, `italian`, `spanish` by the language chosen,
Portuguese ❓ `british`).

**The release's region and language** (`DiscInfo::region` / `language`): `MSystem::GetRegionCode` answers "us" on
SCUS-97328, "uk" on SCES-51719, "jp" on SCPS-17001 and Prologue (each program's own string, 0x68BB20 in the US one). The
projects' language folders are the release's: US, JP, or Europe's GB, FR, DE, IT, ES (with PT and DB, the text's ids,
which the game never offers). On "uk" the boot project opens `LanguageRoot` before anything else at every start (its
`start_next`), five buttons; the one chosen calls `MSystem::SetLanguage` with its code (0x436170: the language index by the
table 0x6AE404, JP US GB FR DE IT ES PT KR CN TW DB, kept in the game data) and `SyncUnitWithLanguage` (0x4361D0 ->
0x472528: the units by the language's locale, 0x472408: US mph HP ft.lb; GB mph BHP kgf.m; FR km/h ch kgf.m t/min; DE km/h
PS kgf.m U/min; IT km/h CV kgf.m G/min; ES and PT km/h CV kgf.m; JP KR CN TW km/h PS kgf.m), then starts `boot` again in
that language. Japan's program has no `SyncUnitWithLanguage`: its units follow `language_for_unit` (MOption, 0x17E6F0;
Option +0x10, a u32 language), a new game's the console's language (the reset 0x42EEC0), set by the option's apply (0x42F0E8)
and a change of language (0x42ECC8 -> 0x42EC28) through 0x46A868 and Japan's own cases (0x46A760): as above but British
English km/h with BHP and kgf.m (Arcade's OptionPopup offers it, `LanguageForUnit`; no script opens the popup). Ours:
`GameState::unit_language` / `units_by_language` on Japan's disc only (✔ `disc_menu_japan_language_for_unit`: Japan's
"JP" km/h PS kgf.m, "GB" applied km/h BHP kgf.m, "US" mph HP ft.lb; the US and European programs have no such option).
`GetVideoSystem` answers "PAL" on "uk" (the main script then takes `main_pal`); the race still steps 60
times a second. ✔ Checked: Europe's language page, French menus after choosing Français, the British English Gran
Turismo mode map (Europe's dealers' places), Japan's menus in Japanese with its kanji font.

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
keys. Before a race the scripts set the mode "capture" (MSystem.SetLoadingMode): the last picture is
kept (here: the pages are drawn into a picture of their own from the call on, since the menus close their pages as they
end) and the tachometer fades in over it in 1 s; the race is then read, in which the picture stands
still, and takes the screen at once (❓ the program fades the page out in 0.25 s). ❓ Not done: "snow",
"fade in", "simple", "off"; the tachometer's red while the disc check 0x548850 fails.

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
- Each line is aligned by itself; its width is the pen less half a space's advance (Prologue's 0x445328: less a quarter of
  the font's size, its record's +0x0C >> 2, the same for the race fonts, which have no space, 1 to 3 less for `sw_16` / `sw_20`).
- Marks: `%%` a percent sign; `%Crgb` a colour (hex digits x 17, 0x493BC0); `%Bx` the button
  x of the `Gaiji` font at scale 1, `c` .. `f` (triangle, circle, cross, square) in the
  colours of the program's table before the name "Gaiji" (found by that), the others gray
  (100, 100, 100); ❓ `%F<name> ` (a font) is skipped.

Checked against the original's first dialog: every word of its lines starts where the
original's does (to the pixel, `disc_menu_text_layout`).

**Tool tips** (mToolTipFace): the text is the page's `TOOLTIP|<key>` (0x249B70; ❓ the key's
own text when there is none, and nothing when the key has no text either). A tip is its page's when it loads (0x24A020 calls attach,
0x249FB8: the page's +0xEC), and then shows the focused widget's own tool tip (0x235CF8, called on focus entry by
mWidget::vf81 0x265610: the root's focus +0xC8, its `tooltip` flag +0x98 bit 25 and its `tooltip_key` +0x8C; no parent's:
with an option's list open, its rows have none and the strip is empty, as in the original's pictures); `remove()` takes it off the page (0x249FF0; here its text is
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

Here: every model face draws through a view of its own (the engine's view is read when the
frame is submitted, so two faces of one size through one view showed the same picture), and a
picture's texture is kept by what the picture holds (pictures are freed with their pages).

The title page left alone for the option `timeout_count_to_autodemo` (20 s) asks for the
sequence `autodemo` (its `onTick`, `context.time_after_last_input`). **The title's
demonstrations** (0x10BE20; MDemonstration at Status + 0x3B018, made by 0x434590): the
configuration's `demonstration.initialize(13, 150, 3)` (0x4345D8) sets the list's length (+0x4C),
a recorded run's time (+0x44) and the tries a time-out makes (`repeat_time`, +0x50); the movie's
count (+0x6C) starts at the option `demo_movie_interval` (Option +0x116C, 3) less 1. A time-out
(0x434638: the tries anew; with `restart_from_beginning` +0x60 the list and the count from their
start) tries one after another (0x434770): when the count has run out and the option is above 0,
the movie (kind `menu`: the project `demo_movie` with `opening`, after the Polyphony logo; the
count from the option less 1 again); else the configuration's `setNextDemo(index)` sets the list's
entry (+0x48, round), the count down one. A try that spends the time-out's tries, or whose script
answered 0, is the last; the movie skipped (`menu_result` "terminate") ends it too; then the last
project again (0x42FC50: the title, focus on Arcade Mode). The kinds by name (0x622D90): none,
autodemo (an Arcade demo race, 0x10BA28), menu (a project), replay (the card's replays 0x4349A8,
then the disc's recorded race 0x10BBB0), skip_replay; the type setter (0x434690) compares text. The
configuration's list holds `Demo(2, "Rdemo0")` .. `Demo(2, "Rdemo12")` (Europe `Rdemo0p` ..
`Rdemo10p`, Korea four `Rdemo<n>k`; Japan its own 17), and `setNextDemo` sets `demonstration.type =
demo.getType()`, which names 0 autodemo, 1 menu, 2 replay: every Gran Turismo 4 disc's list is kind
replay (`gt4tool adc <disc> config/config-main.adc`; checked on the US, European and Japanese discs;
no list gives 0 or 1, and skip_replay is never set). The jump table at 0x68D000 sends replay (and
autodemo) to 0x10BEF0: the card's play list first (0x4349A8, the Replay Theater's list: its next
replay by the demonstration's count +0x64, in order or shuffled as the play list picks it; the list's
length, or 0 with no list or an empty one); with a list, that replay read from the card (0x10BBB0 with
its flag 1, 0x1D14E0; one gone from the card plays nothing and the time-out goes on), and a list
shorter than the time-out's tries (+0x50, repeat_time, 3) ends the time-out after it; with none, the
disc's recording by its name (0x10BBB0 with its flag 0, the file read from the disc, a
RaceReplayInformation race run). 0x10BBB0's time argument (-1 for the card's, +0x44 for the disc's)
is not read by it. A key that ends that replay (end code 2) ends the time-out. So on the console a time-out with the option at 3 plays two
recorded races and then the movie (the count 2, 1, 0). PCSX2 cannot read the disc's recordings
(the Replay Theater's "Data is corrupted", reference 28), so its attract cycle (reference 01-boot,
28) shows the movie and then the title. With the option at 5 the movie every other time-out, at 1
three times running, at 0 never. `resetMovieCount` (the Misc options' OK) sets the count to 0: the
movie at the next time-out. Here: `menu::autodemo_step` (game_demo.cpp) and the menu viewer's
time-out (✔ test `disc_menu_title_demonstrations`; a scripted run's picture 31 s after the title:
the movie): a recorded race of the list (replay/Rdemo0/Rdemo0 ..) plays as the Replay Theater's
Demo tab plays it (the sequence "replay", `RaceStart::attract`), the game's mark over it; any button
ends it and the time-out (its `menu_result` "terminate"), the title again with its wait from that
key (✔ by pictures: a new game, the title left 20 s: Rdemo0, George V Paris, the Bentley; Cross:
the title, Arcade Mode; 20 s later Rdemo1, El Capitan). The card's play list first
(`menu::card_replay_pick`, shared with the theater's Play; ✔ test `disc_menu_title_card_replays`:
three on the list play one each try, two end the time-out after one, one gone from the card plays
nothing, an empty list the disc's; ✔ by pictures: a card with two Fuji replays on its list, the title
left 20 s: the first at Fuji with the game's mark, Cross: the title, 20 s later the second).

❓ Not drawn: `SceneViewFace` (the race draws its scene under its pages), `RaceCourseMapFace` (the Analyzer's course map too), `EyetoyFace`, `VirtualFace`; the cursor's `load` icon; the
loading page's modes other than black and capture (below).

#### The slide show (`SlideShowFace`; `app/menu_slides.h`, `menu/jpeg.*`)

`doPlay()`, `doStop()`, `isPlaying()`; the photos cover the page's 640 x 480 whatever the face's
rectangle. Two layers (0x1C5E48 to 0x1C6160): a photo fades in over T pictures, stands until it is P
old, then waits for the next; that one fades in over it while it fades out over T, then 4 pictures of
nothing. P and T are the options `album_slide_play_time` (3 to 60 s) and
`album_slide_transition_time` x 60. With `album_slide_effect` 1 a photo that carries a focus record
(an APP8 segment "G4PI PDI", type 3: a point and a rectangle in -1..1) moves from the focus,
magnified, to the whole photo, or the other way for half of them. The disc's list is `photo/%03d.JPG`,
`DemonstrationSlideNum` of them (62), shuffled with the attribute `shuffle`. The memory card's list
(`FileGT4SlideShowList`, [photo.md](photo.md)) comes first when it has photos and the face's `demo` is 0 (0x1C6BE8).
❓ The limiting axis of the
focus rectangle (0x1C6160) is taken as the longer one.

#### Color chips (`ColorChipFace`)

The paint row's four words (VARIATION +0x24; 0x445B28, setData 0x1581C0, setColor 0x1588C8): low 24
bits a colour (0x00BBGGRR), bit 24 it is there; of the first, bit 25 a metallic paint (`image_path0`
added to it, else `image_path1`: ALPHA 0x48, 0x1598B8), bit 26 shaded, bit 27 from the bottom up. The n colours stand in bands
ending at 1; 0.5, 1; 0.5, 0.75, 1; 0.5, 0.75, 0.875, 1 of the height; shaded, each runs into the next
(n = 2 over the whole, 3: 0.5, 1, 4: 0.5, 0.75, 1) (0x159C38). Prologue's rows have the one colour.

#### Beside the page

Over a race's picture wider than the page, a `ColorFace` as wide as the page whose colour does not change across goes on to
the screen's edges (`PageDrawer::beside`): the race's menus' shades and strips. An `ImageFace` over the whole page (from 0, 0, at least 640 x 448, not
transformed) makes the sides black, as beside the menus' own pages: the quick projects' `PrizeRoot` (`bg`,
`image/gtmode/prize/event.png`) is a whole-screen page, the race not seen beside it (✔ the original's prize page,
`30-championship/race1_prize_page_pouch.png`; ours `finish/runs/tcgp`).

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
and centred (0x292D70, 0x292E70, 0x272C60), cut to the face's rectangle. The menu movies use only: the header (0x10
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
| `ac3` (scripts: `AC3`), `audio_track` | 0, 0 | the movie start (0x1FE2B8) passes the player a sound kind: 0 none, 1 with `audio`, 2 with `audio` and `ac3`; no script sets `ac3` or `audio_track` on the US disc, and the start does not read `audio_track` (it asks for track 0: "Sound tracks" below) |

**Sound tracks.** A movie's sound is private stream 1 (0xBD), each packet's data after a header
`FF A1 00 <track>` (`FF A0` also PCM sound, `FF 90` AC-3), each track a whole ADS stream (`SShd`,
`SSbd`) of its own. The US opening (`opening_us.pss`) holds two, tracks 1 and 2 (48 kHz stereo
PS-ADPCM, 1024-byte interleave, 200.6 s each), packet for packet; `ending_us.pss` and `pv.pss`
one, track 0; the course and title films none. Track 2 is the same soundtrack as track 1 in
another rendering, not a surround pair to it: its spectrogram matches track 1's event for event
and band for band (the same music and sounds at the same moments), about 3 dB quieter from
60 Hz up and 6 dB louder below; its waveform is offset from track 1's by an amount that changes
through the film (polarity inverted and 7.6 ms early at 30-60 s, about 50 ms late at 100 s,
36 ms at 150 s, 9-14 ms at 185 s; cross-correlation peaks 0.2-0.75), as a separately edited and
mixed copy would be, so at zero offset the two do not correlate. Both of its sides mirror track
1's (the same left-right relation), which a rear pair of a four-channel mix would not.

Which track plays: the movie start (0x1FE2B8) asks the player (`mpegif`, 0x54EB20, its request
word: bits 0-3 the track, 0x10 repeat, 0x20 AC-3, 0x40 sound; 0x54E414) for track **0** in every
case: no sound output mode, language or region enters, and the face's `audio_track` is not read
there. The IOP streaming module (`IRX/PDISTR.IRX`, its demuxer at 0x1F74 of its text) plays,
for track 0, the first sound track it meets, keeping that packet's whole four-byte header and
skipping every packet whose header differs; for track n, the packets whose low 16 bits are n.
So the opening plays track 1 whatever the options, and track 2 is never heard: the start
request has two callers (0x54EBE0 from 0x1FE200, track 0; 0x54EC70 from 0x1FE240, which nothing
calls), the preload (0x1FD4A8 → 0x54DF90) asks for no sound, `audio_track` (face + 0x10C) is
read only by the script getter (0x21E640) and the settings copy (0x2213A8), no script of
`projects/` names it, and the opening's other plays (the demonstration's `MovieRoot`, 0x434770)
go through the same face. The original's
recording (`01-boot/boot_to_title_full.mp4`, Sound Output Stereo) follows track 1's waveform
(correlation 0.84 / 0.80 by side) and not track 2's (0.25 / 0.17). Here (`formats/movie_tracks.*`)
the same rule: a movie plays the first track met, the packets of the others turned into padding
as the file is read, so the engine's demuxer (which takes every private stream 1 packet as one
stream) sees one.

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
| `mirror` | 1 | normal mode: the car turned upside down about its floor (Scale(1, -1, 1)), without its shadow, drawn first, its picture added to the page at 0.3 of the car's alpha (0x156820; the composite's ALPHA is 0x68, Cs x FIX + Cd: 0x2175D4) |
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

A car set on the face (`car_code`), or given as a garage car (`car_garage`, `car_data` set to an MCarGarage, shown in
that car's paint, its record's +0x18), stands where the layout says from that moment, so what the
script does after it holds (the arcade car page brings it in from `tz` 8.5 with its front to the camera at a speed of
0.025; Home turns the garage car to its front, ry 180, when yesterday had no
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
the mechanic walks over, lies on the cart and slides under the car on the jack. His draw program:
the clothes opaque, alpha test off, their texture's alpha written to the frame alpha; a blended
decal; then his reflection shapes, the set's own 32 x 32 picture (texture 3) as a sphere map, ALPHA
0x58 (Cd + Cs x Ad): added where the clothes' alpha lets it. Here that alpha is put into the
reflection's vertex colours (a vertex's, not a pixel's). ❓ What the VU constants of his two
reflection modes (ops 0x56, 0x57) do is not read.

On the jack (the camera motion's `lift`) the body rises by it and each wheel hangs below the body by
min(lift, what its axle's suspension can still extend: the droop +0x08 less the sitting travel +0x10
of the parameter block's +0x308 + 0x58 x axle) (0x156950, 0x364B30; `sim::Showroom::droop`, here for
the car as sold ❓ the garage car's own parts are not put in). A car face draws the car's own shadow
under it (model 2) in light 0's shadow colour, not the shape the race's light throws (0x451D18).

Here a car's showroom model is built on a worker thread and sent to the GPU a few models a
picture (about 3 ms of each), and its shadow made only when its look changes: on the arcade
car page a new car costs about 23 ms in its worst picture, the first one about 39 ms (the stage
loading), where building it in the picture cost 70 to 140 ms.

The reflections' factors: weight C0 ([0x6238A0]) + C1 ([0x62389C]) min(V.N, 0), a car face's 1.3 and 1.1 (0x156820), the
stage's 1.5 and 0.8 (0x1D5C00); the race's are its course's +0x34 and +0x34 x +0x30 (0x397D1C). (Op 0x56 of the models'
programs; a car built with `CarVisual::Options::reflection_by_draw` takes them from the draw.)

The "advanced" stage (0x157378) takes the car at (x, y, -z) turned half round. Its time steps
1/60 s a picture; past the light motion's length (24.15 s) it goes back to its loop's start (1.07 s). **The stage's motions**
(`menu/stage_motion.*`): `menu/select/camera.mot` and `light.mot` are `Mot1` files whose scene
is a byte program of the motion library (0x42B440; the root's +0xC points at it): 0 end, 1..3
the hierarchy's steps, 4 a name, 0x0A the perspective (u8 flags, the field of view in
radians), 0x0B the camera (u16 flags; eye, point looked at, up), 0x0C / 0x0D / 0x0E a light's
diffuse / ambient / specular (the light parameters 1, 0 and 4, 0x42D024 on; a car face's `lN_amb` is parameter 0, 0x21A988) (u8 light, u8 flags, 4 values), 0x0F its place (3 values), 0x10
a named value (u8 flags, the name, 1 value). A value whose flag bit is set is a u16 channel of
the clip, else a constant f32 (0x42D688). The camera stands still: eye (0, 1.28, -9.137)
looking at (0, 0.426, 0.004), up (0, 0.996, 0.093), 0.4495 rad across the page's height (❓
the axis). `light.mot` (24.15 s) keys `Alpha0`..`Alpha3`, the weights of bg.mdl's four models
(the backdrops, one sheet each, no depth written): each drawn over the last at its part of the sum so
far, then the whole faded to black by what the sum lacks of 1 (0x1D5588, 0x1D5620). Its lights
(CustumLightPosition 0x1D6C98): each light's place is the direction toward it, in the world (w 0),
the global ambient 0; light 0 and 1 move (place, ambient, diffuse, specular keyed), light 2 stands at
(3.90, -3.19, -9.43) with ambient and specular 0.2, light 3 is all zero and lights nothing. `BlackLevel` 0 and `WhiteLevel` 1 are constants (no change to the picture); `ColorScale`
is not read by the program. `bottomShadow` (0.7) is the strength of the shadow under the car.

Light 0 throws the car's shadow (0x1D6378, 0x1D6968; light 1's `shadowDarkness1` is 0): per sample i
of n (2 x quality + 2), six throws p = 0..5 of the shadow shape onto y = 0 along
n + tan(shadowBlur0 x (10 + p) / 16 degrees) sin(pi phi) r2, n the unit light, r2 = normalize(-n.z, 0,
n.x), phi = (i + 0.125) / n + T[p] / 8, T = (-1, 1, -2, 2, -3, 3); pass p writes the one alpha bit
7 - p of a half-size mask (so 1/2, 1/4 .. 1/32 for p = 1..5), the mask times `shadowDarkness0` darkens
the background, the samples are averaged. Here the twenty throws are layers one over another, each at
the alpha that leaves that sum where they all cover; each the body's shape and the four tires' (tire.mdl, a unit cylinder at
each wheel's width and radius, 0x452808), no part of it twice. ❓ Not the mask's half size.

**The soft focus** (0x1D56B0) of the backdrops' picture: a copy at half the frame's size, a running average over 7 pixels
across and then down (weights exp(-j^2 / (2 x 2.5^2)), each shifted copy laid over the last at its part of the sum so far in
128ths), put over the picture at 0.6 (in 128ths). Here the backdrops are drawn into a picture of their own, softened with a
320 x 224 copy, and the car and its shadow are put over it at the car's alpha (0x1D6378); while a car loads the stage stands.

**The reflection** (env.mdl, 0x44EB10; `menu/stage_env.*`): a sphere map seen from 0.8 m above the car, the disc's middle the
direction back toward the camera turned down 15 degrees about the view's x axis (the sign by the captures: the Ka's 20 frames
of the reference's walk video; up, the original's dark sides shine), the picture 0.9 of the disc while the lookup reads it
whole; the four models added at the backdrops' weights. The program makes it each picture; here each model's map is made once
(128 x 128) and added.

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

### The Car database

The home screen's Car database (`menu/car_database.*`, `app/screen_database.cpp`; `gt4tool cardb <disc> list | <car>`)
takes its facts from the game's own code and data; on a Gran Turismo 4 disc Gran Turismo mode's project runs behind the
menus' natives on a new game (the save editor's `SaveEditor`, no page shown, nothing kept).

- **The spec sheet**: the rows of the dealer's purchase dialog (`BuyCarDialog`: Type, Displacement, Power, Torque,
  Dimensions, Weight, Drivetrain, Price) as the car's natives write them (`getCatEngineType` + `:` + `getCatEngineCam`,
  `getCatPs` 0x445EE8 and `getCatTq` 0x445F70 truncated in the unit, with `%d%s/%s%s`, `getCatLength` x `getCatWidth` x
  `getCatHeight`); the units are a new game's for the disc's region (`GameState::option_defaults`). ✔ The MOVE Custom RS
  Limited '02's sheet and the Copen Active Top '02's strip equal the original's (reference `05-gtmode`
  `18_purchase_spec_sheet.png`, `17_dealer_new_car_copen.png`). What the page adds below (wheelbase, tread, gears, tires,
  rev limit, weight over power) is read from the parts' rows (database.md).
- **The curves**: `buildEngineCurveData`'s figures (sim::Showroom::charts, the car as sold, a new engine), in the page's
  units. Prologue's program is not one the showroom car is built for: there the ENGINE row's own curve with its output
  factor (❓ not the program's chart).
- **The parts**: the tune shop's groups (TuningRoot's widgets' `parts_types`: Muffler 20; Brake 1, 2; Engine 12, 13, 15,
  16, 17, 18, 27; DriveTrain 11, 21, 22, 24, 23; Turbo 14, 19, 28; Suspension 3; Tire 25; Others 10, 7, 8, 31, 32), each
  stage the scripts' `TuningApplyRoot::getPartsCondition(car, type, category)` says exists ([exists, owned, allowed, price];
  type 32, the rigidity refresh, Cr. 50,000), named by `getPartsShopName` in TuningApplyRoot's text. Category 0 (the part
  as sold or none) is left out but for the tires (the standard tires, free).
- **The events**: `EventData::AllEvents` (the halls' lists, as the save editor reads them) and, for each, the scripts'
  `getViolatedRegulatoin(car, race)` (`share/regulation.ad`) on the event's first race, as the event's page asks it of the
  car driven: a `Regulation` whose `violated` is set, and each of `drivetrain`, `aspiration`, `cartype`, `tiretype`,
  `need_ps`, `need_price`, `weight`, `length`, `country`, `year`, `carlist` non-nil when that rule is broken. The rows are the
  event page's (`EventInfoDialog`'s texts and the race's `getNeed*` / `getLimited*`, a row only where the rule is set). The
  car is the car as sold (`set_car`); the license an event asks is the driver's, not the car's, and is not counted.
- **Words**: the menus' language's; where the app's font cannot draw them (the Japanese release's kana) the database's
  American car names (the Japanese discs carry them), makers by the words their cars' names share, and our own words.

The scripts compare a length limit with `getLength`'s text: an integer's operators take the other value's integer (hInt
asks it of the operand, vtable +0x58; a string's is strtol base 10, hString 0x314A40 -> 0x5A2F88), as the VM now does for
the comparisons and the arithmetic but `+` (❓ `int + string` and an integer with a float: hFloat's +0x58 not read).

### What runs where (the drawing)

The menus' drawing is this repository's code throughout; where it still leans on the shared
engine's Gran Turismo 3 code or on a judgment:

| What | Where | Runs | The Gran Turismo 4 routine to port |
|---|---|---|---|
| 3D faces' models (car faces' extra sets, model faces, the car stage's backdrops) | `app/menu_cars.cpp`, `app/menu_models.cpp` via `formats/model/mdls.cpp` `to_gtm1` | the engine's GTM1 decoder and renderer (`gtm1.cpp`, `render::View3D`, its GT3 lighting model) | ModelSet2's draw (0x455528, the shape submit 0x49D328) and the VU1 lighting |
| A skinned set's blend | `formats/model/mdls_skin.cpp` `blend()` | the program's VU0 micro-routine (0x2E8 / 0x460) operation by operation in the PS2's floats (`ee::F`; ❓ the VU's own rounding of multiply-add and of the reciprocal root is taken as the FPU's: the race step runs no VU0 float instruction, so no recording made on a console judges it); the palette's matrices are host floats | (the palette: 0x4A7698 and the rotations 0x078 / 0x0B8 / 0x0F8) |
| The menu car | `app/menu_cars.cpp` | `race::CarVisual` (Gran Turismo 4's car parts) drawn by the engine's renderer, with the program's reflection factors; ❓ the ride-height raise | mCarModelPS2's draw (0x156AA0, 0x156F18) |
| The car stage | `app/menu_cars.cpp` | the lights, backdrops, soft focus, reflection picture and the shadow's layers (tires too) as the program has them; no half-size mask; the engine's lighting model | GT4CarSelect::AnimationStage (0x1D5C00: 0x1D56B0, 0x44EB10, 0x1D6968) |
| The car's shadows | `race/car_visual.cpp` | the race's shadow models | the stage's shadow (0x4523B0..) |
| A skinned set's reflection | `app/menu_cars.cpp` `weigh_reflections` | the set's picture, weighted at the vertices; the engine's sphere-map kernel (Gran Turismo 3's) | the VU1 lookup, constants of ops 0x56 / 0x57 |
| Model faces' reflections | `app/menu_models.cpp` | the engine's prize-scene path (`static_env_maps`: Gran Turismo 3's kernel), ALPHA 0x58 as plain addition (the frame alpha's weight left out) | the VU1 lookup |
| Movies | `app/menu_movies.cpp` | the engine's MPEG / IPU player | (same format) |
| Fonts | `menu/font.cpp` | the engine's font reader plus Gran Turismo 4's two bytes; the layout is ours (`text_layout.cpp`, 0x491028) | — |
| Text breaking | `app/menu_page_drawer.h` `wrapped()` | the program's word wrapper (0x245C90) with a margin judged from the pictures (❓); the renderer's per-character break (0x490660); a text box's break judged; 0x490D08 (the renderer's other break, T +0x50) not read | mTextBoxFace's break, 0x490D08 |
| The loading page | `app/menu_loading.h` | the black and the capture modes; their holds judged | LoadingPS2's other modes (0x104B68) |
| The slide show | `app/menu_slides.h` | its timing and places from the program; the card's list or the disc's; our JPEG decoder | — |
| The photo pages' faces | `app/screen_photo.cpp`, `app/photo_faces.h` | [photo.md](photo.md) "What runs where" | |
| The Analyzer's graph (LoggerFace) | `app/logger_face.h`, `menu/logger.cpp` | the program's picture drawn straight onto the page ([logger.md](logger.md)); ❓ its three copies of the face's buffer (the second and third a line up and down at 1/3 and 1/4) are not drawn | 0x120CF0's buffer and blend |
| The cursor, transitions, actors, tool tips, widget transforms, the 3D faces' cameras | `app/menu_cursor.h`, `menu/actors.cpp`, `menu/faces_*` | ported from the program (addresses in the sections above) | — |

### What runs where (the game's state, Arcade's opponents, the cars' figures)

GT4's own routines, checked against the user's program where the note says so:
- the settings pages' charts (0x148B60, 0x148940 through 0x34B960, 0x34B808);
- used cars: makeUsed (0x440FB8), the used price (0x441128, 0x4411F0), from the program's tables;
- the showroom car's figures and evaluation (0x364868, 0x34F3F8, 0x356EA8, 0x3F86E8; bit for bit on every
  car), the engine's condition (0x350868; equal on made-up blocks), the cars' figures through them
  (`updateCurrentStatus` 0x146648, garage entries 0x434FD0, the race menus' grid HP and kg 0x364E90 / 0x364FA8);
- Arcade's opponent picker (0x1308C8; its sort, search, draw and dirt step equal the program's) and the
  program's generator (0x579330, 0x578598: the draws equal);
- the option menu (0x2C9098 open, 0x2C98F0 keys, 0x2C9C90 fade and scroll, 0x2C93C0 close, 0x2C9898, 0x2C9B68,
  0x2C8CC8) and the list box (0x2B29C0, 0x2B3918, 0x2B3670, 0x2B4A00, 0x2B48F0; its drag mode not written);
- the B-spec driver's book and the Status page's figures (0x468B58, 0x468710, 0x468860; game_bspec.h), equal to
  the program's and to the original's page;
- a car's figures in the player's units (0x445F70 / 0x445EE8 the catalog's, truncated; 0x1462D8 / 0x146128 the current
  ones, half added; the units' factors 0x4729A0, 0x472A08; ❓ `GetPowerForDisplay` / `GetTorqueForDisplay`, 0x1AF598 /
  0x1AF760, are not read); the options' `*_volume` (raw / 128, 0x180AA8 and its six likes); the A-spec and B-spec meters
  (0x10FA58, 0x114058, not in license tests, missions or Arcade; ❓ a run is counted as all one driver's); whose car a race takes
  by its sequence (0x10F948 the garage's, 0x10F880 / 0x10F8E0 the event's, 0x10F7D8 the scripts' car_code);
- a championship's grid from its second race on (0x3EE9F8: opponents by kg over PS of their showroom cars, ties in
  entry order, the player last; `championship_grid_places`, `menu/game.h`; the reference's Tuning Car Grand Prix race 2
  grid reproduced with the event's parts and race 1's grid as the entrants' order, ❓ that order is inferred from this one
  championship; the race keeps the entries and takes each one's start slot, 0x3EE7C0);
- the B-spec book's doubled change (0x468A60) and the course's marks' flag (cond + 0xCBE7, 0x34C7E0 / 0x468A40: the
  second character of the course's attribute text), save.md "The B-spec driver's book";
- the completion (Home's "Complete", the halls' locks) by the game's own scripts (`getRaceEventRecordStatistics`,
  `canEntryEndurance`, `canEntryGTWorldChamp`, `canEntryExtreme`; save.md "A game with progress");
- the Status page's transfer from Gran Turismo 3 (and, by region, Prologue): the card manager's probes and transfers
  (0x1D4448 .. 0x1D49C8) over the saves found here, the game's own scripts and texts; the choice among the saves is ours
  (save.md "Earlier games' saves");
- Prologue's game data file and its card pages' saving and loading (`save/prologue_data.*`, prologue.md "The saved game";
  the license units by `prologue_license.*`), its new game's payload equal to the program's byte for byte;
- the event's license in a race's menus (0x110498), Arcade's best A-spec score (0x111E98), the scripts' sort (0x2F03C8),
  a sibling's free name (0x206888, 0x25ADF8), a replay's particulars on the card (0x1CF628, 0x1CF400);
- `MCourseData.GetAttrString` (0x1B3320); the options' fields and the screen's figures (0x436D60); the
  license day (0x110800); the gear ranges (0x3553A0, 0x355278); the race result routines (0x114498,
  0x119CB8, 0x11C328, 0x1105A8);
- the car's condition block back into the garage car after each Gran Turismo mode run (0x10FA58: the step's
  distances and dirt; a run without Gran Turismo 4's step adds its distance once, `add_distance`), each run of a
  sequence starting from it (0x10F948); a course visit's one diary day at its sequence's end (0x1179F8, 0x1184DC,
  0x11B0D8, 0x115E90); the options' copies (getOption 0x161B88, setOption 0x161C38); setReturnToLastMenuProject
  (0x166398); the title's demonstrations' counts (MDemonstration, 0x10BE20, 0x434770; the kinds no disc script sets
  play nothing).

- **A stream's level**: `startStreamEx`'s code is turned into a kind (0x4607A0: "pcm" -2, "narration" -3, else a
  sequence, -1), and 0x4608C8 branches on it. A stream (-2 -> 0x460220 -> 0x45FFF8 -> 0x45FFC0) plays at the script's
  volume x sdvol.dat's column 0 (0.3), with no option in it; only a sequence (-1 -> 0x461978 .. 0x461620) takes column 1
  (0.48) and menu_bgm_raw / 128. The pre-race jingle's volume is MOption's `menu_bgm_volume`, raw / 128 (0x180AA8):
  0.3 x 115 / 128 x 16383 = 4415.7, the 4416 the original's sound chip holds. (`RaceBGMPS2::vf5`, 0x33D568, puts the
  option in only for its code 12.)

Still the shared engine's code or a stand-in (file, function → the GT4 routine that should replace it):
- `widget_classes.cpp` the select box's and scroll window's routines are read (0x2DA2B0 on, 0x2D2720 on); the
  closed option menu shows its chosen row in place of the program's `selected_item` copy (the same picture);
- `app/screen_race.cpp` the results movie's points pages (isChampionship, isStage1, getPoint, the ranking's
  rows): what the host answers in each phase is judged from the movie's calls, not read from the program's
  result page (0x3E0xxx) ❓;
