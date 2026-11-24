// The engine's layer players (audio/layer_player.h): the rules on a made-up bank, the crossfade
// table against the program's, and a car's bank of the disc rendered through the mixer. Every
// check that needs sound renders it to memory; nothing opens an audio device.

#include "test_framework.h"

#include "disc/core_image.h"
#include "gt4r/audio/car_sound.h"
#include "gt4r/audio/layer_player.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace gt4r;
using audio::LayerPlayer;

namespace {

gt3r::sound::EngineLayer layer(u16 base, s16 lo, s16 hi, s16 volume, u32 pitch, u32 offset) {
    gt3r::sound::EngineLayer l;
    l.base_rpm = base, l.lo_rpm = lo, l.hi_rpm = hi, l.volume = volume, l.pitch = pitch, l.offset = offset;
    return l;
}

// Three layers: alone to 2,000, 3,000..4,000 and from 5,000; crossfaded between.
std::vector<gt3r::sound::EngineLayer> three_layers() {
    return {layer(2000, 0, 2000, 0x3FFF, 0x1000, 0), layer(4000, 3000, 4000, 0x2000, 0x1000, 0x20), layer(6000, 5000, 32767, 0x3FFF, 0x800, 0x40)};
}

} // namespace

// The rules of 0x558FD8 and its two helpers on made-up layers.
TEST(sound_layer_player_rules) {
    const auto layers = three_layers();
    int a = -1, b = -1;
    CHECK(LayerPlayer::choose(layers, 100, a, b) && a == 0 && b == -1); // (under 200: 200)
    CHECK(LayerPlayer::choose(layers, 2500, a, b) && a == 0 && b == 1);
    CHECK(LayerPlayer::choose(layers, 3000, a, b) && a == 1 && b == -1);
    CHECK(LayerPlayer::choose(layers, 4999, a, b) && a == 1 && b == 2);
    a = b = 7;
    const std::vector<gt3r::sound::EngineLayer> gap = {layer(2000, 1000, 2000, 0x3FFF, 0x1000, 0)};
    CHECK(!LayerPlayer::choose(gap, 500, a, b) && a == 7 && b == 7); // outside every range: kept

    // The pitch: the layer's x revs / its revs, held to half .. twice the last step's.
    CHECK(LayerPlayer::layer_pitch(layers[1], 3500) == 3584);
    CHECK(LayerPlayer::layer_pitch(layers[1], 3500, 1000) == 2000 && LayerPlayer::layer_pitch(layers[1], 3500, 8000) == 4000);
    CHECK(LayerPlayer::layer_pitch(layers[2], 6000) == 0x800);

    // The crossfade: a sine from the far side of the gap; half way both at sin 45 degrees.
    CHECK(LayerPlayer::crossfade(0) == 0 && LayerPlayer::crossfade(512) == 2896 && LayerPlayer::crossfade(1024) == 4096);
    s16 l, r;
    LayerPlayer::layer_levels(layers[0], &layers[1], 2500, 0x3FFF, 0x2000, l, r);
    CHECK(l == 11581 && r == 5791);
    LayerPlayer::layer_levels(layers[1], &layers[0], 2500, 0x3FFF, 0x2000, l, r);
    CHECK(l == 5790 && r == 2895);
    LayerPlayer::layer_levels(layers[1], nullptr, 3500, 0x3FFF, -0x3FFF, l, r);
    CHECK(l == 8190 && r == -8191);
    // Each side's size moves at most 0x1000 a step, keeping the new sign.
    LayerPlayer::Slot last;
    last.left = 100, last.right = 100;
    LayerPlayer::layer_levels(layers[0], nullptr, 1000, 0x3FFF, -0x3FFF, l, r, &last);
    CHECK(l == 4196 && r == -4196);

    // A kind 4 voice plays 120 / 127 of its slot's levels (the driver's center pan); mono gives
    // both sides the mean of the two sizes.
    LayerPlayer::voice_levels(0x3FFF, -0x3FFF, false, l, r);
    CHECK(l == 15477 && r == -15478);
    LayerPlayer::voice_levels(0x3FFF, -0x3FFF, true, l, r);
    CHECK(l == 15477 && r == 15477);

    // Stepping: a slot keeps its layer while that layer is still wanted, the other slot empties.
    auto bank = std::make_shared<gt3r::sound::EngineBank>();
    bank->layers = layers;
    LayerPlayer p;
    p.set_bank(bank);
    p.input().rpm = 2500, p.input().left = p.input().right = 0x3FFF;
    p.step(nullptr);
    CHECK(p.slots()[0].layer == 0 && p.slots()[1].layer == 1 && p.slots()[0].left == 11581 && p.slots()[1].left == 5790);
    p.input().rpm = 3500;
    p.step(nullptr);
    CHECK(p.slots()[0].layer == -1 && p.slots()[1].layer == 1);
    // ... and its pitch and levels move from where they were: 0x1000 x 2500 / 4000 = 2560 then 3584
    // (within twice), 5790 then 8190 (within 0x1000 more).
    CHECK(p.slots()[1].pitch == 3584 && p.slots()[1].left == 8190);
    // A lone layer goes to the slot whose layer is not the empty one the other holds: here the
    // second (the two wants swap when the second's equals the first's layer, -1 alike).
    p.input().rpm = 32000;
    p.step(nullptr);
    CHECK(p.slots()[0].layer == -1 && p.slots()[1].layer == 2 && p.slots()[1].pitch == 0x800 * 32000 / 6000);
}

