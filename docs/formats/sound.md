# Sound

Code: `src/gt4r/formats/sound/` (the files), `src/gt4r/audio/` (what plays them),
`tools/gt4tool/cmd_sound.cpp` (`gt4tool sound`), `tools/gt4tool/cmd_sound_compare.cpp` (`gt4tool sound compare`),
`tests/test_sound.cpp`, `tests/test_sound_race.cpp`, `tests/test_sound_compare.cpp`. Everything is
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

**The output's level.** A voice at side gain 0x3FFF plays its samples at full scale; the voices'
sum is held to 16 bits; a stream plays its samples at its level. Measured: the menus' `cursor`
and `ok` in the original are our rendering at the same gains (1.79 to 1.91 times the shared
mixer's half level at a new game's 115 / 128), the race at the same mix (below). The shared
engine's mixer and streams play at half that (`gt3r::VoiceMixer::kOutputTrim`, kept for Gran
Turismo 3's own screens), so Gran Turismo 4's code sets its mixer's master to 1
(`audio::with_output_level`: `AppSound` while a disc is open, `OfflineSound` for every rendering)
and asks the shared engine for twice a stream's level (`audio::stream_request`, used by
`audio::stream_player`, which the menus and the race take their streams from). Nothing is
clipped that the original does not clip: at the game's levels the sequenced music peaks 1.3 to
2.5 dB under full scale (all ten groups of the US disc; the original's menus 0.8 to 1.9 dB under),
a six-car race on Tsukuba sits at -18.0 dB with no sample at full scale (the original's race
recordings -17 to -19 dB, a few samples at full scale). Prologue's menu music at a new game's
music option reaches full scale, as its recording does (its arcade pages at -11.6 dB).

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

❓ When exactly the root window asks for its music (🔎 as the page opens). The options' values
and the race's music: "A race's sound" below.

## A car's sounds [P] 🔎 (`audio/car_sound.*`)

The race object loads two banks per car, `sound/engine/%05d` and `sound/%s/%05d` [P], from
the car's parts in the database ([database.md](database.md)):

- the number: the car's `ENGINE` row, u16 at +0x18;
- `%s`: `normal` or `turbo` and a digit: `turbo` when the car's `TURBINEKIT` row has a
  non-zero byte at +9; the digit is the byte at +6 of its `MUFFLER` row (0 without one).

All 722 cars' two files are on the US disc (383 different numbers; 259 cars are turbo), e.g.
the Isuzu 117 Coupe: `sound/engine/55201` and `sound/normal0/55201`. The wind's bank is
`sound/roadnoiz.es` [P].

The layers are played by the shared engine's layer player: the game's own (24 players of 0x58
bytes at 0x64FBF8, stepped by 0x558FD8) follows the same rules [P]: nothing under 200 rpm, one
layer or a neighbouring pair crossfaded by a sine table (0x64D3F8), the pitch moving at most
x0.5..x2 a step and the volume by 0x1000 a step, envelope ADSR1 3 / ADSR2 0xA, only the bank's
first layer table.

**The pitch** [P]: the layer's stored pitch x revs / the layer's revs (0x558DB0), then x the
player's Doppler factor (+0x40, 0x1000 = 1) >> 12 (0x55DF70), straight into the SPU2's pitch
register: 0x1000 plays the sample at 48 kHz. Gran Turismo 3 took the same field as 44.1 kHz and
converted (x 0xEB3 >> 12, the shared engine's mixer does the same to everything); the US 1.01
program has no such conversion, so a layer plays 4096 / 3763 (147 cents) higher than Gran Turismo
3's rule makes it. `CarSound` scales the factor it gives the shared player by 4096 / 3763. Checked
against the original (below): the Ford Ka '01's strongest note at 5,650 rpm is 181 Hz in both, at
5,076 rpm 164 Hz in both.

## A race's sound [P] (`audio/race_sound.*`, `car_sound.*`, `course_sound.*`, `race_tables.*`)

`gt4tool sound <disc> race-tables` prints the tables below from the disc's program;
`gt4tool sound <disc> pass-wav <car> <out.wav> [km/h]` renders a car passing a still camera
10 m away, mixed by them. `tests/test_sound_race.cpp` measures what is said here on renderings.

### Per car, every step

0x3B5E80(entry, race, flag) fills a frame for a car and 0x392640(entry + 0x2900, frame) plays it.

- **The view** (the race's virtual +0x108, the camera's kind): below 0 → 4; 0 and 1 (in-car)
  → 0 for the camera's car (the race's virtual +0x168), 1 for the others; 2 (chase) → 2 / 3;
  anything else → 4.
- **The gains** of a view: s16 hundredths, 5 rows of 24 at 0x622330 (US 1.01; loaded by
  0x3F3AB8 to 0x845A10; columns 16..23 hold 100 in every row, which is how `race_tables.cpp`
  finds the table). Columns: 0 engine, 1 exhaust, 2 turbine (the spool), 3 b_off,
  4 turbine2 (the gearbox's whine), 5 clash, 6 the three rumble strips, 7 in_car, 8 wind,
  9 on_road, 10 rain, 11 on_skill, 12 read and not used, 13 off_skill, 14 off_roadnoiz /
  pebble / sand, 15 on_bound. US 1.01 (engine / exhaust): in-car own 6.5 / 7.0, in-car others
  3.5 / 4.7, chase own 3.0 / 6.5, chase others 2.5 / 4.2, other cameras 5.0 / 15.0; the wind
  from other cameras 60.
- **Three places**: the engine's, the body's and the exhaust's. From the car's own in-car view
  the car is at the camera and the three sit 1.25 m ahead / 0.01 m / 1.25 m behind the head for
  a front engine (the engine's place by the car: 1 front, 2 middle, 3 rear; 4 is heard as 1),
  turned by the in-car camera's look (the race's virtual +0x128); from any other view all three
  are the car's position in the camera's space.
- **The Doppler factor**: (u · camera's step + c') / (u · car's step + c') with u the unit vector
  from the camera to the car, steps per 1/60 s, c' = 5.5241666 m (331.45 m/s) [P]; only while
  the race's virtual +0x100 is not 0. Measured: a car coming at 30 m/s gives 1.0995, its note
  rendered 32.7 → 35.9 Hz (1.0991).
- **The panner** 0x391AF0 / 0x39A7B0, one microphone per sound (0 the engine's, 1 the
  exhaust's, 2 the body's and the wheels'): d = hypot(x, z) (x right, z ahead); under 0.25 m
  the sound moves ahead by 0.25 - d; gain 1 / (1 + slope × max(d - reach, 0)) (a microphone
  switched off: 32 / d); the sides cos / sin of clamp((x / d + 1) π/4, 0, π/2) (a sound on the
  right is on the right side only). The microphones by output mode (set by 0x391A60 from
  0x6A0250; reach 9 m for all): mono 0.5 / 0.17 / 0.22, stereo 0.5 / 0.1 / 0.22, surround
  0.5 / 0.07 / 0.16 (slopes of mics 0 / 1 / 2). The output mode is the byte [0x6518C8]: 0 mono,
  1 stereo, 2 surround (the option page's `sound_type` names `MONOPHONIC`, `STEREO`,
  `PROLOGIC2`); a new game's is 1 (the original's saved states).
- **The surround cue** (the panner's two flags): a sound behind the camera has one side inverted
  (in front, x >= 0: the left follows the right's flag, x < 0 the right the left's; behind, the
  same with the flag's opposite). The program does it in **every** output mode: the original's
  stereo recordings of the nose view carry the exhaust's left side inverted (its players' volumes
  -5464 / +5464 in the saved state; the recording's two sides correlate +0.28 there, +0.90 from
  the chase view). Here it follows the shared mixer's surround option (off unless chosen), for the
  layer players and the held and single notes alike. Measured: the engine's gain at 29 m is 1/11 of its gain at 5 m and
  the exhaust's 1/3; the rendered car 3.16 times quieter (between the two).
- **The master** M (0x391CD8) = [0x6213D8] × [0x6213DC] × the fade [0x88F2D0] × the mix
  table's race level (`sdvol`, level 2 of the output mode: 0.40 for stereo on the US disc) ×
  [0x6213E0] (1). [0x6213D8] (0x391ED0) = the race effects option's flag × its raw value / 128
  (in a replay the replay's pair). [0x6213DC] and [0x6213E0] are 1.0 and [0x6213E4] 0.001 in the
  original's race (its saved states at the start and on the straight). The fade (0x391D58(target,
  seconds): a step of (target - now) / 60 / seconds each frame) is set up with the race
  (RaceBase virtual 38, 0x387FB0: [0x6213DC] = 1, both ducks off, then 0x391D98: from 0 to 1 over
  3 s when the race's mode (race + 0xCC8) is 2 or 3, else over 0.3 s). The race's mode, read in the
  original's saved states (the race object 0x80 before its camera manager): 0 racing (the arcade
  race, Grand Canyon, the license test, Seattle), 1 Gran Turismo mode's free run (Practice), 3 the
  replay, 4 the pre-race page (the master's parts there 0.05 and 0.056: the cars are all but silent
  behind it); modes under 2 take the race's options, 2 and up the replay's (0x391ED0, the music's
  likewise). In the license test's result scene the effects option's part is 0: silent. The ducks
  (0x391DF0, each a fade over 1 s from where the master is): to 0.45 while the flag 0x6213EC is
  set, to 0.2 while 0x6213F0 is, else back to 1. RaceBase virtual 75 (0x389B08, every step in
  modes 2 and up, so the step toward the target is worked out again each step: a sixtieth of
  what is left) sets the first with 0x373DA0(the camera manager, screen 0): whether the screen's
  camera kind (the manager + 0x148 + 0x19C × screen; written by each camera as it is chosen:
  CameraDesigner::vf39 writes 2, the on-board camera 1 or 3, the finish 4, ride-on 5, path 6, pit
  7, interrupt 8, point 9, auto fix 10, shot 11, round 12, follow 13, heli 14, grid 15) is 2, the
  course's replay script: **in a replay the cars and the course fall to 0.45 on the script's shots
  and come back on the on-board ones**. (The saved state taken in the replay shows kind 1 and the
  flag off.) The second is the free run's (RaceFreeRun 0x3BA6A0, 0x3BA8B8, 0x3BA938, on a change
  only): on while it plays a recording of its own (`%08d.xsp`, 0x4606D0, through 0x460600 ..
  0x460758, when the module at [0x84688C] is there) ❓ what that is. 0x391DC8 fades out (0x3E1C80,
  0x3E1EA0 ❓). `RaceSound::fade(target, seconds)` is the routine; `set_replay_shot(designer)`
  (every step of a replay) and `set_free_run_duck(on)` are the ducks; `replay()` starts the
  master from 0 over 3 s.
- **The levels** (0x350080, 60 a second): n = the friction torque / (the friction torque + the
  full-throttle torque) is the throttle that just holds the revs; e = |throttle - n| + n,
  halved when the clutch (the record's byte, 1 locked) is not locked and for a hybrid; the
  engine's level byte moves half way to min(127, int(e × 127 × B25)). The exhaust's: load =
  max(throttle - n, 0) / (1 - n), x = (1 - (1 - load)^16) × 0.6 + 0.4, its byte half way to
  min(127, int(x × 127 × B26)). The spool's byte is min(127, int(byte × B27)), the blow-off ×
  B28. A hybrid (P +0x246 = 3) under its idle revs: both 0. Rendered (B25 = B26 = 1, torques
  3 : 1): throttle shut 63, at a quarter (the quietest) 30, open 126, open with the clutch
  open 63; the exhaust 50 shut, 126 open.
- **The layer players**: engine = G0 × level / 64 at the engine's place (mic 0); exhaust =
  G1 × level / 64 at the exhaust's (mic 1); wind = G8 × the wind (0x34FA90) on
  `sound/roadnoiz.es` at the engine's place and gain, its "revs" made from the speed. Each
  player's gain is clamp(placement gain × M, -1, 1) × that.
- **The block of numbers** (42 bytes after twelve 0x28-byte curve records at 0x6224B0; the
  block at 0x622690; made into floats by 0x34F6A0): wind gain = p[0xE] / 10 ×
  (p[0xF] / 1000)² × the car's drag × speed² × (1 - slipstream), at most p[0x14] / 10, its revs
  p[0x12]..p[0x13] × 100 over p[0x10]..p[0x11] × 10 km/h; the cabin's rush (in_car) likewise from
  p[0x16..0x1B] (pitch / 100, at most / 10); the road's roar p[6..0xD]; the whine p[0..4]
  (torque p[2] × 9.8 N m; pitch p[4] / 10 + (p[3] - p[4]) / 10 per 1047.2 rad/s); B25..B28 =
  p[0x1C..0x1E] / 100, p[0x1F] / 10 (US: 0.10, 0.10, 0.20, 2.5); contacts × p[0x26] / 100
  (0.6), thresholds p[0x28], p[0x29] / 100.
- **Held notes** (48 objects at 0x650500; a new note takes the voice of the lowest priority
  below its own; priority from |level|; side gains kept in s16, so up to 2): turbine
  G2 × spool / 128, pitch u16 / 6144, no Doppler; turbine2 (a gearbox that whines: P[0x10D1] is
  set when the equipment's GEAR row (+0x48) has its byte +0x22 not 0 (0x4462A0) or P[0x32] is 2;
  the byte is the gearbox's grade: 0 the road car's own (602 of 722 cars), 1 close (every `_b`
  row), 2 super close (`_c`), 3 racing (`_d` and 120 cars' own); 0x34FED0: none while the byte at
  car + 0x104 + 0x522 is 0; level lo + (hi - lo) × |min(torque, p[2] × 9.8) × ratio[gear] /
  (p[2] × 9.8 × ratio[first])| with the engine's torque (+0x51C) and gear (+0x520, 1 the first),
  pitch p[4] / 10 + slope × |an axle's speed| (0x350DA0, by the layout); a stepless gearbox
  (0x369510) and a motor (0x356D88) have their own ❓); in_car G7 at the
  body; nos 1 for the in-car own view while flowing; body_noiz always 0. in_car, body_noiz and
  nos ease: rising at once, falling 0.05 a step.
- **The wheels** (wheel record car + 0x164 + 0xEC w: +0x89 surface, +0xB4 slip intensity,
  +0xB8 skid pitch, +0x20 rim speed): each at its own place on mic 2, at clamp(gain × M) × M
  (the master twice). Notes by the surface index (the road's material, or its wet variant 20,
  21, 22): on_skill G11 and on_road G9 (the roar: (rim speed / 100)² × gain × the tire's width
  factor from 0.135 to 0.355 m, its pitch by the rim speed) on 0, 7, 8, 9, 12; rain_noiz G10 on
  6, 20, 22; off_roadnoiz G14 on 5; off_skill G13 on 2, 3, 4; pebble G14 on 13; sand G14 on 14;
  rumble G6 on 1, 21; rumble2 on 10; rumble3 on 11; the slipping notes by the slip intensity at
  the skid pitch. The notes past the first three ease as above. ❓ Which slots a course allows
  (0x360458 → 0x3922B0) is not applied.
- **Single notes** (table 0x621418; under 0.001 not played; 0x392EB8): b_off G3 × blow-off at
  the engine; clash / clash2 / clash4 by the contact's kind 0 / 1 / 2, the camera's car only.
  **A contact** goes through 0x350530(car, kind, other, value): the pad's rumble by kind (0x35CF40,
  0x35CF88, 0x35D0C0), the race's virtual +0x190, then the recorder 0x350328, which keeps the
  step's strongest at car + 0x104 + 0x608 (the kind at +0x60C, the other car at +0x60D): kind 0
  value / 50000, kind 1 twice the value, kind 2 |value / 50| and at least 0.2 (while 0x35DF00
  says so); at most 1, under 0.2 none; × p[0x26] / 100. The callers: the two wall routines
  (0x45E288, 0x45E768) pass kind 0 with the contact's impulse |j| (N s), after adding |j| / 100000
  to the body's strength (+0x448); the car-against-car routine 0x45F340 adds |j| / 15000 to both
  bodies' strength and passes that same number, kind 1, for each of the two cars; 0x3E3C48 passes
  kind 2 for a course object (the course's objects at [0x6D6054]). So the sound's strength is
  twice what the contact adds to the body's strength, against a wall and a car alike.
  `sound_state()` takes the simulation's seam (`CarState::contact`, the body's strength summed
  over the step, at most 1) that way: min(2 × contact, 1), none under 0.2 (🔎 a step with two
  contacts: the program keeps the stronger, the seam gives their sum); on_bound for the in-car own view (the strongest wheel landing, 0.5..1,
  × G15 × 2.5); backfire / backfire2 (one at random) at the exhaust, strength 3, when an
  afterfire flame starts (0x3D6D30); heartbeat (P[0x32] = 3) ❓ not played.
- 🔎 The frame's reverb flag (+4) is never written: the voices send to the reverb.

### The course (0x398B70)

Five lists of the `CRSS` section play air (31), city (32), crowds (33), cheer (61), sea (34).
Air and city are not placed: both sides at the first emitter's first point's weight. Crowds are
placed (each point's weight, past 32 m × 32 / distance, a sine pan) with a Doppler pitch; cheer
and sea placed with pitch 1. All × M, at most 1; with more than one camera both sides take the
larger and the pitch is 1. The emitters (0x457918): without gates the gain is 1 at once; with
gates the cars between them weigh 1, 0.8, 0.64 ... (at most 1), the gain rising 1/120 and
falling 1/180 a step. The road's triangle under the camera (asked from 5 m above), flags & 0x18:
0x08 the first tunnel's pair (tunnel1_l on the left, _r on the right) at M and a reverb of 0.5;
0x10 the second's (its _l on the right, _r on the left) and 0.25; 0x18 0.25 only; else 0.125.
The reverb's depth (int(x × 32767) both sides, 0x55CD18) moves 1/36 a step; with more than one
camera it is 0.125. heri plays at 1 (without M) while the race's virtual +0x130 bit 0 is set.
Measured on Tsukuba: four crowd emitters and no others; the crowds at 1 × M.

### The count, the music

- **The count** (RaceDisplay, 0x39E9B0 → flags 0x621514..0x621516 → 0x39DA80, played through
  0x393428 at M): `count_d` (39) when the car's steps to its start (0x34C1C8: the race's count
  plus the car's own offset) are 180, 120 and 60; `count_s` (40) when the start message (4)
  reaches the camera's car with nothing left to count (and with some of the display's messages,
  0x3A06B0 ❓); `chime` (48) when the start reaches another car while ours still counts
  (🔎 staggered starts). Rendered: the beeps one second apart, heard within 2 ms. A rolling
  start (the arcade races) has no count: `count_s` plays when the player's car reaches the line
  (`RaceSound::start_signal`).
- **The music** (`RaceBGMPS2`, race + 0xCF88): messages of type 0x18 (RaceBase virtual +73,
  0x389758): -1 stop, -2 / -3 pause / resume, 10 → the race type's start code (race + 0xD60),
  11 → its goal code (+0xD64), 19 → the start code, else the code itself. Codes 10.. are the
  streamed groups + 10. Code 12 (`racing`) plays the next piece of the player's playlist
  (0x438170: 0x10-byte entries, enabled / played / order) at the stream level × the race music
  option / 128 (in a replay the replay's); the others a piece of their group at the stream level
  when the music is on. When a racing piece ends the next starts (0x33D170). The pairs: the
  default (RaceBase, 0x387C40) start / goal; 0x1D / 0x1E timeattack (0x112620, 0x116E20);
  0x13 / 0x14 license_start / license_gold (0x114E38, 0x11BB60); 0x21 / 0x22 etc (0x1181C8);
  0x1B / 0x1C champ or 0x23 / 0x24 last (0x11A4C0); 0x1F / 0x20 coffee (0x11CB38).
  ❓ Who posts the messages. What the original does (its recordings, below): the arcade race plays
  no start jingle; the racing piece starts as the race begins to run: with the opening shots of a
  rolling start (the picture fading in, 2.4 s after Cross), 0.25 s after the start of a standing
  one (both 15 frames after the request, 🔎 the stream's loading); the next piece follows the end
  of the last within 0.14 s; the replay plays the race's first piece from its beginning.
- **The play list** (the program's MPlayList, 0x437E50 .. 0x4382B0; the live one is the game's
  option object's, the saved one the option block's, [save.md](save.md)): `u32 rows` (the racing
  group's 65), `u16 position` (+4), `u8 shuffle` (+6), `u8 nothing enabled` (+7), `u16 seed` (+8),
  then 16-byte rows from +0x10: +8 played this round, +9 enabled, +0xA s8 place in the order (-1
  none). The next piece (0x438170): the position is taken modulo the enabled rows; the row with
  that place plays unless it played this round (then the next place, the search starting again
  from the second row); the position moves on, and when it comes round to 0 every enabled row is
  marked unplayed (0x437E50: played = not enabled). Ordering (0x437EB8): unshuffled (0x437EF0) the
  enabled rows in their own order; shuffled (0x437F58) as many draws as rows, each a random row
  (the program's generator: s = 17 s + 17, the number s ^ (s rotated 16), 0x579330; made a float
  by 0x578598) from which the first enabled row without a place takes the next place, then a new
  seed = the next float × 65536. The order a new game has is the configuration script's
  (`init_raceplaylist`, run from its `last_strap`: a list of the group's pieces per region applied
  as the rows' places, the others disabled): on the US disc 36 pieces, the photo-mode classics and
  the region's other songs disabled. Seen in the original: the pieces in that order across races
  (Apology For Non-Symmetry, then Born Too Slow, ...), the position kept between races.
  `audio::RacePlaylist` is this list; `RaceSound::set_playlist` takes it.
- **The options** (the game record +0x5C: a flag and a raw value each for menu_bgm, menu_se,
  race_bgm, race_se, replay_bgm, replay_se; raw 0..128, the option page's slider = raw / 8; a new
  game (0x4363A0) has every flag on and 115; so do the original's saved states, the record at
  0x9A6290 and its copies). Menu effects in a race: menu_se / 128 × sdvol level 3.
  `audio::race_sound_options` reads them from the game state's options.
- Pit sounds (`wheelgun`, `dropnut`, ... through 0x393428 and 0x3934A0 from 0x3D0230..) ❓ not played.

### Prologue

Prologue's program (SCPS-15055 v1.01) has the block of numbers and the per-car frame (0x378870,
the Doppler constant at 0x378B88) but other tables:

- **The gains** are floats, five rows of sixteen (one per channel, GT4's channel order 🔎 by the
  channels each use reads) at 0x503CD8, read by 0x3B3978(view, channel) = float[0x503CD8 + 4 (16
  view + channel)]; the last column 1.0 in every row, which is how `race_tables.cpp` finds them.
  Engine / exhaust: in-car own 5.6 / 4.6, in-car others 0.6 / 2.6, chase own 2.0 / 6.0, chase
  others 0.6 / 2.6, other cameras 1.0 / 6.0.
- **No microphones**: its panner (inline in 0x3A1D38 and its two siblings) gives every sound
  32 / distance, then held to -1..1 with the frame's scale, × [0x503978] × [0x50397C] 🔎 (the
  master's parts); the quarter metre, the pan and the surround flags as GT4's. `RaceSoundTables`
  then leaves the microphones off, which is that rule.
- ❓ Its master's parts and fades.
- **Its car sounds** are read as Gran Turismo 4's from its database presented in Gran Turismo 4's
  layout ([prologue.md](prologue.md)): every engine's file is on the disc.

## Checked against the original

The reference captures (the original running in an emulator: its stereo output at 48 kHz in the
videos, its memory in the saved states) measured with `gt4tool sound compare` against renderings
of ours. Everything below is from the US 1.01 disc with a new game's options.

```
gt4tool sound compare <ours.wav> <video or wav> [--ours A:B] [--ref A:B] [--find | --fit | --at T:S]
                     [--band LO:HI] [--pitch LO:HI] [--peaks N] [--onsets LO:HI] [--rise DB]
                     [--track LO:HI[:S]] [--seconds]
```

prints both stretches' levels per side (and how the sides correlate: an inverted side shows as
a low or negative number), their spectra's centroid, shape match and the pitch ratio that lays
ours over the reference (1/48 octave bands, both sides' powers so an inverted side is not lost),
the strongest peaks, a note's fundamental over time, onsets, and with `--find` / `--fit` / `--at`
where ours sits in the reference: by spectra over time, then sample by sample with each side's
gain (least squares under 1 kHz, or in `--band`). Renderings to compare come from `gt4tool sound
<disc> seq-wav | se-wav | stream-wav | drive-wav | pass-wav`.

| What | The original | Ours | |
|---|---|---|---|
| Overall level | the menus' `cursor` on the title page: the shared mixer's half-level rendering × 1.795 (correlation 0.998, 99.6% of the power explained) at an effects option of 1, i.e. exactly 2 × that at a new game's 115 / 128 | the SPU2's level ("The output's level") | same |
| Menu effects | `ok` six times in the Arcade walk: × 1.79..1.91 | as the cursor | same balance |
| Sequenced music | the Arcade pages' `arcade` sequence: 2.0 dB under our rendering at full level (= 1.3 dB under the expected 0.48 × 115 / 128 x 2) | | within 1 dB 🔎 (by level per second; the sequencer's timing differs too much for a sample fit) |
| Streamed music | `race_35` in the race: × 0.20..0.24 of the stored samples (five fits) | the stream level 0.3 × 115 / 128 = 0.27 | the original 1.6 dB under ours ❓ |
| Count | Grand Canyon: `count_d` at 8.860, 9.861, 10.861 s, `count_s` at 11.862 s (sample fits, correlation 0.31..0.64): 60 frames apart (59.94 Hz); gains × 1.39..1.50 of our menu-level rendering (= the race master 0.40 × 115 / 128) | 180 / 120 / 60 / 0 steps | same |
| Rolling start | Tsukuba: `count_s` as START shows (19.67 s), no beeps | `start_signal()` | same once the Drive screen calls it |
| Racing music | `race_35` from the opening shots (rolling), `race_37` 0.25 s after START (standing), `race_36` after `race_35` in the B-spec race, the replay `race_35` again | play list, `rolling()`, `replay()` | same once wired |
| Engine pitch | Ka '01: 181 Hz at 5,650 rpm (chase), 164 Hz at 5,076 rpm (nose) | 181.1 / 163.7 Hz | same (was 147 cents low) |
| Engine levels | players' volumes in the saved states: chase engine 2146 / exhaust 5073 (levels 11 / 12), nose 4651 / 5464 (left inverted) of 16383 a side | 2147 / 4651 and the exhausts at their level | same |
| Engine by view | the fundamental at -26 dB from nose, chase and bonnet views alike; chase at full throttle (21-23.5 s): -27 dB at 176 Hz, -19.9 dB in 120-400 Hz | the Ka at 5,650 rpm from the chase view: -25 dB at 181 Hz, -18.8 dB in 120-400 Hz | within 2 dB |
| Title page | no music; `cursor` every move | | same |

❓ Not compared yet: the course ambience (crowds, tunnels: Route 5's recording has no racing piece
the play list explains), the wheels' notes and contacts (no clean recording of a slide or a hit),
Grand Canyon's dirt sounds, Gran Turismo mode's pages.

