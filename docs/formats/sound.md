# Sound

Code: `src/gt4r/formats/sound/` (the files), `src/gt4r/audio/` (what plays them),
`tools/gt4tool/cmd_sound.cpp` (`gt4tool sound`), `tests/test_sound.cpp`. Everything is
played through the shared engine's SPU2 voice mixer and reverb
([engine/gt3r/docs/formats/sound.md](../../engine/gt3r/docs/formats/sound.md),
[sound_sequencer.md](../../engine/gt3r/docs/specs/sound_sequencer.md)): sample data is
PS-ADPCM, pitches and envelopes are SPU2 register values.

Facts marked [P] are read from the game's program (the driver's routines sit at
0x559330-0x55E600 of `CORE.GT4`, SCUS-97328 v1.01; its two sequencers have the run-time type
names `SDDRV::SqSequencer` and `SDDRV::SeSequencer`); [F] from the files; 🔎 inferred; ❓ open.

## What is on the discs (`gt4tool sound <disc> survey`)

| Files | Magic | Gran Turismo 4 | Prologue | What |
|---|---|---|---|---|
| `sound/engine/<id>`, `sound/normal0..3/<id>`, `sound/turbo0..3/<id>`, `sound/roadnoiz.es`, `rn_01.es` | `ENGN` | 3,576 (21,451 layers) | 1,115 (7,477) | engine, exhaust and wind sounds: Gran Turismo 3's layout |
| `sound/gt4sys.ins`, `gt4race.ins`, `gt4count.ins`, `gt4race2.ins`, `gt4pod.ins`, `gt4shutter*.ins` | `INST` | 25 | 4 | effect banks |
| `music/<region>/*.ins` | `INST` | 11 | 1 | the sequenced music's instruments |
| `music/<region>/*.sqt` | `Ssqt` | 11 (27 sequences, 77,548 notes) | 1 (9; 35,864) | the sequenced music |
| `sound/gt4se.inf` (and Gran Turismo Concept's `se.inf`, unread) | `GTSE` | 73 effects | 58 | effect names |
| `music/<region>/mid.inf` | `MSEQ` 2 | 10 groups, 18 pieces | | the sequenced music's index |
| `music/<region>/bgmmid.inf` | `MSEQ` 1 | (a copy of Prologue's) | 18 names | the earlier index |
| `bgm/<region>/ads.inf` | `MADS` 2 | 27 groups, 91 pieces | | the streamed music's index |
| `bgm/<region>/bgmads.inf` | `MADS` 1 | (unread: names files the disc lacks) | 17 groups, 24 pieces | the earlier index |
| `bgm/<region>/*.adm` | `BMRK` | 100 (16,662 markers) | | a stream's markers |
| `sound/sdvol.dat` | `SDVL` | 1 | | mix levels |
| `bgm/`, `narration/` `*.ads` | `SShd` | | | streams: the shared engine's |

`<region>` is `us` on the US disc, `jp` on Prologue. Everything in the table reads on both
discs (`sound_files_on_disc`, `sound_indexes_on_disc`).

## `ENGN` engine sounds

Gran Turismo 3's layout, read by the shared engine. Five files of the US disc
(`sound/engine/50303`, `sound/normal0/42205`, `42305`, `49703`, `50303`) and two of
Prologue's store their layers' samples in another order than the table lists them, which the
shared reader refuses; `audio::parse_engine_bank` sizes each layer from the next sample in
the data instead and reads all of them.

## `INST` instrument banks [F] [P]

The magic and the first 0x20 bytes are Gran Turismo 3's; the tables are new.

```
0x00 "INST"   0x08 u32 size of the header (= offset of the sample data)
0x10 u32 size of the sample data   0x14 u32 its offset   (0x04, 0x0C, 0x18, 0x1C: 0)
0x20 the table block:
     +0x00 u32 its size   +0x04 u32 size of the sample data   +0x08 u32 0
     +0x0C "SShd"
     +0x10 s32 slot[28]   offsets from 0x20, -1: none
```

| Slot | Table | In |
|---|---|---|
| 0 | programs | music banks, `gt4race.ins`, `gt4race2.ins` |
| 1 | velocity map: 2 bytes, then the played velocity for each of the 128 written ones (the identity in every bank) | all |
| 2 | ❓ 64 bytes; the driver's lookup of an lfo wave reads it as `u16 last, u16 offset[]` to waves of 60 steps, and the one offset points past the header | music banks |
| 3 | effect sequences | `gt4sys`, `gt4count`, `gt4pod`, `gt4shutter*` |
| 4 | ❓ 16-byte records, one per effect sequence (`00 nn 00 64 40 00... 04 00 64 00`); no reader found | the same |
| 5 | effect programs | the same |
| 27 | not an offset: 0 in the music banks, where a tone's sample address counts 16 bytes; -1 in the effect banks, where it counts 8 | |

At load the program turns slots 0 to 5 into pointers (stored over slots 8 to 13).

**A program table**: `u16 last number`, `u16 offset[last + 1]` from the table (0xFFFF: no
such program), the programs.

```
program, 8 bytes, then its tones:
  +0 u8 tones    0xFF: one tone per key from `first key` to `last key`
                 bit 7 set: (value & 0x7F) + 1 tones, every tone whose keys hold the note sounds
                 else: value + 1 tones, the first whose keys hold the note sounds
  +1 u8 volume 0..127      +2 u8 0x40 ❓ (no reader found)     +3 u8 0
  +4 u8 bend range, +5 u8 lfo wave: for tones flagged to take the program's
  +6 u8 first key (lower notes are silent)   +7 u8 last key (for 0xFF)
tone, 16 bytes:
  +0 u8 lowest key, +1 u8 highest key      (effect programs: +0 voice group, +1 voice priority)
  +2 u8 center: the key at which the sample plays at 44.1 kHz
  +3 s8 fine tune, 1/16 semitone
  +4 u8 flags: 0x80 reverb send, 0x40 lfo wave from the program, 0x20 lfo on, 0x10 bend range
               from the program, 0x02 noise (the SPU2's noise generator, clock = +2), 0x01 ❓
  +5 u8, +6 u16: the sample's address (u8 the high part), in units of 8 or 16 bytes from the
               start of the sample data; an unused tone is all 0xFF
  +8 u16 ADSR1, +10 u16 ADSR2
  +12 u8 volume 0..127   +13 u8 pan (0 left, 0x40 center, 0x7F right)
  +14 u8 bend range in semitones   +15 u8 lfo wave (0x7F: none)
```

Every tone's address lands on a sample's start (1,073 tones on the US disc). An effect
program is always of the 0xFF kind: the effect's key picks the tone, and the tone's center
sets the pitch (most menu sounds are stored below 44.1 kHz and so sit 10 to 18 keys under
their center).

**The effect sequence table**: `u16 last set`, `u16 offset[]` (the low bit dropped) to
sets of `u16 last entry`, `u16 offset[]` (low bit dropped) to the sequences, all from the
table's start. A sequence is a stream of events (below).

## `Ssqt` sequence banks [F] [P]

```
"Ssqt", u32 last sequence, u32 offset[last + 1]
sequence: "Sssq", u32 ticks per beat (480), the events
```

The events are MIDI's with changes. The stream is `event, time to the next event` pairs; a
time is 7 bits a byte, the high bit set on all but the last byte. An event's status byte may
be left out when it repeats the previous one. The stream starts with a lone time (the
sequencer reads it as an event of status 0, which does nothing).

| Status | Bytes | Event |
|---|---|---|
| `8n` | key | note off |
| `9n` | key, velocity | note on (velocity 0: note off) |
| `Bn` | controller, value | below |
| `Cn` | program | the channel's program |
| `En` | value | pitch bend, **one byte**: 0x40 none |
| `FF` | type, length, data | 0x2F: end; 0x51: tempo, 3 bytes big endian, microseconds per beat; others skipped |
| `An`, `Dn`, `F0`..`FE` | none | nothing (not in the files) |

| Controller | The sequencer [P] |
|---|---|
| 0x01, 0x02 | lfo rate, lfo depth of the channel (see "lfo") |
| 0x07, 0x0B | channel volume, expression |
| 0x0A | channel pan |
| 0x40 | value 0: every sounding note of the channel is released. Nothing else: a note-off is never held back |
| 0x5B | non-zero: the channel's notes send to the reverb whatever their tones say |
| 0x5E | value mod 3: 1 inverts the left side of the channel's notes, 2 the right (a surround cue), 0 neither |
| 0x63 | value 20: the loop starts here; value 30: go back to the loop's start |
| 0x66 | the loop count: 0x7F loops without end, and so does the 0 it starts at (the count is lowered at each loop end and the loop ends only when it reaches 0 from above) |

The files use 01, 07, 0A, 0B, 5B, 5E and 63 only; every sequence has one loop.

## The sound driver [P] (`audio/sound_driver.*`)

The driver runs once per vertical blank (60 a second): its 12 effect sequencers and 2 music
sequencers advance, then each of its 46 voices has its level and pitch worked out again
where something changed, and the changes go to the SPU2. (`SoundDriver` runs one music
sequencer: the game's menus never play two sequences at once.)

**Clock.** A sequencer keeps the time in ticks as a float. Each update adds
`(1,000,000 / (60,000,000 / bpm)) x resolution / 60`; events are read while the time has
reached the next event's. A music sequence starts at 120 beats a minute (so the update that
reads the opening tempo event still runs at 120); an effect sequence runs at 240 ticks a
beat and 120 beats a minute: 8 ticks an update.

**A music note** (`9n`): nothing sounds when the channel's volume or expression is 0 or the
key is under the program's first key. Otherwise the program's tones for the key (above)
each take a voice; a tone whose sample address is past the SPU2's memory ends the note
there. A note-off releases every voice of that channel and key.

**Level** of a voice, every factor an integer:

```
a = program volume x tone volume                (0..16129)
a = a x sequence volume >> 14                   (0x3FFF unless faded)
a = a x master volume of the voice's kind >> 14 (music, effects, held notes: each its own)
a = a x channel volume / 127 x expression / 127 x velocity / 127
pan = sequence pan + tone pan + channel pan - 0x80, held to 0..127
left  = (a x table[pan].left / 127) x left gain >> 14      gains: 0x3FFF, or -0x3FFF for an
right = (a x table[pan].right / 127) x right gain >> 14           inverted side
```

held to -0x4000..0x3FFF. The level is linear (Gran Turismo 3 squared it). In mono output both
sides get the mean of the two magnitudes. The pan table has 128 pairs: the near side stays at
127 and the far side falls, in steps of 2 from the far end and of 1 over the 15 pans next to
the center; the center itself is 120 / 120 (`sound::pan_gains`).

**Pitch.**

```
p = 16 x key + fine tune - 16 x center          (1/16 semitone)
music:   p += (bend - 0x40) x bend range / 4    (toward zero)
others:  p += (bend - 0x40) x bend range >> 2
p += slide x 4/3                                (effect sequences' pitch slides)
SPU2 pitch = table[p mod 192] >> (12 - p div 192), at most 0x3FFF, x pitch factor >> 12
table[i] = floor(2^24 x 44100/48000 x 2^(i/192))
```

**lfo.** A voice whose tone is flagged for it, or whose channel has a rate or depth set, steps
through a 60-step wave and feeds the result in as the bend. For a music voice the program
then replaces that value with the channel's bend before the pitch is worked out, and the
effect banks carry no waves: no voice of the discs is heard to waver, and the driver here has
no lfo.

**Fades.** A music sequence's volume moves by a step each update toward a target; one of the
three modes stops the sequence when the target is reached. A stop cuts the sequence's voices
(their release is set to the fastest).

**Effect sequences** play on 12 sequencers. Their events:

| Status | Bytes | Event |
|---|---|---|
| `A0` | key, velocity, program | velocity > 0: the effect program's tone for the key takes a voice (number key - first key); 0: the oldest voice of that program and key is released |
| `B0` | 0x60, lo, hi, count | jump to offset `hi lo` of the sequence; `count` times, 0 without end |
| `B0` | type, time, value, program, key | 0x41 (and every type but those below): slide the pitch of that key's voices by `value x 1.2` steps of 1/12 semitone over `time x 1000/15` ms; 0x07: slide the level to `value`; 0x0A: slide the pan (neither is in a file, and neither is built) |
| `B0` | 1 or 2, value, program, key | lfo depth, rate |
| `FF` | 0x2F, 0 | end |

A voice of an effect sequence: tone pan in the place of the channel's, the sequence's two
side gains, pitch factor and pan from whoever started it.

**Held notes** (the race's effects): the first tone of a program at key 60, whatever the
tone's keys; the caller sets the two side gains, the pitch factor, a pan and a priority
every frame. It sends to the reverb when any of the tone's flags is set.

**Reverb.** The game sets both SPU2 cores to effect mode 4 ("studio C") at depth 0x0FFF when
the sequenced music's banks load, as Gran Turismo 3 does at boot. ❓ What the races set.

Measured on renderings (`gt4tool sound <disc> seq-wav`, `tests/test_sound.cpp`): the notes
of `home`, `arcade`, `gtauto` and Prologue's sequence 5 sit on the A = 440 Hz grid (0 cents
of -50..+40 in steps of 10, 4.8 to 7.4 times the energy of the other offsets); `home`'s loop
comes round after 139.800 s and again 139.633 s later, where its events and tempo say 139.786
and 139.637; a made-up square-wave bank plays at the recording's pitch at its center, an
octave down at 12 keys under it, and 189/192 of an octave up at full bend with a range of 12.

## Effect names: `sound/gt4se.inf` (`GTSE`) [F] [P]

Gran Turismo 3's layout (`"GTSE", u32 1, u32 0, u32 count`, then `{u32 name, u32 value}`)
with a new value:

```
value = bank << 24 | kind << 16 | number << 8 | key
kind 1: the bank's effect sequence [number][key]      kind 0: a held note of program `number`
```

Banks: 0 `gt4sys.ins` (the menus: `ok`, `cancel`, `cursor`, `disable`, `next`, `return`,
`select`, `buy`, ...), 1 `gt4race.ins` (`clash`.., `on_skill`, `off_skill`, `rumble`,
`turbine`, `air`, `city`, `crowds`, `sea`, `tunnel1_l`..), 2 `gt4count.ins` (`count_d`,
`count_s`, `zoom`, `car_wash`, `chime`, ...), 3 `gt4race2.ins` (`backfire`, `pebble`, `sand`,
`turf`, `cheer`, `jackup`, `wheelgun`, ...), 4 `gt4shutter<type>.ins` (`shutter`; the type is
the photo mode's camera, `typea` to `types`, lower case, or none). 🔎 Prologue's bank 3 is
`gt4pod.ins` (its 11 `pod_` effects are that bank's 11 sequences). The index names four
effects whose programs the US disc's banks leave out (`off_bound`, `off_jump`, `on_jump`,
`pushup`), and Prologue's `buy` is a tone of volume 0.

## Music indexes: `MSEQ`, `MADS` [F] [P]

```
0x00 "MSEQ" or "MADS", u32 version (1 or 2), u32 0
0x0C u32 groups, 0x10 u32 records, 0x14 {u32 offset, u32 records}[groups]
record, version 1 (20 bytes): u32 name, file, title, artist (strings); u32 value
record, version 2 (24 bytes): u32 name, file, title, artist, genre; u32 value
```

- `mid.inf` (`MSEQ` 2): a group per bank. The group's name (its records' `name`: `home`,
  `powerspeed`, `race`, `hall`, `license`, `mission`, `gtauto`, `arcade`, `map`,
  `replaytheater`) names the files `music/<region>/<name>.ins` and `.sqt`; `value` is the
  sequence's number in the bank. `map` and `race` have five pieces each, the others one.
- `bgmmid.inf` (`MSEQ` 1, Prologue): 18 names (`arcade01`, `gtmenu01`, `option01`, ...) of the
  nine sequences of the one bank `bgmmid`; `value` is the sequence.
- `ads.inf` (`MADS` 2): the streamed pieces; `file` is the `.ads` under `bgm/<region>/`.
  ❓ `value` (the strings' start in every record). The groups have no names in the file: the
  program's, in order, are `start`, `goal`, `racing` (65 pieces), `default_quick` (2),
  `result`, `torophy_champ`, `torophy_solo`, `get_car`, `replay_theater`, `license_start`,
  `license_gold`, `license_goal`, `license_silver`, `license_bronze`, `license_failed`,
  `license_torophy`, `autodemo` (empty), `champ_start`, `champ_goal`, `timeattack_start`,
  `timeattack_goal`, `coffee_start`, `coffee_goal`, `etc_start`, `etc_goal`, `last_start`,
  `last_goal`. All 91 files are on the disc.
- `bgmads.inf` (`MADS` 1): Prologue's 17 groups (🔎 the first 17 names above, by what the
  pieces are); `value` is the offset of the piece's marker block, in Gran Turismo 3's layout.

**Markers** (`.adm`, `BMRK`): `"BMRK", u32 0, u32 samples per second, u32 length in samples,
u32 count[32], u32 offset[32]` (from the file's start), then the tracks' sample positions.
The jingles' files have none; `str91j.adm` says 48,000 samples per second, the rest 44,100.

**Mix levels** (`sound/sdvol.dat`, `SDVL`): `"SDVL", u32 1, u32 0, u32 modes (3), u32 levels
per mode (16)`, then the floats. The program asks for `level[output mode][n]` with n under 4:
0 the streams' (0.3), 1 the sequenced music's (0.48), 3 the menu effects' (0.5); 🔎 2 (0.4)
the race's. The three modes' rows are the same.

## The menus' sound object [P] (`audio/menu_sound.*`)

The scripts' `main::sound` is a `menu::MSound`. Its members, from the program's table, and
what the 34 projects' scripts call:

| Member | Calls | What it does |
|---|---|---|
| `play(name)` | 1,128: `ok` 595, `cancel` 231, `disable` 133, `cursor` 90, `next` 26, `return` 17, `select` 16, `disabled` 5 (not in the index: silent), `buy`, `shutter`, `old_buy`, `license_ok`, `car_wash`, `oil`, `oil_jackup`, `oil_jackdown`, `zoom`, `zoom1` | the effect of that name at both side gains = the effects option x `sdvol` level 3, with a priority that takes a voice from anything |
| `load([group])` | 10 (`"ARCADE"` once) | stops the music; with a group's name (any case) reads its bank; the default `"midi"` names no group |
| `unload()` | 10 | stops the music |
| `startMusic(group)` | 0 | stops, then starts the group |
| `keepMusic(group)` | 3: `"ARCADE"`, `"POWERSPEED"`, `"RACE"` | starts the group unless it is the one playing; `"keep"` does nothing |
| `stopMusic()` | 0 | |
| `fadeOutMusic([seconds = 1])` | 3 | the sequence's volume falls by 1 / (seconds x 60) an update, then it stops |
| `fadeInMusic(group, [seconds = 1])` | 0 | as `keepMusic`, then raises the sequence's volume toward full from where it is: a sequence just started is at full already |
| `startStream(kind, code)` | 5 | `kind` `"pcm"`: a piece of the streamed group `code` drawn at random; `"narration"`: `narration/<region>/<code>.ads` |
| `startStreamEx(kind, code, [number = -1, [repeat = 0, [volume = 1]]])` | 15 | `number`: the piece (mod the group's count), -1 at random; with 0x1000 added, the file named `i_<file>` |
| `stopStream()`, `pauseStream(flag)`, `isPlayingStream()` | 107, 4, 2 | |

**Which piece of a group.** Starting a group plays the piece after the one it played last
(the program remembers `number + 1` per group when the group's music stops), mod the group's
count: `map` and `race` walk through their five. The music's master volume is the piece's
level (1) x `sdvol` level 1 x the music option; a stream's level is its volume x `sdvol`
level 0 x that option.

**To use it** (the menu runtime): one `audio::SoundDriver` on the app's mixer, updated 60
times a second (`audio::AppSound` does both), and

```cpp
audio::MenuSound sound(vfs, driver, {}, streams);
sound.play("ok");                 // sound.play(name)
sound.load("ARCADE");             // sound.load(group) / sound.unload()
sound.keep_music("ARCADE");       // keepMusic; start_music, stop_music, fade_out_music(seconds),
                                  // fade_in_music(group, seconds)
sound.keep_music(root.text("bgm", "keep"));   // a page opening: its root window's music
sound.start_stream("pcm", "get_car", number, repeat, volume);   // startStream / startStreamEx
sound.stop_stream(); sound.pause_stream(true); sound.is_playing_stream();
```

**Which music a page plays** [F] [P]. A page's root window in the layout (`.mproject`) names
it: `bgm string {"MAP"}` is a group of the sequenced music (any case) or `"keep"`. The root
window's own code (`mRootWindow`, 0x234D60) hands that string to the same routine as
`keepMusic`: the group plays on when it is the one playing, starts otherwise, and `keep`
leaves the music alone. 49 of the US disc's 215 pages name one (`gt4tool sound <disc> pages`):

| Group | Pages |
|---|---|
| `MAP` | Gran Turismo mode's map (`TopRoot`), `NationRoot`, `DealerRoot`, `UsedCarRoot`, `BuyCarRoot`, `TuningRoot`, `TuningApplyRoot`, `PhotoTravelRoot` |
| `HOME` | `HomeRoot`, `LogRoot`, `GarageRoot`, `CarViewRoot`, `StatusRoot` |
| `GTAUTO` | `GTAutoRoot`, `WheelMakerRoot`, `WheelShopRoot`, `CarWashRoot`, `OilChangeRoot`, `WingShopRoot` |
| `LICENSE`, `MISSION`, `HALL` | `LicenseRoot`, `LicenseDetailRoot`; `MissionRoot`; `EventHallRoot`, `EventCourseRoot` |
| `ARCADE` | the arcade project's ten pages from `GameRoot` on |
| `REPLAYTHEATER` | `labomode`'s `ReplayTheaterRoot` |
| `keep` | the dialogs and popups (14) |

`POWERSPEED` and `RACE` are started by Gran Turismo mode's script (`keepMusic`). A root window
may also name a `navigate_sound` (`select`, `license_cursor`), an effect its code plays
(0x235680) 🔎 when the focus moves. Prologue's layouts name no music.

❓ The options' volume values, when exactly the root window asks for its music (🔎 as the
page opens), and the race's music (`RaceBGMPS2`).

## A car's sounds [P] 🔎 (`audio/car_sound.*`)

The race object loads two banks per car, `sound/engine/%05d` and `sound/%s/%05d` [P], from
the car's parts in the database ([database.md](database.md)):

- the number: the car's `ENGINE` row, u16 at +0x18;
- `%s`: `normal` or `turbo` and a digit: `turbo` when the car's `TURBINEKIT` row has a
  non-zero byte at +9; the digit is the byte at +6 of its `MUFFLER` row (0 without one).

All 722 cars' two files are on the US disc (383 different numbers; 259 cars are turbo), e.g.
the Isuzu 117 Coupe: `sound/engine/55201` and `sound/normal0/55201`. The wind's bank is
`sound/roadnoiz.es` [P].

🔎 How the layers are played is taken from Gran Turismo 3 (the shared engine's engine-sound
model): the game's own routine here (0x392640) is not read yet, and neither are its rules for
the tire, surface, wind and collision effects (`gt4race.ins`, `gt4race2.ins`). Rendered
through that model, the 117 Coupe's note is at 0.49 of a cycle per turn of the engine at
2,000, 3,000, 4,000 and 5,000 rpm alike (16.4, 24.5, 32.7, 40.8 Hz), across the layer changes.
