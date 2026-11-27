# Playing GT4 Revisited

GT4 Revisited runs Gran Turismo 4 from your own disc image. This page says how to start, how to
play, what works and what does not yet. It describes the US disc (SCUS-97328); the European (SCES-51719) and Japanese
(SCPS-17001) discs and the Japanese Prologue disc start the same way. The European disc asks for its language (English,
Français, Deutsch, Italiano, Español) at every start, as the console does, and sets the units by it.

## Starting

1. Start `gt4r`. Without a disc it asks for one: *Choose a disc image* (Enter or Cross) opens a file window for
   your Gran Turismo 4 or Gran Turismo 4 Prologue disc image (`.iso`, or `.chd` made from one), or drop the file
   on the window. Nothing is copied from the disc; the game's files are read from the image as they are needed.
   The disc images opened before that are still there are offered below it. A file that cannot be played says
   why and what to do: a file that is not there any more (moved, renamed, a drive not connected), a file that
   is not a disc image, another game's disc (a Gran Turismo 3 A-Spec or Gran Turismo Concept image says so), an
   image cut short or damaged (make it again from your disc).
2. With a disc chosen, the game starts as the console does, without its questions about a memory card. The first
   time, it asks for your name (the game's own name entry; your game is made then, so everything from the start is
   saved); then the Polyphony Digital logo, the opening movie (Start skips it), the title. The disc is remembered:
   the next launch goes straight there. `gt4r <disc image>` starts with that disc.
3. Your saved games, options and replays are kept in the app's data folder (on macOS
   `~/Library/Application Support/GT4Revisited/GT4Revisited/`; a `portable.ini` beside the program
   keeps them beside it: on macOS in the folder that holds `gt4r.app`, not inside the app, which would break its
   signature; `GT4R_DATA_DIR` names another folder). The game saves itself after every race and purchase (Options,
   Miscellaneous, Autosave) and the next launch goes on with it.
   **Saved games** (the home screen, the title's *Save Game*, Prologue's *Save* and *Load*) lists them: play one, start a
   new one beside the others, delete one, *Export to PS2* (a memory card image the original loads) and *Import from a PS2
   memory card image*.
4. GT4 Revisited's own menu, the home screen, is underneath the game: press **Select** on the title (on a keyboard Tab),
   or click or tap the **GT4R menu** button beside it. The button is beside Gran Turismo mode's map too (there Select is
   the map's own: a pad reaches the home screen through the title). Leaving saves the game first, as it saves itself
   (Options, Miscellaneous, Autosave). (On Prologue's board, Select stays the board's while a sign has the
   focus: Triangle first goes back to its menu.) The home screen: *Play* (the game from the start: the opening movie, then
   the title), *Quick race*, *Saved games*, *Settings*, *Controls*, *Mods*, the *Car database*, the *Save editor*, *Discs...* (the disc library: switch to
   another disc, or add one), *Developer tools* (viewers of the disc's cars, courses, menu pages and files, and *Drive*, a
   race set up by hand) and *Quit*. *Settings > Disc & saves > Starts in* opens the home screen first.
   The home screen and the app's own pages take a pad, the keyboard and the mouse (a click chooses, the
   wheel scrolls, the right button goes back) and a touch screen (a tap chooses; **< Back** at the top left of Settings, Controls,
   Saved games, the Save editor and Developer tools goes back, **< Back to game** where the page was opened from the game's);
   the game's pages a pad, the keyboard or the on-screen controls.
5. The *Car database* lists every car of the disc (by maker, name, power, price or year: Square, or the *Order* button) with
   its picture and four tabs: *Specifications* (the dealer's spec sheet in the game's units for the disc's region, then the
   wheelbase, gears, tires and more), *Power & torque* (the engine's curves as the settings pages chart them), *Tune shop
   parts* (what the tune shops sell for it, with prices) and *Events* (which of Gran Turismo mode's events the car as sold
   may enter, and the rules it breaks in the others). Up and down choose a car, left and right the tab, Cross moves into the
   details to scroll them; a click or tap chooses, the wheel or a finger's drag scrolls. Prologue's has the first two tabs.
6. The *Save editor* changes a saved game: credits and the day, the garage (any car of the game, its paint, parts and
   settings), licenses, events, missions, prize cars, records, what Arcade mode has open. Nothing is written until you
   save, and the save as it was is kept in the save folder's `backups/` first. It also opens PlayStation 2 memory card
   images. [SAVE_EDITOR.md](SAVE_EDITOR.md).
7. *Quick race* races any event of the game with any car, with nothing to unlock and nothing saved to your game: every race
   of Gran Turismo mode's event lists (a championship's races one at a time), the license tests (the test, or a free run on
   its course), the missions, and Arcade's single race on any course with Arcade's difficulty and laps. Choose the event,
   then the car (by maker or every car by name) and its color, the gearbox, and Start: the race runs with the game's own
   pages before and after it, and Exit there comes back to Quick race. The event's entry rules are not asked for. The last
   choices are kept (`config.ini` `[quickrace]`). On Prologue, Quick race is its arcade's single race on any of its courses
   with any of its arcade's cars, at its three difficulties (no custom race).
   Its first item, *Custom race*, is your own race: any course, the laps, your car and color and your place on the grid, and
   up to as many opponents as the course's field holds, each any car in any color. *Fill the grid like Arcade* draws the
   opponents as Arcade does for your car at its difficulty level (-10 to +10); then change any of them. The race is Arcade's
   single race (its start, tires and pages). Gran Turismo 4 sets no skill per driver (Arcade's difficulty only chooses which
   cars race), and no race of the game has a catch-up: the opponents drive as Arcade's do. *Save as...* keeps a race under a
   name (the same name replaces it); saved races follow *Custom race* in the list ({Square} deletes one).

## Controls

In the menus a pad works as the console's (the Gran Turismo layout of a DualShock 2: Cross confirms,
Triangle and Square go back). In races, unless you change it:

| | Pad | Keyboard | Steering wheel |
|---|---|---|---|
| Accelerate, brake | right, left trigger | Up, Down (or W, S) | the pedals |
| Steer | left stick (or the direction buttons) | Left, Right (or A, D) | the wheel |
| Shift up, down (manual gears) | Cross, Square (A, X) | E, Q | right, left paddle |
| Handbrake | Circle (B) | Space | Circle |
| Reverse | L1 | R | Triangle |
| Change view | Triangle (Y), Select | C, Tab | Select |
| Look back | right stick pressed | X | the direction buttons' Down |
| Nitrous | R1, direction buttons' Down | F | R2 button (Controls: *Nitrous*) |
| Pause | Start | Escape, P | Start |

Menus on a keyboard: arrows move, Enter (or W) is Cross, Escape and Backspace Triangle, Space Circle, S Square,
Q and E L1 and R1, Z and C L2 and R2, P Start, Tab Select. On the name entry a keyboard types: each letter presses the
page's own key (the game's capital and small letters as its keyboard gives them), Backspace deletes, Enter is the page's
Enter; its keys can also be chosen with the arrows, or tapped on a phone (whose own keyboard stays closed). F11 switches full screen (remembered), F12 saves a screenshot in `screenshots/` in the data folder. The
mouse's arrow goes after two seconds without it.

**The game's own Options** work as the console's, but for the controller pages: the button configurations (every pad's and
wheel's, both players'), *Vibration* and *Steering* show a **Controller Settings** button instead of their table (Cross,
Select, a click or a tap). It opens our Controls, which holds what those pages set: for that page's player (the first
from Vibration and Steering), at the wheel's rows from a wheel's page, the rumble from Vibration, the steering from
Steering. Triangle there comes back to the page, and Triangle on the page back to the Options. The race's options: the
gearbox for Gran Turismo mode's races, license tests and missions; Arcade asks on its own pages.

