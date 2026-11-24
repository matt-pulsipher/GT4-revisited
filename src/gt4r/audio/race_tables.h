#pragma once

// The tables of the game's program that mix a race's sound (docs/formats/sound.md "A race's
// sound"): the gains of each kind of sound by the view, how a sound fades with distance, and
// the block of numbers behind the wind, the road's roar, the gearbox's whine and the levels
// of the engine's two banks. They are read from the user's own program (CORE.GT4) when a race
// is set up and found by what they are, not by where a release has them. Without them
// (found() false) every gain is 1, a sound fades with distance by the program's other rule
// (32 m over the distance) and only the engine, the exhaust and the tires sound.

#include "gt4r/audio/layer_player.h"
#include "gt4r/base/types.h"

#include <array>

namespace gt4r::audio {

// How a car is heard: the camera's car ("target") or another, by the kind of camera.
enum class SoundView : u8 { InCarTarget = 0, InCarOther, ChaseTarget, ChaseOther, Other, Count };

// The kinds of sound a view has a gain for (the columns of the program's table).
enum class SoundChannel : u8 {
    Engine = 0,
    Exhaust = 1,
    Turbine = 2,      // the turbo's spool
    BlowOff = 3,
    GearWhine = 4,    // "turbine2"
    Clash = 5,
    Rumble = 6,       // the three rumble strips
    InCar = 7,
    Wind = 8,
    OnRoad = 9,       // the road's roar
    Rain = 10,
    OnSkill = 11,     // squeal on tarmac
    Unused12 = 12,    // read and thrown away by the program
    OffSkill = 13,    // sliding on loose ground
    OffRoad = 14,     // rolling on loose ground, pebbles, sand
    OnBound = 15,     // a wheel landing
    Count = 24,
};

// The output the player chose (the options' sound_type): the fading with distance differs.
enum class OutputMode : u8 { Mono = 0, Stereo = 1, Surround = 2 };

struct RaceSoundTables {
    static constexpr int kViews = int(SoundView::Count), kChannels = int(SoundChannel::Count);
    // The three microphones: 0 the engine's, 1 the exhaust's, 2 the body's and the wheels'.
    static constexpr int kMics = 3, kModes = 3;

    std::array<std::array<float, kChannels>, kViews> gain{};
    struct Mic {
        float slope = 0, reach = 0; // gain = 1 / (1 + slope x max(distance - reach, 0)); slope 0: 32 / distance
    };
    std::array<std::array<Mic, kMics>, kModes> mics{};

    // The block of numbers, made into what the program makes of its bytes.
    struct Params {
        float wind_gain = 0;                        // x drag x speed^2
        float wind_speed_lo = 0, wind_speed_hi = 1; // metres per second
        float wind_rpm_lo = 2500, wind_rpm_hi = 2500;
        float wind_max = 1;
        float incar_gain = 0;
        float incar_speed_lo = 0, incar_speed_hi = 1;
        float incar_pitch_lo = 1, incar_pitch_hi = 1;
        float incar_max = 1;
        float roar_narrow = 1, roar_wide = 1;       // the factor for a narrow and a wide tire
        float roar_gain = 0;                        // x (rim speed / 100)^2
        float roar_speed_lo = 0, roar_speed_hi = 1;
        float roar_pitch_lo = 1, roar_pitch_hi = 1;
        float roar_max = 1;
        float whine_gain_lo = 0, whine_gain_hi = 0;
        float whine_torque = 1;                     // N m at full gain
        float whine_pitch_rate = 0, whine_pitch_lo = 1; // pitch = lo + rate x shaft speed (rad/s)
        float engine_level = 0, exhaust_level = 0;  // x 127: the two banks' levels at most
        float turbine_level = 0, blowoff_scale = 0;
        float clash_scale = 1, clash_mid = 0, clash_hard = 0;
    } params;

    // The afterfire's chance (0x622320, the 16 bytes before the views' gains; 0x3F3278): a pair of
    // powers (x 10 PS) for each of the spec's kind byte +0x120 (0..3) and whether the engine has a
    // boost (no, yes). Under the first no flame, from it 0.25 rising to 0.75 at the second.
    std::array<u8, 16> afterfire{};
    float afterfire_chance(int kind, bool boosted, float ps) const;

    bool found_gains = false, found_mics = false, found_params = false, found_afterfire = false;
    // The master takes the effects option's level (Gran Turismo 4: its raw value / 128). Prologue's
    // program (its gains as floats) takes only the option's flag: 1 or 0 ([0x503978], 0x3A13E0).
    bool option_level = true;
    // The layer players' attack (ADSR1 of a new layer's voice): Prologue's program (found by its
    // float gains, as above) keys its layers with a slower attack than Gran Turismo 4's.
    u16 layer_adsr1 = LayerPlayer::kAdsr1;
    bool found() const { return found_gains && found_mics && found_params; }

    float view_gain(SoundView v, SoundChannel c) const { return gain[size_t(v)][size_t(c)]; }
    const Mic& mic(OutputMode mode, int n) const { return mics[size_t(mode)][size_t(n)]; }

    // Every gain 1, flat curves.
    static RaceSoundTables flat();
    // From the unpacked program. What is not found stays flat.
    static RaceSoundTables find(ByteSpan image);
    // From the packed program file of the disc (CORE.GT4); flat when it does not read.
    static RaceSoundTables from_core(ByteSpan core_file);
};

} // namespace gt4r::audio
