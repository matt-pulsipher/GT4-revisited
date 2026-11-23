#include "gt4r/formats/sound/sound_index.h"

#include <cstring>

namespace gt4r::sound {

namespace {

std::string text_at(ByteSpan d, u32 offset) {
    if (offset >= d.size()) throw FormatError("sound index: a string starts past the end");
    size_t end = offset;
    while (end < d.size() && d[end]) ++end;
    return std::string(reinterpret_cast<const char*>(d.data()) + offset, end - offset);
}

void need(ByteSpan d, size_t offset, size_t size, const char* what) {
    if (offset > d.size() || d.size() - offset < size) throw FormatError(std::string(what) + ": truncated");
}

float rdf(ByteSpan d, size_t o) {
    const u32 bits = rd32(d, o);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// A marker block: u32 0, rate, length, count[32], offset[32]; the offsets from `base`.
gt3r::sound::MadsMarkers read_markers(ByteSpan d, size_t block, size_t base) {
    gt3r::sound::MadsMarkers m;
    constexpr size_t kTracks = gt3r::sound::MadsMarkers::kTracks;
    need(d, block, 12 + kTracks * 8, "markers");
    m.rate = rd32(d, block + 4);
    m.length = rd32(d, block + 8);
    for (size_t t = 0; t < kTracks; ++t) {
        const u32 count = rd32(d, block + 12 + t * 4), offset = rd32(d, block + 12 + kTracks * 4 + t * 4);
        if (!count) continue;
        need(d, base + offset, size_t(count) * 4, "markers");
        for (u32 i = 0; i < count; ++i) m.tracks[t].push_back(rd32(d, base + offset + i * 4));
    }
    return m;
}

const char* const kEffectBankFiles[kEffectBanks] = {
    "sound/gt4sys.ins", "sound/gt4race.ins", "sound/gt4count.ins", "sound/gt4race2.ins", "sound/gt4shutter.ins",
};

const char* const kStreamGroupNames[kStreamGroups] = {
    "start",         "goal",           "racing",        "default_quick",  "result",           "torophy_champ", "torophy_solo",
    "get_car",       "replay_theater", "license_start", "license_gold",   "license_goal",     "license_silver", "license_bronze",
    "license_failed", "license_torophy", "autodemo",     "champ_start",    "champ_goal",       "timeattack_start", "timeattack_goal",
    "coffee_start",  "coffee_goal",    "etc_start",     "etc_goal",       "last_start",       "last_goal",
};

} // namespace

const EffectEntry* EffectIndex::find(const std::string& name) const {
    for (const EffectEntry& e : entries)
        if (e.name == name) return &e;
    return nullptr;
}

EffectIndex EffectIndex::parse(ByteSpan d) {
    if (!gt3r::has_magic(d, "GTSE") || d.size() < 0x10) throw FormatError("GTSE: wrong magic");
    const u32 count = rd32(d, 0x0C);
    need(d, 0x10, size_t(count) * 8, "GTSE");
    EffectIndex index;
    for (u32 i = 0; i < count; ++i) {
        const u32 value = rd32(d, 0x14 + i * 8);
        EffectEntry e;
        e.name = text_at(d, rd32(d, 0x10 + i * 8));
        e.bank = u8(value >> 24);
        e.sequence = (value >> 16 & 0xFF) != 0;
        e.index = u8(value >> 8);
        e.key = u8(value);
        index.entries.push_back(std::move(e));
    }
    return index;
}

const char* effect_bank_file(u8 bank) { return bank < kEffectBanks ? kEffectBankFiles[bank] : nullptr; }

size_t MusicIndex::records() const {
    size_t n = 0;
    for (const auto& g : groups) n += g.size();
    return n;
}

MusicIndex MusicIndex::parse(ByteSpan d) {
    const bool streams = gt3r::has_magic(d, "MADS");
    if ((!streams && !gt3r::has_magic(d, "MSEQ")) || d.size() < 0x14) throw FormatError("music index: wrong magic");
    MusicIndex index;
    index.version = rd32(d, 4);
    if (index.version != 1 && index.version != 2) throw FormatError("music index: unknown version " + std::to_string(index.version));
    const u32 groups = rd32(d, 0x0C);
    need(d, 0x14, size_t(groups) * 8, "music index");
    const size_t record_size = index.version == 2 ? 24 : 20;
    index.groups.resize(groups);
    for (u32 g = 0; g < groups; ++g) {
        const u32 offset = rd32(d, 0x14 + g * 8), count = rd32(d, 0x18 + g * 8);
        need(d, offset, size_t(count) * record_size, "music index");
        for (u32 i = 0; i < count; ++i) {
            const size_t r = offset + i * record_size;
            MusicRecord rec;
            rec.name = text_at(d, rd32(d, r));
            rec.file = text_at(d, rd32(d, r + 4));
            rec.title = text_at(d, rd32(d, r + 8));
            rec.artist = text_at(d, rd32(d, r + 12));
            if (index.version == 2) {
                rec.genre = text_at(d, rd32(d, r + 16));
                rec.value = rd32(d, r + 20);
            } else {
                rec.value = rd32(d, r + 16);
                if (streams && rec.value) {
                    try {
                        rec.markers = read_markers(d, rec.value, rec.value);
                    } catch (const FormatError&) { // (an index the game does not read points every piece at its end)
                        rec.markers = {};
                    }
                }
            }
            index.groups[g].push_back(std::move(rec));
        }
    }
    return index;
}

const char* stream_group_name(size_t group) { return group < kStreamGroups ? kStreamGroupNames[group] : nullptr; }

int stream_group(const std::string& name) {
    for (size_t i = 0; i < kStreamGroups; ++i)
        if (name == kStreamGroupNames[i]) return int(i);
    return -1;
}

gt3r::sound::MadsMarkers parse_markers(ByteSpan d) {
    if (!gt3r::has_magic(d, "BMRK")) throw FormatError("BMRK: wrong magic");
    return read_markers(d, 4, 0);
}

float VolumeTable::level(int mode, int level) const {
    if (mode < 0 || mode >= 3 || level < 0 || level >= 4) return 1.0f;
    const size_t i = size_t(mode) * per_mode + size_t(level);
    return i < values.size() ? values[i] : 1.0f;
}

VolumeTable VolumeTable::parse(ByteSpan d) {
    if (!gt3r::has_magic(d, "SDVL") || d.size() < 0x14) throw FormatError("SDVL: wrong magic");
    VolumeTable t;
    const u32 modes = rd32(d, 0x0C);
    t.per_mode = rd32(d, 0x10);
    need(d, 0x14, size_t(modes) * t.per_mode * 4, "SDVL");
    for (size_t i = 0; i < size_t(modes) * t.per_mode; ++i) t.values.push_back(rdf(d, 0x14 + i * 4));
    return t;
}

} // namespace gt4r::sound