**The home screen's Controls** (Select on the title, then Controls; player 2's in Settings > Controls; also the Options'
controller pages' **Controller Settings**) has
what a computer's devices need: which device drives; the race controls, Modern (the table above), Gran Turismo 4
original (always the console's pad: its keys listed as your saved game has them, with *Restore the game's layout*) or
your own bindings (Custom, once there are any); the steering's dead zone and response; the rumble, Off (the game's own
vibration option), Modern (Automatic is Modern too: hits, rough ground, kerbs, slides, on both motors) or Gran
Turismo 4 original (the console's one level on the large motor, as the game sends it); *Steering (the game's own
options)*: active steering, the GT FORCE and Driving Force steering type (Amateur to Simulation), the Driving Force
Pro's power assist and force strength; the triggers' effects (a
DualSense's brake resistance and the edge of grip under the throttle, an Xbox pad's impulse triggers); tilting a
controller to steer; for a wheel which of the console's wheels it is, how far it turns, and its force feedback (one
setting with the game's own); what
each control is bound to; the songs' controls; the menu buttons; and bars that show what the race reads. On a
control, Cross binds it, Square adds one more input to it, Select (Delete) clears it. *Songs*: the next song of the
race's music and the one before, on any buttons or keys held together (none to begin with). Every controller and the
keyboard work on this page, whichever device is chosen. The game's own options (rumble Off, the steering, the force
feedback, the original layout) are the game's: opened from the game they change the game being played, which saves
them as it saves the Options; from the home screen they change the saved game being played, written when you leave the
page (a backup first, in the save folder's `backups/`). The rest is kept in `config.ini` when you leave the page:

```
[player1]
race = modern            ; modern | original
rumble = auto            ; auto (Modern) | modern | original
[wheel]
kind = driving_force_pro ; driving_force_pro (900 degrees) | driving_force (200 degrees)
rotation = 900           ; how far your wheel turns, lock to lock, as its own driver has it
[ffb]                    ; wheel, wheel_mode (auto | modern | original), strength, steering, spring, understeer,
                         ; triggers, trigger_force, invert
```

**Settings** (the home screen) has pages for the display, the sound, the controls, the disc and the saves, and the mods.
Every change is applied at once and kept in `config.ini`; the 3D renderer is taken at the next start (the row says so).

- *Display*: window or full screen; the aspect ratio (Fill the screen, the default on every platform: races and
  replays fill it whatever its shape, and the game's pages stay 4:3 in the middle; or 16:9 or 4:3 throughout); vsync; the frame rate (the display's, or about 60 on a faster one); a frame
  rate counter; gamma over the whole picture; anti-aliasing; anisotropic filtering; the 3D views' resolution, and
  whether it is lowered while the GPU is too slow; the menus' cars and prizes at the screen's resolution or the
  console's 640x448; the 3D renderer.
- *Audio*: the output device; the music's and the effects' volume, on top of the game's own options (both at 100% to start, so the
  game's own option levels are what plays); whether sounds behind you
  are sent as the console sends them (one side turned over, for a surround decoder) or plain. *Your music* (below).
- *Controls*: both players' controls; whether controllers keep driving while another window is in front; on Windows how
  Xbox controllers are read.
- *Views & replays*: the views the change-view control goes through and whether the in-car view shakes; in replays the
  heat haze, the vignette, the motion trail and the depth of field. A race takes these as it starts and as its pause closes.
- *Disc & saves*: the disc in use, whether its files are a tested release's own and whether the image is complete; the
  disc images opened before (Cross opens one, Square takes it off the list); the folder the games, replays and photos
  are kept in (a new one gets a copy of what it does not have; nothing is written over); the server of the races over the
  internet (none until you choose one); what opens at the start; the data
  folder; the log of this session and of the one before (`gt4r.log`, `gt4r.prev.log` in the data folder: an uncaught error or
  a crash is its last line; your name is taken out of the paths); the version. *Updates*: *Check for updates* at startup
  (the default) or only when asked; at startup it asks only the internet server you chose, and *Check now* asks that one or
  else `gt3r.gameversions.com`. When a newer release exists, or online races need a newer one, a window over the home
  screen or the title says so once per release, with *Download update*, *Later* and *Release notes*; playing on this
  machine and over the local network is never blocked.
- *Mods*: below.

```
[video]
fullscreen = off         ; on | off
aspect = fill            ; fill | 16:9 | 4:3
vsync = on               ; on | off
limit_60fps = off        ; on: about 60 frames a second on a faster display
show_fps = off           ; on | off
gamma = 1.00             ; 0.50 .. 2.00
msaa = 4                 ; 1 | 2 | 4 | 8
anisotropy = 4           ; 1 | 2 | 4 | 8 | 16
render_scale = auto      ; auto | 0.5 .. 4.0
dynamic_resolution = off ; on | off
menu_models = screen     ; screen | console
gpu = auto               ; auto | gles | gles_driver | off
texture_packs = on       ; on | off
heat_haze = off          ; on | off (replays, as the three below)
replay_vignette = on     ; on | off
replay_trail = on        ; on | off
depth_of_field = high    ; off | standard | high
[cameras]
race_views = original    ; original | all
in_car_shake = on        ; on | off
[audio]
device =                 ; an output device's name; empty: the system's
music_volume = 1.00
effects_volume = 1.00
rear_cue = on            ; on | off
[music]
folder =                 ; empty: the data folder's music/
source = yours           ; game | yours | both (unset: yours, the game's until a song is found)
next_race = next         ; next | same
show_song = on           ; on | off (the song on the race's pause, L1 / R1 there)
race_song =              ; the song the last race ended on (disc:<piece> or yours:<id>)
[saves]
folder =                 ; empty: the data folder
[game]
start = game             ; game | hub
```

**Mods** are folders in `mods/` in the data folder: `mods/<name>/mod.ini` (`[mod]` with `name`, `version`, `author`, `description`,
`priority`, `discs`), `files/` with files that stand in for the disc's (or add to them) under the same paths, and
`textures/` with pictures. *Install a mod* takes a `.zip`, as does dropping one on the app's own pages. Each mod is
used or not for each disc, and where two change the same file the upper one of the list wins (*Move up* and *Move
down* on a mod's page write its `priority`). A texture pack's pictures (PNG, any size up to 4096 a side) are named by
the texture they replace: *Save the game's textures* writes every car's, course's and figure's texture under that name
(`gt4tool textures <disc> <folder> car/lod/fdeu0006` a few of them). Packs change what is loaded from then on. They replace
the textures of the 3D models; the menus' pictures, the fonts and the race display are not replaced. Mods' cars are
below; their events and liveries are not read.

**Mod cars** are text files, `mods/<name>/cars/<label>.ini`, each naming a car of the disc it is made from (its donor)
and what is different; the file format is GT3 Revisited's (its MODDING.md, "Adding cars"), with this game's limits:

```ini
[car]
label = my_coupe_26          ; a-z, 0-9 and _, at most 40; not a car of the disc
name = My Coupe `26          ; as the menus and the race show it
maker = Mazda                ; a maker of the game: its dealer sells the car (else the donor's maker)
year = 2026
price = 31500                ; credits (the donor's when left out)
category = tuning            ; normal | racing | tuning | concept
donor = impreza_wrx_sti_00   ; needed: every part the file leaves out is the donor's (gt4tool car <disc> lists the labels)
model = rx8_03               ; optional: another car of the disc whose body and paints it has (else the donor's)
fit_body = yes               ; that body stretched to the dimensions below (no: drawn as it is)
[body]
weight_kg = 1180             ; also front_weight_pct, length_mm, width_mm, height_mm, wheelbase_mm,
front_track_mm = 1500        ;   rear_track_mm, ride_height_mm, cd, frontal_area_m2;
                             ;   scale_suspension, scale_brakes = yes | no (springs and brakes for the new weight)
[powertrain]
power_kw = 220               ; or power_hp / power_ps: the donor's curve made this strong; or torque_nm / torque_lbft
max_rpm = 8000               ; the rev limit; also idle_rpm, output_pct, sound_id (an engine sound of the disc),
; curve = 1000:200 4000:260  ;   layout, aspiration, displacement (the specs' texts); curve: rpm:Nm, up to 24 points
[drivetrain]
drive = FR                   ; FF | FR | MR | RR | AWD; front_torque_pct for AWD
gears = 3.2 2.1 1.5 1.15 0.92 0.78   ; up to 11
final_drive = 4.1            ; and reverse
[tires]
front = 225/40R18            ; rear = ..., or size = ... for both; width and aspect in fives
grip_pct = 105               ; its tires as sold
[dealer]
for_sale = yes               ; at its maker's dealer in Gran Turismo mode
thumb = images/thumb.png     ; the car strips' picture; picture = the showroom's; logo = the name's artwork (PNG)
[model]
file = models/my_coupe.glb   ; a body of its own (binary glTF, as GT3 Revisited's MODDING.md "Car models" says); also
                             ;   menu = (the dealers' and showrooms'), race =, day =, eve =, night = (a race's by the time
                             ;   of day), each falling back to the others; lower_mm = moves it down
[paints]                     ; a body of its own's colors, in the menus' order: name = #RRGGBB
Carmine Red = #9B1B24
[opponent]
races = am_sunday_*          ; Gran Turismo mode's races whose opponents it may be drawn among (gt4tool ai <disc> events
                             ;   lists the labels; * at the end: every race whose label starts so), as sold
[raw.CHASSIS]                ; bytes of the car's own rows of BRAKE, SUSPENSION, CHASSIS, RACINGMODIFY, DRIVETRAIN,
0x1D = 120                   ;   GEAR, ENGINE, FRONTTIRE or REARTIRE (docs/formats/database.md): 0x30:u16 = 1000
```

The car is made of the donor's rows of the game's own database (docs/formats/database.md "Mod cars"), so it races,
is sold, tuned, saved and replayed as the game's own cars are; it is drawn with its body car's model stretched to its
length, wheelbase, width and height (the wheel arches over its wheels), or with a body of its own in that model's place
(the body car's wheels, lamps, shadow, driver and camera places; glTF's metres, +Y up, the car facing +Z, the origin on
the ground at the wheelbase's middle; materials by name as GT3 Revisited reads them, the paint ones in the [paints]
color chosen), and with its body car's pictures or the mod's. A mod car is at the end of its maker's dealer and of its maker's page in Arcade (always available
there). The Mods page lists each mod's cars, and why one is not in the game; `gt4tool modcar <disc> <mods folder>
[disc:<label> ...]` does the same and prints the weight, power, gears and tires the race builds of each, next to its donor's. A save keeps the car by a code its label
gives: with its mod removed (or switched off) the garage leaves the car out and the save keeps it as it was, the car driven
too (no car is driven meanwhile); with the mod back the car is there again. A mod installed while the game runs (the Mods
page, a download in a Multi-LAN Race) adds its cars to the dealers and pages at once; a mod removed while the game runs
leaves its cars there until the game starts again. In a Multi-LAN Race a player who lacks
another's mod car, and has its donor, gets its file and pictures from him: they are checked and installed as a mod of
their own (`mods/dl-<number>/`). Gran Turismo 4 Prologue takes no mod cars. The Car database's *Export* (Start) writes the car shown whole as a mod, `mods/cars_<disc>/`, for your other discs (it is
off on the disc it came from): its rows of the database in a records file, its models and pictures. A disc that has the
car makes it from its own; one that lacks it (the European disc's Vauxhalls on the American disc) takes the rows from
the file, the rows it has under the same names its own. The exported files are your disc's: keep them for your own use;
online, an exported car is never sent. Not yet: mods' makers (`makers/`), another rim for a body of its own
(`[model] wheel`). A file with `type = electric` is not loaded: the game has no drive without an engine (its motors,
the Priuses', the Insight's and the DN-X's, each work beside one; a car made from one of them keeps it), and a drive of
ours would behave as no car of the game does; one gear and a curve of your own come closest.

**Your music.** MP3, FLAC, WAV and Ogg Vorbis files in the music folder (by default `music/` in the data folder, any
depth; on Android the Files app shows it as *GT4 Revisited > music*) play as race songs. Settings > Audio > *Race songs*:
the game's music list (the jukebox's, on Gran Turismo mode's map), *Your songs*, or *Both, mixed* (shuffled, each song
drawn from both lists by how many each has left in its round; in order, one of each in turn). The order or shuffle is the
game's list's own (the jukebox's play mode), for yours too. *Your songs' list* checks songs on and off; songs added to the
folder are checked. Each of your songs plays as loud as the game's list's pieces on average (measured as it first plays,
and once for eight of the game's list's pieces between races; songs not measured yet start as they are and ease in). *Music folder*
chooses another folder (on Android the system's folder picker, after allowing music access; a folder in a cloud drive's app
does not work), *Songs* reads it again. Other formats (AAC, Opus, WMA) and damaged files are counted and skipped. *Next
race starts with*: the next song of the list, or the song the last race ended on. The race's pause shows the song's title
and artist with L1 / R1 to skip (held, every 0.3 s; the song waits silent until the race goes on); the songs' controls
(Settings > Controls) and the on-screen song buttons (shown from the layout's settings) skip while driving, and show the
new song for a moment. The replay's caption names your song by its tags.

A mod's recording of the menus' music plays in place of a piece of the sequenced music: `files/music/<region>/<piece>.wav`
(or `.flac`, `.ogg`, `.mp3`; also `files/music/<piece>.<ext>`), `<piece>` being the piece's file name in the disc's
`mid.inf` without `.mid` (`gt4mn-02` for the map's second piece, `gt4ac-01` for Arcade's: `gt4tool sound <disc> music`).
It loops at its own loop points (a WAV's `smpl` loop, or `LOOPSTART` with `LOOPEND` or `LOOPLENGTH` tags in a FLAC or Ogg
file), else from its start, at the level the game streams its music at; a fade out of the menus' music stops it at once.

A race pauses when its window loses the focus (unless the controller is set to keep driving) and when
the pad or wheel is unplugged. Rumble, triggers and force feedback are sent only while you drive: they
stop on a pause, a replay, the menus and the end of a run.

## Racing others (Multi-LAN Race)

Arcade's *Multi-LAN Race* races other copies of the game, up to six players, each with his own car, with the game's own
pages. Both need the same disc (a modified copy meets only the same modified copy), and Gran Turismo 4: Prologue has no such
race.

1. Arcade, *Multi-LAN Race*. The page that opens connects: a lobby found on the local network is listed (Cross joins it);
   *Open a lobby* makes one; *Join by address* takes the address the host's panel shows (UDP port 47634). *Open a lobby with a
   code* and *Join with a code* go through a server, for players who are not on one network: the first time it asks which
   server to use, and nothing is sent to any before that. Your own server can be typed.
2. The host chooses: how many players the menus wait for, laps, tire wear and boost (the 2 Player Battle's options), a power
   limit, garage cars or cars as sold, every car or the favorites alone (as the original), and above which round trip two
   cars do not touch.
3. *Waiting for connection*: a panel lists the players with their links and the rules, and for a lobby with a code the code
   to pass on. The host's Start begins with those who are there. Select opens the panel on the pages after it: the host
   removes a player with Square, and Start leaves the lobby.
4. The course: whoever takes one first takes it for all. Then everyone's car: the makers, the garages of the memory cards
   (1 and 2), the favorites; its color, gearbox, aids and tires. The race starts when everyone is through. A car the host does
   not let race is told there; a car that is different on another machine's disc is named on the panel and the race does not
   start with it.
5. The race goes on for nobody's pause: Start opens a window to leave it. The names stand over the cars, the map has the
   players' letters, the list at the right the order. A player whose connection goes leaves the race; the others race on.
6. After the results the host takes everyone back to the course page for another race.

Copies of two versions of the game may not meet: the page says which must be updated. The server is kept in `config.ini`,
with the rules a host chose:

```
[online]
server =                 ; none until you choose one (Settings > Disc & saves, or the page that asks)
port = 47634             ; the local network's (the next three are tried too)
players = 2              ; 2 .. 6: how many the menus wait for
max_power = 0            ; PS; 0: no limit
garage_cars = 1          ; 1 | 0: cars as sold alone
any_car = 1              ; 1 | 0: the favorites alone
ghost_ms = 150           ; contact up to this round trip; 0: always
```

## What works

- **The boot**: the logo, the opening movie, the title with its
  moving background. A game saved in Gran Turismo mode starts again on its map, as the console
  does.
- **Arcade**: Single Race (course categories, the course pages, every maker and car, paints, the
  driving options), the race's menus before and after a race, the race itself (A-spec with the pad,
  B-spec watching the computer drive), the results, the replay, back to Arcade for another race.
  Time Trial and the other race kinds reach their races the same way. The tires of the driving options are the race's, on
  every car of it (dirt and snow courses put on theirs).
- **2 Player Battle**: the course, each player's car, paint and driving options, the race on a split screen (one half above the
  other, or side by side with the options at a 16:9 screen) with its handicap, boost, tire damage and laps from the options, the
  results, the replay. The second player's controller is the second one connected (Controls); without one, his keys of the
  keyboard. The first player's Start pauses.
- **Options**: every page opens and keeps its values in the data folder; the laps, the units and
  the race settings (the gearbox among them), the button configurations, the vibration and the wheel's
  settings take effect in the races.
- **Gran Turismo mode**: a new game (name, introduction), the map, the used car and new car
  showrooms (buying a car, getting in it), the events (the Sunday Cup and the others: the event
  page, the regulations, the pre-race page with the drawn field), racing them for real (A-spec or
  B-spec), the results, the prize page paying the prize, the day going by, the game saving itself
  when you come back to the map, and loading it at the next start.
- **Gran Turismo 3 save transfer**: on Gran Turismo mode's Game Status page (Home, Status), Start ("Transfer game data")
  takes the B and A licenses and up to 100,000 credits from a Gran Turismo 3 game, once each, as the game does from a memory
  card. A page lists the GT3 Revisited games on this computer (its own folder, or the save folder chosen in its Settings;
  the one it plays first) and *Choose a file* takes a GT3 Revisited save (`.sav`), a PS2 memory card image (`.ps2`, `.mcd`), a
  `.psu` or the save's data file, of the US Gran Turismo 3 A-Spec; a card's `BASCUS-97102GAMEDATA` folder or card image put in
  the data folder's `memorycard/` is offered too. On Android and iOS GT3 Revisited's folder is that app's own: choose its save
  with *Choose a file* (on Android the file picker shows GT3 Revisited's files; its saves are in `saves/`). The game then asks
  as it does with a card, naming the save; nothing is written to it. `GT4R_GT3R_DIR` names GT3 Revisited's folder.
  GT4 Prologue games count too for the licenses (B after its first six lessons, B and A after its first 25, as the Japanese and
  European games give them): the Prologue games saved in this app, or a Prologue card image, `.psu` or save file chosen. A
  Prologue game has no credits: the credits are then asked for from a Gran Turismo 3 game.
- **Races**: the courses drawn in full with the computer drivers racing, the three driving views,
  the race display (in the units the options set), the B-spec display and commands, the pause
  menu, the results, the replays through the course's cameras or the car's own.
- **License tests**: the License Center, a test's page, the test driven with its own display and
  trophy times, ended when the car stops in the box or fails (cones, walls, off the course), its
  results with Retry and Exit, the trophy on the license's grid.
- **Later in Gran Turismo mode**: the halls of Professional Events, a championship of five races
  (each race's pre-race page with the points so far, Session Results, Next Race, Abandon), the
  dealers' tune shops (buying a part and fitting it), the calendar going by a day a race.
- **Photo Travel and Photo Lab**: the 15 places with the car you drive; the map (place the car and the
  camera: select them with Cross, the left stick moves them, L2 / R2 turn the car, L1 / R1 zoom), the
  camera (hold Cross on a button, or L1, L2, R1, R2, with the left stick), lens, aperture, focus, filters,
  the wheels' angle, portrait, the aspect and quality options; the photo, saved (Start) with the game's save files in
  the original's format; Photo Lab's list, preview, renaming, deleting and the slide show.

## Watching a replay

Up and Down follow the car ahead or behind. Cross changes between the course's cameras and the car's own: there R1 goes through its
places, L1 looks back, L2 and R2 are the cameras beside the cabin, Left and Right held look to the sides. Triangle held zooms in.
Square changes the playback (without the race display, Dive, with it), Circle switches the music, L3 plays four times as fast (Dive cuts to the music, between all the cars; there Cross cuts),
Select holds the picture, Start leaves. *Settings > Views & replays* has the depth of field, the vignette, the heat haze and the
in-car camera's shake; its *Views* "All" adds a view without the mirror, the front bumper, a farther chase view and the course's
cameras to the race's views. On a pad whose right stick does nothing else it looks around in the in-car and chase views.

## Known problems

The ones a player meets first:

- Discs: Gran Turismo 4 US (SCUS-97328, v1.01), Europe (SCES-51719, v1.00), Japan (SCPS-17001, v1.05) and Gran
  Turismo 4 Prologue (Japan, SCPS-15055) are the tested releases. Another release opens with "not a tested release" on
  the home screen, and parts of it may not work. The Japanese release races on the US release's driving, computer-driver
  and race rules for now. The European release races at 60 frames a second like the others, with its own rules.
- Photo Lab: films cannot be developed; the printer page finds no printer, and the USB memory is the data folder's
  `photos/`.
- Mods: files, textures, texture packs and cars are used; mods' events, liveries and makers are not read yet, and a mod
  car has no body or electric drive of its own yet.

## Scripted play (for checks)

The app can be driven without a pad, for pictures and long runs:

```
GT4R_DATA_DIR=<a scratch folder> SDL_AUDIODRIVER=dummy gt4r <disc> \
    --keys "wait120,left,cross,wait60,right,cross,wait700,start,wait150,shot:title" \
    --shot-frame 1200 --shot out.png --stats stats.txt
```

- `--keys`: presses (`cross`, `down`, `start`, ...), `waitN` (N frames), `shot:<name>` (a
  picture beside `--shot`'s), `quit`; the mouse on the app's pages (`click:x:y`, `move:x:y`, `back:x:y`,
  `wheel:turns:x:y`, `drag:x:y:x:y` in 640 x 360 units), `drop:<file>`, `f12`; race controls held until changed: `gas=0..1`,
  `brake=0..1`, `steer=-1..1`, `reverse=0..1`, `handbrake=0|1`, `lx=` `ly=` `rx=` `ry=` (the sticks, -1..1),
  `l1=0|1` and any other button so (held), `release`; the second player's pad: `gas2=` `brake2=` `steer2=`
  `reverse2=` `handbrake2=` and `p2:<button>`; `@<file>` reads keys
  from a file (`#` comments; files may name files).
- With `--shot` a frame is a 60th of a second and the frames are held to that pace, so the
  movies keep step with the keys (`--fast`: not held).
- `--stats <file>`: a line a second: the frame, the seconds since launch, the frames' mean and
  longest work in ms, the resident memory in MB; without a window also the frames' mean wait for the GPU in ms,
  which the work is then without. With a window the work has the presenting in it, and in a run nothing paces that is
  the wait for the display.
- `GT4R_MENU="boot=1,photo=ss_times,car=ford_gt_05"` starts a photo shoot at once; `GT4R_MENU_LOG=1` logs
  what the menus do (`calls`: each script call too).
- `GT4R_MENU` and `GT4R_RACE` set up the menu pages and *Drive* screens for a check (the tops of
  `src/gt4r/app/screen_menu_viewer.cpp` and `screen_race.cpp`).
- `--screen quickrace` with `GT4R_QUICKRACE="event=am_sunday_0002,car=ford_gt_05,start"` starts a Quick race at once
  (`event=arcade:tsukuba`, `license:l0b0001`, `mission:<label>`; a custom race `page=custom,course=tsukuba,grid=ka_01:2/cobra_67:0,place=1`;
  the top of `src/gt4r/app/screen_quick_race.cpp`).
- Scripted runs (`--shot`, `--sweep`, `--keys`, `--stats`, or `GT4R_RACE`, `GT4R_MENU` or `GT4R_VIEWER` set) show
  nothing: no window, no Dock icon, nothing to take the focus; they play no sound (`SDL_AUDIODRIVER` names another
  driver), leave the display to sleep, and read no key, mouse, touch, pen or controller but the script's (no controller is
  opened, so none rumbles). The pictures are the same as with the window. They draw with the same GPU into the same
  size of picture; nothing is presented, so neither the display nor its rate holds them: `--shot` keeps its 60 frames a second, `--fast` and `--sweep` go as fast as the machine draws, and a
  run that goes by the clock (`--keys` or `--stats` alone) is held to 60 (`--pace 120`, `--pace display`: the display's rate,
  `--pace off`). A run whose script has no end stays until it is ended (`quit` among the keys, SIGTERM).
- `--visible` (`GT4R_VISIBLE=1`) shows a scripted run: a `--shot` or `--sweep` window is not made the active one, cannot
  take the keyboard's focus and reads no real input either; a window with `--keys`, `--stats` or `GT4R_RACE` alone is
  played as any other. `--headless` (`GT4R_HEADLESS=1`) hides a run that would have shown one; the command line
  comes before the environment.
- Scripted runs open no file windows, folders or full screen (`GT4R_DIALOG=<path>` is a file
  window's answer). `GT4R_WINDOW=1600x700` is the window's size, `GT4R_SHOT_GAMMA=1` takes the pictures with the
  gamma, `GT4R_CRASH_TEST=throw` (`segv`, `abort`) closes the app as a fault would.

## Sweeps (for checks)

- `gt4r <disc> --sweep <list> --sweep-out <dir>`: the races of a list, one after another in one
  window, a picture of each at the frames the list asks for. A line: `<name>` TAB `<GT4R_RACE's
  setup>` [TAB `<frames, ';' between>`]. `<dir>/results.tsv` gets a line a race: its status, the
  race screen's setup time, the frames' work, the memory after it, the warnings it logged. Started
  again with the same list, the run goes on after the last race it finished, and a race it ended
  inside is written as a crash and passed over (`src/gt4r/app/screen_sweep.cpp`).
- `gt4tool sweep <disc> courses [filter]`: every course file with a field of computer drivers for a
  lap: set-up time, lap times, spins (and where they began), contacts, cars stuck.
- `gt4tool sweep <disc> cars [filter] [course=..] [seconds=..]`: every car of the database driven
  by the course's computer driver: distance, top speed, 0-100 km/h, spins, stuck time.
- `gt4tool sweep <disc> pad [courses]`: the player's car driven through the pad's path (an analog
  stick and pedals following the road), with the automatic and the manual gearbox, the aids on and
  off, against computer drivers: lap times, contacts, shifts, the place at the end, spins, the time
  spent sliding.
- `gt4tool sweep <disc> stop [course=..] [car=..]`: the player's car driven, braked to a stop, stood
  and launched at full throttle: gear, speed and engine speed every quarter second.
- `gt4tool sweep <disc> wall [event=..] [car=..] [steer=..]`: a wall hit under an event's rules: the
  penalty's seconds and the speed after it.
- `gt4tool sweep <disc> grid [filter]`: where every course's grid places stand along the course.
- A Gran Turismo mode game played forward (every license, the Beginner and Professional events
  but the World Championship won, 26.8 % complete, the presents) for playing the
  later events: `gt4tool save <disc> progress <data folder> [name] [credits]`, then
  `GT4R_DATA_DIR=<data folder> gt4r <disc>`.

## What runs where (the app's shell)

- The window, the frame loop, the screens, the scripted keys, the pictures and the sweeps are the
  app's own (`src/gt4r/app/app.*`, `screen_sweep.cpp`; which runs are scripted and what they do not read:
  `src/gt4r/settings/run_mode.h`); the home screen (its Developer tools too), the setup screen, the asset browser and the
  GT4R menu button beside the title and Gran Turismo mode's map and the Controller Settings button in place of the Options'
  controller pages (which pages: `menu/app_button.h`; `screen_menu_viewer.cpp` `draw_menu_button`, `hide_key_table`; what
  they set: `app/game_options.*` into the game's own options)
  are this app's, not pages of the game. The
  messages for a file that cannot be played are ours (`src/gt4r/disc/disc.cpp`).
- Quick race (`screen_quick_race.cpp`) and the car picker (`car_picker.*`) are the app's pages. What they list is the
  disc's, read by the game's own code (`menu/save_editor.h`: Gran Turismo mode's event lists, the license tests, the
  missions, the courses, the cars and makers, the colors). The race is the game's: menus of a new game of their own (never
  saved; the player's options read from the data folder) given the request the game's pages give: an event's race as the
  event page's entry (its race label, `solo_event`; tests `license`, missions `mission`, a test's course `freerun`), and
  Arcade's single race through Arcade's own script (`GameRoot.set_enemy_cars`, then `MCarData.ShiftCarCode` on each entry,
  as `doGame`); the race sequence's pages (quick-event, quick-arcade, ...) run before and after it. A custom race is
  Arcade's single race on its row `sample_entry_6` with the entry table set as `doGame` leaves it (the cars chosen, each
  through `ShiftCarCode`) and the grid's places ours (`RaceStart::grid_slots`); *Fill the grid like Arcade* runs
  `set_enemy_cars` at the level chosen; the course's field is `MCourseData.GetMaxCarNum`. Ours (judged): the open
  car's two-car field is applied for model type 2 only (Arcade's open-top option page is not asked), and the tire is the
  setting page's default. Prologue's: its arcade's lists (the configuration script's `ArcadeCourseList`, `ArcadeCarList`) and
  its `setupRace` (the dirt race and the car's dirt version on a dirt course, else the car's own race; `_h` / `_p` after it at
  the second and third difficulty). Names the app's pages cannot show (Japanese) are the database's American names or labels.
- The data folder's place (GT4R_DATA_DIR, a `portable.ini` beside the program or, on macOS, beside gt4r.app: the engine's
  `game::app_bundle_folder`, else SDL's preference folder) is ours (`app.cpp` `user_data_dir`). The update check and the
  online session's relay requests are the shared engine's (`net::VersionCheck`, `net::Session`) carrying our game tag
  (`gt4r` and this game's version: `net/version_check.*`), so a relay holds us to this game's versions.
- The pad, wheel and keyboard are read by the shared engine's input layer (`gt3r::InputManager`: devices,
  hot plug, bindings, the steering's dead zone and response); from its virtual pads on the race's frame is
  the game's own (the controller 0x43CF90, `src/gt4r/input`, equal to the program's), and so is what the
  step makes of it (0x349FD0). The original rumble's level and the wheel's force are the game's; the Modern
  rumble, the triggers and the Modern wheel force are the shared engine's, fed from the game's step
  (`docs/specs/input.md`).
- Mod cars are our addition (`formats/db/mod_cars.*`, `car_export.*`, `race/mod_car_body.*`): their rows are made of the
  game's own rows of the donor (or an exported car's), and they race, sell and draw through the game's code; a body of a
  mod's own is read by the shared engine's glTF loader and stretched bodies by its `fit_body`, both drawn by our GT4 car
  path with the course's reflection factors; no GT3 value enters (an electric drive is refused for that reason).
- Settings, Mods, Controls and the mouse are the app's own (`src/gt4r/settings`, `app_settings.cpp`, `page.*`,
  `screen_settings.cpp`, `screen_controls.cpp`) over the shared engine's parts: the GPU device's options, dynamic
  resolution, the gamma pass, the output devices, the log file and crash notes, the disc list, the mods' archives,
  the texture packs. The race display's place on a 4:3 picture is the program's, and its 16:9 push of the parts tied to a
  side (96 units) too; on a picture wider than 16:9 those parts go out with the picture's edge, and on one narrower
  than 4:3 (fill on a screen squarer than 4:3, down to 1:1) the display fits the width with its top and bottom parts at
  the picture's edges (ours: `race/game_area.h`, `RaceHud`'s EdgeScope; Prologue's display likewise: its own EdgeScope in `race_hud_prologue.cpp`, the middle's messages staying).
  The 3D view of a race or replay shows more at the sides on a picture wider than 4:3 and keeps 4:3's width of the scene,
  showing more above and below, on a narrower one (ours: `ViewPose::fovy(aspect)`). With touch controls up, the chase
  panel's side groups move clear of them; where that would bring a group onto the sideways G meter it is drawn smaller about its
  outer bottom corner (down to 70 %), and if still too wide lifted above the meter (ours). The game's menus and pages stay 4:3
  in the middle at every shape, with bars beside or above and below them.
