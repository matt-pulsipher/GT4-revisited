# Menus: layouts, pictures, text, fonts

Code: `src/gt4r/menu/` (`layout.*`, `image_pack.*`, `text_table.*`, `font.*`, `project.*`,
`runtime.*`); `gt4tool menu`, `gt4tool menu-run`; the app's *Menu pages* screen (`src/gt4r/app/screen_menu_viewer.cpp`).

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

❓ Not drawn: `round`, `rotate`, `scale`, `skew`, `texcoord`, the boxes' packing (`VBox`,
`HBox`: children carry their rectangles already), `ModelFace`, `CarFace`, `MovieFace`,
`SceneViewFace`, the tool tips. The widgets' behaviour (focus, navigation, the actors that
move and fade them) is the program's and the scripts'.

## Running (`runtime.*`)

A first part of the program's menu system, enough for the scripts to open their pages and
answer keys: the title page builds its own menu and leads on (*Menu pages*, R1 on a project;
`gt4tool menu-run <disc> arcade down accept`). The program's side is written from what the
scripts ask of it; nothing here is compared with the program's own code yet, and what is not
written is a stand-in that records the call (`gt4tool menu-run ... stubs` lists them).

How a project runs, as far as it is written:

1. Each widget of the layout is a module of the script world named as in the layout
   (`ArcadeProject::TopRoot::StartScroll::SelectBox`): its children are members, its
   properties are attributes (`widget.visible = 0`, `label.text = ...`, `x`, `y`, `w`, `h`).
2. The project's script runs (`projects/<name>/<name>.adc`): its `module` blocks add the
   event functions to those modules.
3. The project's `onLoad(context)` is called. It opens a page with
   `context.startPage(page)` (through the script's own `start_page`, which queues an event).
4. A page that opens gets `onInitialize(context)`; one that closes, `onFinalize(context)`.
5. Keys: the directions move the focus; accept calls `onActivate(context, event)` and cancel
   `onCancel(context, event, item)` on the focused widget, then on each parent up to the
   project, until one returns a number other than 0. `event.widget` is the focused widget.
6. `context.finish()` ends the project; `main::game.next_menu_project` names the next one.

| Of | Written |
|---|---|
| `context` | `startPage`, `pushPage`, `closePage`, `finish`, `pushEvent`, `translate(page, key)`, `cursor_visible`, `transition` (nil); `sync`, `flushKeyEvent`, `createRenderContext` do nothing |
| `menu::MScriptEvent(context, module, function, arguments)`, `menu::MFunctionEvent(function, arguments...)` | Queued by `pushEvent`, called on the next frame |
| `Button`, `OptionMenu`, `SliderBar`, `ScaleBar`, and any widget with `can_focus` | Take the focus |
| Any widget | `doCopy()`, `appendChild([context,] widget)`, `clearChildren()`, `clearWindow()`, `first`, `last`, `next`, `prev`, `setFocus(widget)`, `actor` |
| `menu::MFadeActor(widget, alpha, alpha when out)`, `menu::MMoveActor(widget, x when out, y when out)` | The widget's `actor`: `out` switches between the two states, `period` (seconds, fade) and `ratio` (of what is left each frame, move) set the speed, `warp()` jumps. ❓ The curves are not the program's yet (its fade period starts as 1/30 s) |
| `SelectBox`, `SelectBar` | Show one child at a time: `index` chooses it, the keys along the box move it (`repeat`: around the ends), the chosen child has the focus. ❓ Their placing is by eye |
| `main::game` | `next_menu_project`, `next_menu_arg`, `major_menu_project`, `major_menu_page`, `last_menu_project`, `option.special_arcade_tuner`, `option.can_watch_ending` |
| `main::sound.play(name)` | Recorded |
| `menu::MSystem.GetRegionCode()` | `us` |

❓ Next: the other actors (`MBlinkActor`, `MRotateActor`, `MChaseActor`, ...) and the two written ones as the program has them,
`context.sync` (a script waits while the page draws: scripts are threads in the program),
the watcher (`MScriptWatcher`: a function every so many frames), the other widgets' own
functions (`OptionMenu`, `ListBox`, `ScrollWindow`, ...), `main::game`'s state (the garage,
the options, the memory card: the boot project waits on the memory card manager), focus
navigation as the program does it (`navigate_target`, `navigate_source`), the main script
(`projects/GT4Application.adc`) and the configuration it loads.

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
