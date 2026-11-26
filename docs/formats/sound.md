# Sound

Code: `src/gt4r/formats/sound/` (the files), `src/gt4r/audio/` (what plays them),
`tools/gt4tool/cmd_sound.cpp` (`gt4tool sound`), `tools/gt4tool/cmd_sound_compare.cpp` (`gt4tool sound compare`),
`tools/gt4tool/cmd_sound_players.cpp` (`gt4tool sound players`), `tests/test_sound.cpp`,
`tests/test_sound_race.cpp`, `tests/test_sound_layers.cpp`, `tests/test_sound_compare.cpp`. Everything is
played through the shared engine's SPU2 voice mixer and reverb
([engine/gt3r/docs/formats/sound.md](../../engine/gt3r/docs/formats/sound.md),
[sound_sequencer.md](../../engine/gt3r/docs/specs/sound_sequencer.md)): sample data is
PS-ADPCM, pitches and envelopes are SPU2 register values.

Facts marked [P] are read from the game's program (the driver's routines sit at
0x559330-0x55E600 of `CORE.GT4`, SCUS-97328 v1.01; its two sequencers have the run-time type
names `SDDRV::SqSequencer` and `SDDRV::SeSequencer`); [F] from the files; 🔎 inferred; ❓ open.

## What runs where

