#pragma once

// Gran Turismo 4's player of an "ENGN" bank (docs/formats/sound.md "The layer players"): the
// sound driver's 24 players of 0x58 bytes at 0x64FBF8, each stepped 60 times a second by
// 0x558FD8. A bank holds recordings of one engine at set revs ("layers"); the player sounds the
// layer whose range holds the revs, or the two neighbours the revs fall between, crossfaded by a
// sine. The race writes each player's revs, two side levels, Doppler factor and priority every
// step (0x392640); the player works out its two slots' layers, pitches and levels from them and
// sends them to the SPU2 through two voices of the driver's kind 4.
//
//   choose    the first layer with lo <= revs <= hi plays alone; else the first pair with
//             hi[i - 1] < revs < lo[i] crossfades; revs under 200 count as 200; outside every
//             range the slots keep their layers
//   pitch     the layer's pitch x revs / its revs (0x1000 plays the sample at 48 kHz), held to
//             half .. twice the last step's; the voice plays it x the Doppler factor >> 12
//   level     the crossfade x (0..0x4000 from the far side of the gap) through the sine table
//             (0x64D3F8: 4096 sin(i / 1024 pi / 2)), x 0x3FFF >> 12, x the layer's volume >> 14,
//             x each side's level >> 14; each side's size moves at most 0x1000 a step
//   voice     a new layer takes a voice keyed with ADSR1 3, ADSR2 0xA (a release of about 43 ms)
//             at the layer's sample; the old one is released. The voice's level is the driver's
//             for a voice of kind 4 at the center pan (120 / 127 of the slot's), both sides the
//             mean of the two sizes in mono output.

#include "audio/voice_mixer.h"
#include "formats/sound/engn.h"
#include "gt4r/base/types.h"

#include <array>
#include <memory>

namespace gt4r::audio {

class SoundDriver;

class LayerPlayer {
public:
    static constexpr int kMinRpm = 200;
    // The envelope of a layer's voice: Gran Turismo 4's (0x559268); Prologue's player (0x481588)
    // keys with ADSR1 0x1003, an attack of about 0.8 ms.
    static constexpr u16 kAdsr1 = 0x0003, kAdsr2 = 0x000A, kPrologueAdsr1 = 0x1003;

    // What the race writes each step (0x392640).
    struct Input {
        int rpm = kMinRpm;
        s16 left = 0, right = 0; // the side levels, -0x3FFF..0x3FFE (negative: that side inverted)
        int doppler = 0x1000;    // Q12, 0..0x3FFF
        u32 priority = 0;        // the level's size x 2^31, at most 0x7FFFFFFE
        bool dry = false;        // no reverb send
        u16 adsr1 = kAdsr1;      // the program's envelope for a new voice
    };
    struct Slot {
        int want = -1, layer = -1; // the layer chosen this step, the layer the voice plays
        s16 left = 0, right = 0;   // the slot's side levels
        int pitch = 0;             // before the Doppler factor; 0x1000 = 48 kHz
        gt3r::VoiceId voice;
        bool sounding = false;     // the slot holds a voice
    };

    void set_bank(std::shared_ptr<const gt3r::sound::EngineBank> bank);
    bool loaded() const { return bank_ != nullptr; }
    const gt3r::sound::EngineBank* bank() const { return bank_.get(); }

    Input& input() { return input_; }
    const Input& input() const { return input_; }

    // One step: the slots, then their voices on `mixer` (none: the slots only). With `driver` (the one on
    // that mixer) a new voice is one of the driver's, found by the player's priority (0x558FD8 → 0x55D0B0).
    void step(gt3r::VoiceMixer* mixer, bool mono = false, SoundDriver* driver = nullptr);
    void stop(gt3r::VoiceMixer& mixer);
    const std::array<Slot, 2>& slots() const { return slots_; }

    // The pieces of a step, for checks against the program's records.
    // The layers for `rpm` (at least 200); false when no range holds it.
    static bool choose(const std::vector<gt3r::sound::EngineLayer>& layers, int rpm, int& want0, int& want1);
    // A slot's pitch (0x558DB0); `last` > 0: held to half .. twice it.
    static int layer_pitch(const gt3r::sound::EngineLayer& layer, int rpm, int last = 0);
    // A slot's levels (0x558E28); `other` the other slot's layer or null; `last`: step-limited
    // against those levels.
    static void layer_levels(const gt3r::sound::EngineLayer& layer, const gt3r::sound::EngineLayer* other, int rpm, s16 left, s16 right,
                             s16& out_left, s16& out_right, const Slot* last = nullptr);
    // The SPU2 levels of a kind 4 voice whose slot levels are `l`, `r` (0x55D890).
    static void voice_levels(int l, int r, bool mono, s16& out_l, s16& out_r);
    // The crossfade gain table (0x64D3F8), 1025 entries of 0..4096.
    static int crossfade(int index);

private:
    std::shared_ptr<const gt3r::sound::EngineBank> bank_;
    Input input_;
    std::array<Slot, 2> slots_{};
};

} // namespace gt4r::audio