// The crossfade table is the program's own (0x64D3F8 in the US 1.01 program): our 1,025 values
// stand in the program's image.
TEST(sound_layer_crossfade_in_program) {
    if (test::discs().empty()) SKIP("no disc");
    bool any = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        const gt3r::CoreImage core = gt3r::CoreImage::decode(disc->read_program());
        std::vector<u8> ours(2 * 1025);
        for (int i = 0; i <= 1024; ++i) {
            const s16 v = s16(LayerPlayer::crossfade(i));
            std::memcpy(ours.data() + 2 * i, &v, 2);
        }
        const auto at = std::search(core.program.begin(), core.program.end(), ours.begin(), ours.end());
        std::printf("  the table %s in the program\n", at != core.program.end() ? "found" : "not found");
        CHECK(at != core.program.end());
        any = true;
    }
    if (!any) SKIP("no Gran Turismo 4 disc");
}

// A car's engine bank of the disc through the mixer: two voices while the revs fall between two
// layers, one when they are inside a layer, the old layer's voice released over the ADSR2 0xA
// release (about 43 ms) rather than cut.
TEST(sound_layer_player_voices_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        gt3r::Vfs vfs(&disc->volume());
        const audio::CarSoundFiles files = audio::car_sound_files(vfs, "ka_01");
        const auto data = files.engine.empty() ? std::nullopt : vfs.read(files.engine);
        if (!data) SKIP("no Ka engine bank");
        auto bank = std::make_shared<const gt3r::sound::EngineBank>(audio::parse_engine_bank(*data));
        // A gap between two layers.
        int gap = -1;
        for (size_t i = 1; i < bank->layers.size() && gap < 0; ++i)
            if (bank->layers[i - 1].hi_rpm + 2 < bank->layers[i].lo_rpm) gap = (bank->layers[i - 1].hi_rpm + bank->layers[i].lo_rpm) / 2;
        CHECK(gap > 0);
        if (gap <= 0) return;
        gt3r::VoiceMixer mixer;
        LayerPlayer p;
        p.set_bank(bank);
        p.input().rpm = gap, p.input().left = p.input().right = 0x2000;
        std::vector<s16> pcm(2 * 800);
        p.step(&mixer);
        mixer.mix(pcm.data(), 800);
        const int both = mixer.active_voices();
        const gt3r::VoiceId lower = p.slots()[0].voice;
        // Into the upper layer alone: the lower one's voice releases.
        p.input().rpm = bank->layers.back().lo_rpm + 1;
        int releasing_ms = 0;
        for (int ms = 0; ms < 200; ++ms) {
            p.step(&mixer);
            mixer.mix(pcm.data(), 48);
            if (mixer.playing(lower)) releasing_ms = ms + 1;
        }
        std::printf("  revs %d: %d voices; the released layer sounds %d ms more\n", gap, both, releasing_ms);
        CHECK(both == 2 && releasing_ms >= 30 && releasing_ms <= 60);
        CHECK(mixer.active_voices() == 1);
        return;
    }
    SKIP("no Gran Turismo 4 disc");
}