- Android (`android/`, [android/README.md](../android/README.md)): the APK's activity, the disc picker's lasting access,
  the save folder's documents and the user data folder in the Files app are the shared engine's Java, compiled from the
  submodule; the app is ours (`GT4Activity`, the manifest, `org.gt4revisited.gt4r`). The on-screen touch controls are the
  shared engine's (`input/touch_controls`, `app/screen_touch_overlay`): its D-pad and buttons over the game's menus and pauses,
  its driving controls while the player drives (`Screen::touch_layout`); the title's hint is a button there. Ours: Select and
  Start under L1 and R1 at the screen's sides over the menus, the four in one row at the top's middle over a race's display
  (`Screen::touch_top_row`: its pause, replays; clear of the place and the lap times at the corners); the race display's lower corner parts (meters, signs, fuel) moved
  sideways just clear of the controls (`race::set_touch_shift`; the upper blocks stay); the race's pause, PIT STOP menu and
  split-screen pause take a tap on a row (the first focuses it, the second chooses it; a mouse's hover focuses); their settings in Settings › Controls
  (the engine's TouchConfig, and its phone vibration); Android's back button as one press of Start while driving (the pause),
  else of Triangle (`Screen::back_is_start`). Not yet: the engine's layout editor (dragging each control), which is written
  for its own app.
  The 3D pipelines are built ahead of use on background threads at start: the shared engine's list (`GpuContext::warm_pipelines`)
  and ours, the three it lacks for GT4's draws (`app/pipeline_warm.*`). The Android build keeps them between runs
  (`vulkan_pipelines.cache` in the user data folder, the shared engine's SDL patch): a Pixel Fold (Mali-G710) built the engine's
  list in 3.8 s on a first start and in 0.3 s with the cache (several times longer while the system's file picker is in front:
  a background app's threads run on the slow cores).
  Meta Quest: the `-Pquest` app, as an immersive OpenXR app ([VR.md](VR.md)).
- VR headsets ([VR.md](VR.md)): the OpenXR session, its canvas and eyes, the controllers' virtual pad and pointer are the shared
  engine's (`src/xr`); the frame loop's use of them is ours (`app_xr.cpp`), and so is the race in stereo (`race_vr.cpp`,
  `RaceWorld::render_eyes`): GT4's own draw, from the head's place, per eye. Not on a headset yet.
- The movies are the shared engine's player (`video::MoviePlayer`, its MPEG-2 decoder and PSS demuxer); which of a movie's
  sound tracks plays is ours, as the game's player picks it (`formats/movie_tracks.*`, [formats/menu.md](formats/menu.md)
  "Sound tracks").
- The Car database (`app/screen_database.cpp`) is the app's page; its facts are the game's (`menu/car_database.*`): the
  spec sheet as the car's natives write it, the settings pages' chart, the tune shops' parts and prices and the events'
  regulations by Gran Turismo mode's own scripts run behind the menus' natives (formats/menu.md "The Car database").
  On the Japanese release the page's words are the database's American names and ours, since the app's font has no kana.
- The 2D drawing toolkit of the home screen, the setup screen and the browser is the shared
  engine's (`ui.cpp`, `ui_font.cpp`, `button_prompts.cpp`); the game's pages are drawn by the menu
  code (`src/gt4r/menu`, `screen_menu_viewer.cpp`).
