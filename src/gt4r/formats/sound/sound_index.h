#pragma once

// The small index files of the sound: which effect a name means (sound/gt4se.inf), the
// sequenced music by group (music/<region>/mid.inf, bgmmid.inf), the streamed music by group
// (bgm/<region>/ads.inf, bgmads.inf), a stream's markers (.adm) and the volume table
// (sound/sdvol.dat). docs/formats/sound.md.

#include "formats/sound/mads.h"
#include "gt4r/base/types.h"

#include <string>
#include <vector>

namespace gt4r::sound {

// "GTSE": an effect's name and where it is.
struct EffectEntry {
    std::string name;
    u8 bank = 0;          // 0 gt4sys, 1 gt4race, 2 gt4count, 3 gt4race2, 4 gt4shutter (effect_bank_file)
    bool sequence = false; // an effect sequence of the bank, else a held note of a program
    u8 index = 0;          // the sequence's set, or the program
    u8 key = 0;            // the sequence's entry in its set, or the note's key (60)
};
struct EffectIndex {
    std::vector<EffectEntry> entries;
    const EffectEntry* find(const std::string& name) const;
    static EffectIndex parse(ByteSpan file); // throws FormatError
};
// The bank file of an effect index's bank number ("sound/gt4sys.ins"); nullptr past the last.
const char* effect_bank_file(u8 bank);
constexpr int kEffectBanks = 5;

// One piece of a music index.
struct MusicRecord {
    std::string name;   // the id the game asks for
    std::string file;   // the stream's file name, or the sequence's source name
    std::string title, artist;
    std::string genre;  // version 2 only ("jingle", "Classical", "instrument")
    u32 value = 0;      // "MSEQ": the sequence's number in the group's bank; "MADS" version 1: its marker block
    gt3r::sound::MadsMarkers markers; // "MADS" version 1: the markers in the index itself
};
struct MusicIndex {
    u32 version = 0;
    std::vector<std::vector<MusicRecord>> groups;
    size_t records() const;

    // "MSEQ" (sequenced) or "MADS" (streamed), version 1 or 2. Throws FormatError.
    static MusicIndex parse(ByteSpan file);
};
// The game's names for the groups of the streamed index, in the index's order ("start",
// "goal", "racing", ...); nullptr past the last. The scripts ask for a group by this name.
const char* stream_group_name(size_t group);
int stream_group(const std::string& name); // -1 when no group has the name
constexpr size_t kStreamGroups = 27;

// "BMRK": a stream's markers (bgm/<region>/<name>.adm), sample positions on up to 32 tracks.
gt3r::sound::MadsMarkers parse_markers(ByteSpan file); // throws FormatError

// "SDVL": the mix levels by output mode.
struct VolumeTable {
    enum Level { Stream = 0, Sequence = 1, Race = 2, Menu = 3 };
    u32 per_mode = 0;
    std::vector<float> values; // [mode * per_mode + level]
    // 1.0 outside the table, as the game answers.
    float level(int mode, int level) const;
    static VolumeTable parse(ByteSpan file); // throws FormatError
};

} // namespace gt4r::sound