| Part | Runs | Follows |
|---|---|---|
| The SPU2: voices, PS-ADPCM, envelopes, interpolation, the reverb unit | the shared `gt3r::VoiceMixer`, `gt3r::SpuReverb` | the hardware (not program code) |
| The sound driver: the two sequencers, the effect sequences, held notes, each voice's level and pitch (0x559330 .. 0x55E600) | `sound_driver.*` | Gran Turismo 4's; the lfo is not built (no voice of the discs is heard to waver) |
| The driver's voices: which of its 46 a sound takes, and which it cuts when none is free (0x55C758, 0x55D0B0, 0x55D1C0) | `sound_driver.*` (records over the shared mixer's voices), `layer_player.*` | Gran Turismo 4's (the shared mixer's own stealing rule is not used) |
| The engine, exhaust and wind banks' players (0x558FD8 and its helpers; the voice of kind 4) | `layer_player.*` | Gran Turismo 4's: every started player of 46 saved states the same; Prologue's (0x481588) with its own attack |
| A car's frame and mix (0x3B5E80, 0x392640): views, gains, microphones, panner, Doppler, the master, its fades and ducks | `car_sound.*`, `race_sound.*`, `race_tables.*` | Gran Turismo 4's, tables from the disc's program; on Prologue's disc its own (0x378870, 0x3A18A8: "Prologue" below) |
| The surround cue (the panner's two flags) | the shared `gt3r::SurroundCue` | the same rule as the program's, in every output mode as the program does it: the shared mixer's option is on while a disc is open (`with_surround_cue`; the shared engine's own screens keep their setting) |
| A car's banks and whine by its parts (0x445518, 0x445548, 0x445650, 0x4462A0) | `car_sound_files(db, equipment)` | Gran Turismo 4's: the entry's equipment (a tuned car's muffler and turbo kit), as sold for a car without one |
| The levels (0x350080: the session's own bytes F +0x604 / +0x605), wind (0x34FA90), cabin (0x34FBC0), whine (0x34FED0, 0x350DA0; a one-speed box's 0x369510, layout 7's motors' 0x356D88), afterfire (0x3F3368, 0x3F3278), the hit sound's recorder (0x350328), landings (0x3FA7B8), the heartbeat (0x355868, 0x355950) | `sim_sound_state.*`, `car_sound.*`, the recorder in `sim/collide.cpp` | Gran Turismo 4's, from the session's records; the heartbeat's phase kept by `CarSound` (the program keeps it in the car record, car + 0x788) |
| Contacts with the course's cones and boards (kind 2, 0x3E3C48 → 0x350530) | `race/gadget_bodies.*` (the impulse), the Drive screen's license judge → `RaceSound::contact` | Gran Turismo 4's: heard only while the rule block's +0x46 (the cones a test counts) is not 0 (0x35DF00) |
| The run's replay | `RaceSound` keeps each step's `CarSoundState` from the session and plays them again | Gran Turismo 4's inputs as the live run had them (the program runs its replay through the step again from the pad records, so its sound is the step's) |
| A saved run played back (no session, no recording) | `sound_state(race::CarState)` | judged from what the seam keeps: the quietest throttle 3/8 (torques 1 : 0.6, no boost), the skid's pitch from the slip, the rim's speed from the car's, a landing at the downward speed / 5, a contact as twice the step's summed strength |
| The course's ambience (0x398B70, the emitters 0x457918) | `course_sound.*` | Gran Turismo 4's, the `CRSS` section read by the shared reader (the same layout); the slots a course allows (0x3922B0) are not applied, as a wheel's notes follow the surface under it |
| The count, the music's messages, the play list (0x39DA80, 0x389758, 0x438170), which music a run plays (0x3C0208, 0x389D48, 0x3A0228, 0x3E1C80, 0x3E1EA0) | `race_sound.*`, `race_playlist.*` | Gran Turismo 4's; Prologue's race music silent as its options have it |
| The menus' sound object (`menu::MSound`) | `menu_sound.*` | Gran Turismo 4's; Prologue's music at its own level 1.0 (its level setter 0x421AB8 has no caller) |
| Streams (`.ads`): reading, decoding, playing | `stream_voices.*`: a PS-ADPCM piece on two of the shared mixer's voices (its music bus), as the IOP's stream module plays it on core 1's voices 22 and 23; anything else (a mod's PCM `.ads`) through the shared `gt3r::AudioEngine` stream player | Gran Turismo 4's voices: the SPU2 pitch rate x 4096 / 48000 (3763 at 44.1 kHz), envelope 0x000F / 0x0000, no reverb, the level x 16384 on each voice's side, the piece from its start again when it repeats (the headers' loop fields are -1), the SPU2's interpolation. The stream module itself is not in `CORE.GT4` (its reading ahead is ours: two seconds at once, the rest on a thread) |
| Pit sounds | `RaceSound::pit_effect`, `RaceSound::refueling`; the Drive world takes the stop's bit 0x200 | `jackdown` as Gran Turismo 4's pit scene plays it (0x35BE38, for the car shown 🔎), heard in the app's B-spec stop; the crew's (`wheelgun`, `dropnut`, `droptire`, `jackup`: their motions' channels), `oiltank` and the held `refueling` wait for the pit crews' scene (0x3CFA98, 0x3D03E0), which is to call these two |

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
| 2 | the lfo waves: the driver's lookup of an lfo wave (0x559A88, from the two routines that set a voice up: 0x55A078, 0x55A9B0) reads it as `u16 last, u16 offset[]` to waves of 60 steps (the one offset points past the header); its only reader, and what it gives a music voice is replaced by the channel's bend before the pitch is made (below, "lfo"): it is never heard | music banks |
| 3 | effect sequences | `gt4sys`, `gt4count`, `gt4pod`, `gt4shutter*` |
| 4 | ❓ a 16-byte head (`u8 100`, then 0) and 24 records of 16 bytes numbered 0 to 23 (`u8 0, u8 n, u8 0, u8 100, u8 0x40, 7 × 0, u8 4, u8 0, u8 100, u8 0`): 🔎 a default for each of 24 channels or voices (a volume 100, the center pan, 4, 100). The same bytes in every effect bank of both discs (`gt4sys`, `gt4count`, Prologue's `gt4pod` too), whatever their sequences; no reader found in `CORE.GT4`, and the effects' levels match the original without it | the same |
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

**Voices** (0x55C758, 0x55D0B0, 0x55D1C0; the records at 0x6518C8 + 0xA8, 0xE0 bytes each). The driver keeps
48 voice records and hands out 46 (record i is SPU2 voice i of core i / 24; records 46 and 47 are the stream's two,
core 1's 22 and 23). Every request may take any of the 46 (its mask, 0x6C8BD0 and the sequencers' own, is
0x3FFFFFFFFFFF in every saved state). A record keeps +0x34 its priority (signed 32 bits), +0x40 a voice group, +0x1A
bit 4 a "kept" flag, +0x14 its age (the driver's steps since it sounded, counted by 0x55D890) and +0x48 a serial (the
order it was handed out, from a counter at +0xA0). A free record comes first: the head of a free list, onto whose head
0x55D890 puts each record whose voice has ended, in the records' order (so the last ended is reused first). With none
free:

| Who | Rule | Its voice takes |
|---|---|---|
| a held note (0x559EB0), a layer player (0x558FD8), an effect sequence given a priority (0x611AA0) | 0x55D0B0: the record of the lowest priority at or under the new one's; among equals the oldest by age, and of equal ages the later record (0x55D070). None at or under it: no voice | the caller's priority, sent again every frame; an effect's voice is kept when its tone's flag 0x01 is set |
| a music note (0x611CF0) | 0x55D1C0 → 0x55D148: the first handed out (the lowest serial) of all, whatever its priority | priority 0, group 0 |
| an effect sequence without one (priority -1, the default of 0x55A748) | the first handed out of its tone's voice group (tone byte 0, when not 0), else of the voices no tone keeps | the tone's priority (tone byte 1) and group |

The cut voice's record takes a new serial (0x55D028) and the new sound starts on it at once. In the saved states the
menus' and the races' effect sequences have priority 0x7FFFFFFF (they take a voice from anything); a race's held notes
and layer players |level| × 2147483520 (0x392640, 0x398B70: in the EE's floats, at most 0x7FFFFFFE), sent again each
step. A race uses all 46: in the saved states of Tsukuba's replay and of Route 5 at night none is free (25 / 21
layer players' voices, 21 / 25 held notes).
`SoundDriver` keeps these records itself and plays each on one of the shared mixer's voices (whose own stealing rule
is then never used); `audio::level_priority` is the race's priority of a level. Tested: `sound_driver_voice_records`.

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
the sequenced music's banks load (0x460ED8, both cores, the work areas ending at 0x1FFFFF and 0x1DFFFF), the
effect-attribute call's only caller (0x55CC10): nothing sets another mode. A race moves only the depth, by the
road under the camera (the course's sound below). The SPU2s of all 389 saved states of the reference (both discs: menus,
pre-race pages, races from every view, replays, license tests, the B-spec demos) hold studio C's coefficients on
both cores, and the depth 0x0FFF (open road and menus), 0x3FFF (Route 5 at night in its tunnel: 0.5) or 0x1FFF (the
B-3 demos: 0.25). The shared mixer's boot setting is the same mode and depth, so it stays.

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
the parts of the entry's equipment record (0x3B5D90: the record at the entry + 0x20; [database.md](database.md)):

- the number (0x445518): the car's `ENGINE` row, u16 at +0x18;
- `%s` (0x445548, 0x445650; the eight names' pointers at 0x6213F8): `normal` or `turbo` and a digit: `turbo` when
  the car's `TURBINEKIT` row has a non-zero byte at +9; the digit is the byte at +6 of its `MUFFLER` row (0 without
  one).

So a fitted muffler or turbo kit changes the exhaust heard: another muffler names another bank for 574 of the 722 cars
(`sound_equipped_cars_on_disc`; the stock parts name the same files for all 722).

All 722 cars' two files are on the US disc (383 different numbers; 259 cars are turbo), e.g.
the Isuzu 117 Coupe: `sound/engine/55201` and `sound/normal0/55201`. The wind's bank is
`sound/roadnoiz.es` [P].

### The layer players [P] (`audio/layer_player.*`)

The driver has 24 players of 0x58 bytes at 0x64FBF8 (US 1.01), stepped 60 times a second by
0x5596F8 → 0x558FD8 while their word +0x54 is 2 (0x559330 starts a player, 0x559378 stops it and
releases its voices). 0x559470 sets one up on a bank's first layer table (+0x30 the table, +0x34
the count), 0x5593F8 its inputs to 200 rpm, Doppler 0x1000 and silence. A race writes the inputs
every step (0x392640): +0x48 the revs (s16 of the float), +0x4C / +0x4E the two side levels
(int(side x level x 16383), at least -0x3FFF and 0x3FFE for anything from 0x3FFF up), +0x40 the
Doppler factor (int(x 4096) held to 0..0x3FFF), +0x3C the priority (|level| x 2147483520, at most
0x7FFFFFFE), +0x50 the reverb flag (0: the voices send). The two slots, 0x18 bytes each from +0:
`+0 voice, +4 layer wanted, +8 layer playing, +0xC s16 left, +0xE s16 right, +0x10 pitch, +0x14
the sample's address / 8`.

A step:

- **The layers**: revs under 200 count as 200. The first layer i (from 0) whose `lo..hi` holds the
  revs plays alone, unless before it a pair `hi[i - 1] < revs < lo[i]` is found (the program checks
  layer i's range, then the pair (i - 1, i), then layer i + 1's ...): the pair crossfades. Outside
  every range the slots keep what they want. Then when the first wanted layer is the second slot's
  playing one, or the second wanted is the first's (-1 alike), the two wants swap: a layer stays in
  its slot.
- **A slot whose wanted layer is the one it plays and that has a voice**: the pitch (0x558DB0) =
  layer pitch x revs / layer revs (unsigned, at least 1), held to half .. twice the last; the levels
  (0x558E28): x = 0x4000, or with the other slot's layer wanted: (revs - other hi) << 14 / (lo -
  other hi) under this layer's range, (other lo - revs) << 14 / (other lo - hi) over it, at least 0;
  g = sine[x >> 4] (0x64D3F8, 1,025 halfwords 4096 sin(i / 1024 × π/2): the program's table is
  exactly that) × 0x3FFF >> 12; level = layer volume × g >> 14; each side = input × level >> 14,
  its size held to the last size ± 0x1000 with the new sign. The voice gets the pitch, the Doppler
  factor, the levels, the priority.
- **A slot whose wanted layer changed**: its voice is released (0x55D688: the ADSR's release), the
  pitch and levels are worked out without the limits, and a new voice (kind 4) is keyed at the
  layer's sample with ADSR1 3, ADSR2 0xA: at once to full, a dip to a quarter and straight back (a
  few samples), released in about 43 ms (linear, rate 0xA). No voice free: the slot plays nothing
  and tries again the next step.

**The voice** (0x55D890, 0x55DF70): a kind 4 voice has the driver's level rule with base 0x3FFF,
sequence 0x3FFF, the kind's master (0x3FFF in every saved state), 0x7F everywhere else and the
center pan (120 / 127 on both sides): each SPU2 level = (15478 × slot side) >> 14, held to
-0x4000..0x3FFF, and in mono output both the mean of the two sizes. The SPU2 pitch = slot pitch
(u16) × the Doppler factor >> 12, 0x1000 playing the sample at 48 kHz (Gran Turismo 3 took the
same number as 44.1 kHz). Checked: the Ford Ka '01's strongest note at 5,650 rpm is 181 Hz in both
the original and ours, at 5,076 rpm 164 Hz in both.

**Checked against the original** (`gt4tool sound players <eeMemory.bin>...`): in 46 saved states
of Gran Turismo 4 (menus, pre-race pages, races from every view, replays, license tests) every
started player (594 records, 175 of them crossfading two layers) has the slots `LayerPlayer` works
out, and every slot's voice the SPU2 levels.

**Prologue's players** (SCPS-15055 v1.01: 24 of 0x54 bytes at 0x516278, stepped by 0x481588 while
+0x50 is 2; 0x481360 / 0x4813D8 the pitch and the levels) have the same rules and record but no
reverb flag (every voice sends), and a new layer's voice is keyed with ADSR1 0x1003: an attack of
about 0.8 ms (`RaceSoundTables::layer_adsr1`). Its saved state of a race: the three started players
the same, two of them crossfading. In most race states the
race had already written the next step's inputs; the tool then finds revs and side levels of the
step before that give both slots at once (the same revs for both pitches, one input for both
sides' crossfaded levels).

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
  same with the flag's opposite). The flags are the car's frame's (0x391AF0, after the panner 0x39A7B0): x and z
  over the distance, z read from the place before the quarter metre's move (a sound at the camera, or just behind it,
  counts as behind: its left side inverted), the distance after it; Prologue's mix (0x3A18A8) takes the moved place
  (a sound at the camera counts as ahead). The rule is the shared `gt3r::SurroundCue`'s, z = 0 behind and x = 0 on
  the right in both, the camera's forward being the program's -z. The program does it in **every** output mode: the original's
  stereo recordings of the nose view carry the exhaust's left side inverted (its players' volumes
  -5464 / +5464 in the saved state; the recording's two sides correlate +0.28 there, +0.90 from
  the chase view). Here the shared mixer's surround option is on while a disc is open (`with_surround_cue`; the
  shared engine's own screens keep their setting), for the layer players and the held and single
  notes alike. Measured: the engine's gain at 29 m is 1/11 of its gain at 5 m and
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
  only): on while it plays a narration clip (`narration/<region>/%08d.xsp`, 0x4606D0 → 0x460658:
  the narration's level 1.0 through 0x460600, the clip through the stream routines 0x5587C0 /
  0x558830). The narration module reads `narration/<region>/narration.inf` (0x460580: 16 bytes to
  0x846880: `"NARR", u32 1, u32 0, u32`) and plays clips only while the last word ([0x84688C]) is
  not 0; on the US disc it is 0 and the folder holds no `.xsp` (its 56 `.ads` are the event and
  license narrations, `ev*e.ads`, `l0p*e.ads`): **the free run's duck never turns on** there. 0x391DC8 fades out (0x3E1C80,
  0x3E1EA0 ❓). `RaceSound::fade(target, seconds)` is the routine; `set_replay_shot(designer)`
  (every step of a replay) and `set_free_run_duck(on)` are the ducks; `replay()` starts the
  master from 0 over 3 s.
