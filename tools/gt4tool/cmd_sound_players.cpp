// gt4tool sound players: the layer players of a saved state of the original (its main memory, the
// eeMemory.bin of a PCSX2 state, address = offset) against GT4R's layer player (no disc needed).
//
//   gt4tool sound players <eeMemory.bin>...
//
// For each of the 24 players (0x64FBF8 + 0x58 i, US 1.01) with a bank and started, the inputs the race wrote
// (revs, the two side levels, the Doppler factor), each slot's layer, pitch and side levels as the
// state holds them and as audio::LayerPlayer works them out from those inputs (the step limits
// taken from the state's own last values), and the slot's voice's SPU2 levels against
// LayerPlayer::voice_levels. A line ends in "ok" when every number is the same. The record's
// layout: docs/formats/sound.md "The layer players".

#include "base/file.h"
#include "gt4r/audio/layer_player.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

// Where the two programs keep their players: Gran Turismo 4 US 1.01 0x64FBF8, 0x58 bytes, started
// while +0x54 is 2; Prologue (SCPS-15055 v1.01) 0x516278, 0x54 bytes (no reverb flag), started
// while +0x50 is 2.
struct Layout {
    const char* name;
    u32 players, size, started;
};
constexpr Layout kLayouts[] = {{"Gran Turismo 4", 0x64FBF8, 0x58, 0x54}, {"Prologue", 0x516278, 0x54, 0x50}};
constexpr u32 kCount = 24;

struct Memory {
    Bytes d;
    bool ok(u32 a, u32 n) const { return (a & 0x1FFFFFF) + n <= d.size(); }
    template <class T> T at(u32 a) const {
        T v{};
        a &= 0x1FFFFFF;
        if (a + sizeof v <= d.size()) std::memcpy(&v, d.data() + a, sizeof v);
        return v;
    }
};

