#pragma once

// "Ssqt" sequence banks (music/<region>/*.sqt): the menus' sequenced music. A bank is a list
// of "Sssq" sequences; a sequence is one stream of MIDI-like events for 16 channels, played
// with the programs of the instrument bank of the same name. docs/formats/sound.md.

#include "gt4r/base/types.h"

#include <memory>
#include <vector>

namespace gt4r::sound {

struct SqtSequence {
    u32 resolution = 480; // ticks per beat
    u32 events = 0;       // offset of the first event in SqtBank::data
};

struct SqtBank {
    std::shared_ptr<const Bytes> data; // the whole file
    std::vector<SqtSequence> sequences;

    // Throws FormatError.
    static SqtBank parse(ByteSpan file);
};

// A number of the event streams: 7 bits a byte, the high bit set on all but the last.
// Advances `pos`; throws FormatError past the end.
u32 read_sequence_number(ByteSpan d, size_t& pos);

// One event of a music sequence, decoded. A stream is `event, time to the next` pairs; an
// event's status byte may be left out when it repeats the last one (running status), and
// the stream opens with a lone time.
struct SqtEvent {
    enum class Kind : u8 { NoteOff, NoteOn, Control, Program, Bend, Tempo, End, Meta, None };
    Kind kind = Kind::None;
    u8 channel = 0;
    u8 a = 0, b = 0; // key and velocity; controller and value; program; bend (0x40: none)
    u32 tempo = 0;   // microseconds per beat
    u32 tick = 0;    // from the sequence's start
};

// Controllers the game's sequencer acts on (others are skipped).
enum SqtControl : u8 {
    kCtlLfoRate = 0x01,
    kCtlLfoDepth = 0x02,
    kCtlVolume = 0x07,
    kCtlPan = 0x0A,
    kCtlExpression = 0x0B,
    kCtlHold = 0x40,    // value 0 releases every note of the channel
    kCtlReverb = 0x5B,  // non-zero: the channel's notes send to the reverb
    kCtlPhase = 0x5E,   // value mod 3: 1 inverts the left side, 2 the right (a surround cue)
    kCtlLoop = 0x63,    // value 20 marks the loop's start, 30 jumps back to it
    kCtlLoopCount = 0x66,
};
constexpr u8 kLoopStart = 20, kLoopEnd = 30;

// A sequence's events from start to end, the loop not followed. Throws FormatError.
std::vector<SqtEvent> decode_sequence(const SqtBank& bank, size_t index);

struct SqtSummary {
    u32 ticks = 0;         // to the end event
    s64 loop_start = -1;   // tick of the loop's start, -1 without a loop
    s64 loop_end = -1;
    u32 notes = 0;
    u32 first_tempo = 0;   // the tempo in force when the first note sounds
    u32 channels = 0;      // bit per channel with a note
    std::vector<u8> programs;
    double loop_seconds = 0; // the loop's length
    double intro_seconds = 0; // from the start to the loop's start
};
SqtSummary summarize_sequence(const SqtBank& bank, size_t index);

} // namespace gt4r::sound