- **The levels** (0x350080, 60 a second): n = the friction torque / (the friction torque + the
  full-throttle torque) is the throttle that just holds the revs; e = |load - n| + n with the engine's load
  min(F +0x4F0, 1) (the throttle, for a boosted engine times its boost: a turbo car is near full level from about 0.6 of
  the throttle), halved when the clutch (the record's byte, 1 locked) is not locked and for a hybrid; the
  engine's level byte moves half way to min(127, int(e × 127 × B25)). The exhaust's: y =
  max(load - n, 0) / (1 - n), x = (1 - (1 - y)^16) × 0.6 + 0.4, its byte half way to
  min(127, int(x × 127 × B26)). The spool's byte is min(127, int(byte × B27)), the blow-off ×
  B28. A hybrid (P +0x246 = 3) under its idle revs: both 0. The step
  keeps both bytes (F +0x604 / +0x605; `rules::engine_sound`, bit for bit) and they start at 127; `sound_state(session, i)`
  hands them over. In 1,559 cars of the saved states of races the bytes are what the rule gives within its rounding but for
  40 (a level on its way, a ghost). Rendered (B25 = B26 = 1, torques
  3 : 1): throttle shut 63, at a quarter (the quietest) 30, open 126, open with the clutch
  open 63; the exhaust 50 shut, 126 open.
- **The layer players**: engine = G0 × level / 64 at the engine's place (mic 0); exhaust =
  G1 × level / 64 at the exhaust's (mic 1); wind = G8 × the wind (0x34FA90) on
  `sound/roadnoiz.es` at the engine's place and gain, its "revs" made from the speed. Each
  player's gain is clamp(placement gain × M, -1, 1) × that.
  The wind's drag is P +0x1064 and so is the cabin's: the body's drag coefficient alone (the spec's byte +0x42 / 100), not
  the force's +0x1068; the wind's slipstream is F +0x59C (this step's sum), the cabin's F +0x5A0 (its filtered value).
- **The block of numbers** (42 bytes after twelve 0x28-byte curve records at 0x6224B0; the
  block at 0x622690; made into floats by 0x34F6A0): wind gain = p[0xE] / 10 ×
  (p[0xF] / 1000)² × the car's drag coefficient × speed² × (1 - slipstream), at most p[0x14] / 10, its revs
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
  (p[2] × 9.8 × ratio[first])| with the clutch's torque (F +0x51C), the gear (+0x520, 1 the first)
  and the driveline block's ratios (P + 0x234 + 0x40 + 4 gear), none while the clutch (F +0x522) is
  open; pitch p[4] / 10 + slope × |the driven axles' speed| (0x350DA0: by the layout, F +0x52C the
  front's, +0x530 the rear's, their mean, or the rear's plus the split × the difference), all read by
  `sound_state(session, i)` from Gran Turismo 4's drive routines. 0x34FED0 first asks the car's draw mode
  (0x366678: 3 for a car in the race, F +0x441 0; a ghost has no whine), then two drives make their own, whatever
  the gearbox's grade and the clutch: **layout 7's motors** (P + 0x234 byte 0 = 7, 0x355D48: the Acura DN-X;
  0x356D88): pitch 0.4 + 0.012 × the front wheels' mean |rim speed| (wheels 0 and 1, +0x20), level the motors'
  output (F +0x51C) / 4, its sign kept; **a one-speed box** (the gearbox's kind, P + 0x234 + 0x12, = 3, 0x368398:
  the Toyota Priuses; 0x369510): only while the brake pedal (F +0x48C) and the clutch's request (F +0x514) are
  both not 0, pitch 0.3 + 0.012 × |the mean rim speed| of the driven group's two wheels (the layout row's +0x0C +
  P + 0x237 and its partner +0x24 + w), level 0.2 × their brake pressures (+0x64) summed; else pitch 1, level 0.
  Rendered (`CarSoundState::whine_given`): the DN-X at 56 km/h pitch 0.59, level 0.30; a Prius braking up to
  0.37); in_car G7 at the
  body; nos 1 for the in-car own view while flowing; body_noiz always 0. in_car, body_noiz and
  nos ease: rising at once, falling 0.05 a step.