int players(const std::string& path) {
    const auto data = gt3r::read_host_file(path);
    if (!data) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 1;
    }
    Memory m{*data};
    // The program: the layout with a started player on a layer table.
    const Layout* lay = nullptr;
    for (const Layout& l : kLayouts)
        for (u32 i = 0; i < kCount && !lay; ++i) {
            const u32 p = l.players + l.size * i;
            const s32 n = m.at<s32>(p + 0x34);
            if (m.at<s32>(p + l.started) == 2 && n > 0 && n <= 64 && m.at<u32>(p + 0x30) && m.ok(m.at<u32>(p + 0x30), 16u * u32(n))) lay = &l;
        }
    if (!lay) {
        std::printf("== %s: no started player\n", path.c_str());
        return 0;
    }
    std::printf("== %s (%s)\n", path.c_str(), lay->name);
    int same = 0, differ = 0;
    for (u32 i = 0; i < kCount; ++i) {
        const u32 p = lay->players + lay->size * i;
        const u32 table = m.at<u32>(p + 0x30);
        const s32 count = m.at<s32>(p + 0x34);
        if (!table || count <= 0 || count > 64 || !m.ok(table, 16u * u32(count))) continue;
        if (m.at<s32>(p + lay->started) != 2) continue; // (stepped only while 2: started)
        std::vector<gt3r::sound::EngineLayer> layers(static_cast<size_t>(count));
        for (s32 k = 0; k < count; ++k) {
            const u32 o = table + 16u * u32(k);
            auto& l = layers[size_t(k)];
            l.base_rpm = m.at<u16>(o), l.lo_rpm = m.at<s16>(o + 2), l.hi_rpm = m.at<s16>(o + 4), l.volume = m.at<s16>(o + 6);
            l.pitch = m.at<u32>(o + 8), l.offset = m.at<u32>(o + 12);
        }
        const int rpm = std::max(m.at<s32>(p + 0x48), audio::LayerPlayer::kMinRpm);
        const s16 left = m.at<s16>(p + 0x4C), right = m.at<s16>(p + 0x4E);
        std::printf("player %2u: %d layers, revs %d, sides %d / %d, Doppler %d\n", i, count, m.at<s32>(p + 0x48), left, right, m.at<s32>(p + 0x40));
        int want[2] = {m.at<s32>(p + 0x04), m.at<s32>(p + 0x1C)};
        int ours[2] = {want[0], want[1]};
        audio::LayerPlayer::choose(layers, rpm, ours[0], ours[1]);
        // (the slots keep their layers: the pair may be either way round)
        if (ours[0] == want[1] && ours[1] == want[0]) std::swap(ours[0], ours[1]);
        audio::LayerPlayer::Slot last[2];
        const gt3r::sound::EngineLayer* layer_of[2] = {};
        for (int s = 0; s < 2; ++s) {
            const u32 q = p + 0x18 * u32(s);
            last[s].left = m.at<s16>(q + 0xC), last[s].right = m.at<s16>(q + 0xE), last[s].pitch = m.at<s32>(q + 0x10);
            last[s].layer = m.at<s32>(q + 8);
            if (want[s] >= 0 && want[s] < count) layer_of[s] = &layers[size_t(want[s])];
        }
        // The slots from inputs (revs, sides): whether every slot is what the state holds.
        const auto matches = [&](int r, s16 in_l, s16 in_r, bool sides) {
            for (int s = 0; s < 2; ++s) {
                if (!layer_of[s]) continue;
                if (audio::LayerPlayer::layer_pitch(*layer_of[s], r, last[s].pitch) != last[s].pitch) return false;
                if (!sides) continue;
                s16 l, rr;
                audio::LayerPlayer::layer_levels(*layer_of[s], layer_of[s ^ 1], r, in_l, in_r, l, rr, &last[s]);
                if (l != last[s].left || rr != last[s].right) return false;
            }
            return true;
        };
        bool ok = ours[0] == want[0] && ours[1] == want[1] && matches(rpm, left, right, true);
        std::string how = "the inputs as written";
        if (!ok) {
            // The race writes the inputs once a step and the player reads them on the sound thread: a
            // state taken between the two holds the next step's inputs over the last step's slots.
            // Look for the inputs of the step before: revs near these, sides that give every slot.
            for (int r = std::max(rpm - 600, audio::LayerPlayer::kMinRpm); !ok && r <= rpm + 600; ++r) {
                if (!matches(r, 0, 0, false)) continue;
                int found_l = 0x10000, found_r = 0x10000;
                for (int v = -0x3FFF; v <= 0x3FFE && found_l > 0xFFFF; ++v) {
                    bool all = true;
                    for (int s = 0; s < 2 && all; ++s) {
                        if (!layer_of[s]) continue;
                        s16 l, rr;
                        audio::LayerPlayer::layer_levels(*layer_of[s], layer_of[s ^ 1], r, s16(v), 0, l, rr, &last[s]);
                        all = l == last[s].left;
                    }
                    if (all) found_l = v;
                }
                for (int v = -0x3FFF; v <= 0x3FFE && found_r > 0xFFFF; ++v) {
                    bool all = true;
                    for (int s = 0; s < 2 && all; ++s) {
                        if (!layer_of[s]) continue;
                        s16 l, rr;
                        audio::LayerPlayer::layer_levels(*layer_of[s], layer_of[s ^ 1], r, 0, s16(v), l, rr, &last[s]);
                        all = rr == last[s].right;
                    }
                    if (all) found_r = v;
                }
                if (found_l <= 0xFFFF && found_r <= 0xFFFF) {
                    ok = true;
                    char b[96];
                    std::snprintf(b, sizeof b, "the step before's: revs %d, sides %d / %d", r, found_l, found_r);
                    how = b;
                }
            }
        }
        const bool layers_ok = ours[0] == want[0] && ours[1] == want[1] && last[0].layer == want[0] && last[1].layer == want[1];
        for (int s = 0; s < 2; ++s) {
            const u32 voice = m.at<u32>(p + 0x18 * u32(s));
            if (!layer_of[s]) {
                std::printf("  slot %d: none (ours %d)\n", s, ours[s]);
                continue;
            }
            std::printf("  slot %d: layer %d (ours %d), pitch %d, sides %d / %d", s, want[s], ours[s], last[s].pitch, last[s].left, last[s].right);
            if (voice && m.ok(voice, 0xA0)) {
                const bool mono = m.at<u32>(m.at<u32>(voice + 8)) == 0; // (the voice's driver: its output mode, 0 mono)
                s16 vl, vr;
                audio::LayerPlayer::voice_levels(m.at<s16>(voice + 0x64), m.at<s16>(voice + 0x66), mono, vl, vr);
                const auto spu = [](u16 v) { return s16(v & 0x4000 ? int(v & 0x7FFF) - 0x8000 : int(v & 0x7FFF)); };
                const s16 hl = spu(m.at<u16>(voice + 0x96)), hr = spu(m.at<u16>(voice + 0x98));
                std::printf(", voice %d / %d (ours %d / %d), SPU2 pitch %d", hl, hr, vl, vr, (int(m.at<u16>(voice + 0x90)) * m.at<s32>(voice + 0x74)) >> 12);
                if (hl != vl || hr != vr) ok = false;
            }
            std::printf("\n");
        }
        ok = ok && layers_ok;
        std::printf("  %s (%s)\n", ok ? "ok" : "DIFFERS", how.c_str());
        (ok ? same : differ)++;
    }
    std::printf("%d players the same, %d differ\n", same, differ);
    return 0;
}

} // namespace

int sound_players(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: gt4tool sound players <eeMemory.bin>...\n");
        return 1;
    }
    int r = 0;
    for (int i = 3; i < argc; ++i) r |= players(argv[i]);
    return r;
}

} // namespace gt4r::tool
