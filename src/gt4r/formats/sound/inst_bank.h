#pragma once

// "INST" instrument banks as Gran Turismo 4 writes them (sound/*.ins, music/<region>/*.ins):
// SPU2 sample data with the tables that say how to play it: programs of tones for the
// sequenced music, effect programs and small effect sequences for the menu and race sounds.
// docs/formats/sound.md has the layout; the sequencer that plays it is audio/sound_driver.h.

#include "gt4r/base/types.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace gt4r::sound {

// One sample with its envelope and tuning.
struct Tone {
    static constexpr u32 kNoSample = 0xFFFFFFFF;

    // A music program's tone: the keys it sounds for. An effect program's tone: the voice
    // group it belongs to (0: none) and its voice priority.
    u8 key_lo = 0, key_hi = 0;
    u8 center = 0; // the key at which the sample plays at 44.1 kHz
    gt3r::s8 fine = 0;   // added to the pitch, in 1/16 semitone
    u8 flags = 0;
    u32 address = kNoSample; // byte offset into InstBank::adpcm; kNoSample: an empty slot
    u16 adsr1 = 0, adsr2 = 0;
    u8 volume = 0;     // 0..127
    u8 pan = 0x40;     // 0 left, 0x40 center, 0x7F right
    u8 bend_range = 0; // semitones at full pitch bend
    u8 lfo = 0x7F;     // the number of an lfo wave, 0x7F: none (docs: no voice of the discs follows one)

    enum : u8 {
        kNoise = 0x02,       // the voice plays the SPU2's noise generator
        kProgramBend = 0x10, // the bend range is the program's
        kLfo = 0x20,
        kProgramLfo = 0x40, // the lfo table is the program's
        kReverb = 0x80,
    };
    bool reverb() const { return flags & kReverb; }
    bool empty() const { return address == kNoSample; }
    u8 group() const { return key_lo; }
    u8 priority() const { return key_hi; }
};

struct Program {
    // How a key picks tones (byte 0 of the record).
    enum class Mode : u8 {
        FirstMatch, // the first tone whose key range holds the key
        Layered,    // every tone whose key range holds the key
        ByKey,      // tone number key - key_base, up to key_top (drum kits, effect programs)
    };
    Mode mode = Mode::FirstMatch;
    u8 volume = 0; // 0..127
    u8 pan = 0x40; // byte 2; 0x40 in every bank (no reader of it was found)
    u8 bend_range = 0, lfo = 0x7F; // used by tones flagged kProgramBend / kProgramLfo
    u8 key_base = 0;               // keys below it are silent
    u8 key_top = 0;                // ByKey: the last key
    std::vector<Tone> tones;

    // The tones a key sounds, in order (ByKey: at most one; empty slots left out).
    std::vector<const Tone*> tones_for_key(int key) const;
};

// A small sequence of an effect bank: the bytes from its first event to the end of the
// bank's tables (the events end themselves).
struct EffectSequence {
    u32 offset = 0; // into InstBank::head
};

struct InstBank {
    // Programs by number; a number the bank leaves out is empty.
    std::vector<std::optional<Program>> programs;        // for the sequenced music and held notes
    std::vector<std::optional<Program>> effect_programs; // for effect sequences (tones by key)
    // Effect sequences by [set][entry]; a set the bank leaves out is empty.
    std::vector<std::vector<EffectSequence>> effect_sequences;
    std::array<u8, 128> velocity_map{}; // a note's velocity as played (identity without a table)
    int address_unit = 8;               // bytes per unit of a tone's sample address (8 or 16)
    std::shared_ptr<const Bytes> head;  // the file up to the sample data (effect sequences read it)
    std::shared_ptr<const Bytes> adpcm; // the sample data

    const Program* program(int n) const {
        return n >= 0 && size_t(n) < programs.size() && programs[size_t(n)] ? &*programs[size_t(n)] : nullptr;
    }
    const Program* effect_program(int n) const {
        return n >= 0 && size_t(n) < effect_programs.size() && effect_programs[size_t(n)] ? &*effect_programs[size_t(n)] : nullptr;
    }
    const EffectSequence* effect_sequence(int set, int entry) const {
        if (set < 0 || size_t(set) >= effect_sequences.size()) return nullptr;
        const auto& s = effect_sequences[size_t(set)];
        return entry >= 0 && size_t(entry) < s.size() ? &s[size_t(entry)] : nullptr;
    }
    size_t tone_count() const;

    // True for Gran Turismo 4's layout ("INST" with an "SShd" table block); Gran Turismo 3's
    // banks have the same magic and are read by the shared engine.
    static bool is_gt4(ByteSpan file);
    // Throws FormatError.
    static InstBank parse(ByteSpan file);
};

// The pitch the sound driver gives a voice: `offset` is the distance from the tone's center in
// 1/16 semitone. SPU2 pitch units (0x1000 = 48 kHz); a sample recorded at 44.1 kHz plays at
// its own rate at offset 0 (0xEB3). Limited to 0x3FFF.
u16 pitch_for_offset(int offset_16th);
// A game pitch (0x1000 = 44.1 kHz, the shared mixer's unit) that the mixer turns into `spu`.
u16 game_pitch_for_spu(u16 spu);

// The driver's pan table: the gain (0..127) of each side at a pan of 0..127.
void pan_gains(int pan, int& left, int& right);

} // namespace gt4r::sound
