#include "gt4r/formats/sound/inst_bank.h"

#include <algorithm>
#include <cmath>

namespace gt4r::sound {

namespace {

constexpr size_t kTables = 0x20;      // the table block: sizes, "SShd", the slots
constexpr size_t kSlots = 28;
constexpr size_t kProgramHeader = 8, kToneSize = 16;

enum Slot { kPrograms = 0, kVelocityMap = 1, kEffectSequences = 3, kEffectPrograms = 5, kWideAddresses = 27 };

struct Reader {
    ByteSpan d;
    size_t end; // of the table block
    u8 b(size_t o) const {
        if (o >= end) throw FormatError("INST: a table runs past the header");
        return d[o];
    }
    u16 w(size_t o) const { return u16(b(o) | b(o + 1) << 8); }
};

Tone read_tone(const Reader& r, size_t o, int unit, size_t adpcm_size) {
    Tone t;
    t.key_lo = r.b(o);
    t.key_hi = r.b(o + 1);
    t.center = r.b(o + 2);
    t.fine = gt3r::s8(r.b(o + 3));
    t.flags = r.b(o + 4);
    const u64 address = (u64(r.b(o + 5)) << 16 | r.w(o + 6)) * u64(unit);
    t.address = address < adpcm_size ? u32(address) : Tone::kNoSample;
    t.adsr1 = r.w(o + 8);
    t.adsr2 = r.w(o + 10);
    t.volume = r.b(o + 12);
    t.pan = r.b(o + 13);
    t.bend_range = r.b(o + 14);
    t.lfo = r.b(o + 15);
    return t;
}

// A table of records: u16 last number, u16 offset[last + 1] from the table (0xFFFF: none).
std::vector<std::optional<Program>> read_programs(const Reader& r, size_t table, bool effects, int unit, size_t adpcm_size) {
    const size_t count = size_t(r.w(table)) + 1;
    std::vector<std::optional<Program>> out(count);
    for (size_t i = 0; i < count; ++i) {
        const u16 off = r.w(table + 2 + i * 2);
        if (off == 0xFFFF) continue;
        const size_t p = table + off;
        Program g;
        const u8 kind = r.b(p);
        g.volume = r.b(p + 1);
        g.pan = r.b(p + 2);
        g.bend_range = r.b(p + 4);
        g.lfo = r.b(p + 5);
        g.key_base = r.b(p + 6);
        g.key_top = r.b(p + 7);
        size_t tones;
        if (effects || kind == 0xFF) {
            g.mode = Program::Mode::ByKey;
            tones = g.key_top >= g.key_base ? size_t(g.key_top - g.key_base) + 1 : 0;
        } else {
            g.mode = kind & 0x80 ? Program::Mode::Layered : Program::Mode::FirstMatch;
            tones = size_t(kind & 0x7F) + 1;
        }
        g.tones.reserve(tones);
        for (size_t t = 0; t < tones; ++t) g.tones.push_back(read_tone(r, p + kProgramHeader + t * kToneSize, unit, adpcm_size));
        out[i] = std::move(g);
    }
    return out;
}

} // namespace

std::vector<const Tone*> Program::tones_for_key(int key) const {
    std::vector<const Tone*> out;
    if (key < key_base) return out;
    if (mode == Mode::ByKey) {
        const size_t n = size_t(key - key_base);
        if (key <= key_top && n < tones.size() && !tones[n].empty()) out.push_back(&tones[n]);
        return out;
    }
    for (const Tone& t : tones) {
        if (key < t.key_lo || key > t.key_hi) continue;
        if (t.empty()) break; // the game drops the rest of the note at a sample outside its memory
        out.push_back(&t);
        if (mode == Mode::FirstMatch) break;
    }
    return out;
}

size_t InstBank::tone_count() const {
    size_t n = 0;
    for (const auto* list : {&programs, &effect_programs})
        for (const auto& p : *list)
            if (p) n += size_t(std::count_if(p->tones.begin(), p->tones.end(), [](const Tone& t) { return !t.empty(); }));
    return n;
}

bool InstBank::is_gt4(ByteSpan file) {
    return file.size() >= kTables + 0x10 + kSlots * 4 && gt3r::has_magic(file, "INST") && gt3r::has_magic(file.subspan(kTables + 0x0C), "SShd");
}

InstBank InstBank::parse(ByteSpan file) {
    if (!gt3r::has_magic(file, "INST")) throw FormatError("INST: wrong magic");
    if (!is_gt4(file)) throw FormatError("INST: not Gran Turismo 4's layout (no SShd block)");
    const u32 data_size = rd32(file, 0x10), data_offset = rd32(file, 0x14);
    const u32 tables_size = rd32(file, kTables);
    if (data_offset > file.size() || data_size > file.size() - data_offset) throw FormatError("INST: the sample data runs past the file");
    if (tables_size < 0x10 + kSlots * 4 || kTables + tables_size > data_offset) throw FormatError("INST: bad table block size");

    InstBank bank;
    bank.head = std::make_shared<const Bytes>(file.begin(), file.begin() + data_offset);
    bank.adpcm = std::make_shared<const Bytes>(file.begin() + data_offset, file.begin() + data_offset + data_size);
    const Reader r{file, kTables + tables_size};

    std::array<s32, kSlots> slot{};
    for (size_t i = 0; i < kSlots; ++i) slot[i] = s32(rd32(file, kTables + 0x10 + i * 4));
    const auto at = [&](Slot s) -> size_t {
        if (slot[s] < 0) return 0;
        if (u32(slot[s]) >= tables_size) throw FormatError("INST: a table starts past the header");
        return kTables + u32(slot[s]);
    };
    // The address unit: the last slot is 0 in the music banks (16 bytes) and -1 in the effect
    // banks (8 bytes).
    bank.address_unit = slot[kWideAddresses] == 0 ? 16 : 8;

    if (const size_t t = at(kPrograms)) bank.programs = read_programs(r, t, false, bank.address_unit, data_size);
    if (const size_t t = at(kEffectPrograms)) bank.effect_programs = read_programs(r, t, true, bank.address_unit, data_size);

    for (size_t i = 0; i < 128; ++i) bank.velocity_map[i] = u8(i);
    if (const size_t t = at(kVelocityMap))
        for (size_t i = 0; i < 128; ++i) bank.velocity_map[i] = r.b(t + 2 + i);

    if (const size_t t = at(kEffectSequences)) {
        const size_t sets = size_t(r.w(t)) + 1;
        bank.effect_sequences.resize(sets);
        for (size_t i = 0; i < sets; ++i) {
            const u16 off = r.w(t + 2 + i * 2);
            if (off == 0xFFFF) continue;
            const size_t set = t + (off & 0xFFFE);
            const size_t entries = size_t(r.w(set)) + 1;
            for (size_t k = 0; k < entries; ++k) {
                const size_t p = t + (r.w(set + 2 + k * 2) & 0xFFFE);
                r.b(p);
                bank.effect_sequences[i].push_back({u32(p)});
            }
        }
    }
    return bank;
}

u16 pitch_for_offset(int offset) {
    // 192 steps an octave; the game's table holds floor(2^24 * 44100/48000 * 2^(i/192)).
    int octave = offset / 192, step = offset % 192;
    if (step < 0) {
        step += 192;
        --octave;
    }
    const int shift = 12 - octave;
    if (shift < 0) return 0x3FFF;
    if (shift > 31) return 0;
    const u32 entry = u32(std::floor(16777216.0 * 0.91875 * std::pow(2.0, step / 192.0)));
    return u16(std::min<u32>(entry >> shift, 0x3FFF));
}

u16 game_pitch_for_spu(u16 spu) {
    // The shared mixer converts with pitch * 0xEB3 >> 12.
    u32 g = (u32(spu) * 4096 + 0xEB2) / 0xEB3;
    while (g > 0 && (g * 0xEB3) >> 12 > spu) --g;
    while ((g * 0xEB3) >> 12 < spu) ++g;
    return u16(std::min<u32>(g, 0xFFFF));
}

void pan_gains(int pan, int& left, int& right) {
    const auto side = [](int i) { return i < 64 ? 127 : i == 64 ? 120 : i < 80 ? 175 - i : std::max(0, 254 - 2 * i); };
    pan = std::clamp(pan, 0, 127);
    left = side(pan);
    right = pan == 0 ? 0 : side(128 - pan);
}

} // namespace gt4r::sound