- **The wheels** (wheel record car + 0x164 + 0xEC w: +0x89 surface, +0xB4 slip intensity,
  +0xB8 skid pitch, +0x20 rim speed): each at its own place on mic 2, at clamp(gain × M) × M
  (the master twice). Notes by the surface index (the road's material, or its wet variant 20,
  21, 22): on_skill G11 and on_road G9 (the roar: (rim speed / 100)² × gain × the tire's width
  factor from 0.135 to 0.355 m, its pitch by the rim speed) on 0, 7, 8, 9, 12; rain_noiz G10 on
  6, 20, 22; off_roadnoiz G14 on 5; off_skill G13 on 2, 3, 4; pebble G14 on 13; sand G14 on 14;
  rumble G6 on 1, 21; rumble2 on 10; rumble3 on 11; the slipping notes by the slip intensity at
  the skid pitch. The notes past the first three ease as above. **The slots a course allows**
  (0x3922B0, at the race's setup from 0x360458: a mask 0x34D6E0 → 0x360348 makes from the course
  object's set of the surfaces it has, its word +0x94, bit s for surface s, and its flags +0x98):
  for each surface under 15 in the set, tarmac (0, 7, 8, 9, 12) allows on_skill and on_road, and
  rain_noiz too when the flags' bit 1 (wet) is set; 6 rain_noiz; 1 rumble; 2, 3, 4 off_skill; 5
  off_roadnoiz; 10 rumble2; 11 rumble3 and on_skill; 13 pebble; 14 sand (the mask's bits 0 .. 10
  → slots 0, 1, 2, 3, 7, 4, 8, 9, 10, 5, 6). Not applied: a wheel's notes follow the surface under
  it, so a slot whose surfaces the course lacks never sounds; the mask only differs for a wet
  variant (20, 21, 22) on a course whose set lacks the dry one, which the set's being the course's
  own makes 🔎 impossible.
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
  kind 2 for a course object (the course's objects at [0x6D6054]): the impulse 0x3E3ED0 gives the cone or board
  (`race::GadgetBodies::impulses`), recorded only while 0x35DF00 (the race's rule block +0x46, the cones a license
  test counts) is not 0, so a cone is silent in a race and in a test that fails at the first cone, and heard
  (`clash4`) in one that counts them. So the sound's strength is
  twice what the contact adds to the body's strength, against a wall and a car alike.
  The race step (0x34DB30) clears the recorder (0x350308, kinds 0 and 1) before the collision
  step; the sound frame reads it (0x350468; a kind 2 is cleared there). Built in the collision step
  (`sim::collide::Body::hit_sound`, `hit_kind`, `hit_other`), which `sound_state(session, i)` takes.
  Without a session, `sound_state(CarState)` takes the seam (`CarState::contact`, the body's
  strength summed over the step, at most 1) as min(2 × contact, 1), none under 0.2: the same for one
  contact, the sum where the program keeps the stronger. Measured: driving straight on into
  Tsukuba's first corner, 60 steps against the wall, one over 0.2; on_bound for the in-car own view (the strongest wheel landing, 0.5..1,
  × G15 × 2.5; a wheel's landing, 0x3FA7B8: on the step its contact state, wheel + 0xDC, is 2, the
  hub's speed into the ground, -wheel + 0xD8, from 0.5 at most 1; none on surfaces 1, 2, 3, 4, 6,
  10, 11, 13, 14; `sound_state(session, i)` reads both from Gran Turismo 4's body routines); backfire / backfire2 (one at random) at the exhaust, strength 3, when an
  afterfire flame starts (0x3B6B78: the flame object's latch +0x64, read and cleared by
  0x3D6F30). **The afterfire** (`RaceSound` from the simulation; `CarSoundState::shift_flame`,
  `afterfire_chance`): 0x3F3368 (each step, from 0x34DB30) sets the car's byte +0x6C4 when the
  throttle (F +0x494) is over 0.9 and the gearbox's lock-out (F +0x526) is 11, the step after the
  clutch locks at the end of a shift (it is set to 12 there); 0x3F33A8 makes the flame's strength
  the parameter block's float +0x10DC while the byte is set, else 0. The flame object
  (0x3D6D30) starts its flames as the strength rises past 0.1 when the object's random number
  (0x578598 on its seed +0x68) is not over it. +0x10DC is set when the car is built (0x360780 →
  0x3F3278) from the car's peak power in PS (0x34F3F8: the torque curve's kgf m × rpm / 716.2 at
  every 100 rpm, with the boost) by the table at 0x622320 (the 16 bytes before the views' gains):
  for the spec's kind byte +0x120 (0 a road car with its own muffler, 1 one with another muffler,
  2 and 3 by the car's kind byte, [database.md](database.md)) and whether the engine has a boost
  (P +0x230, the boost's cap, over 0), a pair of powers × 10 PS: under the first 0, from it 0.25
  rising to 0.75 at the second and above. US 1.01: kind 0 500..1000 PS (with a boost 250..500), 1
  200..400 (150..400), 2 200..400 (150..400), 3 100..400 (100..400). The Ka and the Tsukuba
  session's two stock cars: 0. **The heartbeat** (single note 10, `heartbeat`, frame +0x2C8, at the body; 0x355950 gives it
  in the car's own in-car view only, for a car of category 3, P + 0x32: the Nike One): 0x355868 in the race step (0x34DB30)
  adds (0.8 + 2.2 x) / 60 to a phase (car + 0x788) each step, x = |the speed along the body| (F + 0x53C) / 66.67 m/s at most
  1; when the phase reaches 1 it loses 1 and car + 0x78C is 0.6 + 0.4 x for that step (else 0): a beat, 48 a minute
  standing and 180 from 240 km/h, at that strength (no view gain: 0x392EB8 takes it as it is, times the body's place).
  The other single notes: 0 `b_off`, 1..3 `clash` / `clash2` / `clash4` (by the contact's kind), 4..6 `clash10` .. `clash12`
  (always 0), 7 `on_bound`, 8 / 9 `backfire` / `backfire2`; the table at 0x621418 pairs each with its place (0 the
  engine's, 1 the body's, 2 the exhaust's).
- 🔎 The frame's reverb flag (+4) is never written: the voices send to the reverb.

### The course (0x398B70)

Five lists of the `CRSS` section play air (31), city (32), crowds (33), cheer (61), sea (34).
Air and city are not placed: both sides at the first emitter's first point's weight. Crowds are
placed (each point's weight, past 32 m × 32 / distance, a sine pan) with a Doppler pitch; cheer
and sea placed with pitch 1. All × M, at most 1; with more than one camera both sides take the
larger and the pitch is 1. The emitters (0x457918): without gates the gain is 1 at once; with
gates the cars between them weigh 1, 0.8, 0.64 ... in the order of the race's list of cars, which is
sorted by race position (the entries' words +0x10, the list's head at the race's +0x60 → +0x14: the
first car counts 1, the second 0.8 ..., in or out of the gates), at most 1, the gain rising 1/120 and
falling 1/180 a step. The road's triangle under the camera (asked from 5 m above), flags & 0x18:
0x08 the first tunnel's pair (tunnel1_l on the left, _r on the right) at M and a reverb of 0.5;
0x10 the second's (its _l on the right, _r on the left) and 0.25; 0x18 0.25 only; else 0.125.
The reverb's depth (int(x × 32767) both sides, 0x55CD18) moves 1/36 a step; with more than one
camera it is 0.125. heri plays at 1 (without M) while the race's virtual +0x130 bit 0 is set.
Measured on Tsukuba: four crowd emitters and no others; the crowds at 1 × M. In 34 saved states at
Tsukuba (the camera's place and its right axis from the camera object, CameraManager +0x13C, +0x10 and
+0x100; the cars' distances and positions from their records) the crowds' two side gains are this rule's
wherever the gains have had time to settle (e.g. 1894 / 2245 against 1895 / 2246, with six cars strung
out); the tunnels' pair plays at M on its sides (5887) and the reverb's target is 0.5 in the states in
Route 5's and Special Stage Route 5's tunnels; Grand Canyon's cheer at M. `gt4tool sound <disc> crss` lists every
course's emitters by list.

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
  The free runs (0x112620, 0x116E20) and the machine test (0x115A90) have timeattack, the license's driving
  trainings (0x114E38) and the missions (0x11BB60) license_start / license_gold, the free practice (0x1181C8) etc,
  a series' races champ and its last last, the license tests license_start / license_gold and the coffee
  breaks (the class letter c: the race's virtual +0x300 answers 9) coffee.
  **Who posts them** (0x3B76F0 into the mailbox at the race information + 0xF4): 0x3C0208 (from RaceBase's
  virtual +75, as a run is set up) by the start's type (race information +0xB4): -1 (rolling) code 12 at
  once, 1 (a hold of 8 s) code 19 and 2 (10 s) code 10, both the start code, anything else nothing.
  At a start message (the mailbox holds one of type 4) RaceBase's virtual +79 (0x389D48) posts code 12
  once in a run, unless the type is -1; the license tests' (RaceLicense 0x3BC528) and the driving trainings'
  (RaceTrainingBase 0x3ED180) virtual is empty: they run without music. At the player's finish
  RaceDisplay's virtual +36 (0x3A0228) posts code 11; the license tests' display (0x3BD330) posts 0x14 only
  while its word +0x20 is 2 ❓, nothing in a test. A test's result posts (0x3E1C80) 0x15 (license_goal) for
  the ranks 1 and 2, 0x16 (silver) for 3, 0x17 (bronze) for 4 and 5, or code 11 where the virtual +0x300
  answers 9 or 10, and a failed one (0x3E1EA0) 0x18 (license_failed); both fade the race's master to 0
  over a second (0x391DC8). RaceBase's virtuals +89 and +90 post -3 and -2.
  Here: `RaceSound::begin_run(kind, start)`, `start()`, `finish()`, `license_result(grade)`.
  Seen in the original's recordings (each piece found by its sound, 15 frames after its message: the stream's loading):
  the arcade's standing start at Grand Canyon `gt4jg-12b` as the hold begins and `race_37` 0.25 s after START; a
  championship's race `gt4jg-06`, the racing piece 10.0 s after it, `gt4jg-04a` at the end; the Family Cup the racing
  piece until the frame FINISH shows and `gt4jg-12a` then; license B-1 `gt4jg-19`, no piece during the run, `gt4jg-09` as the
  car stops (a bronze) and `gt4jglf` on a fail, then `gt4qm-02`; a mission's rolling start its racing piece
  with the opening shots. The next racing piece follows the end of the last within 0.14 s; the replay
  plays the race's first piece from its beginning.
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
- **Pit sounds** (0x393428: an effect of the index by its number in the name table 0x623A78, key 0x40, at the
  master's level × the given level, pitch 1; 0x3934A0 the same held, by a handle): the pit scene 0x3CFA98 for each car's
  box, for the car it shows (the scene's +0x10000 - 0x6988 🔎): `wheelgun` (67), `dropnut` (63), `droptire` (64) and
  `jackup` (66) when the crew's motion channels `wheel_gun`, `wheel_nut`, `tire_sound2` and `jack_up` (0x429740, between
  the last frame and this one) are above 0; `jackdown` (65, at 1) when the stop's service has its bit 0x200 (the tires on,
  set at the stop's 59th step from the end): 0x35BE38 clears it as it plays (so the original's B-spec stop reads it 0 ten
  steps later); `oiltank` (69) when the crew's fifth man (0x3CC960: index 4) takes another motion, his words +0x170 and +0x174 both 0;
  `refueling` (70) held from the step one of the scene's six hoses (+0x98B8 + 8 k, the last step's value 4 bytes after) starts until
  none runs (0x3D06A0, released by 0x393528). Built: `jackdown`, for the camera's car, and the two calls the crews' scene makes
  (`RaceSound::pit_effect` for the others, `RaceSound::refueling`). In the original's recording of the Family Cup's stop (its music taken
  out, each effect found by its own samples, video seconds) `jackup` 102.79, `oiltank` 104.04, `wheelgun` 104.29 .. 104.76, `dropnut`
  105.48 and 105.56, `oiltank` 105.74, `droptire` 107.33 .. 107.73 (pairs at once), `wheelgun` 111.17 .. 111.60, `jackdown` 113.81, each at 0.78 .. 0.81 of the
  menu-level rendering where the master's is 0.80 (`jackup`, `oiltank`, `wheelgun`; `jackdown`'s first quarter second 0.75; the overlapping ones do not tell).

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
- **Its master**: [0x503978] × [0x50397C]. 0x3A13E0 sets the first to 1.0 or 0 by the race
  effects option's flag alone (no level, no mix table: Prologue's disc has no `sdvol.dat`); the race's
  setup (RaceBase virtual 22, 0x37BB08, and 0x37D794) sets the second to 1.0. Both read 1.0 in the
  original's saved states of a race and of the pre-race page. `RaceSoundTables::option_level` is
  false for this program and `RaceSound` takes the option as 1 or 0.
- **Its music's level** stays 1.0: the level 0x421AB8 sets is never called (no caller, no `jal` to it and no word 0x00421AB8
  anywhere in the unpacked program), so [0x50587C] keeps the image's 1.0 and a piece plays at the script's volume × 1.0 (0x421800). It reads 1.0 in both its saved states (the level 0x421AB8 keeps at [0x50587C]: a
  sequence through 0x421238, the driver's music master × 16383, a stream through 0x4225B0): the
  driver's music master is 16383 and on the starting grid page the stream's two voices play at
  16383, where Gran Turismo 4's carry the options' 115 / 128 (its recording's arcade pages at
  -11.6 dB agree: our options sequence at 1.0 renders at -12.7 dB). Ours: the menus give `MenuSound` 1.0 on
  Prologue's disc.
- **Its frame** (0x378870) has one place for a car's three sounds, the car's from the camera, and from the car's own
  in-car view the camera's own (0, 0, 0; the constant at 0x503310): nothing is ahead or behind and no side is inverted. Its
  mix (0x3A18A8, 0x3A1D38, 0x3A20C8, 0x3A2388): gain = clamp(32 / d × the frame's scale, -1, 1), then × [0x503978] ×
  [0x50397C] (Gran Turismo 4 holds gain × M); the held notes take their level at once and the wheels' the master once; four
  single notes (b_off and the three clashes, from 1 / 16,384) and no landing, afterfire, nitrous or heartbeat; the wheels' notes are
  its seven (`on_skill`, `on_road`, `rain_noiz`, `off_roadnoiz`, none, `rumble`, `off_skill`). The wind (0x3B8AD8) and the cabin
  (0x3B8BC8) both take the filtered slipstream (F +0x530), the drag P +0xE74. A category 1 car's mood would sound at its place
  (the `pod_` effects, 0x571950); ❓ no car of its disc was found to have that category. Its master has no fade and no duck
  ([0x50397C] is only ever set to 1).
- **Its ambience** (0x3A2900, the lists' effects 0x3A2728): the five lists are air, city, none, **crowds** (the fourth: Gran Turismo
  4's cheer), sea; a list of air or city with an emitter plays at 1, each placed list at its own pitch, and a gated emitter's gain
  moves by 1/60 a step both ways (0x40FE80).
- **Its race music**: the racing piece's level is the option block's own float (+0x1C in a race, +0x28 in a replay), and the block's
  constructor (0x3FFA50) sets 0.9 at +0x28 and leaves +0x1C 0; nothing else writes them, and its saved states hold 0 and 0.9: a race
  runs without music (its recording has none after START, the `str01j` jingle over the hold at 1.0), a replay with it at 0.9. The
  codes are the groups + 18 (0x14 the racing group). The block is the option object at Status + 0x2F510 (vtable 0x53A490):
  RaceBGMPS2::vf5 (0x37E168) reads Status + 0x2F534 in a race and + 0x2F540 in a replay (the session's +0x340), the
  constructor writes 0 and 0.9 there, and no other instruction of the program writes either (every saved state: 0 and 0.9).
  **Checked on the recordings** (in game time; each piece found by its own samples, `scan.py` over the disc's pieces): the
  arcade race (`14-prologue`, Tsukuba) `str10j` on the grid page, `str01j` from 346.35 s, START at 356.18 s (the clock), then no
  piece to the end (41 s); lesson 1's passing run (`37-prologue-medal`) `str04j` at its start, no piece while driving,
  `str08j` with the medal (119.46 s); the replay after EXIT `str01j` as it begins (3.11 s, the start's shots again), then
  `race_j21` from 13.39 s, 10.28 s after the jingle (the live run's START 9.83 s after its jingle), at 0.878 x the stored
  samples (correlation 0.993, 14 .. 26 s), the grid page's `str10j` (level 1.0) at 0.983 in the same recording: 0.893, the
  option's 0.9. Ours (`RaceSound::replay`): a replay of a run with a standing start plays the start's jingle again and the
  racing piece at 0.9 from the step the run started (🔎 the original's 0.45 s after it not followed); test
  `sound_race_prologue_master`.
- **Its car sounds** are read as Gran Turismo 4's from its database presented in Gran Turismo 4's
  layout ([prologue.md](prologue.md)): every engine's file is on the disc.
- **Its whine** (0x3B8E88) is Gran Turismo 4's rule (0x34FED0) on its own record, the shared engine's layout four bytes
  on (the record at car + 0xD8): the clutch's torque (+0x4F8), the gear (+0x43A) and its ratio against the first's (the
  driveline block's +0x3C + 4 gear), the clutch's state (+0x43B, none while open), the driven axles' speed by the layout
  (0x3B99B8: rear +0x460, front +0x45C, their mean for 3 and 10, rear + k (front - rear) for 8: Gran Turismo 4's cases),
  with layout 7's motors (0x3BEE58) and a one-speed box (0x3CD378: the driveline's +0x12 = 3) as Gran Turismo 4's. A car
  whines when its byte at car + 0xC58 is set (❓ what sets it; ours: the gearbox's `GEAR` row, as on Gran Turismo 4's disc).
  `sound_state` reads them from the session's record (test `sound_race_prologue_whine_inputs`).

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
| Streamed music | `photo_01` under four whole recordings (Time Trial, `25-sound-cars`), followed sample by sample (a stream's voice plays at 44,098.6 Hz, pitch 3763): × 0.2685 .. 0.2694 of the stored samples on both sides | 0.3 × 115 / 128 = 0.2695 | the program's level (within 0.04 dB). The SPU2 itself (the saved states' `.spu2`: the stream's two voices, core 1's 22 and 23, pitch 3763, ADSR 0x000F / 0x0000, one side each) holds 4416 = 0.2695 × 16383 in every race and pre-race state, 4915 = 0.3 in the license test's (a jingle group at the stream level alone) |
| Count | Grand Canyon: `count_d` at 8.860, 9.861, 10.861 s, `count_s` at 11.862 s (sample fits, correlation 0.31..0.64): 60 frames apart (59.94 Hz); gains × 1.39..1.50 of our menu-level rendering (= the race master 0.40 × 115 / 128) | 180 / 120 / 60 / 0 steps | same |
| Rolling start | Tsukuba: `count_s` as START shows (19.67 s), no beeps | `start_signal()` | same once the Drive screen calls it |
| Racing music | `race_35` from the opening shots (rolling), `race_37` 0.25 s after START (standing), `race_36` after `race_35` in the B-spec race, the replay `race_35` again | play list, `rolling()`, `replay()` | same once wired |
| Engine pitch | Ka '01: 181 Hz at 5,650 rpm (chase), 164 Hz at 5,076 rpm (nose) | 181.1 / 163.7 Hz | same (was 147 cents low) |
| Engine levels | players' volumes in the saved states: chase engine 2146 / exhaust 5073 (levels 11 / 12), nose 4651 / 5464 (left inverted) of 16383 a side | 2147 / 4651 and the exhausts at their level | same |
| Engine by view | the fundamental at -26 dB from nose, chase and bonnet views alike; chase at full throttle (21-23.5 s): -27 dB at 176 Hz, -19.9 dB in 120-400 Hz | the Ka at 5,650 rpm from the chase view: -25 dB at 181 Hz, -18.8 dB in 120-400 Hz | within 2 dB |
| Title page | no music; `cursor` every move | | same |

| Gran Turismo mode's page effects | `oil_jackdown` in the oil change (197.3 s of `home_garage_gtauto_tuneshop_walk`): gain 0.97 / 0.95 (correlation 0.23 under the page's music); the car wash (110 to 121 s) adds -23.2 / -25.7 / -29.4 / -37.0 dB in the octaves at 645 / 322 / 1289 / 2579 Hz | `car_wash` at the menus' level: -25.9 / -27.3 / -29.2 / -36.6 dB | same; the wash 1.5 to 2.7 dB under at 0.3 to 0.65 kHz |
| Contacts | the Ka scraping the tire wall (`tsukuba_start_chase_view_throttle_held`, 30 to 49 s): bursts at 2 to 8 kHz up to -24 dB; `clash` / `clash2` / `clash4` found there by their spectra (match 0.64 to 0.72) | | the levels not settled (sample fits correlate under 0.19) |
| The finish | the racing piece plays until FINISH shows, the goal jingle's message with it (the Family Cup: 185.65 s, `gt4jg-12a` from 185.9) | `finish()` | same |

**The streams in the saved states** (the stream's two SPU2 voices and their ring buffers in the SPU2's
memory matched against the disc's `.ads` by their data; levels × 16383):

| State | Piece (group) | Level |
|---|---|---|
| Arcade single race, time trial and Gran Turismo mode's Sunday Cup pre-race pages; Sunday Cup's result page; the pre-race page after a replay | `gt4qm-01` / `gt4qm-02` (`default_quick`) | 4416: the menus' stream level, 0.3 × 115 / 128 |
| Tsukuba (start, first corner, replay), Grand Canyon, Gran Turismo mode's free run, Route 5 at night, Seattle at dusk | `race_35`, `race_50` (the arcade race's start), `race_37`, `race_68`, `race_40` (`racing`) | 4416: the racing level 0.3 × the race music option 115 / 128 |
| License B-1: the count, the result | `gt4jg-19`, `gt4jg-09` (`license_bronze`) | 4915: a race's other pieces at the stream level 0.3 alone (RaceBGMPS2 vf5, 0x33D568: only code 12 takes the option) |
| Prologue's starting grid page | `str10j` | 16383 |

**On two voices, against the original.** The Time Trial walk's pre-race page (`11-license-tt/arcade_time_trial_walk_to_prerace.mp4`,
from 120 s: `gt4qm-02` 1.97 s in, correlation 0.96) against the piece rendered on two SPU2 voices (`stream-wav ... spu`)
and against the stored samples resampled plainly to 48 kHz, each laid on the recording by one gain at 0.2 .. 2 kHz, by
2 kHz bands from 2 to 20 kHz: the voices within 0.2 dB of the recording up to 18 kHz (+1.3 at 18 .. 20 kHz), the plain
resampling 0.2 .. 0.7 dB over at 2 .. 6 kHz, 1.5 .. 3.2 over at 6 .. 12 kHz and 4.6 .. 6.7 over from 12 kHz. Ours plays the
voices (`sound_stream_voices_on_disc`: the level of every second at 0.2 .. 2 kHz as stored, 2.8 dB under it at 8 .. 12 kHz).
The shared mixer has 46 voices for the driver's 46 and these two: with a stream on and 44 of the driver's sounding, the
driver's rule chooses the voice a new sound cuts.

**A menu stream's level** (0x4608C8, kind -2, 0x460220 → 0x45FFF8 → 0x45FFC0): the script's volume × `sdvol` level 0 (0.3),
no option; `option.menu_bgm_volume` (0x180AA8) is the raw option / 128, so the pages that pass it play at 0.3 × 115 / 128,
4416. Kind -1 (0x461978 → 0x461808 → 0x4616E8, which 0x461C28 marks 2) starts a *sequence*, with level 1 (0.48) and the
menu music option through 0x461620. The kind is the script's word (0x4607A0): `pcm` -2, `narration` -3, anything else -1. `startStream` without a volume
is taken as 1 ❓.

So the pre-race and result pages' music is `default_quick`'s (two pieces), started as a menu stream
(the menus' level), not by the race; Route 5 at night plays `race_68`.

**Four cars at Tsukuba** (`reference/25-sound-cars`: Time Trial, AT, the car stopped, four seconds' idle,
then full throttle through the gears). The recordings carry the race's music (`photo_01`, the play list of that card in the
pieces' own order); it is followed and taken out (× 0.2685), and what is left is compared.
Ours is `gt4tool sound <disc> session-wav <car> <out> "hold:0.5,full:2.5,brake:3,idle:4,full:8" incar|chase tsukuba` (the session
driving the car), laid on the recording by the revs: the revs of a recording are read off it against a sweep of ours
(`drive-wav <car> <out> 700:9000 60`), and `drive-wav ... @<file of seconds and revs>` plays a car at a recording's own revs where
its driver steered (the Ka's launches, the chase launches of the RX-8 and the R390). Mid and side in octaves from 45 Hz to
5.8 kHz, each half second:

| Car, view | Ours against the original |
|---|---|
| R390, idle, bonnet | level -30.3 against -29.8 .. -31.2 dB, the sides correlate -0.97 against -0.73 .. -0.90 (what is left of the music); the same notes, at 800 rpm. What stood out at 161, 322 and 645 Hz was the music |
| R390, bonnet launch, three gears | the revs the same within 0.03 s over 5 s; outside the two shifts' half seconds every octave of mid and side within about 2 dB, most within 1 (the turbo's whistle, the gearbox's whine and the squeal among them) |
| Mustang, bonnet launch | the side at 90 Hz .. 1.4 kHz within 2 dB, the launch's squeal at 1.4 .. 2.9 kHz within 1; the mid at 45 .. 720 Hz within 2; over that ours carries the main straight's crowds, the recording is on the back straight |
| RX-8, bonnet launch | in second gear the side at 180 .. 720 Hz within 1 dB, the mid at 45 .. 360 Hz within 2; over that as the Mustang |
| Ka, nose launch at its own revs | in the steady stretches (131.5 .. 133.5 s, 135 s) the level within 0.4 dB, the side's octaves from 90 Hz within 1.7; 2 to 3 dB over while the clutch slips (that rendering has no clutch) |
| Ka, bonnet launch at its own revs | the side from 180 Hz within 2 dB mostly (3 to 5 at 1.4 .. 2.9 kHz in the first second); the mid 3 to 7 dB under at 0.7 .. 2.9 kHz (no crowds in that rendering); two bursts at 45 .. 90 Hz, 🔎 the rumble strips (the driver steers left all along) |
| Mustang, chase launch | level within 0.6 dB each half second, the octaves mostly within 2 (up to 6 at 45 .. 180 Hz in one of them), the crowds in |
| R390, RX-8, chase launches at their own revs | level within 2 dB, 90 .. 720 Hz mostly within 2; over that the recordings' crowds |

**The frame against the saved states** (the car's entry + 0x2900 holds its three players, its held notes and each wheel's). In the 115
states of 322 in which the two players' levels tell the own car's view and so the master: the wind is ours in 95 (the others at a
side's full scale), the cabin's rush in 113 and the spool in all 59, the squeal in 80 of 88 (the others within 20 %), the loose
ground's notes ours or by the eased level's own 0.025 over it; the rumble strips' 4 of 17 the same, the others 🔎 on their way down
(0.05 a step) from a step before, its strength coming in pulses.
**Prologue's 240ZG** (`14-prologue/round4`, the nose view: an earlier reading took it for the chase view): in second gear, laid on by
the revs, 720 Hz .. 5.8 kHz within 1.1 dB mid and 2.1 side, most within 0.3, 90 .. 180 Hz within 1.4 (mid); 180 .. 360 Hz swings by up to
8 dB either way, the engine's and the exhaust's banks holding the same note there (🔎 their phases meet differently).

❓ Not compared: the blow-off's and the contacts' levels (no clean recording), the heartbeat (none), what the
license display's word +0x20 is.
