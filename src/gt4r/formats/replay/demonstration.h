#pragma once

// A race's recorded run as the program keeps it (Serialize::ReplayData): the disc's demonstration runs
// (replay/license/<event label>: a license test's or a mission's "View a demo run") and, in the same layout, the
// memory card's saved replays. docs/formats/replay.md has the layout; what follows is the reader.
//
// A file: the Serialize header (0x40 bytes: u32 its size, u32 the body's size, u16 0, u16 4, u32 the body's key,
// the serializer's name), then the body enciphered byte by byte (0x579E00, with two tables of the program: the
// cipher's state is a float in the console's arithmetic). The body: the race's header (0x1AA4 bytes, RaceBase's
// virtual 27: "GT4M", the race's class name, the event's course and race, the recorded time, every entry's car and
// settings), then the drivers' controls (AutomobileControlRecord::Manager, 0x346BB8): one recorder's stream (or one
// per car), each a frame count, a byte length and a bit stream of the changes of the 0x20-byte pad record the race's
// input phase reads (0x345D70).

#include "gt4r/base/types.h"
#include "gt4r/sim/equipment.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt4r::sim {
class Program;
}

namespace gt4r::replay {

using gt3r::s8;

// The race's pad record (0x20 bytes, the controller's frame its input phase reads, physics.md "A race entry"): +0 flags
// (bit 0 the steering analog, 1 the throttle, 2 the brake, 3 the back axis, 4 the third), +1 the controller's kind (4
// and 6 steering wheels), +2 s8 the shift (+1 up, -1 down), +3 the count of button bytes, +4 s16 the steering (+-4096,
// + left), +7 a byte, +8 the throttle, +0xA the brake (0 .. 4096), +0xC / +0xE two axes, +0x10 .. the button bytes.
struct PadRecord {
    std::array<u8, 0x20> bytes{};
    u8 flags() const { return bytes[0]; }
    u8 kind() const { return bytes[1]; }
    s8 shift() const { return s8(bytes[2]); }
    s16 steer() const { return s16(bytes[4] | (bytes[5] << 8)); }
    s16 throttle() const { return s16(bytes[8] | (bytes[9] << 8)); }
    s16 brake() const { return s16(bytes[0xA] | (bytes[0xB] << 8)); }
    s16 axis_c() const { return s16(bytes[0xC] | (bytes[0xD] << 8)); }
    s16 axis_e() const { return s16(bytes[0xE] | (bytes[0xF] << 8)); }
    bool operator==(const PadRecord&) const = default;
};

// One recorder's frames as its playback (0x345D70) hands them out: frame i is the record the race reads at its step
// i + 1 counted from the recording's start; T - 1 frames for a stream of T, then the playback is done.
struct ControlStream {
    u32 frame_count = 0; // T, as stored
    Bytes state;         // the recorder's own header (0x14F4 bytes; a save of several recorders, a free run's lap: a recording from
                         // a moment of the race on), else empty (RecorderState)
    std::vector<PadRecord> frames;
    std::optional<PadRecord> last; // the T-th record, which the recorder kept and no playback reaches
    // The record at step i (past the end: an empty record, as the done playback leaves it).
    PadRecord frame(size_t i) const { return i < frames.size() ? frames[i] : PadRecord{}; }
};

// What a recorder's header holds of the moment its stream starts at (0x35AB08, read back by 0x35ABA8; docs/formats/replay.md).
struct RecorderState {
    u8 units = 0;        // +0x02 the race clock's units (0 .. 50) since the line when it was kept
    u8 remainder = 0;    // +0x03 what the race clock's whole milliseconds leave of its units then (0 .. 2, 0x358468)
    u32 frame = 0;       // +0x04 the recorder's frame then
    u32 share = 0;       // +0x0C (f32) the share of the step after the line
    std::span<const u8> car;  // +0x54 the car's record (0x10A0)
    std::span<const u8> body; // +0x10F4 its collision body's contacts (0x400; all 0: none)
};
std::optional<RecorderState> recorder_state(const ControlStream& s);

// An entry of the race's header (0x228 bytes from +0x960, written from the race entry by RaceBase's virtual 27).
struct DemoEntry {
    sim::Equipment equipment;         // +0x000 the entry's equipment (entry +0x20)
    std::array<u8, 0x20> condition{}; // +0x178 the condition block (entry +0x3468)
    u32 x198 = 0;                     // +0x198 the word after the equipment (entry +0x198) ❓
    s32 grid_place = 0;               // +0x19C (entry +0x3444)
    u8 pedal_style = 0;               // +0x1A0 (entry +0x3448)
    u8 driver_kind = 0;               // +0x1A1 (entry +0x344C)
    std::array<u8, 4> aids{};         // +0x1A2 .. +0x1A5 the aids' switches (entry +0x3450 .. +0x345C)
};

struct Demonstration {
    u32 key = 0;              // the Serialize header's +0xC: the body's key (CRC-32's low 24 bits, the first byte on top)
    u32 version = 0;          // the race header's +8 (0x40000)
    u32 race_mode = 0;        // +0xC (the race's +0xCC4)
    std::string race_class;   // +0x10 the race's class ("RaceLicense")
    Bytes header;             // the body's first 0x1AA4 bytes as read (encode writes the fields below over them; empty: zeros)
    std::array<u8, 0x60> information{}; // +0x50 the race information's first 0x60 bytes ❓
    u64 code_b0 = 0, code_b8 = 0; // +0xB0 / +0xB8 two database codes of the race information (+0x68, +0x78) ❓
    s32 time_ms = 0;          // +0xC0 the first entry's recorded time (ms)
    s32 time2_ms = 0;         // +0xC4 the race's time (0x3BBB60 of -1) ❓
    std::vector<DemoEntry> entries; // +0x958 the count, then the entries
    u32 controls_flag = 0;    // +0x1AA0 (1 once the race's virtual 58 wrote the controls)
    // The controls: recorder 0 only (mode 0: count 0, one stream), or one stream per recorder (count n).
    std::vector<ControlStream> streams;

    // nullopt (with *error) when the file is not a deciphered ReplayData of the program's layout. The cipher's
    // tables are the program's (0x655C90, 0x655888): a program that is not the mapped build gives nullopt.
    static std::optional<Demonstration> parse(ByteSpan file, const sim::Program& program, std::string* error = nullptr);
    // The file of it as the program writes one (Serialize::ReplayData, then not scrambled): its body enciphered under its key.
    // Empty without Gran Turismo 4's tables (another game's program).
    Bytes encode(const sim::Program& program) const;
};

// A recorder's stream of records as the program's recorder writes it (0x346138 a step, 0x346498 its end): `frames` then `last`.
Bytes encode_stream(const std::vector<PadRecord>& frames, const std::optional<PadRecord>& last);
// A body enciphered (0x579F38); `key` takes its key (CRC-32's low 24 bits, the first byte on top). Empty, the key 0, without
// Gran Turismo 4's tables.
Bytes encipher_body(ByteSpan plain, u32& key, const sim::Program& program);

// The body deciphered (0x579E00): nullopt when the key does not match the deciphered body.
std::optional<Bytes> decipher_body(ByteSpan cipher, u32 key, const sim::Program& program);
// The demonstration's file for an event (a license test's or a mission's label): "replay/license/<label>".
std::string demonstration_path(const std::string& event_label);

} // namespace gt4r::replay
